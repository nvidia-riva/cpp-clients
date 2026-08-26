/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "audio_chunks.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

#include "riva/utils/wav/wav_data.h"
#include "riva/utils/wav/wav_reader.h"

namespace nvidia::riva::realtime {

// ============================================================================
// Base AudioChunks class implementation
// ============================================================================

AudioChunks::AudioChunks(const int& chunk_size_ms) : chunk_size_ms_(chunk_size_ms) {}

void
AudioChunks::CalculateChunkSizeBytes(int sample_rate)
{
  chunk_size_bytes_ = (sample_rate * chunk_size_ms_ / 1000) * sizeof(int16_t);
  std::cout << "[AudioChunks] Calculated chunk size: " << chunk_size_bytes_ << " bytes"
            << std::endl;
}

std::string
AudioChunks::EncodeBase64(const std::vector<char>& data)
{
  const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";

  std::string result;
  int val = 0, valb = -6;

  for (unsigned char c : data) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      result.push_back(base64_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }

  if (valb > -6) {
    result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  }

  while (result.size() % 4) {
    result.push_back('=');
  }

  return result;
}

bool
AudioChunks::Init()
{
  if (initialized_) {
    std::cout << "[AudioChunks] Chunks already initialized" << std::endl;
    return true;
  }

  std::cout << "[AudioChunks] Initializing audio chunks..." << std::endl;

  if (!InitializeAudio()) {
    std::cerr << "[AudioChunks] Error: Failed to initialize audio" << std::endl;
    return false;
  }

  ProcessAudioData();

  initialized_ = true;
  std::cout << "[AudioChunks] Successfully initialized with " << chunk_base64s_.size() << " chunks"
            << std::endl;

  return initialized_;
}

// Getter implementations
size_t
AudioChunks::GetChunkSizeMs() const
{
  return chunk_size_ms_;
}

size_t
AudioChunks::GetChunkSizeBytes() const
{
  return chunk_size_bytes_;
}

bool
AudioChunks::IsInitialized() const
{
  return initialized_;
}

const std::vector<std::string>&
AudioChunks::GetChunkBase64s() const
{
  return chunk_base64s_;
}

// ============================================================================
// FileAudioChunks derived class implementation
// ============================================================================

FileAudioChunks::FileAudioChunks(const std::string& filepath, const int& chunk_size_ms)
    : AudioChunks(chunk_size_ms), filepath_(filepath)
{
}

void
FileAudioChunks::SplitIntoChunks()
{
  const std::vector<char>& raw_data = wav_data_->data;
  size_t total_size = raw_data.size();

  std::cout << "[FileAudioChunks] Splitting WAV file into chunks of " << chunk_size_bytes_
            << " bytes" << std::endl;

  chunk_base64s_.clear();
  for (size_t i = 0; i < total_size; i += chunk_size_bytes_) {
    size_t current_chunk_size = std::min(chunk_size_bytes_, total_size - i);
    std::vector<char> chunk(raw_data.begin() + i, raw_data.begin() + i + current_chunk_size);
    std::string chunk_base64 = EncodeBase64(chunk);
    chunk_base64s_.push_back(chunk_base64);
  }
}

bool
FileAudioChunks::InitializeAudio()
{
  std::cout << "[FileAudioChunks] Initializing file audio for: " << filepath_ << std::endl;
  fs::path path(filepath_);
  std::string extension = path.extension().string();

  // File exists
  if (!fs::exists(filepath_)) {
    std::cerr << "[FileAudioChunks] Error: File does not exist, " << filepath_ << std::endl;
    return false;
  }

  // File is a WAV file
  if (extension != ".wav") {
    std::cerr << "[FileAudioChunks] Error: File is not a WAV file, " << filepath_ << std::endl;
    return false;
  }

  // Load WAV file using the existing WAV utilities
  std::vector<std::shared_ptr<WaveData>> all_wav;
  LoadWavData(all_wav, filepath_);

  if (all_wav.empty()) {
    std::cerr << "[FileAudioChunks] Error: Failed to load WAV file, " << filepath_ << std::endl;
    return false;
  }

  wav_data_ = all_wav[0];  // Use the first WAV file

  CalculateChunkSizeBytes(GetSampleRateHz());

  return true;
}

void
FileAudioChunks::ProcessAudioData()
{
  SplitIntoChunks();
}

// FileAudioChunks getter implementations
std::string
FileAudioChunks::GetFilepath() const
{
  return filepath_;
}

int
FileAudioChunks::GetSampleRateHz() const
{
  return wav_data_->sample_rate;
}

int
FileAudioChunks::GetNumChannels() const
{
  return wav_data_->channels;
}

int
FileAudioChunks::GetBitDepth() const
{
  // Calculate bit depth from data size and sample rate
  if (wav_data_->channels > 0 && wav_data_->sample_rate > 0) {
    return (wav_data_->data.size() * 8) / (wav_data_->channels * wav_data_->sample_rate);
  }
  return 16;  // Default to 16-bit
}

double
FileAudioChunks::GetDurationSeconds() const
{
  if (wav_data_->sample_rate > 0 && wav_data_->channels > 0) {
    return static_cast<double>(wav_data_->data.size()) /
           (wav_data_->sample_rate * wav_data_->channels * 2);  // Assuming 16-bit
  }
  return 0.0;
}

int
FileAudioChunks::GetNumSamples() const
{
  if (wav_data_->channels > 0) {
    return wav_data_->data.size() / (wav_data_->channels * 2);  // Assuming 16-bit
  }
  return 0;
}

// ============================================================================
// MicrophoneChunks derived class implementation
// ============================================================================

MicrophoneChunks::MicrophoneChunks(
    const std::string& device_name, const int& chunk_size_ms, int sample_rate, int num_channels,
    int bit_depth)
    : AudioChunks(chunk_size_ms), device_name_(device_name), alsa_handle_(nullptr),
      sample_rate_(sample_rate), num_channels_(num_channels), bit_depth_(bit_depth),
      is_capturing_(false), request_exit_(false)
{
}

MicrophoneChunks::~MicrophoneChunks()
{
  StopCapture();
  CloseAudioDevice();
}

bool
MicrophoneChunks::OpenAudioDevice()
{
  int rc;
  static snd_output_t* log;

  std::cout << "[MicrophoneChunks] Opening ALSA device: " << device_name_ << std::endl;
  std::cout << "[MicrophoneChunks] Sample rate: " << sample_rate_
            << " Hz, Channels: " << num_channels_ << std::endl;

  if ((rc = snd_pcm_open(&alsa_handle_, device_name_.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    std::cerr << "[MicrophoneChunks] Unable to open PCM device for recording: " << snd_strerror(rc)
              << std::endl;
    return false;
  }

  if ((rc = snd_output_stdio_attach(&log, stderr, 0)) < 0) {
    std::cerr << "[MicrophoneChunks] Unable to attach log output: " << snd_strerror(rc)
              << std::endl;
    return false;
  }

  // Set audio parameters
  snd_pcm_format_t format = (bit_depth_ == 16) ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_S32_LE;
  unsigned int latency = 100000;  // 100ms latency

  if ((rc = snd_pcm_set_params(
           alsa_handle_, format, SND_PCM_ACCESS_RW_INTERLEAVED, num_channels_, sample_rate_, 1,
           latency)) < 0) {
    std::cerr << "[MicrophoneChunks] snd_pcm_set_params error: " << snd_strerror(rc) << std::endl;
    return false;
  }

  // Set software parameters for capture
  snd_pcm_sw_params_t* sw_params = nullptr;
  if ((rc = snd_pcm_sw_params_malloc(&sw_params)) < 0) {
    std::cerr << "[MicrophoneChunks] snd_pcm_sw_params_malloc error: " << snd_strerror(rc)
              << std::endl;
    return false;
  }

  if ((rc = snd_pcm_sw_params_current(alsa_handle_, sw_params)) < 0) {
    std::cerr << "[MicrophoneChunks] snd_pcm_sw_params_current error: " << snd_strerror(rc)
              << std::endl;
    snd_pcm_sw_params_free(sw_params);
    return false;
  }

  if ((rc = snd_pcm_sw_params_set_start_threshold(alsa_handle_, sw_params, 1)) < 0) {
    std::cerr << "[MicrophoneChunks] snd_pcm_sw_params_set_start_threshold failed: "
              << snd_strerror(rc) << std::endl;
    snd_pcm_sw_params_free(sw_params);
    return false;
  }

  if ((rc = snd_pcm_sw_params(alsa_handle_, sw_params)) < 0) {
    std::cerr << "[MicrophoneChunks] snd_pcm_sw_params failed: " << snd_strerror(rc) << std::endl;
    snd_pcm_sw_params_free(sw_params);
    return false;
  }

  snd_pcm_sw_params_free(sw_params);

  std::cout << "[MicrophoneChunks] Successfully opened ALSA device" << std::endl;
  return true;
}

void
MicrophoneChunks::CloseAudioDevice()
{
  if (alsa_handle_) {
    snd_pcm_close(alsa_handle_);
    alsa_handle_ = nullptr;
    std::cout << "[MicrophoneChunks] Closed ALSA device" << std::endl;
  }
}

bool
MicrophoneChunks::InitializeAudio()
{
  std::cout << "[MicrophoneChunks] Initializing microphone audio for device: " << device_name_
            << std::endl;

  if (!OpenAudioDevice()) {
    std::cerr << "[MicrophoneChunks] Error: Failed to open audio device" << std::endl;
    return false;
  }

  CalculateChunkSizeBytes(sample_rate_);

  return true;
}

void
MicrophoneChunks::ProcessAudioData()
{
  // For microphone, we don't pre-process data - it comes in real-time
  // This method is called during Init() but doesn't populate chunks initially
  std::cout << "[MicrophoneChunks] Microphone initialized, ready for capture" << std::endl;
}

bool
MicrophoneChunks::StartCapture()
{
  if (is_capturing_) {
    std::cout << "[MicrophoneChunks] Already capturing audio" << std::endl;
    return true;
  }

  if (!initialized_) {
    std::cerr << "[MicrophoneChunks] Error: Microphone not initialized" << std::endl;
    return false;
  }

  request_exit_ = false;
  is_capturing_ = true;

  // Start capture thread
  capture_thread_ = std::thread(&MicrophoneChunks::CaptureThreadMain, this);

  std::cout << "[MicrophoneChunks] Started audio capture" << std::endl;
  return true;
}

void
MicrophoneChunks::StopCapture()
{
  if (!is_capturing_) {
    return;
  }

  request_exit_ = true;
  is_capturing_ = false;

  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  std::cout << "[MicrophoneChunks] Stopped audio capture" << std::endl;
}

void
MicrophoneChunks::CaptureThreadMain()
{
  std::cout << "[MicrophoneChunks] Capture thread started" << std::endl;

  const size_t chunk_size = chunk_size_bytes_;
  std::vector<char> chunk(chunk_size);

  while (is_capturing_ && !request_exit_) {
    // Read audio chunk from microphone
    snd_pcm_sframes_t frames_read =
        snd_pcm_readi(alsa_handle_, &chunk[0], chunk_size / sizeof(int16_t));

    if (frames_read < 0) {
      std::cerr << "[MicrophoneChunks] Read failed: " << snd_strerror(frames_read) << std::endl;
      // Try to recover from error
      if (snd_pcm_recover(alsa_handle_, frames_read, 0) < 0) {
        std::cerr << "[MicrophoneChunks] Failed to recover from error" << std::endl;
        break;
      }
      continue;
    }

    if (frames_read > 0) {
      // Convert frames to bytes
      size_t bytes_read = frames_read * sizeof(int16_t);

      // Create chunk with actual data read
      std::vector<char> actual_chunk(chunk.begin(), chunk.begin() + bytes_read);
      std::string chunk_base64 = EncodeBase64(actual_chunk);

      // Add to chunks with thread safety
      {
        std::lock_guard<std::mutex> lock(chunks_mutex_);
        chunk_base64s_.push_back(chunk_base64);

        // Keep only last 100 chunks to prevent memory issues
        if (chunk_base64s_.size() > 100) {
          chunk_base64s_.erase(chunk_base64s_.begin());
        }
      }

      // Notify waiting threads
      chunks_cv_.notify_all();

      std::cout << "[MicrophoneChunks] Captured chunk " << chunk_base64s_.size() << " ("
                << bytes_read << " bytes)" << std::endl;
    }
  }

  std::cout << "[MicrophoneChunks] Capture thread ended" << std::endl;
}

// MicrophoneChunks getter implementations
std::string
MicrophoneChunks::GetDeviceName() const
{
  return device_name_;
}

bool
MicrophoneChunks::IsCapturing() const
{
  return is_capturing_;
}

int
MicrophoneChunks::GetSampleRateHz() const
{
  return sample_rate_;
}

int
MicrophoneChunks::GetNumChannels() const
{
  return num_channels_;
}

int
MicrophoneChunks::GetBitDepth() const
{
  return bit_depth_;
}

double
MicrophoneChunks::GetDurationSeconds() const
{
  // For microphone, duration is ongoing - return 0
  return 0.0;
}

int
MicrophoneChunks::GetNumSamples() const
{
  // For microphone, samples are ongoing - return 0
  return 0;
}

std::string
MicrophoneChunks::GetLatestChunk() const
{
  std::lock_guard<std::mutex> lock(chunks_mutex_);
  if (chunk_base64s_.empty()) {
    return "";
  }
  return chunk_base64s_.back();
}

void
MicrophoneChunks::WaitForNewChunk()
{
  std::unique_lock<std::mutex> lock(chunks_mutex_);
  chunks_cv_.wait(lock, [this] { return !chunk_base64s_.empty(); });
}

}  // namespace nvidia::riva::realtime
