/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "realtime_client.h"
#include "base_client.h"
#include <chrono>
#include <iomanip>
#include <functional>
#include <ratio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <fstream>

// Helper method for HTTP requests using raw sockets
std::string nvidia::riva::realtime::RealtimeClient::MakeHttpRequest(const std::string& host, int port, const std::string& path, const std::string& method, const std::string& body) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return "";
    }
    
    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
        std::cerr << "Failed to resolve host: " << host << std::endl;
        close(sock);
        return "";
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Failed to connect to " << host << ":" << port << std::endl;
        close(sock);
        return "";
    }
    
    // Build HTTP request
    std::ostringstream request;
    request << method << " " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << ":" << port << "\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << body.length() << "\r\n";
    request << "Connection: close\r\n";
    request << "\r\n";
    request << body;
    
    std::string request_str = request.str();
    
    // Send request
    if (send(sock, request_str.c_str(), request_str.length(), 0) < 0) {
        std::cerr << "Failed to send HTTP request" << std::endl;
        close(sock);
        return "";
    }
    
    // Receive response
    std::string response;
    char buffer[4096];
    int bytes_received;
    
    while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        response += buffer;
    }
    
    close(sock);
    
    // Extract JSON body from HTTP response
    size_t body_start = response.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        return response.substr(body_start + 4);
    }
    
    return response;
}

bool nvidia::riva::realtime::RealtimeClient::InitializeHttpSession() {
    if (server_url_.empty()) {
        std::cerr << "Server URL not set" << std::endl;
        return false;
    }
    
    // Parse server URL to extract host and port
    std::string host = server_url_;
    int port = 80; // Default HTTP port
    
    // Check if port is specified
    size_t colon_pos = host.find(':');
    if (colon_pos != std::string::npos) {
        port = std::stoi(host.substr(colon_pos + 1));
        host = host.substr(0, colon_pos);
    }
    
    std::string path = "/v1/realtime/transcription_sessions";
    std::string response_body = MakeHttpRequest(host, port, path, "POST", "{}");
    
    if (response_body.empty()) {
        std::cerr << "HTTP request failed" << std::endl;
        return false;
    }
    
    try {
        // Parse JSON response using rapidjson
        rapidjson::Document session_data;
        if (session_data.Parse(response_body.c_str()).HasParseError()) {
            std::cerr << "Failed to parse JSON response" << std::endl;
            return false;
        }
        
        // Extract session ID
        if (session_data.HasMember("id")) {
            session_id_ = session_data["id"].GetString();
        } else {
            std::cerr << "No session ID found in response" << std::endl;
            return false;
        }
        
        // Store server defaults but don't overwrite user-provided values
        SessionConfig serverDefaults;
        
        if (session_data.HasMember("input_audio_transcription")) {
            const auto& transcription = session_data["input_audio_transcription"];
            if (transcription.HasMember("language")) {
                serverDefaults.language_code_ = transcription["language"].GetString();
            }
            if (transcription.HasMember("model")) {
                serverDefaults.model_name_ = transcription["model"].GetString();
            }
        }
        
        if (session_data.HasMember("recognition_config")) {
            const auto& recognition = session_data["recognition_config"];
            if (recognition.HasMember("max_alternatives")) {
                serverDefaults.max_alternatives_ = recognition["max_alternatives"].GetInt();
            }
            if (recognition.HasMember("enable_automatic_punctuation")) {
                serverDefaults.automatic_punctuation_ = recognition["enable_automatic_punctuation"].GetBool();
            }
            if (recognition.HasMember("enable_word_time_offsets")) {
                serverDefaults.word_time_offsets_ = recognition["enable_word_time_offsets"].GetBool();
            }
            if (recognition.HasMember("enable_profanity_filter")) {
                serverDefaults.profanity_filter_ = recognition["enable_profanity_filter"].GetBool();
            }
            if (recognition.HasMember("enable_verbatim_transcripts")) {
                serverDefaults.verbatim_transcripts_ = recognition["enable_verbatim_transcripts"].GetBool();
            }
        }
        
        if (session_data.HasMember("speaker_diarization")) {
            const auto& diarization = session_data["speaker_diarization"];
            if (diarization.HasMember("enable_speaker_diarization")) {
                serverDefaults.speaker_diarization_ = diarization["enable_speaker_diarization"].GetBool();
            }
            if (diarization.HasMember("max_speaker_count")) {
                serverDefaults.diarization_max_speakers_ = diarization["max_speaker_count"].GetInt();
            }
        }
        
        if (session_data.HasMember("endpointing_config")) {
            const auto& endpointing = session_data["endpointing_config"];
            if (endpointing.HasMember("start_history")) {
                serverDefaults.start_history_ = endpointing["start_history"].GetInt();
            }
            if (endpointing.HasMember("start_threshold")) {
                serverDefaults.start_threshold_ = endpointing["start_threshold"].GetDouble();
            }
            if (endpointing.HasMember("stop_history")) {
                serverDefaults.stop_history_ = endpointing["stop_history"].GetInt();
            }
            if (endpointing.HasMember("stop_threshold")) {
                serverDefaults.stop_threshold_ = endpointing["stop_threshold"].GetDouble();
            }
            if (endpointing.HasMember("stop_history_eou")) {
                serverDefaults.stop_history_eou_ = endpointing["stop_history_eou"].GetInt();
            }
            if (endpointing.HasMember("stop_threshold_eou")) {
                serverDefaults.stop_threshold_eou_ = endpointing["stop_threshold_eou"].GetDouble();
            }
        }
        
        // Only use server defaults for values that haven't been set by user
        if (sessionConfig_.language_code_.empty()) {
            sessionConfig_.language_code_ = serverDefaults.language_code_;
        }
        if (sessionConfig_.model_name_.empty()) {
            sessionConfig_.model_name_ = serverDefaults.model_name_;
        }
        if (sessionConfig_.max_alternatives_ == 0) {
            sessionConfig_.max_alternatives_ = serverDefaults.max_alternatives_;
        }
        if (sessionConfig_.start_history_ == -1) {
            sessionConfig_.start_history_ = serverDefaults.start_history_;
        }
        if (sessionConfig_.start_threshold_ == -1.0) {
            sessionConfig_.start_threshold_ = serverDefaults.start_threshold_;
        }
        if (sessionConfig_.stop_history_ == -1) {
            sessionConfig_.stop_history_ = serverDefaults.stop_history_;
        }
        if (sessionConfig_.stop_threshold_ == -1.0) {
            sessionConfig_.stop_threshold_ = serverDefaults.stop_threshold_;
        }
        if (sessionConfig_.stop_history_eou_ == -1) {
            sessionConfig_.stop_history_eou_ = serverDefaults.stop_history_eou_;
        }
        if (sessionConfig_.stop_threshold_eou_ == -1.0) {
            sessionConfig_.stop_threshold_eou_ = serverDefaults.stop_threshold_eou_;
        }
        
        // Convert rapidjson document to string for logging
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        session_data.Accept(writer);
        
        std::cout << "[" << objectName_ << "] Session initialized with defaults: " << buffer.GetString() << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse session response: " << e.what() << std::endl;
        return false;
    }
}

nvidia::riva::realtime::RealtimeClient::RealtimeClient( 
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
    
    // Initialize default session config
    sessionConfig_.language_code_ = "en-US";
    sessionConfig_.model_name_ = "parakeet-1.1b-en-US-asr-streaming-silero-vad-asr-bls-ensemble";
    sessionConfig_.max_alternatives_ = 1;
    sessionConfig_.automatic_punctuation_ = true;
    sessionConfig_.word_time_offsets_ = true;
    sessionConfig_.profanity_filter_ = false;
    sessionConfig_.verbatim_transcripts_ = true;
    sessionConfig_.speaker_diarization_ = false;
    sessionConfig_.diarization_max_speakers_ = 4;
}


void nvidia::riva::realtime::RealtimeClient::SetTimingConfig(  const std::size_t connectionTimeoutInMs, 
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

void nvidia::riva::realtime::RealtimeClient::Log(const std::string& message) {
    std::cout << "[" << objectName_ << "]" <<  message << std::endl;
}

bool nvidia::riva::realtime::RealtimeClient::WaitForTranscriptionCompletion() {
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

bool nvidia::riva::realtime::RealtimeClient::WaitForSessionUpdate() {
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
void nvidia::riva::realtime::RealtimeClient::SendAudioAppend(const std::string& audioBase64) 
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
void nvidia::riva::realtime::RealtimeClient::SendAudioCommit() {
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
void nvidia::riva::realtime::RealtimeClient::SendAudioDone() {
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

// Modify the InitializeSession method to call HTTP initialization first
bool nvidia::riva::realtime::RealtimeClient::InitializeSession() {
    std::cout << "[" << objectName_ << "]" <<  " Initializing session..." << std::endl;
    
    // Step 1: Initialize HTTP session
    if (!InitializeHttpSession()) {
        std::cerr << "Failed to initialize HTTP session" << std::endl;
        return false;
    }
    
    // Step 2: Wait for the initial connection and session creation
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    
    // Step 3: Check if we're still connected
    if (IsConnectionClosed()) {
        std::cerr << "Connection lost during session initialization" << std::endl;
        return false;
    }
    
    // Step 4: Update session configuration
    return UpdateSessionConfig();
}

bool nvidia::riva::realtime::RealtimeClient::UpdateSessionConfig() {
    int sampleRateHz = audioChunksPtr_->GetSampleRateHz();
    int numChannels = audioChunksPtr_->GetNumChannels();
    
    std::cout << "Updating session configuration..." << std::endl;
    std::cout << "Using WAV file parameters - Sample rate: " << sampleRateHz 
              << " Hz, Channels: " << numChannels << std::endl;
    
    // Create session configuration using sessionConfig_ (which now has defaults + user overrides)
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
    
    // Create session config
    rapidjson::Value session_config(rapidjson::kObjectType);
    
    // Add modalities
    rapidjson::Value modalities(rapidjson::kArrayType);
    modalities.PushBack(rapidjson::Value("text", allocator), allocator);
    session_config.AddMember("modalities", modalities, allocator);
    
    // Add input audio format
    session_config.AddMember("input_audio_format", rapidjson::Value("pcm16", allocator), allocator);
    
    // Input audio transcription config
    rapidjson::Value transcription_config(rapidjson::kObjectType);
    transcription_config.AddMember("language", rapidjson::Value(sessionConfig_.language_code_.c_str(), allocator), allocator);
    transcription_config.AddMember("model", rapidjson::Value(sessionConfig_.model_name_.c_str(), allocator), allocator);
    transcription_config.AddMember("prompt", rapidjson::Value(rapidjson::kNullType), allocator);
    session_config.AddMember("input_audio_transcription", transcription_config, allocator);
    
    // Input audio params - use actual WAV file parameters
    rapidjson::Value audio_params(rapidjson::kObjectType);
    audio_params.AddMember("sample_rate_hz", sampleRateHz, allocator);
    audio_params.AddMember("num_channels", numChannels, allocator);
    session_config.AddMember("input_audio_params", audio_params, allocator);
    
    // Recognition config - use session configuration
    rapidjson::Value recognition_config(rapidjson::kObjectType);
    recognition_config.AddMember("max_alternatives", sessionConfig_.max_alternatives_, allocator);
    recognition_config.AddMember("enable_automatic_punctuation", sessionConfig_.automatic_punctuation_, allocator);
    recognition_config.AddMember("enable_word_time_offsets", sessionConfig_.word_time_offsets_, allocator);
    recognition_config.AddMember("enable_profanity_filter", sessionConfig_.profanity_filter_, allocator);
    recognition_config.AddMember("enable_verbatim_transcripts", sessionConfig_.verbatim_transcripts_, allocator);
    recognition_config.AddMember("custom_configuration", rapidjson::Value(sessionConfig_.custom_configuration_.c_str(), allocator), allocator);
    session_config.AddMember("recognition_config", recognition_config, allocator);
    
    // Speaker diarization config
    rapidjson::Value diarization_config(rapidjson::kObjectType);
    diarization_config.AddMember("enable_speaker_diarization", sessionConfig_.speaker_diarization_, allocator);
    diarization_config.AddMember("max_speaker_count", sessionConfig_.diarization_max_speakers_, allocator);
    session_config.AddMember("speaker_diarization", diarization_config, allocator);
    
    // Word boosting config
    rapidjson::Value word_boosting_config(rapidjson::kObjectType);
    bool enable_word_boosting = !sessionConfig_.boosted_words_file_.empty();
    word_boosting_config.AddMember("enable_word_boosting", enable_word_boosting, allocator);
    
    if (enable_word_boosting) {
        rapidjson::Value word_list(rapidjson::kArrayType);
        std::ifstream file(sessionConfig_.boosted_words_file_);
        std::string word;
        while (std::getline(file, word)) {
            if (!word.empty()) {
                word_list.PushBack(rapidjson::Value(word.c_str(), allocator), allocator);
            }
        }
        word_boosting_config.AddMember("word_boosting_list", word_list, allocator);
    } else {
        rapidjson::Value empty_list(rapidjson::kArrayType);
        word_boosting_config.AddMember("word_boosting_list", empty_list, allocator);
    }
    session_config.AddMember("word_boosting", word_boosting_config, allocator);
    
    // Endpointing config
    rapidjson::Value endpointing_config(rapidjson::kObjectType);
    endpointing_config.AddMember("start_history", sessionConfig_.start_history_, allocator);
    endpointing_config.AddMember("start_threshold", sessionConfig_.start_threshold_, allocator);
    endpointing_config.AddMember("stop_history", sessionConfig_.stop_history_, allocator);
    endpointing_config.AddMember("stop_threshold", sessionConfig_.stop_threshold_, allocator);
    endpointing_config.AddMember("stop_history_eou", sessionConfig_.stop_history_eou_, allocator);
    endpointing_config.AddMember("stop_threshold_eou", sessionConfig_.stop_threshold_eou_, allocator);
    session_config.AddMember("endpointing_config", endpointing_config, allocator);
    
    // Create update request
    rapidjson::Value update_request(rapidjson::kObjectType);
    update_request.AddMember("type", rapidjson::Value("transcription_session.update", allocator), allocator);
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
void nvidia::riva::realtime::RealtimeClient::SendAudioChunks(const bool simulateRealtime) {
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

void nvidia::riva::realtime::RealtimeClient::HandleMessage(const std::string& message) {
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
 