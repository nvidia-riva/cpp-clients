/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#pragma once

#include <opus.h>

#include <cstdlib>
#include <cstring>
#include <istream>
#include <vector>

namespace riva::utils::opus {

/**
 * Wrapper for Opus encoder (based on lipopus implementation)
 */
class Encoder {
  OpusEncoder* encoder_;
  int32_t rate_;
  int32_t channels_;

 public:
  /**
   * Constructor
   * @param rate accepted 48000, 16000,..
   * @param channels 1 or 2
   */
  Encoder(int32_t rate, int32_t channels);
  ~Encoder();

  /**
   * Bitrate setter, 96000 might be good starting point for speech
   * @param bitrate
   */
  void SetBitrate(int bitrate) { opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate)); }
  [[nodiscard]] int GetBitrate() const
  {
    int32_t rate;
    int err = opus_encoder_ctl(encoder_, OPUS_GET_BITRATE(&rate));
    return err == OPUS_OK ? rate : err;
  }

  /**
   * Variable bitrate setter
   * @param bitrate
   */
  void SetVarBitrate(int bitrate) { opus_encoder_ctl(encoder_, OPUS_SET_VBR(bitrate)); }
  [[nodiscard]] int GetVarBitrate() const
  {
    int32_t rate;
    int err = opus_encoder_ctl(encoder_, OPUS_GET_VBR(&rate));
    return err == OPUS_OK ? rate : err;
  }

  /**
   * 16-bit PCM to OPUS encoder. OPUS works with small frames of size FRAME_SIZE.
   * Each frame usually gets compressed in 1:10 ratio
   * @param pcm
   * @return Array of encoded frames
   */
  [[nodiscard]] std::vector<std::vector<unsigned char>> EncodePcm(
      const std::vector<int16_t>& pcm) const;
  /**
   * This function unifies multiple OPUS encoded frames into one container for sending it by wire.
   * @param opus
   * @return
   */
  [[nodiscard]] std::vector<unsigned char> SerializeOpus(
      const std::vector<std::vector<unsigned char>>& opus) const;

  template <typename T>
  void static StoreLittleEndian(unsigned char* os, T value, unsigned int size = sizeof(T))
  {
    int i = 0;
    for (; size; --size, value >>= 8) {
      os[i++] = static_cast<unsigned char>(value & 0xFF);
    }
  }

 private:
  static inline constexpr std::size_t FRAME_SIZE = 480U;
};

}  // namespace riva::utils::opus