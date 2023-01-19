/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "opus_decoder.h"

#include <glog/logging.h>

namespace riva::utils::opus {

int
ReaderCallBack(void* _decoder, unsigned char* ptr, int nbytes)
{
  Decoder* decoder = (Decoder*)_decoder;
  if (decoder->opus_file_ == nullptr) {
    return 0;
  }
  if (nbytes > decoder->buffer_.unread_) {
    nbytes = decoder->buffer_.unread_;
  }
  if (nbytes > 0) {
    std::memcpy(ptr, decoder->buffer_.cur_, nbytes);
    decoder->buffer_.cur_ += nbytes;
    decoder->buffer_.unread_ -= nbytes;
  }
  return nbytes;
}

Decoder::Decoder(int rate, int channels)
    : decoder_(nullptr), callbacks_(), opus_file_(nullptr), buffer_(), rate_(rate),
      channels_(channels)
{
  buffer_.begin_ = buffer_.data_;
  buffer_.cur_ = buffer_.data_;
  buffer_.unread_ = 0;
  callbacks_.read = ReaderCallBack;
  callbacks_.seek = nullptr;
  callbacks_.tell = nullptr;
  callbacks_.close = nullptr;
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

bool
Decoder::EnqueueChunk(const char* data, std::size_t& size)
{
  std::size_t bufferTotal = sizeof(buffer_.data_);
  std::size_t bufferUsed = buffer_.unread_;
  if (bufferUsed + size > bufferTotal) {
    LOG(ERROR) << "Failed to decode " << size << " bytes. Too big chunk: " << bufferUsed << " + "
               << size << " > " << bufferTotal;
    return false;
  }
  buffer_.cur_ = buffer_.begin_;
  if (opus_file_ == nullptr) {
    if (buffer_.unread_ + size > OPUS_HEADER_LENGTH) {
      size = OPUS_HEADER_LENGTH - buffer_.unread_;
    }
    std::memcpy(buffer_.cur_ + buffer_.unread_, data, size);
    buffer_.unread_ += (int)size;
    int err;
    opus_file_ = op_open_callbacks(this, &callbacks_, buffer_.cur_, buffer_.unread_, &err);
    if (err == 0) {
      const std::string head(buffer_.cur_, buffer_.cur_ + buffer_.unread_);
      const std::size_t head_pos = head.find("OpusHead");
      const std::size_t tags_pos = head.find("OpusTags");
      if (head_pos != std::string::npos && tags_pos != std::string::npos) {
        // TODO: Find out why this thing returns err=-133 no matter what.
        // err = opus_head_parse(&opus_head_, head.data() + head_pos, tags_pos - head_pos);
        const unsigned char* headpos = (const unsigned char*)head.data() + head_pos;
        opus_head_.version = (int)ReadLittleEndian<uint8_t>(headpos + 8);
        opus_head_.channel_count = (int)ReadLittleEndian<uint8_t>(headpos + 9);
        opus_head_.pre_skip = ReadLittleEndian<uint16_t>(headpos + 10);
        opus_head_.input_sample_rate = ReadLittleEndian<uint16_t>(headpos + 12);
        opus_head_.output_gain = ReadLittleEndian<uint16_t>(headpos + 16);
        // Currently, we don't use these tags, but we might will.
        //        opus_tags_parse(&opus_tags_, (const unsigned char*)head.data() + tags_pos,
        //                        buffer_.unread_ - tags_pos);
      }
      LOG(INFO) << "OggOpusFile instantiated, " << buffer_.unread_ << " bytes read so far";
      buffer_.unread_ = 0;
    } else {
      return false;
    }
  } else {
    buffer_.unread_ += (int)size;
    std::memcpy(buffer_.cur_, data, size);
  }
  return true;
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
  float float_buffer[DECODED_CHUNK_SIZE];
  std::vector<float> ret;
  int decoded;
  std::size_t pos = 0U;
  std::size_t read = 0U;
  while (pos < chunk.size()) {
    read = std::min(chunk.size() - pos, READ_SIZE);
    if (EnqueueChunk(chunk.data() + pos, read)) {
      while ((decoded = op_read_float(opus_file_, float_buffer, DECODED_CHUNK_SIZE, nullptr)) > 0) {
        ret.insert(ret.end(), float_buffer, float_buffer + decoded);
      }
    }
    pos += read;
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
      LOG(ERROR) << "Failed to create decoder: " << opus_strerror(err);
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
  return {ret.data(), ret.data() + samples * channels_};
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

}  // namespace riva::utils::opus
