/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */


#include "wav_reader.h"

#include <dirent.h>
#include <glog/logging.h>
#include <sys/stat.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "rapidjson/document.h"
#include "riva/proto/riva_asr.pb.h"

namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;

template <typename T>
T
little_endian_val(const char* str)
{
  T val = T();
  T shift = 0;
  for (int i = 0; i < sizeof(T); ++i) {
    val += T(str[i] & 0xFF) << shift;
    shift += 8;
  }
  return val;
}

inline std::string
GetFileExt(const std::string& s)
{
  size_t i = s.rfind('.', s.length());
  if (i != std::string::npos) {
    return (s.substr(i + 1, s.length() - i));
  }
  return ("");
}

/**
 * Reads WAV header and sets stream position to the first byte of audio, returns this position.
 * Returns zero if wrong format discovered.
 *
 * @param wavfile
 * @param header
 */
void
SeekToData(std::istream& wavfile, WAVHeader& header)
{
  int32_t curr_chunk_size = 0;
  char curr_chunk_id[4];
  while (wavfile.good()) {
    wavfile.read(curr_chunk_id, 4);
    wavfile.read((char*)&curr_chunk_size, 4);
    if (strncmp(curr_chunk_id, "RIFF", 4) == 0) {
      header.file_tag = "RIFF";
      header.file_size = curr_chunk_size;
      wavfile.read(curr_chunk_id, 4);
      if (strncmp(curr_chunk_id, "WAVE", 4) != 0) {
        break;
      }
      header.format = "WAVE";
    } else if (strncmp(curr_chunk_id, "fmt ", 4) == 0) {
      wavfile.read((char*)&header.audioformat, 2);
      wavfile.read((char*)&header.numchannels, 2);
      wavfile.read((char*)&header.samplerate, 4);
      wavfile.read((char*)&header.byterate, 4);
      wavfile.read((char*)&header.blockalign, 2);
      wavfile.read((char*)&header.bitspersample, 2);
      // Six values above took 16 bytes, skipping extra params if they exist:
      if (curr_chunk_size > 16) {
        wavfile.seekg(curr_chunk_size - 16, std::ios_base::cur);
      } else if (curr_chunk_size < 16) {
        break;
      }
    } else if (strncmp(curr_chunk_id, "data", 4) == 0) {
      header.data_size = (std::size_t)curr_chunk_size;
      break;
    } else if (strncmp(curr_chunk_id, "fLaC", 4) == 0) {
      header.file_tag = "fLaC";
      wavfile.seekg(0, std::ios_base::beg);
      break;
    } else if (strncmp(curr_chunk_id, "OggS", 4) == 0) {
      header.file_tag = "OggS";
      wavfile.seekg(0, std::ios_base::beg);
      break;
    } else {
      wavfile.seekg(curr_chunk_size, std::ios_base::cur);
    }
  }
}

bool
ParseHeader(
    std::string file, nr::AudioEncoding& encoding, int& samplerate, int& channels,
    long& data_offset)
{
  std::ifstream file_stream(file);
  WAVHeader header;
  SeekToData(file_stream, header);
  if (header.file_tag == "RIFF") {
    if (header.audioformat == WaveFormat::kPCM)
      encoding = nr::LINEAR_PCM;
    else if (header.audioformat == WaveFormat::kMULAW)
      encoding = nr::MULAW;
    else if (header.audioformat == WaveFormat::kALAW)
      encoding = nr::ALAW;
    else
      return false;
    data_offset = file_stream.tellg();
    samplerate = header.samplerate;
    channels = header.numchannels;
    return true;
  } else if (header.file_tag == "fLaC") {
    // TODO parse sample rate and channels from stream
    encoding = nr::FLAC;
    samplerate = 16000;
    channels = 1;
    data_offset = file_stream.tellg();
    return true;
  } else if (header.file_tag == "OggS") {
    encoding = nr::OGGOPUS;
    char header_buf[OPUS_HEADER_LENGTH];
    std::size_t read = file_stream.readsome(header_buf, OPUS_HEADER_LENGTH);
    if (read == OPUS_HEADER_LENGTH) {
      const std::string head(header_buf, header_buf + OPUS_HEADER_LENGTH);
      const std::size_t head_pos = head.find("OpusHead");
      if (head_pos != std::string::npos) {
        channels = (int)little_endian_val<uint8_t>(head.data() + head_pos + 9);
        samplerate = (int)little_endian_val<uint16_t>(head.data() + head_pos + 12);
      }
    }
    data_offset = file_stream.tellg();
    return true;
  }
  return false;
}

bool
IsDirectory(const char* path)
{
  struct stat s;

  stat(path, &s);
  if (s.st_mode & S_IFDIR)
    return true;
  else
    return false;
}

bool
ParseJson(const char* path, std::vector<std::string>& filelist)
{
  std::ifstream manifest_file;
  manifest_file.open(path, std::ifstream::in);

  if (!manifest_file.is_open()) {
    std::cout << "Could not open manifest file" << path << std::endl;
    return false;
  }

  std::string filepath_name("audio_filepath");

  std::string line;
  while (std::getline(manifest_file, line)) {
    rapidjson::Document doc;

    doc.Parse(line.c_str());

    if (!doc.IsObject()) {
      std::cout << "Problem parsing line: " << line << std::endl;
    }

    if (!doc.HasMember(filepath_name.c_str())) {
      std::cout << "Line: " << line << " does not contain " << filepath_name << " key" << std::endl;
      continue;
    }
    std::string filepath = doc[filepath_name.c_str()].GetString();
    filelist.push_back(filepath);
  }

  manifest_file.close();

  return true;
}

void
ParsePath(const char* path, std::vector<std::string>& filelist)
{
  DIR* dir;
  struct dirent* ent;
  char real_path[PATH_MAX];

  if (realpath(path, real_path) == NULL) {
    std::cerr << "invalid path: " << path << std::endl;
    return;
  }

  if (!IsDirectory(real_path)) {
    filelist.push_back(real_path);
    return;
  }

  if ((dir = opendir(real_path)) != NULL) {
    /* print all the files and directories within directory */
    while ((ent = readdir(dir)) != NULL) {
      if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
        continue;
      std::string full_path = real_path;
      full_path.append("/");
      full_path.append(ent->d_name);
      if (IsDirectory(full_path.c_str()))
        ParsePath(full_path.c_str(), filelist);
      else if (
          full_path.find(".wav") != std::string::npos ||
          full_path.find(".opus") != std::string::npos ||
          full_path.find(".ogg") != std::string::npos ||
          full_path.find(".flac") != std::string::npos)
        filelist.push_back(full_path);
    }
    closedir(dir);
  } else {
    /* could not open directory */
    perror("Could not open");
    return;
  }
}

std::string
AudioToString(nr::AudioEncoding& encoding)
// map nr::AudioEncoding to std::string
{
  if (encoding == 0) {
    return "ENCODING_UNSPECIFIED";
  } else if (encoding == 1) {
    return "LINEAR_PCM";
  } else if (encoding == 2) {
    return "FLAC";
  } else if (encoding == 4) {
    return "OPUS";
  } else if (encoding == 20) {
    return "ALAW";
  } else {
    return "";
  }
}


void
LoadWavData(std::vector<std::shared_ptr<WaveData>>& all_wav, std::string& path)
// pre-loading data
// we don't want to measure I/O
{
  std::cout << "Loading eval dataset..." << std::flush << std::endl;

  std::vector<std::string> filelist;
  std::string file_ext = GetFileExt(path);
  if (file_ext == "json" || file_ext == "JSON") {
    ParseJson(path.c_str(), filelist);
  } else {
    ParsePath(path.c_str(), filelist);
  }

  std::vector<std::pair<uint64_t, std::string>> files_size_name;
  files_size_name.reserve(filelist.size());
  for (auto& filename : filelist) {
    // Get the size
    std::cout << "filename: " << filename << std::endl;
    std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
    if (!in.good()) {
      throw std::runtime_error(std::string("Failed to open file ") + filename);
    }
    uint64_t file_size = in.tellg();
    files_size_name.emplace_back(std::make_pair(file_size, filename));
  }


  for (auto& file_size_name : files_size_name) {
    std::string filename = file_size_name.second;

    nr::AudioEncoding encoding;
    int samplerate;
    int channels;
    long data_offset;
    if (!ParseHeader(filename, encoding, samplerate, channels, data_offset)) {
      throw std::runtime_error(std::string("Invalid file/format ") + filename);
    }
    std::shared_ptr<WaveData> wav_data = std::make_shared<WaveData>();

    wav_data->sample_rate = samplerate;
    wav_data->filename = filename;
    wav_data->encoding = encoding;
    wav_data->channels = channels;
    wav_data->data_offset = data_offset;
    wav_data->data.assign(
        std::istreambuf_iterator<char>(std::ifstream(filename).rdbuf()),
        std::istreambuf_iterator<char>());
    all_wav.push_back(std::move(wav_data));
  }
  std::cout << "Done loading " << files_size_name.size() << " files" << std::endl;
}

int
ParseWavHeader(std::istream& wavfile, WAVHeader& header, bool read_header)
{
  if (read_header) {
    bool is_header_valid = false;
    SeekToData(wavfile, header);

    if (header.format == "WAVE") {
      if (header.audioformat == WaveFormat::kPCM && header.bitspersample == 16) {
        is_header_valid = true;
      } else if (
          (header.audioformat == WaveFormat::kMULAW || header.audioformat == WaveFormat::kALAW) &&
          header.bitspersample == 8) {
        is_header_valid = true;
      }
    }

    if (!is_header_valid) {
      LOG(INFO) << "error: unsupported format " << header.format << " audioformat "
                << header.audioformat << " channels " << header.numchannels << " rate "
                << header.samplerate << " bitspersample " << header.bitspersample << std::endl;
      return -1;
    }
  }

  if (wavfile) {
    // calculate size of samples
    std::streampos curr_pos = wavfile.tellg();
    wavfile.seekg(0, wavfile.end);
    int wav_size = wavfile.tellg() - curr_pos;
    wavfile.seekg(curr_pos);
    return wav_size;
  }
  return -2;
}
