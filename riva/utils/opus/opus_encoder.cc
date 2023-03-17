/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "opus_encoder.h"

#include <glog/logging.h>

#include <iostream>

namespace riva::utils::opus {

Encoder::Encoder(int32_t rate, int32_t channels)
    : encoder_(nullptr), rate_(rate), channels_(channels)
{
  int err;
  encoder_ = opus_encoder_create(rate, channels, OPUS_APPLICATION_AUDIO, &err);
  if (err < 0) {
    LOG(ERROR) << "Failed to create OPUS Encoder: " << opus_strerror(err);
    return;
  }
}

Encoder::~Encoder()
{
  if (encoder_ != nullptr) {
    opus_encoder_destroy(encoder_);
  }
}

int32_t
Encoder::MaxPossibleFrameSize(int32_t ceiling) const
{
  //
  // Note: Opus doc is not very specific about all possible frame lengths.
  // This is their validation code which we follow (here Fs stands for sample rate):
  //
  //  if (400*new_size!=Fs  && 200*new_size!=Fs   && 100*new_size!=Fs   &&
  //      50*new_size!=Fs   &&  25*new_size!=Fs   &&  50*new_size!=3*Fs &&
  //      50*new_size!=4*Fs &&  50*new_size!=5*Fs &&  50*new_size!=6*Fs)
  //      return -1;
  //
  // RFC says:
  // Opus can encode frames of 2.5, 5, 10, 20, 40, or 60 ms.  It can also
  // combine multiple frames into packets of up to 120 ms.
  //
  const int32_t quantum = rate_ / (400 * channels_);
  const auto mults = {48, 40, 32, 24, 16, 8, 4, 2, 1};
  for (auto mult : mults) {
    int32_t ret = quantum * mult;
    if (ret <= ceiling) {
      return ret;
    }
  }
  return quantum;
}

std::vector<std::vector<unsigned char>>
Encoder::EncodePcm(const std::vector<int16_t>& pcm, bool last_chunk, int32_t* samples_encoded) const
{
  std::vector<std::vector<unsigned char>> ret;
  auto bytes_to_encode = (int32_t)(pcm.size() * sizeof(int16_t));
  int32_t pos = 0;
  if (samples_encoded != nullptr) {
    *samples_encoded = 0;
  }
  int32_t last_frame_size = 0;
  while (bytes_to_encode > 0) {
    const int32_t frame_size = MaxPossibleFrameSize((int32_t)(pcm.size() - pos));
    if (!last_chunk && frame_size < last_frame_size) {
      // don't slow down, postpone till next chunk
      break;
    }
    last_frame_size = frame_size;
    const int32_t frame_size_byte = frame_size * (int32_t)sizeof(int16_t);
    if (frame_size_byte > bytes_to_encode) {
      break;
    }
    std::vector<unsigned char> encoded_frame(frame_size_byte);
    int32_t bytes_encoded =
        opus_encode(encoder_, &pcm[pos], frame_size, encoded_frame.data(), frame_size_byte);
    if (bytes_encoded < 0) {
      LOG(ERROR) << "Failed to encode: " << opus_strerror(bytes_encoded)
                 << ", bytes_to_encode: " << bytes_to_encode << ", frame_length: " << frame_size;
      break;
    }
    pos += frame_size;
    if (samples_encoded != nullptr) {
      *samples_encoded += frame_size;
    }
    if (bytes_encoded > 0) {
      ret.emplace_back(
          std::vector<unsigned char>{encoded_frame.data(), encoded_frame.data() + bytes_encoded});
    }
    if (bytes_to_encode >= frame_size_byte) {
      bytes_to_encode -= frame_size_byte;
    }
  }
  return ret;
}

std::vector<unsigned char>
Encoder::SerializeOpus(const std::vector<std::vector<unsigned char>>& opus) const
{
  std::size_t length = 0U;
  for (auto& frame : opus) {
    length += frame.size() + sizeof(int32_t);
  }
  std::vector<unsigned char> ret(length);
  std::size_t pos = 0U;
  for (auto& frame : opus) {
    StoreLittleEndian(ret.data() + pos, (int32_t)frame.size());
    pos += sizeof(int32_t);
    std::memcpy(ret.data() + pos, frame.data(), frame.size());
    pos += frame.size();
  }
  return ret;
}

int32_t
Encoder::AdjustRateIfUnsupported(int32_t rate)
{
  int32_t adjusted_rate = rate;
  if (rate > 48000) {
    adjusted_rate = 48000;
  } else if (rate > 24000 && rate < 48000) {
    adjusted_rate = 24000;
  } else if (rate > 16000 && rate < 24000) {
    adjusted_rate = 16000;
  } else if (rate > 8000 && rate < 16000) {
    adjusted_rate = 8000;
  } else if (rate < 8000) {
    adjusted_rate = 8000;
  }
  return adjusted_rate;
}

}  // namespace riva::utils::opus
