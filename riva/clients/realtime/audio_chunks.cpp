/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "audio_chunks.h"
#include "riva/utils/wav/wav_reader.h"
#include "riva/utils/wav/wav_data.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <future>
#include <unistd.h>
#include <vector>

nvidia::riva::realtime::AudioChunks::AudioChunks(const std::string& filepath, const int& chunk_size_ms) 
    : filepath_(filepath), chunk_size_ms_(chunk_size_ms) {
}

void nvidia::riva::realtime::AudioChunks::CalculateChunkSizeBytes() {
    chunk_size_bytes_ = (GetSampleRateHz() * GetChunkSizeMs() / 1000) * sizeof(int16_t);
    std::cout << "[AudioChunks] Calculated chunk size: " << chunk_size_bytes_ << " bytes" << std::endl;
}

void nvidia::riva::realtime::AudioChunks::SplitIntoChunks() {
    const std::vector<char>& raw_data = wav_data_->data;
    size_t total_size = raw_data.size();
    
    std::cout << "[AudioChunks] Splitting WAV file into chunks of " << chunk_size_bytes_ << " bytes" << std::endl;
    
    chunk_base64s_.clear();
    for (size_t i = 0; i < total_size; i += chunk_size_bytes_) {
        size_t current_chunk_size = std::min(chunk_size_bytes_, total_size - i);
        std::vector<char> chunk(raw_data.begin() + i, raw_data.begin() + i + current_chunk_size);
        std::string chunk_base64 = EncodeBase64(chunk);
        chunk_base64s_.push_back(chunk_base64);
    }
}

std::string nvidia::riva::realtime::AudioChunks::EncodeBase64(const std::vector<char>& data) {
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

bool nvidia::riva::realtime::AudioChunks::Init() {
    if (initialized_) {
        std::cout << "[AudioChunks] Chunks already initialized" << std::endl;
        return true;
    }

    std::cout << "[AudioChunks] Initializing chunks for file: " << filepath_ << std::endl;
    fs::path path(filepath_);
    std::string extension = path.extension().string();
    
    // File exists
    if (!fs::exists(filepath_)) {
        std::cerr << "[AudioChunks] Error: File does not exist, " << filepath_ << std::endl;
        return false;
    }

    // File is a WAV file
    if (extension != ".wav") {
        std::cerr << "[AudioChunks] Error: File is not a WAV file, " << filepath_ << std::endl;
        return false;
    }
    
    // Load WAV file using the existing WAV utilities
    std::vector<std::shared_ptr<WaveData>> all_wav;
    LoadWavData(all_wav, filepath_);
    
    if (all_wav.empty()) {
        std::cerr << "[AudioChunks] Error: Failed to load WAV file, " << filepath_ << std::endl;
        return false;
    }
    
    wav_data_ = all_wav[0]; // Use the first WAV file
    
    CalculateChunkSizeBytes();
    SplitIntoChunks();
    
    initialized_ = true;
    
    return initialized_;
}

// Getter implementations
std::string nvidia::riva::realtime::AudioChunks::GetFilepath() const { 
    return filepath_; 
}

size_t nvidia::riva::realtime::AudioChunks::GetChunkSizeMs() const { 
    return chunk_size_ms_; 
}

size_t nvidia::riva::realtime::AudioChunks::GetChunkSizeBytes() const { 
    return chunk_size_bytes_; 
}

bool nvidia::riva::realtime::AudioChunks::IsInitialized() const { 
    return initialized_; 
}

// WAV file properties
int nvidia::riva::realtime::AudioChunks::GetSampleRateHz() const { 
    return wav_data_->sample_rate; 
}

int nvidia::riva::realtime::AudioChunks::GetNumChannels() const { 
    return wav_data_->channels; 
}

int nvidia::riva::realtime::AudioChunks::GetBitDepth() const { 
    // Calculate bit depth from data size and sample rate
    if (wav_data_->channels > 0 && wav_data_->sample_rate > 0) {
        return (wav_data_->data.size() * 8) / (wav_data_->channels * wav_data_->sample_rate);
    }
    return 16; // Default to 16-bit
}

double nvidia::riva::realtime::AudioChunks::GetDurationSeconds() const { 
    if (wav_data_->sample_rate > 0 && wav_data_->channels > 0) {
        return static_cast<double>(wav_data_->data.size()) / (wav_data_->sample_rate * wav_data_->channels * 2); // Assuming 16-bit
    }
    return 0.0;
}

int nvidia::riva::realtime::AudioChunks::GetNumSamples() const { 
    if (wav_data_->channels > 0) {
        return wav_data_->data.size() / (wav_data_->channels * 2); // Assuming 16-bit
    }
    return 0;
}
