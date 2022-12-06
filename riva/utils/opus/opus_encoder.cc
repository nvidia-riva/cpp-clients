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

std::vector<std::vector<unsigned char>>
Encoder::EncodePcm(const std::vector<int16_t>& pcm) const
{
  std::vector<std::vector<unsigned char>> ret;
  const std::size_t frame = FRAME_SIZE * channels_ * sizeof(int16_t);
  const std::size_t data_length = pcm.size() * sizeof(int16_t);
  for (std::size_t i = 0U; i < data_length; i += frame) {
    std::size_t bytes_to_encode = frame;
    if (i + frame >= data_length) {
      bytes_to_encode = data_length - i;
    }
    std::vector<unsigned char> encoded_frame(bytes_to_encode);
    auto bytes_encoded = opus_encode(
        encoder_, pcm.data() + (i / sizeof(int16_t)), FRAME_SIZE, encoded_frame.data(),
        (int32_t)bytes_to_encode);
    if (bytes_encoded < 0) {
      LOG(ERROR) << "Failed to encode: " << opus_strerror(bytes_encoded) << ", i: " << i
                 << ", bytes_to_encode: " << bytes_to_encode;
      break;
    }
    ret.emplace_back(
        std::vector<unsigned char>{encoded_frame.data(), encoded_frame.data() + bytes_encoded});
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

}  // namespace riva::utils::opus
