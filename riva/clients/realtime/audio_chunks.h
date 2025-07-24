/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef AUDIO_CHUNKS_H
#define AUDIO_CHUNKS_H

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include "riva/utils/wav/wav_reader.h"
#include "riva/utils/wav/wav_data.h"

namespace fs = std::filesystem;

namespace nvidia::riva::realtime {
    class AudioChunks {
    private:
        bool initialized_ = false;
        std::string filepath_;
        size_t chunk_size_ms_;
        size_t chunk_size_bytes_;
        std::shared_ptr<WaveData> wav_data_;
        std::vector<std::string> chunk_base64s_;

        void CalculateChunkSizeBytes();
        void SplitIntoChunks();
        std::string EncodeBase64(const std::vector<char>& data);

    public:
        AudioChunks(const std::string& filepath, const int& chunk_size_ms);
        ~AudioChunks() = default;

        bool Init();

        // Getters
        std::string GetFilepath() const;
        size_t GetChunkSizeMs() const;
        size_t GetChunkSizeBytes() const;
        bool IsInitialized() const;

        // WAV file properties
        int GetSampleRateHz() const;
        int GetNumChannels() const;
        int GetBitDepth() const;
        double GetDurationSeconds() const;
        int GetNumSamples() const;
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

} // namespace nvidia::riva::realtime

#endif // AUDIO_CHUNKS_H 