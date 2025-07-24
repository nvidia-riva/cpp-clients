/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
 
#include "stats_builder.h"
#include <iostream>
#include <chrono>
#include <string>
#include <vector>

namespace nvidia::riva::utils {

PerformanceStats::PerformanceStats(const std::string& objectName) 
    : success_(false), 
      objectName_(objectName), 
      processing_start_time_(std::chrono::steady_clock::now()),
      processing_end_time_(std::chrono::steady_clock::now()),
      audio_duration_seconds_(0.0) {}

StatsBuilder::StatsBuilder(const std::string& objectName, double audio_duration_seconds, std::size_t num_iterations) 
    : audio_duration_seconds_(audio_duration_seconds), num_iterations_(num_iterations), object_name_(objectName) {
    // Pre-allocate the vector with the expected number of iterations
    performanceStats_.reserve(num_iterations);
    
    // Create PerformanceStats objects for each iteration
    for (std::size_t i = 0; i < num_iterations; ++i) {
        std::string iteration_name = objectName + "-" + std::to_string(i);
        performanceStats_.emplace_back(iteration_name);
        // Set the audio duration for each performance stats object
        performanceStats_.back().SetAudioDurationInSeconds(audio_duration_seconds);
    }
}

void PerformanceStats::StartProcessingTimer() {
        processing_start_time_ = std::chrono::steady_clock::now();
        //std::cout << "Starting processing timer: " << std::chrono::duration_cast<std::chrono::milliseconds>(processing_start_time_.time_since_epoch()).count() << std::endl; 
    }
    
void PerformanceStats::EndProcessingTimer() {
        processing_end_time_ = std::chrono::steady_clock::now();
        //std::cout << "Ending processing timer: " << std::chrono::duration_cast<std::chrono::milliseconds>(processing_end_time_.time_since_epoch()).count() << std::endl; 
    }
    
double PerformanceStats::GetRuntimeInMs() const {
    auto durationInMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        processing_end_time_ - processing_start_time_);
    return durationInMs.count();
}

double PerformanceStats::GetRuntimeInSeconds() const {
    return GetRuntimeInMs() / 1000.0;
}
    
void PerformanceStats::SetAudioDurationInSeconds(double audio_duration_seconds) {
    audio_duration_seconds_ = audio_duration_seconds;
}
    
double PerformanceStats::GetThroughputRTFX() const {
        double runtimeInMs = GetRuntimeInMs();
        if (runtimeInMs > 0.0 && audio_duration_seconds_ > 0.0) {
            // RTFX = (Total Audio Processed in seconds) × 1000 ÷ (Total Runtime in milliseconds)
            return (audio_duration_seconds_ * 1000.0) / runtimeInMs;
        }
        return 0.0;
    }

void PerformanceStats::SetObjectName(const std::string& objectName) {
    objectName_ = objectName;
}

std::string PerformanceStats::GetObjectName() const {
    return objectName_;
}

void PerformanceStats::ReportStats() {
    std::cout << "Object Name: " << GetObjectName() << std::endl;
    std::cout << "Success: " << IsSuccess() << std::endl;
    std::cout << "Audio Duration: " << audio_duration_seconds_ << " seconds" << std::endl;
    std::cout << "Total Runtime: " << GetRuntimeInMs() << " ms (" << GetRuntimeInSeconds() << " seconds)" << std::endl;
    std::cout << "Throughput: " << GetThroughputRTFX() << " RTFX" << std::endl;
}



void StatsBuilder::ReportCumulativeStats() {
    std::cout << "Cumulative Stats" << std::endl;
    std::cout << "=================" << std::endl;
    for (auto performanceStats : performanceStats_) {
        std::cout << "Object Name: " << performanceStats.GetObjectName() << std::endl;
        std::cout << "Total Runtime: " << performanceStats.GetRuntimeInMs() << " ms (" << performanceStats.GetRuntimeInSeconds() << " seconds)" << std::endl;
        std::cout << "Throughput: " << performanceStats.GetThroughputRTFX() << " RTFX" << std::endl;
    }
}

// Helper function to calculate percentile
double CalculatePercentile(const std::vector<double>& values, double percentile) {
    if (values.empty()) return 0.0;
    
    std::vector<double> sorted_values = values;
    std::sort(sorted_values.begin(), sorted_values.end());
    
    double index = (percentile / 100.0) * (sorted_values.size() - 1);
    int lower_index = static_cast<int>(index);
    int upper_index = lower_index + 1;
    
    if (upper_index >= sorted_values.size()) {
        return sorted_values[lower_index];
    }
    
    double weight = index - lower_index;
    return sorted_values[lower_index] * (1 - weight) + sorted_values[upper_index] * weight;
}

// Statistical methods for runtime
double StatsBuilder::GetAverageRuntime() const {
    if (performanceStats_.empty()) return 0.0;
    
    double sum = 0.0;
    for (const auto& stats : performanceStats_) {
        sum += stats.GetRuntimeInMs();
    }
    return sum / performanceStats_.size();
}

double StatsBuilder::GetP50Runtime() const {
    std::vector<double> runtimes;
    for (const auto& stats : performanceStats_) {
        runtimes.push_back(stats.GetRuntimeInMs());
    }
    return CalculatePercentile(runtimes, 50.0);
}

double StatsBuilder::GetP90Runtime() const {
    std::vector<double> runtimes;
    for (const auto& stats : performanceStats_) {
        runtimes.push_back(stats.GetRuntimeInMs());
    }
    return CalculatePercentile(runtimes, 90.0);
}

double StatsBuilder::GetP95Runtime() const {
    std::vector<double> runtimes;
    for (const auto& stats : performanceStats_) {
        runtimes.push_back(stats.GetRuntimeInMs());
    }
    return CalculatePercentile(runtimes, 95.0);
}

double StatsBuilder::GetP99Runtime() const {
    std::vector<double> runtimes;
    for (const auto& stats : performanceStats_) {
        runtimes.push_back(stats.GetRuntimeInMs());
    }
    return CalculatePercentile(runtimes, 99.0);
}

double StatsBuilder::GetMinRuntime() const {
    if (performanceStats_.empty()) return 0.0;
    
    double min_runtime = performanceStats_[0].GetRuntimeInMs();
    for (const auto& stats : performanceStats_) {
        min_runtime = std::min(min_runtime, stats.GetRuntimeInMs());
    }
    return min_runtime;
}

double StatsBuilder::GetMaxRuntime() const {
    if (performanceStats_.empty()) return 0.0;
    
    double max_runtime = performanceStats_[0].GetRuntimeInMs();
    for (const auto& stats : performanceStats_) {
        max_runtime = std::max(max_runtime, stats.GetRuntimeInMs());
    }
    return max_runtime;
}

// Statistical methods for throughput
double StatsBuilder::GetAverageThroughput() const {
    if (performanceStats_.empty()) return 0.0;
    
    double sum = 0.0;
    for (const auto& stats : performanceStats_) {
        sum += stats.GetThroughputRTFX();
    }
    return sum / performanceStats_.size();
}

// Statistical methods for throughput
double StatsBuilder::GetCumulativeThroughput() const {
    if (performanceStats_.empty()) return 0.0;
    
    double sum = 0.0;
    for (const auto& stats : performanceStats_) {
        sum += stats.GetThroughputRTFX();
    }
    return sum;
}

double StatsBuilder::GetP90Throughput() const {
    std::vector<double> throughputs;
    for (const auto& stats : performanceStats_) {
        throughputs.push_back(stats.GetThroughputRTFX());
    }
    return CalculatePercentile(throughputs, 90.0);
}

double StatsBuilder::GetP95Throughput() const {
    std::vector<double> throughputs;
    for (const auto& stats : performanceStats_) {
        throughputs.push_back(stats.GetThroughputRTFX());
    }
    return CalculatePercentile(throughputs, 95.0);
}

double StatsBuilder::GetP99Throughput() const {
    std::vector<double> throughputs;
    for (const auto& stats : performanceStats_) {
        throughputs.push_back(stats.GetThroughputRTFX());
    }
    return CalculatePercentile(throughputs, 99.0);
}

bool StatsBuilder::AreAllIterationsSuccessful() const {
    if (performanceStats_.empty()) return false;
    
    for (const auto& stats : performanceStats_) {
        if (!stats.IsSuccess()) {
            return false;
        }
    }
    return true;
}

std::size_t StatsBuilder::GetSuccessfulIterationsCount() const {
    std::size_t success_count = 0;
    for (const auto& stats : performanceStats_) {
        if (stats.IsSuccess()) {
            success_count++;
        }
    }
    return success_count;
}

std::size_t StatsBuilder::GetFailedIterationsCount() const {
    return performanceStats_.size() - GetSuccessfulIterationsCount();
}

double StatsBuilder::GetSuccessRate() const {
    if (performanceStats_.empty()) return 0.0;
    return static_cast<double>(GetSuccessfulIterationsCount()) / performanceStats_.size() * 100.0;
}

void StatsBuilder::ReportDetailedStats() const {
    std::cout << "\n=== DETAILED PERFORMANCE STATISTICS ===" << std::endl;
    std::cout << "Audio Duration: " << audio_duration_seconds_ << " seconds" << std::endl;
    std::cout << "Number of Iterations: " << num_iterations_ << std::endl;
    std::cout << "Sample Count: " << performanceStats_.size() << std::endl;
    
    // Add success rate information
    std::cout << "Success Rate: " << GetSuccessRate() << "% (" << GetSuccessfulIterationsCount() 
              << "/" << performanceStats_.size() << " iterations)" << std::endl;
    std::cout << "All Iterations Successful: " << (AreAllIterationsSuccessful() ? "YES" : "NO") << std::endl;
    
    std::cout << "\n--- RUNTIME STATISTICS (ms) ---" << std::endl;
    std::cout << "Average: " << GetAverageRuntime() << " ms" << std::endl;
    std::cout << "P50:     " << GetP50Runtime() << " ms" << std::endl;
    std::cout << "P90:     " << GetP90Runtime() << " ms" << std::endl;
    std::cout << "P95:     " << GetP95Runtime() << " ms" << std::endl;
    std::cout << "P99:     " << GetP99Runtime() << " ms" << std::endl;
    std::cout << "Min:     " << GetMinRuntime() << " ms" << std::endl;
    std::cout << "Max:     " << GetMaxRuntime() << " ms" << std::endl;
    
    std::cout << "\n--- THROUGHPUT STATISTICS (RTFX) ---" << std::endl;
    std::cout << "Average: " << GetAverageThroughput() << " RTFX" << std::endl;
    std::cout << "Cumulative: " << GetCumulativeThroughput() << " RTFX" << std::endl;
    std::cout << "P90:     " << GetP90Throughput() << " RTFX" << std::endl;
    std::cout << "P95:     " << GetP95Throughput() << " RTFX" << std::endl;
    std::cout << "P99:     " << GetP99Throughput() << " RTFX" << std::endl;
    
    std::cout << "=====================================" << std::endl;
}

}  // namespace nvidia::riva::utils