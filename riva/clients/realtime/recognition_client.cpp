/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "recognition_client.h"
#include "base_client.h"
#include <chrono>
#include <iomanip>
#include <functional>
#include <ratio>


nvidia::riva::realtime::RecognitionClient::RecognitionClient( 
    const std::string& objectName,
    const std::shared_ptr<AudioChunks> audioChunksPtr,
    nvidia::riva::utils::PerformanceStats& perfCounter)
    : WebSocketClientBase("ws://127.0.0.1:9090/v1/realtime?intent=transcription"),
      sessionInitialized_(false),
      sessionUpdated_(false),
      transcriptionCompleted_(false),
      finalTranscriptionCount_(0),
      connectionTimeoutInMs_(std::size_t(10000)),
      sessionInitTimeoutInMs_(std::size_t(10000)),
      sessionUpdateTimeoutInMs_(std::size_t(10000)),
      transcriptionTimeoutInMs_(std::size_t(10000)),
      chunkDelayTimeInMs_(std::size_t(1000)),
      objectName_(objectName),
      audioChunksPtr_(audioChunksPtr),
      perfCounter_(perfCounter) {

    nvidia::riva::realtime::WebSocketClientBase::SetConnectionTimeout(connectionTimeoutInMs_);
}


void nvidia::riva::realtime::RecognitionClient::SetTimingConfig(  const std::size_t connectionTimeoutInMs, 
                                                    const std::size_t sessionInitTimeoutInMs,
                                                    const std::size_t sessionUpdateTimeoutInMs, 
                                                    const std::size_t transcriptionTimeoutInMs,
                                                    const std::size_t chunkDelayTimeInMs) {
    connectionTimeoutInMs_ = connectionTimeoutInMs;
    sessionInitTimeoutInMs_ = sessionInitTimeoutInMs;
    sessionUpdateTimeoutInMs_ = sessionUpdateTimeoutInMs;
    transcriptionTimeoutInMs_ = transcriptionTimeoutInMs;
    chunkDelayTimeInMs_ = chunkDelayTimeInMs;
    nvidia::riva::realtime::WebSocketClientBase::SetConnectionTimeout(connectionTimeoutInMs_);
}

void nvidia::riva::realtime::RecognitionClient::Log(const std::string& message) {
    std::cout << "[" << objectName_ << "]" <<  message << std::endl;
}

bool nvidia::riva::realtime::RecognitionClient::WaitForTranscriptionCompletion() {
    std::unique_lock<std::mutex> lock(transcriptionMutex_);
    
    // Reset completion flag
    transcriptionCompleted_ = false;
    
    // Wait for completion event with timeout (increased from 3 seconds to 10 seconds)
    bool completed = transcriptionCv_.wait_for(lock, 
        std::chrono::milliseconds(transcriptionTimeoutInMs_), 
        [this] { return transcriptionCompleted_; });
    
    if (!completed) {
        Log(" Timeout waiting for transcription completion after " + std::to_string(transcriptionTimeoutInMs_) + " milliseconds");
    }
    else if (transcriptionCompleted_) {
        // Close the connection
        Close();
    }
    
    return completed;
}

bool nvidia::riva::realtime::RecognitionClient::WaitForSessionUpdate() {
    std::unique_lock<std::mutex> lock(sessionMutex_);
    
    if (sessionUpdated_) {
        return true;
    }

    // Wait for session update event with timeout
    sessionUpdated_ = sessionCv_.wait_for(
        lock,
        std::chrono::milliseconds(sessionUpdateTimeoutInMs_),
        [this] { return sessionUpdated_; }
    );

    if (!sessionUpdated_) {
        Log("Timeout waiting for session update after " + std::to_string(sessionUpdateTimeoutInMs_) + " milliseconds");
    }
    
    return sessionUpdated_;
}

// Send audio buffer append message (inspired by Python realtime.py)
void nvidia::riva::realtime::RecognitionClient::SendAudioAppend(const std::string& audioBase64) 
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (IsConnectionOpen()) 
    {
        rapidjson::Document doc;
        doc.SetObject();
        rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
        doc.AddMember("type", rapidjson::Value("input_audio_buffer.append", allocator), allocator);
        doc.AddMember("audio", rapidjson::Value(audioBase64.c_str(), allocator), allocator);
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        websocketpp::lib::error_code ec;
        wsClient_.send(connectionHdl_, buffer.GetString(), websocketpp::frame::opcode::text, ec);
        if (ec) {
            Log("Audio append failed: " + ec.message());
            // Mark connection as failed
            {
                std::lock_guard<std::mutex> conn_lock(connectionMutex_);
                connectionClosedByServer_ = true;
            }
        }
    } 
    else {
        Log("Skipping audio append - connection closed");
    }
}

// Send audio buffer commit message (inspired by Python realtime.py)
void nvidia::riva::realtime::RecognitionClient::SendAudioCommit() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (IsConnectionOpen()) 
    {
        rapidjson::Document doc;
        doc.SetObject();
        rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
        
        doc.AddMember("type", rapidjson::Value("input_audio_buffer.commit", allocator), allocator);
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        websocketpp::lib::error_code ec;
        wsClient_.send(connectionHdl_, buffer.GetString(), websocketpp::frame::opcode::text, ec);
        if (ec) {
            Log("Audio commit failed: " + ec.message());
            // Mark connection as failed
            {
                std::lock_guard<std::mutex> conn_lock(connectionMutex_);
                connectionClosedByServer_ = true;
            }
        }
    } 
    else {
        Log("Skipping audio commit - connection closed");
    }
}

// Send audio buffer done message (inspired by Python realtime.py)
void nvidia::riva::realtime::RecognitionClient::SendAudioDone() {
    std::lock_guard<std::mutex> lock(connectionMutex_);
    if (IsConnectionOpen()) 
    {
        rapidjson::Document doc;
        doc.SetObject();
        rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
        
        doc.AddMember("type", rapidjson::Value("input_audio_buffer.done", allocator), allocator);
        
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        
        websocketpp::lib::error_code ec;
        wsClient_.send(connectionHdl_, buffer.GetString(), websocketpp::frame::opcode::text, ec);
        if (ec) {
            Log("Audio done failed: " + ec.message());
            // Mark connection as failed
            {
                std::lock_guard<std::mutex> conn_lock(connectionMutex_);
                connectionClosedByServer_ = true;
            }
        } else {
            Log("Audio streaming completed");
        }
    } 
    else {
        Log("Skipping audio done - connection closed");
    }
}

// Session initialization (inspired by Python realtime.py)
bool nvidia::riva::realtime::RecognitionClient::InitializeSession() {
    std::cout << "[" << objectName_ << "]" <<  " Initializing session..." << std::endl;
    
    // Wait for the initial connection and session creation (increased from 1000ms to 3000ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    
    // Check if we're still connected
    if (IsConnectionClosed()) {
        std::cerr << "Connection lost during session initialization" << std::endl;
        return false;
    }
    
    return UpdateSessionConfig();
}

bool nvidia::riva::realtime::RecognitionClient::UpdateSessionConfig() {
    int sampleRateHz = audioChunksPtr_->GetSampleRateHz();
    int numChannels = audioChunksPtr_->GetNumChannels();
    
    std::cout << "Updating session configuration..." << std::endl;
    std::cout << "Using WAV file parameters - Sample rate: " << sampleRateHz 
              << " Hz, Channels: " << numChannels << std::endl;
    
    // Create session configuration similar to Python client
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
    
    // Create session config
    rapidjson::Value session_config(rapidjson::kObjectType);
    
    // Input audio transcription config
    rapidjson::Value transcription_config(rapidjson::kObjectType);
    transcription_config.AddMember("language", "en-US", allocator);
    transcription_config.AddMember("model", "parakeet-1.1b-en-US-asr-streaming-silero-vad-asr-bls-ensemble", allocator);
    transcription_config.AddMember("prompt", "", allocator);
    session_config.AddMember("input_audio_transcription", transcription_config, allocator);
    
    // Input audio params - use actual WAV file parameters
    rapidjson::Value audio_params(rapidjson::kObjectType);
    audio_params.AddMember("sample_rate_hz", sampleRateHz, allocator);
    audio_params.AddMember("num_channels", numChannels, allocator);
    session_config.AddMember("input_audio_params", audio_params, allocator);
    
    // Recognition config
    rapidjson::Value recognition_config(rapidjson::kObjectType);
    recognition_config.AddMember("max_alternatives", 1, allocator);
    recognition_config.AddMember("enable_automatic_punctuation", false, allocator);
    recognition_config.AddMember("enable_word_time_offsets", false, allocator);
    recognition_config.AddMember("enable_profanity_filter", false, allocator);
    recognition_config.AddMember("enable_verbatim_transcripts", false, allocator);
    session_config.AddMember("recognition_config", recognition_config, allocator);
    
    // Create update request
    rapidjson::Value update_request(rapidjson::kObjectType);
    update_request.AddMember("type", "transcription_session.update", allocator);
    update_request.AddMember("session", session_config, allocator);
    
    // Send the update request
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    update_request.Accept(writer);
    
    if (IsConnectionOpen()) 
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        websocketpp::lib::error_code ec;
        wsClient_.send(connectionHdl_, buffer.GetString(), websocketpp::frame::opcode::text, ec);
        if (ec) {
            std::cout << "Session update failed: " << ec.message() << std::endl;
            return false;
        } else {
            std::cout << "Session update request sent" << std::endl;
        }
    }

    WaitForSessionUpdate();
    return true;
}

// Send audio chunks
void nvidia::riva::realtime::RecognitionClient::SendAudioChunks(const bool simulateRealtime) {
    if (audioChunksPtr_ == nullptr) {
        std::cerr << "Audio chunks pointer is null. Please call InitializeSession first." << std::endl;
        return;
    }
    
    if (!IsSessionInitialized()) {
        std::cerr << "Session is not initialized. Please call InitializeSession first." << std::endl;
        return;
    }

    if (audioChunksPtr_->size() == 0) {
        std::cerr << "No audio chunks to send. Please add audio chunks to the audio chunks pointer." << std::endl;
        return;
    }

    std::cout << "Sending audio chunks with " << (simulateRealtime ? "real-time" : "burst") << " timing..." << std::endl;
    
    // Track timing for accurate real-time simulation
    auto stream_start_time = std::chrono::steady_clock::now();
    size_t chunk_index = 0;
    
    for (const std::string& chunk_base64 : *audioChunksPtr_) {
        SendAudioAppend(chunk_base64);
        SendAudioCommit();
        
        if (simulateRealtime) {
            // Calculate the exact time when this chunk should be sent
            auto chunk_duration_ms = audioChunksPtr_->GetChunkSizeMs();
            auto expected_send_time = stream_start_time + 
                std::chrono::milliseconds((chunk_index + 1) * chunk_duration_ms);
            
            auto current_time = std::chrono::steady_clock::now();
            auto time_to_wait = expected_send_time - current_time;
            
            // Log timing information
            // Timing calculations for real-time simulation (commented out as unused)
            // auto elapsed_ms = std::chrono::duration<double, std::milli>(current_time - stream_start_time).count();
            // auto expected_ms = (chunk_index + 1) * chunk_duration_ms;
            // auto drift_ms = elapsed_ms - expected_ms;
            
            //auto wait_ms = std::chrono::duration<double, std::milli>(time_to_wait).count();
            //std::cout << "[" << objectName_ << "] Chunk " << (chunk_index + 1) << "/" << audioChunksPtr_->size() 
            //          << " - Elapsed: " << std::fixed << std::setprecision(1) << elapsed_ms << "ms"
            //          << " Expected: " << expected_ms << "ms"
            //          << " Drift: " << drift_ms << "ms";
            //          << " Waiting: " << wait_ms << "ms" << std::endl;
            
            if (time_to_wait > std::chrono::milliseconds(0)) {
                std::this_thread::sleep_for(time_to_wait);
            }
        } 
        else {
            // Burst mode - just log progress
            if ((chunk_index + 1) % 10 == 0 || chunk_index == audioChunksPtr_->size() - 1) {
                //zstd::cout << "[" << objectName_ << "] Sent " << (chunk_index + 1) << "/" << audioChunksPtr_->size() << " chunks" << std::endl;
            }
        }
        
        chunk_index++;
    }
    SendAudioDone();
}

void nvidia::riva::realtime::RecognitionClient::HandleMessage(const std::string& message) {
    bool is_last_result = false;
    rapidjson::Document doc;
    
    if (doc.Parse(message.c_str()).HasParseError()) {
        std::cerr << "Failed to parse JSON message" << std::endl;
        return;
    }
    
    std::string eventType = doc.HasMember("type") ? doc["type"].GetString() : "";
    
    if (eventType == "conversation.created") {
        std::cout << "Conversation created" << std::endl;
    }
    else if (eventType == "transcription_session.updated") {
        std::cout << "Session updated successfully" << std::endl;
        sessionInitialized_ = true;            
        // Signal session update completion
        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            sessionUpdated_ = true;
        }
        sessionCv_.notify_one();
    }
    else if (eventType == "conversation.item.input_audio_transcription.delta") {
        if (doc.HasMember("delta")) {
            std::string delta = doc["delta"].GetString();
            
            //std::cout << "Delta: " << delta << std::endl;
            std::cout.flush(); // Ensure immediate output for streaming
        }
    }
    else if (eventType == "conversation.item.input_audio_transcription.completed") {
        finalTranscriptionCount_++;
        std::string transcript = doc.HasMember("transcript") ? doc["transcript"].GetString() : "";
        is_last_result = doc.HasMember("is_last_result") ? doc["is_last_result"].GetBool() : false;

        if (is_last_result) {
            std::cout << "--------------------------------" << std::endl;
            std::cout << "Final transcript: " << transcript << std::endl;
            std::cout << "Final transcription count: " << finalTranscriptionCount_ << std::endl;
            std::cout << "--------------------------------" << std::endl;
            
            // Transcription completed
            std::lock_guard<std::mutex> lock(transcriptionMutex_);
            transcriptionCompleted_ = true;
            transcriptionCv_.notify_one();
        } 
        else {
            std::cout << "Interim transcript: " << transcript << std::endl;
        }
    }
    else if (eventType.find("error") != std::string::npos) {
        std::string errorMsg = "Unknown error";
        if (doc.HasMember("error") && doc["error"].HasMember("message")) {
            errorMsg = doc["error"]["message"].GetString();
        }
        std::cerr << "Error: " << errorMsg << std::endl;
    }
    else {
        //std::cout << "Received message type: " << event_type << std::endl;
    }
}
 