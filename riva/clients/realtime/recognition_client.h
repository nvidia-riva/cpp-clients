/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef RECOGNITION_CLIENT_H
#define RECOGNITION_CLIENT_H

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio_chunks.h"
#include "base_client.h"
#include "riva/utils/stats_builder/stats_builder.h"

namespace nvidia::riva::realtime {
    class RecognitionClient : public WebSocketClientBase {
        private:
            
            // Session tracking
            bool sessionInitialized_;
            bool sessionUpdated_;
            std::condition_variable sessionCv_;
            std::mutex sessionMutex_;
            nvidia::riva::utils::PerformanceStats& perfCounter_;
            

            // Event tracking
            bool transcriptionCompleted_;
            std::condition_variable transcriptionCv_;
            std::mutex transcriptionMutex_;

            std::size_t finalTranscriptionCount_;

            // Configurable timing parameters (in milliseconds)
            std::size_t connectionTimeoutInMs_;
            std::size_t sessionInitTimeoutInMs_;
            std::size_t sessionUpdateTimeoutInMs_;
            std::size_t transcriptionTimeoutInMs_;
            std::size_t chunkDelayTimeInMs_;
            
            std::string objectName_;

            // Audio processing
            std::shared_ptr<AudioChunks> audioChunksPtr_;

            // Audio streaming methods
            void SendAudioAppend(const std::string& audioBase64);
            void SendAudioCommit();
            void SendAudioDone();
            
            // Override base class methods
            void HandleMessage(const std::string& message) override;

        public:
            RecognitionClient(  const std::string& objectName,
                                const std::shared_ptr<AudioChunks> audioChunksPtr, 
                                nvidia::riva::utils::PerformanceStats& perfCounter);
            ~RecognitionClient() = default;
            
            void Log(const std::string& message);
            
            // Timing configuration
            void SetTimingConfig(   const std::size_t connectionTimeoutInMs, 
                                    const std::size_t sessionInitTimeoutInMs,
                                    const std::size_t sessionUpdateTimeoutInMs, 
                                    const std::size_t transcriptionTimeoutInMs,
                                    const std::size_t chunkDelayTimeInMs);

            // Session management methods
            bool InitializeSession();
            bool UpdateSessionConfig();

            bool IsSessionInitialized() const { return sessionInitialized_; }
            
            // Wait methods
            bool WaitForSessionUpdate();
            bool WaitForTranscriptionCompletion();
            
            // WAV file processing methods
            void SendAudioChunks(const bool simulateRealtime = false);
    };

} // namespace nvidia::riva::realtime

#endif // RECOGNITION_CLIENT_H