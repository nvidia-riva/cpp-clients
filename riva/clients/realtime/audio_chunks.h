/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef AUDIO_CHUNKS_H
#define AUDIO_CHUNKS_H

#include <alsa/asoundlib.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "riva/utils/wav/wav_data.h"
#include "riva/utils/wav/wav_reader.h"

namespace fs = std::filesystem;

namespace nvidia::riva::realtime {

// Forward declarations - we'll include the actual headers in the .cpp file
void LoadWavData(std::vector<std::shared_ptr<WaveData>>& all_wav, const std::string& filepath);

// Base class for audio input
class AudioChunks {
 protected:
  bool initialized_ = false;
  size_t chunk_size_ms_;
  size_t chunk_size_bytes_;
  std::vector<std::string> chunk_base64s_;

  // Common methods for derived classes
  void CalculateChunkSizeBytes(int sample_rate);
  std::string EncodeBase64(const std::vector<char>& data);

  // Virtual methods for derived classes to implement
  virtual bool InitializeAudio() = 0;
  virtual void ProcessAudioData() = 0;

 public:
  AudioChunks(const int& chunk_size_ms);
  virtual ~AudioChunks() = default;

  bool Init();

  // Getters
  size_t GetChunkSizeMs() const;
  size_t GetChunkSizeBytes() const;
  bool IsInitialized() const;

  // Audio properties (to be implemented by derived classes)
  virtual int GetSampleRateHz() const = 0;
  virtual int GetNumChannels() const = 0;
  virtual int GetBitDepth() const = 0;
  virtual double GetDurationSeconds() const = 0;
  virtual int GetNumSamples() const = 0;
  const std::vector<std::string>& GetChunkBase64s() const;

  // Iterator support
  using iterator = std::vector<std::string>::iterator;
  using const_iterator = std::vector<std::string>::const_iterator;
  using reverse_iterator = std::vector<std::string>::reverse_iterator;
  using const_reverse_iterator = std::vector<std::string>::const_reverse_iterator;

  // Iterator methods
  iterator begin() { return chunk_base64s_.begin(); }
  const_iterator begin() const { return chunk_base64s_.begin(); }
  iterator end() { return chunk_base64s_.end(); }
  const_iterator end() const { return chunk_base64s_.end(); }

  // Reverse iterator methods
  reverse_iterator rbegin() { return chunk_base64s_.rbegin(); }
  const_reverse_iterator rbegin() const { return chunk_base64s_.rbegin(); }
  reverse_iterator rend() { return chunk_base64s_.rend(); }
  const_reverse_iterator rend() const { return chunk_base64s_.rend(); }

  // Const iterator methods
  const_iterator cbegin() const { return chunk_base64s_.cbegin(); }
  const_iterator cend() const { return chunk_base64s_.cend(); }
  const_reverse_iterator crbegin() const { return chunk_base64s_.crbegin(); }
  const_reverse_iterator crend() const { return chunk_base64s_.crend(); }

  // Size methods
  size_t size() const { return chunk_base64s_.size(); }
  bool empty() const { return chunk_base64s_.empty(); }
};

// Derived class for file-based audio input
class FileAudioChunks : public AudioChunks {
 private:
  std::string filepath_;
  std::shared_ptr<WaveData> wav_data_;

  void SplitIntoChunks();
  bool InitializeAudio() override;
  void ProcessAudioData() override;

 public:
  FileAudioChunks(const std::string& filepath, const int& chunk_size_ms);
  ~FileAudioChunks() = default;

  std::string GetFilepath() const;
  int GetSampleRateHz() const override;
  int GetNumChannels() const override;
  int GetBitDepth() const override;
  double GetDurationSeconds() const override;
  int GetNumSamples() const override;
};

// Derived class for microphone input
class MicrophoneChunks : public AudioChunks {
 private:
  std::string device_name_;
  snd_pcm_t* alsa_handle_;
  std::thread capture_thread_;
  std::atomic<bool> is_capturing_;
  std::atomic<bool> request_exit_;
  mutable std::mutex chunks_mutex_;  // Make mutable for const member functions
  std::condition_variable chunks_cv_;

  // Audio capture parameters
  int sample_rate_;
  int num_channels_;
  int bit_depth_;

  // Capture thread function
  void CaptureThreadMain();
  bool OpenAudioDevice();
  void CloseAudioDevice();
  bool InitializeAudio() override;
  void ProcessAudioData() override;

 public:
  MicrophoneChunks(
      const std::string& device_name, const int& chunk_size_ms, int sample_rate = 16000,
      int num_channels = 1, int bit_depth = 16);
  ~MicrophoneChunks();

  // Microphone-specific methods
  bool StartCapture();
  void StopCapture();
  bool IsCapturing() const;
  std::string GetDeviceName() const;

  // Audio properties
  int GetSampleRateHz() const override;
  int GetNumChannels() const override;
  int GetBitDepth() const override;
  double GetDurationSeconds() const override;
  int GetNumSamples() const override;

  // Real-time chunk access
  std::string GetLatestChunk() const;
  void WaitForNewChunk();
};

}  // namespace nvidia::riva::realtime

#endif  // AUDIO_CHUNKS_H