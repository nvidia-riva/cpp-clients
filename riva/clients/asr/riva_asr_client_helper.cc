/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */


#include "riva_asr_client_helper.h"

std::vector<std::string>
ReadPhrasesFromFile(const std::string& phrases_file)
{
  std::vector<std::string> phrases;
  if (!phrases_file.empty()) {
    std::ifstream infile(phrases_file);

    if (infile.is_open()) {
      std::string phrase;
      while (getline(infile, phrase)) {
        phrases.push_back(phrase);
      }
    } else {
      std::string err = "Could not open file " + phrases_file;
      throw std::runtime_error(err);
    }
  }
  return phrases;
}

bool
WaitUntilReady(
    std::shared_ptr<grpc::Channel> channel, std::chrono::system_clock::time_point& deadline)
{
  auto state = channel->GetState(true);
  while (state != GRPC_CHANNEL_READY) {
    if (!channel->WaitForStateChange(state, deadline)) {
      return false;
    }
    state = channel->GetState(true);
  }
  return true;
}


bool
OpenAudioDevice(
    const char* devicename, snd_pcm_t** handle, snd_pcm_stream_t stream_type, int channels,
    int rate, unsigned int latency)
{
  int rc;
  static snd_output_t* log;

  std::cerr << "latency " << latency << std::endl;

  if ((rc = snd_pcm_open(handle, devicename, stream_type, 0)) < 0) {
    printf("unable to open pcm device for recording: %s\n", snd_strerror(rc));
    return false;
  }

  if ((rc = snd_output_stdio_attach(&log, stderr, 0)) < 0) {
    printf("unable to attach log output: %s\n", snd_strerror(rc));
    return false;
  }

  if ((rc = snd_pcm_set_params(
           *handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate,
           1 /* resample = false */, latency)) < 0) {
    printf("snd_pcm_set_params error: %s\n", snd_strerror(rc));
    return false;
  }

  if (stream_type == SND_PCM_STREAM_CAPTURE) {
    snd_pcm_sw_params_t* sw_params = NULL;
    if ((rc = snd_pcm_sw_params_malloc(&sw_params)) < 0) {
      printf("snd_pcm_sw_params_malloc error: %s\n", snd_strerror(rc));
      return false;
    }

    if ((rc = snd_pcm_sw_params_current(*handle, sw_params)) < 0) {
      printf("snd_pcm_sw_params_current error: %s\n", snd_strerror(rc));
      return false;
    }

    if ((rc = snd_pcm_sw_params_set_start_threshold(*handle, sw_params, 1)) < 0) {
      printf("snd_pcm_sw_params_set_start_threshold failed: %s\n", snd_strerror(rc));
      return false;
    }

    if ((rc = snd_pcm_sw_params(*handle, sw_params)) < 0) {
      printf("snd_pcm_sw_params failed: %s\n", snd_strerror(rc));
      return false;
    }

    snd_pcm_sw_params_free(sw_params);
  }

  // snd_pcm_dump(*handle, log);
  snd_output_close(log);
  return true;
}

bool
CloseAudioDevice(snd_pcm_t** handle)
{
  if (*handle != NULL) {
    snd_pcm_drain(*handle);
    snd_pcm_close(*handle);
    *handle = nullptr;
  }
  return true;
}

void
AppendResult(
    Results& output_result, const nr_asr::SpeechRecognitionResult& result, bool word_time_offsets,
    bool speaker_diarization)
{
  if (output_result.final_transcripts.size() < 1) {
    output_result.final_transcripts.resize(1);
    output_result.final_transcripts[0] = "";
  }

  int num_alternatives = result.alternatives_size();
  output_result.final_transcripts.resize(num_alternatives);
  output_result.final_scores.resize(num_alternatives);
  output_result.final_time_stamps.resize(num_alternatives);
  for (int a = 0; a < num_alternatives; ++a) {
    // Append to transcript
    output_result.final_transcripts[a] += result.alternatives(a).transcript();
    output_result.final_scores[a] += result.alternatives(a).confidence();
  }
  if (word_time_offsets || speaker_diarization) {
    if (num_alternatives > 0) {
      for (int a = 0; a < num_alternatives; ++a) {
        for (int w = 0; w < result.alternatives(a).words_size(); ++w) {
          output_result.final_time_stamps[a].push_back(result.alternatives(a).words(w));
        }
      }
    }
  }
  output_result.audio_processed = result.audio_processed();
}

void
PrintResult(
    Results& output_result, const std::string& filename, bool word_time_offsets,
    bool speaker_diarization)
{
  std::cout << "-----------------------------------------------------------" << std::endl;
  std::cout << "File: " << filename << std::endl;
  std::cout << std::endl;
  std::cout << "Final transcripts: " << std::endl;

  if (output_result.final_transcripts.size()) {
    for (uint32_t a = 0; a < output_result.final_transcripts.size(); ++a) {
      std::cout << a << " : " << output_result.final_transcripts[a] << std::endl;
      std::cout << std::endl;

      if (word_time_offsets || speaker_diarization) {
        std::cout << std::setw(40) << std::left << "Word";
        if (word_time_offsets) {
          std::cout << std::setw(16) << std::left << "Start (ms)";
          std::cout << std::setw(16) << std::left << "End (ms)";
        }
        std::cout << std::setw(16) << std::left << "Confidence";
        if (a == 0 && speaker_diarization) {
          std::cout << std::setw(16) << std::left << "Speaker";
        }
        std::cout << std::endl;
        for (uint32_t w = 0; a < output_result.final_time_stamps.size() &&
                             w < output_result.final_time_stamps[a].size();
             ++w) {
          auto& word_info = output_result.final_time_stamps[a][w];
          std::cout << std::setw(40) << std::left << word_info.word();
          if (word_time_offsets) {
            std::cout << std::setw(16) << std::left << word_info.start_time();
            std::cout << std::setw(16) << std::left << word_info.end_time();
          }
          std::cout << std::setw(16) << std::setprecision(4) << std::scientific
                    << word_info.confidence();
          if (a == 0 && speaker_diarization) {
            std::cout << std::setw(16) << std::left << word_info.speaker_tag();
          }
          std::cout << std::endl;
        }
      }
      std::cout << std::endl;
    }
  }
  std::cout << "Audio processed: " << output_result.audio_processed << " sec." << std::endl;
  std::cout << "-----------------------------------------------------------" << std::endl;
  std::cout << std::endl;
}

std::unordered_map<std::string, std::string>
ReadCustomConfiguration(std::string& custom_configuration)
{
  custom_configuration = absl::StrReplaceAll(custom_configuration, {{" ", ""}});
  std::unordered_map<std::string, std::string> custom_configuration_map;
  // Split the input string by commas to get key-value pairs

  std::vector<absl::string_view> pairs = absl::StrSplit(custom_configuration, ',');
  for (const auto& pair : pairs) {
    // Split each pair by colon to separate the key and value
    if (pair != "") {
      std::vector<absl::string_view> key_value = absl::StrSplit(pair, absl::ByString(":"));
      if (key_value.size() == 2) {
        if (custom_configuration_map.find(std::string(key_value[0])) ==
            custom_configuration_map.end()) {
          // If the key does not exist, insert the new key-value pair
          custom_configuration_map[std::string(key_value[0])] = std::string(key_value[1]);
        } else {
          std::string err = "custom_configuration key already used " + std::string(key_value[0]);
          throw std::runtime_error(err);
        }
      } else {
        std::string err = "Invalid custom_configuration key:value pair " + std::string(pair);
        throw std::runtime_error(err);
      }
    }
  }
  return custom_configuration_map;
}