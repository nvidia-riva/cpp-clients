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
#include "riva/clients/realtime/recognition_client.h"
#include "riva/utils/stats_builder/stats_builder.h"

using namespace nvidia::riva::utils;
using namespace nvidia::riva::realtime;


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
    
    client.SetVerboseLogging(false);
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
    std::size_t num_iterations = 1;
    std::size_t num_parallel_clients = 50;
    bool simulateRealtime = true;
    
    const std::size_t connectionTimeoutInMs = 1000 * 100;
    const std::size_t sessionInitTimeoutInMs = 1000 * 100;
    const std::size_t sessionUpdateTimeoutInMs = 1000 * 100;
    const std::size_t transcriptionTimeoutInMs = 1000 * 100;
    
    // Realistic audio chunk timing - based on typical microphone sampling
    // For 16kHz audio with 160ms chunks, this would be 160ms
    const std::size_t chunkDelayTimeInMs = 160; // Realistic delay matching chunk duration

    const std::string uri = "ws://127.0.0.1:9090/v1/realtime?intent=transcription";
    const std::string audio_file_path = "/home/yhayaran/workspace/codebase/web-socket/new_ws_client/test_files/out5.wav";
    const std::size_t chunk_duration_ms = chunkDelayTimeInMs;
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

    // Uncomment this section to show detailed stats including success rates
    statsBuilder.ReportCumulativeStats();
    statsBuilder.ReportDetailedStats();
    statsBuilder.ReportTabularStats();
    
    overallPerf.ReportStats();
    return 0;
} 