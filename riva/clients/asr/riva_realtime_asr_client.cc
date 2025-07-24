/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <signal.h>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <future>
#include <unistd.h>
#include <vector>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "riva/clients/realtime/recognition_client.h"
#include "riva/utils/stats_builder/stats_builder.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

// Add these includes for HTTP functionality
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>

using namespace nvidia::riva::utils;
using namespace nvidia::riva::realtime;

// Define command-line flags (matching streaming client)
DEFINE_string(audio_file, "", "Folder that contains audio files to transcribe or individual audio file name");
DEFINE_int32(max_alternatives, 1, "Maximum number of alternative transcripts to return (up to limit configured on server)");
DEFINE_bool(profanity_filter, false, "Flag that controls if generated transcripts should be filtered for the profane words");
DEFINE_bool(automatic_punctuation, true, "Flag that controls if transcript should be punctuated");
DEFINE_bool(word_time_offsets, true, "Flag that controls if word time stamps are requested");
DEFINE_bool(simulate_realtime, false, "Flag that controls if audio files should be sent in realtime");
DEFINE_string(audio_device, "", "Name of audio device to use");
DEFINE_string(riva_uri, "ws://127.0.0.1:9090/v1/realtime?intent=transcription", "URI to access riva-server");
DEFINE_int32(num_iterations, 1, "Number of times to loop over audio files");
DEFINE_int32(num_parallel_requests, 1, "Number of parallel requests to keep in flight");
DEFINE_int32(chunk_duration_ms, 100, "Chunk duration in milliseconds");
DEFINE_bool(print_transcripts, true, "Print final transcripts");
DEFINE_bool(interim_results, true, "Print intermediate transcripts");
DEFINE_string(output_filename, "final_transcripts.json", "Filename of .json file containing output transcripts");
DEFINE_string(model_name, "", "Name of the TRTIS model to use");
DEFINE_string(language_code, "en-US", "Language code of the model to use");
DEFINE_string(boosted_words_file, "", "File with a list of words to boost. One line per word.");
DEFINE_double(boosted_words_score, 10., "Score by which to boost the boosted words");
DEFINE_bool(verbatim_transcripts, true, "True returns text exactly as it was said with no normalization. False applies text inverse normalization");
DEFINE_string(ssl_root_cert, "", "Path to SSL root certificates file");
DEFINE_string(ssl_client_key, "", "Path to SSL client certificates key");
DEFINE_string(ssl_client_cert, "", "Path to SSL client certificates file");
DEFINE_bool(use_ssl, false, "Whether to use SSL credentials or not. If ssl_root_cert is specified, this is assumed to be true");
DEFINE_string(metadata, "", "Comma separated key-value pair(s) of metadata to be sent to server");
DEFINE_int32(start_history, -1, "Value (in milliseconds) to detect and initiate start of speech utterance");
DEFINE_double(start_threshold, -1., "Threshold value to determine at what percentage start of speech is initiated");
DEFINE_int32(stop_history, -1, "Value (in milliseconds) to detect endpoint and reset decoder");
DEFINE_double(stop_threshold, -1., "Threshold value to determine when endpoint detected");
DEFINE_int32(stop_history_eou, -1, "Value (in milliseconds) to detect endpoint and generate an intermediate final transcript");
DEFINE_double(stop_threshold_eou, -1., "Threshold value for likelihood of blanks before detecting end of utterance");
DEFINE_string(custom_configuration, "", "Custom configurations to be sent to the server as key value pairs <key:value,key:value,...>");
DEFINE_bool(speaker_diarization, false, "Flag that controls if speaker diarization is requested");
DEFINE_int32(diarization_max_speakers, 4, "Max number of speakers to detect when performing speaker diarization. Default is 4 (Max)");
DEFINE_uint64(timeout_ms, 10000, "Timeout for GRPC channel creation");
DEFINE_uint64(max_grpc_message_size, 16777216, "Max GRPC message size");

// Additional realtime-specific flags
DEFINE_int32(connection_timeout_ms, 100000, "Connection timeout in milliseconds");
DEFINE_int32(session_init_timeout_ms, 100000, "Session initialization timeout in milliseconds");
DEFINE_int32(session_update_timeout_ms, 100000, "Session update timeout in milliseconds");
DEFINE_int32(transcription_timeout_ms, 100000, "Transcription timeout in milliseconds");
DEFINE_int32(chunk_delay_time_ms, 160, "Delay between audio chunks in milliseconds");
DEFINE_bool(verbose_logging, false, "Enable verbose logging");
DEFINE_bool(show_detailed_stats, true, "Show detailed statistics");
DEFINE_bool(show_tabular_stats, true, "Show tabular statistics");

// Global client pointer for signal handling
std::vector<nvidia::riva::realtime::RecognitionClient*> g_clients;
std::mutex g_clients_mutex;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    for (auto client : g_clients) {
        std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
        client->Close();
    }
    exit(0);
}

// Helper function to format throughput as 10.246e00 instead of 1.0246e+01
std::string format_throughput(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << value << "e00";
    return oss.str();
}



// Function to run the client example
void client_runner( const std::string& uri,
                    const std::shared_ptr<nvidia::riva::realtime::AudioChunks>& audio_chunks, 
                    PerformanceStats& perfCounter,
                    const std::size_t connectionTimeoutInMs,
                    const std::size_t sessionInitTimeoutInMs,
                    const std::size_t sessionUpdateTimeoutInMs,
                    const std::size_t transcriptionTimeoutInMs,
                    const std::size_t chunkDelayTimeInMs, 
                    const bool simulateRealtime = false) 
{
    nvidia::riva::realtime::RecognitionClient client(perfCounter.GetObjectName(), audio_chunks, perfCounter); 
    
    // Extract server URL from URI (remove ws:// and path)
    std::string server_url = uri;
    if (server_url.find("ws://") == 0) {
        server_url = server_url.substr(5); // Remove "ws://"
    } else if (server_url.find("wss://") == 0) {
        server_url = server_url.substr(6); // Remove "wss://"
    }
    
    // Remove path part (everything after first /)
    size_t path_pos = server_url.find('/');
    if (path_pos != std::string::npos) {
        server_url = server_url.substr(0, path_pos);
    }
    
    client.SetServerUrl(server_url);
    
    // Set session configuration from command line flags (these will override defaults)
    nvidia::riva::realtime::SessionConfig sessionConfig;
    
    // Only set values if they were provided by user (not default values)
    if (!FLAGS_language_code.empty() && FLAGS_language_code != "en-US") {
        sessionConfig.language_code_ = FLAGS_language_code;
    }
    if (!FLAGS_model_name.empty()) {
        sessionConfig.model_name_ = FLAGS_model_name;
    }
    if (FLAGS_max_alternatives != 1) {
        sessionConfig.max_alternatives_ = FLAGS_max_alternatives;
    }
    if (!FLAGS_automatic_punctuation) { // Default is true, so only override if false
        sessionConfig.automatic_punctuation_ = FLAGS_automatic_punctuation;
    }
    if (!FLAGS_word_time_offsets) { // Default is true, so only override if false
        sessionConfig.word_time_offsets_ = FLAGS_word_time_offsets;
    }
    if (FLAGS_profanity_filter) { // Default is false, so only override if true
        sessionConfig.profanity_filter_ = FLAGS_profanity_filter;
    }
    if (!FLAGS_verbatim_transcripts) { // Default is true, so only override if false
        sessionConfig.verbatim_transcripts_ = FLAGS_verbatim_transcripts;
    }
    if (!FLAGS_boosted_words_file.empty()) {
        sessionConfig.boosted_words_file_ = FLAGS_boosted_words_file;
        sessionConfig.boosted_words_score_ = FLAGS_boosted_words_score;
    }
    if (FLAGS_speaker_diarization) { // Default is false, so only override if true
        sessionConfig.speaker_diarization_ = FLAGS_speaker_diarization;
        sessionConfig.diarization_max_speakers_ = FLAGS_diarization_max_speakers;
    }
    if (FLAGS_start_history > 0) {
        sessionConfig.start_history_ = FLAGS_start_history;
    }
    if (FLAGS_start_threshold > 0) {
        sessionConfig.start_threshold_ = FLAGS_start_threshold;
    }
    if (FLAGS_stop_history > 0) {
        sessionConfig.stop_history_ = FLAGS_stop_history;
    }
    if (FLAGS_stop_threshold > 0) {
        sessionConfig.stop_threshold_ = FLAGS_stop_threshold;
    }
    if (FLAGS_stop_history_eou > 0) {
        sessionConfig.stop_history_eou_ = FLAGS_stop_history_eou;
    }
    if (FLAGS_stop_threshold_eou > 0) {
        sessionConfig.stop_threshold_eou_ = FLAGS_stop_threshold_eou;
    }
    if (!FLAGS_custom_configuration.empty()) {
        sessionConfig.custom_configuration_ = FLAGS_custom_configuration;
    }
    
    client.SetSessionConfig(sessionConfig);
    client.SetVerboseLogging(FLAGS_verbose_logging);
    client.SetTimingConfig(connectionTimeoutInMs, sessionInitTimeoutInMs, sessionUpdateTimeoutInMs, transcriptionTimeoutInMs, chunkDelayTimeInMs);
    
    // Step 1: Connect to the WebSocket server
    client.Connect(uri);
        
    std::thread client_thread([&client]() {
        client.Run();
    });
        
    // Step 2: Wait for the connection to be established
    if (!client.WaitForConnection()) {
        std::cerr << "Failed to establish WebSocket connection" << std::endl;
        client.Close();
        client_thread.join();
        return;
    }
        
    std::cout << "WebSocket connection established" << std::endl;

    // Step 3: Initialize the session
    if (!client.InitializeSession()) {
        std::cerr << "Failed to initialize session" << std::endl;
        client.Close();
        client_thread.join();
        return;
    }
    
    std::cout << "Waiting for session update confirmation..." << std::endl;

    // Step 4: Wait for the session to be updated
    if (!client.WaitForSessionUpdate()) {
        std::cerr << "Session update timeout" << std::endl;
        client.Close();
        client_thread.join();
        return;
    }
    
    // Step 5: Send the audio chunks with realistic timing
    perfCounter.StartProcessingTimer();
    perfCounter.SetAudioDurationInSeconds(audio_chunks->GetDurationSeconds());
    
    // Send chunks with realistic timing
    client.SendAudioChunks(simulateRealtime);
    
    std::cout << "Waiting for transcription completion..." << std::endl;

    // Step 6: Wait for the transcription to be completed
    if (client.WaitForTranscriptionCompletion()) {
        std::cout << "Transcription completed successfully!" << std::endl;
        perfCounter.EndProcessingTimer();
        perfCounter.SetSuccess(true);
    } else {
        std::cout << "Transcription did not complete within timeout" << std::endl;
        perfCounter.EndProcessingTimer();
    }
    
    // Step 7: Close the WebSocket connection
    client.Close();
    client_thread.join();
    
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_clients.push_back(&client);
    }

    // Step 8: Report the stats
    perfCounter.ReportStats();
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    // Set up usage message
    std::stringstream str_usage;
    str_usage << "Usage: riva_realtime_asr_client " << std::endl;
    str_usage << "           --audio_file=<filename or folder> " << std::endl;
    str_usage << "           --audio_device=<device_id (such as hw:5,0)> " << std::endl;
    str_usage << "           --automatic_punctuation=<true|false>" << std::endl;
    str_usage << "           --max_alternatives=<integer>" << std::endl;
    str_usage << "           --profanity_filter=<true|false>" << std::endl;
    str_usage << "           --word_time_offsets=<true|false>" << std::endl;
    str_usage << "           --riva_uri=<server_name:port> " << std::endl;
    str_usage << "           --chunk_duration_ms=<integer> " << std::endl;
    str_usage << "           --interim_results=<true|false> " << std::endl;
    str_usage << "           --simulate_realtime=<true|false> " << std::endl;
    str_usage << "           --num_iterations=<integer> " << std::endl;
    str_usage << "           --num_parallel_requests=<integer> " << std::endl;
    str_usage << "           --print_transcripts=<true|false> " << std::endl;
    str_usage << "           --output_filename=<string>" << std::endl;
    str_usage << "           --verbatim_transcripts=<true|false>" << std::endl;
    str_usage << "           --language_code=<bcp 47 language code (such as en-US)>" << std::endl;
    str_usage << "           --boosted_words_file=<string>" << std::endl;
    str_usage << "           --boosted_words_score=<float>" << std::endl;
    str_usage << "           --ssl_root_cert=<filename>" << std::endl;
    str_usage << "           --ssl_client_key=<filename>" << std::endl;
    str_usage << "           --ssl_client_cert=<filename>" << std::endl;
    str_usage << "           --model_name=<model>" << std::endl;
    str_usage << "           --metadata=<key,value,...>" << std::endl;
    str_usage << "           --start_history=<int>" << std::endl;
    str_usage << "           --start_threshold=<float>" << std::endl;
    str_usage << "           --stop_history=<int>" << std::endl;
    str_usage << "           --stop_history_eou=<int>" << std::endl;
    str_usage << "           --stop_threshold=<float>" << std::endl;
    str_usage << "           --stop_threshold_eou=<float>" << std::endl;
    str_usage << "           --custom_configuration=<key:value,key:value,...>" << std::endl;
    str_usage << "           --speaker_diarization=<true|false>" << std::endl;
    str_usage << "           --diarization_max_speakers=<int>" << std::endl;
    str_usage << "           --timeout_ms=<uint64_t>" << std::endl;
    str_usage << "           --max_grpc_message_size=<uint64_t>" << std::endl;
    str_usage << "           --connection_timeout_ms=<integer>" << std::endl;
    str_usage << "           --session_init_timeout_ms=<integer>" << std::endl;
    str_usage << "           --session_update_timeout_ms=<integer>" << std::endl;
    str_usage << "           --transcription_timeout_ms=<integer>" << std::endl;
    str_usage << "           --chunk_delay_time_ms=<integer>" << std::endl;
    str_usage << "           --verbose_logging=<true|false>" << std::endl;
    str_usage << "           --show_detailed_stats=<true|false>" << std::endl;
    str_usage << "           --show_tabular_stats=<true|false>" << std::endl;
    
    gflags::SetUsageMessage(str_usage.str());
    
    if (argc < 2) {
        std::cout << gflags::ProgramUsage();
        return 1;
    }

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (argc > 1) {
        std::cout << gflags::ProgramUsage();
        return 1;
    }

    // Validate arguments
    if (FLAGS_max_alternatives < 1) {
        std::cerr << "max_alternatives must be greater than or equal to 1." << std::endl;
        return 1;
    }

    if (FLAGS_num_iterations < 1) {
        std::cerr << "num_iterations must be greater than 0" << std::endl;
        return 1;
    }

    if (FLAGS_num_parallel_requests < 1) {
        std::cerr << "num_parallel_requests must be greater than 0" << std::endl;
        return 1;
    }

    // Check if audio file or device is specified
    if (FLAGS_audio_file.empty() && FLAGS_audio_device.empty()) {
        std::cerr << "Either --audio_file or --audio_device must be specified" << std::endl;
        return 1;
    }

    // Validate audio file exists if specified
    if (!FLAGS_audio_file.empty() && !std::filesystem::exists(FLAGS_audio_file)) {
        std::cerr << "Audio file does not exist: " << FLAGS_audio_file << std::endl;
        return 1;
    }

    // Use command-line arguments
    const std::string uri = FLAGS_riva_uri;
    const std::string audio_file_path = FLAGS_audio_file;
    const std::size_t num_iterations = FLAGS_num_iterations;
    const std::size_t num_parallel_clients = FLAGS_num_parallel_requests;
    const bool simulateRealtime = FLAGS_simulate_realtime;
    
    const std::size_t connectionTimeoutInMs = FLAGS_connection_timeout_ms;
    const std::size_t sessionInitTimeoutInMs = FLAGS_session_init_timeout_ms;
    const std::size_t sessionUpdateTimeoutInMs = FLAGS_session_update_timeout_ms;
    const std::size_t transcriptionTimeoutInMs = FLAGS_transcription_timeout_ms;
    const std::size_t chunkDelayTimeInMs = FLAGS_chunk_delay_time_ms;
    const std::size_t chunk_duration_ms = FLAGS_chunk_duration_ms;

    const auto audio_chunks = std::make_shared<nvidia::riva::realtime::AudioChunks>(audio_file_path, chunk_duration_ms);
    if (!audio_chunks->Init()) {
        std::cerr << "Failed to initialize audio chunks" << std::endl;
        return 1;
    }
    
    PerformanceStats overallPerf("Overall");
    
    overallPerf.SetAudioDurationInSeconds(audio_chunks->GetDurationSeconds() * num_iterations * num_parallel_clients);
    
    // Create StatsBuilder for all clients
    StatsBuilder statsBuilder("client", audio_chunks->GetDurationSeconds(), num_parallel_clients);

    // Run iterations asynchronously
    std::vector<std::future<void>> futures;
    std::cout << "Starting " << num_parallel_clients << " async clients..." << std::endl;

    overallPerf.StartProcessingTimer();
    for (std::size_t N = 0; N < num_parallel_clients; ++N) {
        // Launch each client asynchronously
        futures.emplace_back(std::async(std::launch::async, [&, N]() {
            std::cout << "Starting client " << (N + 1) << "/" << num_parallel_clients << std::endl;
            
            for (std::size_t M = 0; M < num_iterations; ++M) {
                std::cout << "  Running iteration " << (M + 1) << "/" << num_iterations << std::endl;
                client_runner(  uri, 
                                audio_chunks, 
                                statsBuilder.GetPerformanceStats(N), 
                                connectionTimeoutInMs, 
                                sessionInitTimeoutInMs, 
                                sessionUpdateTimeoutInMs, 
                                transcriptionTimeoutInMs, 
                                chunkDelayTimeInMs,
                                simulateRealtime);
            }
            
            std::cout << "Completed client " << (N + 1) << "/" << num_parallel_clients << std::endl;
        }));
    }
    
    // Wait for all iterations to complete
    std::cout << "Waiting for all iterations to complete..." << std::endl;
    for (auto& future : futures) {
        future.wait();
    }
    std::cout << "All iterations completed!" << std::endl;
    overallPerf.EndProcessingTimer();

    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Conditional stats reporting based on flags
    if (FLAGS_show_detailed_stats) {
        statsBuilder.ReportDetailedStats();
    }
    if (FLAGS_show_tabular_stats) {
        statsBuilder.ReportTabularStats();
    }
    
    statsBuilder.ReportCumulativeStats();
    overallPerf.ReportStats();
    return 0;
} 