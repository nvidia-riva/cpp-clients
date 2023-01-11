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

#include <opus/opusfile.h>

#include <cstdlib>
#include <cstring>
#include <istream>
#include <vector>

namespace riva::utils::opus {

struct Buffer {
  //  As per https://xiph.org/ogg/doc/oggstream.html
  //  pages are at maximum of just under 64kB
  unsigned char data_[64 * 1024];
  unsigned char *begin_, *cur_;
  int unread_;
};

class Decoder {
 public:
  OpusDecoder* decoder_;
  OpusFileCallbacks callbacks_;
  OggOpusFile* opus_file_;
  OpusHead opus_head_;
  //  OpusTags opus_tags_;
  Buffer buffer_;

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
  std::vector<float> DecodeChunk(const std::string& chunk);

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


  template <typename T>
  static T ReadLittleEndian(const unsigned char* str)
  {
    T val = T();
    T shift = 0;
    for (int i = 0; i < sizeof(T); ++i) {
      val += T(str[i] & 0xFF) << shift;
      shift += 8;
    }
    return val;
  }

 private:
  int rate_;
  int channels_;

  bool EnqueueChunk(const char* data, std::size_t& size);
  // Header length
  static inline constexpr std::size_t OPUS_HEADER_LENGTH = 8192U;
  // While testing, it's been noticed that op_read_float consumes data stream
  // by chunks of 5120 bytes. Technically, we can set whatever size we want here, but it makes
  // more sense to use their step size for better performance.
  static inline constexpr std::size_t READ_SIZE = 5120U;
  // According to https://wiki.xiph.org/Opus_Recommended_Settings
  // "Opus can encode frames of 2.5, 5, 10, 20, 40, or 60 ms.
  // It can also combine multiple frames into packets of up to 120 ms."
  static inline constexpr std::size_t DECODED_CHUNK_SIZE = 120U * 48U;  // 120ms x 48khz
};

}  // namespace riva::utils::opus
