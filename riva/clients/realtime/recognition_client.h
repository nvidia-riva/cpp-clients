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

// Add these includes for HTTP functionality
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>

namespace nvidia::riva::realtime {
    class SessionConfig {
        public:
            std::size_t connectionTimeoutInMs_;
            std::size_t sessionInitTimeoutInMs_;
            std::size_t sessionUpdateTimeoutInMs_;
            std::size_t transcriptionTimeoutInMs_;
            std::size_t chunkDelayTimeInMs_;
            
            // Add session configuration parameters
            std::string language_code_;
            std::string model_name_;
            int max_alternatives_;
            bool automatic_punctuation_;
            bool word_time_offsets_;
            bool profanity_filter_;
            bool verbatim_transcripts_;
            std::string boosted_words_file_;
            double boosted_words_score_;
            bool speaker_diarization_;
            int diarization_max_speakers_;
            int start_history_;
            double start_threshold_;
            int stop_history_;
            double stop_threshold_;
            int stop_history_eou_;
            double stop_threshold_eou_;
            std::string custom_configuration_;
            
            // Add HTTP session data
            std::string session_id_;
            std::string server_url_;
    };
    
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

            // Add session configuration
            SessionConfig sessionConfig_;

            // Add HTTP session data
            std::string session_id_;
            std::string server_url_;
            
            // HTTP session initialization method
            bool InitializeHttpSession();
            
            // Helper method for HTTP requests
            std::string MakeHttpRequest(const std::string& host, int port, const std::string& path, const std::string& method, const std::string& body);

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

            // Session configuration
            void SetSessionConfig(const SessionConfig& config) { sessionConfig_ = config; }

            // Session management methods
            bool InitializeSession();
            bool UpdateSessionConfig();

            bool IsSessionInitialized() const { return sessionInitialized_; }
            
            // Wait methods
            bool WaitForSessionUpdate();
            bool WaitForTranscriptionCompletion();
            
            // WAV file processing methods
            void SendAudioChunks(const bool simulateRealtime = false);
            
            // Add method to set server URL
            void SetServerUrl(const std::string& server_url) { server_url_ = server_url; }
            std::string GetSessionId() const { return session_id_; }
    };

} // namespace nvidia::riva::realtime

#endif // RECOGNITION_CLIENT_H