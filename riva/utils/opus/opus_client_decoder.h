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

#include <opusfile.h>

#include <cstdlib>
#include <cstring>
#include <istream>
#include <vector>

namespace riva::utils::opus {

class Decoder {
 public:
  OpusDecoder* decoder_;
  OggOpusFile* opus_file_;
  OpusTags opus_tags_;

  Decoder(int rate = 48000, int channels = 1);
  ~Decoder();

  /**
   * Decode from stream till it ends.
   * @param is input stream
   * @return wave
   */
  std::vector<float> DecodeStream(std::istream& is);

  /**
   * Decode raw audio buffer
   * @param audio
   * @return
   */
  std::vector<float> DecodeOGG(const std::string& ogg);

  /**
   * Streaming decoder for single OPUS frame
   * @param packet
   * @return
   */
  std::vector<int16_t> DecodePcm(const std::vector<unsigned char>& packet);

  /**
   * Streaming decoder for multiple OPUS frames
   * @param packets
   * @return
   */
  std::vector<int16_t> DecodePcm(const std::vector<std::vector<unsigned char>>& packets);

  /**
   * Deserializer of multiple OPUS frames serialized by Encoder. Call it before DecodePcm
   * @param opus
   * @return
   */
  [[nodiscard]] std::vector<std::vector<unsigned char>> DeserializeOpus(
      const std::vector<unsigned char>& opus) const;

  /**
   * Bitrate of decoded audio
   * @return rate
   */
  [[nodiscard]] int Rate() const { return rate_; }

  /**
   * Number of channels of decoded audio
   * @return channels
   */
  [[nodiscard]] int Channels() const { return channels_; }

  /**
   * Returns OpusTags structure after parsing, empty otherwise
   * @return tags
   */
  [[nodiscard]] OpusTags Tags() const { return opus_tags_; }

  /**
   * Length of decoded audio in seconds
   * @return length
   */
  [[nodiscard]] float Length() const { return length_; }

  template <typename T>
  static T ReadLittleEndian(const unsigned char* str)
  {
    T val = T();
    T shift = 0;
    for (int i = 0; i < (int)sizeof(T); ++i) {
      val += T(str[i] & 0xFF) << shift;
      shift += 8;
    }
    return val;
  }

  /**
 * If requested rate is not supported this helper computes nearest supported one.
 * @param rate
 * @return
 */
  static int32_t AdjustRateIfUnsupported(int32_t rate);

 private:
  int rate_;
  int channels_;
  float length_;
  // While testing, it's been noticed that op_read_float consumes data stream
  // by chunks of 5120 bytes. Technically, we can set whatever size we want here, but it makes
  // more sense to use their step size for better performance.
  static inline constexpr std::size_t READ_SIZE = 5120U;
};

}  // namespace riva::utils::opus
