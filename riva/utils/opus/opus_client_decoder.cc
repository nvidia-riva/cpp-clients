/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "opus_client_decoder.h"

#include <glog/logging.h>

namespace riva::utils::opus {

Decoder::Decoder(int rate, int channels)
    : decoder_(nullptr), opus_file_(nullptr), opus_tags_{}, rate_(rate), channels_(channels),
      length_(0.F)
{
}

Decoder::~Decoder()
{
  if (decoder_ != nullptr) {
    opus_decoder_destroy(decoder_);
  }
  if (opus_file_ != nullptr) {
    op_free(opus_file_);
  }
}

std::vector<float>
Decoder::DecodeStream(std::istream& is)
{
  std::stringstream istream_buffer;
  istream_buffer << is.rdbuf();
  return DecodeChunk(istream_buffer.str());
}

std::vector<float>
Decoder::DecodeChunk(const std::string& chunk)
{
  std::vector<float> ret;
  float pcmdata[READ_SIZE];
  int err = 0;
  opus_file_ = op_open_memory((const unsigned char*)chunk.data(), chunk.size(), &err);
  if (opus_file_ == nullptr) {
    LOG(ERROR) << "Opus content can't be parsed, error " << opus_strerror(err);
    return ret;
  }
  while ((err = op_read_float(opus_file_, pcmdata, READ_SIZE, nullptr)) > 0) {
    ret.insert(ret.end(), pcmdata, pcmdata + err);
  }
  if (err == 0) {
    const std::size_t head_pos = chunk.find("OpusHead");
    if (head_pos != std::string::npos) {
      const unsigned char* ptr = (const unsigned char*)chunk.data() + head_pos + 12;
      rate_ = ReadLittleEndian<int32_t>(ptr);
    } else {
      LOG(ERROR) << "OpusHead can't be parsed";
      return ret;
    }
    channels_ = op_channel_count(opus_file_, -1);
    length_ = (float)op_pcm_total(opus_file_, -1) / (float)rate_;
    const std::size_t tags_pos = chunk.find("OpusTags");
    if (tags_pos != std::string::npos) {
      err = opus_tags_parse(
          &opus_tags_, (const unsigned char*)chunk.data() + tags_pos, chunk.size() - tags_pos);
      if (err != 0) {
        LOG(ERROR) << "OpusTags can't be parsed, error " << opus_strerror(err);
        return ret;
      }
    }
  } else {
    LOG(ERROR) << "Opus file can't be parsed, error " << opus_strerror(err);
    return ret;
  }
  return ret;
}

std::vector<int16_t>
Decoder::DecodePcm(const std::vector<unsigned char>& packet)
{
  if (decoder_ == nullptr) {
    int err;
    decoder_ = opus_decoder_create(rate_, channels_, &err);
    if (err < 0) {
      LOG(ERROR) << "Failed to create encoder: " << opus_strerror(err);
      return {};
    }
  }
  // the longest frame length accepted
  const std::size_t frame_length = rate_ * 6 / (50 * channels_);
  std::vector<int16_t> ret(frame_length);
  int samples = opus_decode(decoder_, packet.data(), packet.size(), ret.data(), frame_length, 0);
  if (samples < 0) {
    LOG(ERROR) << "Decoding error: " << opus_strerror(samples);
    return {};
  }
  return {ret.data(), ret.data() + samples};
}

std::vector<int16_t>
Decoder::DecodePcm(const std::vector<std::vector<unsigned char>>& packets)
{
  std::vector<int16_t> ret;
  for (auto& packet : packets) {
    std::vector<int16_t> packet_decoded = DecodePcm(packet);
    ret.insert(ret.end(), packet_decoded.begin(), packet_decoded.end());
  }
  return ret;
}

std::vector<std::vector<unsigned char>>
Decoder::DeserializeOpus(const std::vector<unsigned char>& opus) const
{
  std::vector<std::vector<unsigned char>> ret;
  std::size_t length = opus.size();
  std::size_t pos = 0U;
  while (pos < length) {
    auto frame_size = ReadLittleEndian<int32_t>(opus.data() + pos);
    pos += sizeof(int32_t);
    ret.emplace_back(std::vector<unsigned char>(opus.data() + pos, opus.data() + pos + frame_size));
    pos += frame_size;
  }
  return ret;
}

int32_t
Decoder::AdjustRateIfUnsupported(int32_t rate)
{
  int32_t adjusted_rate = 0;
  if (rate < 800) {
    adjusted_rate = 800;
  } else if (rate > 800 && rate < 16000) {
    adjusted_rate = 16000;
  } else if (rate > 16000 && rate < 24000) {
    adjusted_rate = 24000;
  } else {
    adjusted_rate = 48000;
  }
  return adjusted_rate;
}

}  // namespace riva::utils::opus
