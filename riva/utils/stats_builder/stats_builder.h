/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef STATS_BUILDER_H
#define STATS_BUILDER_H

#include <algorithm>
#include <chrono>
#include <iomanip>  // Required for std::setw and std::fixed
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace nvidia::riva::utils {

class PerformanceStats {
 private:
  bool success_;
  std::string objectName_;
  // Timing measurement
  std::chrono::steady_clock::time_point processing_start_time_;
  std::chrono::steady_clock::time_point processing_end_time_;
  double audio_duration_seconds_;

 public:
  PerformanceStats(const std::string& objectName);
  ~PerformanceStats() = default;

  bool IsSuccess() const { return success_; }
  void SetSuccess(bool success) { success_ = success; }

  void StartProcessingTimer();
  void EndProcessingTimer();
  std::chrono::steady_clock::time_point GetStartTime() const { return processing_start_time_; }
  double GetRuntimeInMs() const;
  double GetRuntimeInSeconds() const;
  void SetAudioDurationInSeconds(double audio_duration_seconds);
  double GetAudioDurationInSeconds() const { return audio_duration_seconds_; }
  double GetThroughputRTFX() const;

  void SetObjectName(const std::string& objectName);
  std::string GetObjectName() const;

  void ReportStats();
};

class StatsBuilder {
 private:
  std::vector<PerformanceStats> performanceStats_;
  double audio_duration_seconds_;
  std::size_t num_iterations_;
  std::string object_name_;  // Added to store the object name

 public:
  StatsBuilder(
      const std::string& objectName, double audio_duration_seconds, std::size_t num_iterations);
  ~StatsBuilder() = default;

  void SetAudioDurationInSeconds(double audio_duration_seconds);
  void SetNumIterations(std::size_t num_iterations);
  void ReportCumulativeStats();
  PerformanceStats& GetPerformanceStats(std::size_t index) { return performanceStats_[index]; }

  // Statistical methods
  double GetAverageRuntime() const;
  double GetP50Runtime() const;
  double GetP90Runtime() const;
  double GetP95Runtime() const;
  double GetP99Runtime() const;
  double GetMinRuntime() const;
  double GetMaxRuntime() const;

  // Throughput statistics
  double GetAverageThroughput() const;
  double GetCumulativeThroughput() const;
  double GetP90Throughput() const;
  double GetP95Throughput() const;
  double GetP99Throughput() const;

  // Comprehensive reporting
  void ReportDetailedStats() const;

  // Success checking methods
  bool AreAllIterationsSuccessful() const;
  std::size_t GetSuccessfulIterationsCount() const;
  std::size_t GetFailedIterationsCount() const;
  double GetSuccessRate() const;

  void ReportTabularStats() const
  {
    std::cout << "\n=== Tabular Performance Statistics ===" << std::endl;
    std::cout << std::left << std::setw(15) << "Name" << std::setw(10) << "Success" << std::setw(12)
              << "Runtime (s)" << std::setw(15) << "Audio (s)" << std::setw(15) << "Throughput"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;

    for (size_t i = 0; i < performanceStats_.size(); ++i) {
      const auto& stats = performanceStats_[i];
      std::string name = object_name_ + "-" + std::to_string(i);
      std::string success = stats.IsSuccess() ? "true" : "false";
      double runtime = stats.GetRuntimeInSeconds();     // Changed to GetRuntimeInSeconds
      double audio_duration = audio_duration_seconds_;  // Total audio processed
      double throughput = stats.GetThroughputRTFX();    // Changed to GetThroughputRTFX

      std::cout << std::left << std::setw(15) << name << std::setw(10) << success << std::fixed
                << std::setprecision(3) << std::setw(12) << runtime << std::setw(15)
                << audio_duration << std::setw(15) << throughput << std::endl;
    }
    std::cout << std::string(60, '-') << std::endl;

    // Summary row
    size_t success_count = 0;
    double total_runtime = 0.0;
    double total_audio_processed =
        audio_duration_seconds_ * performanceStats_.size();  // Total audio across all iterations
    double total_throughput = 0.0;

    for (const auto& stats : performanceStats_) {
      if (stats.IsSuccess())
        success_count++;
      total_runtime += stats.GetRuntimeInSeconds();   // Changed to GetRuntimeInSeconds
      total_throughput += stats.GetThroughputRTFX();  // Changed to GetThroughputRTFX
    }

    std::cout << std::left << std::setw(15) << "SUMMARY" << std::setw(10)
              << (success_count == performanceStats_.size()
                      ? "ALL"
                      : std::to_string(success_count) + "/" +
                            std::to_string(performanceStats_.size()))
              << std::fixed << std::setprecision(3) << std::setw(12) << total_runtime
              << std::setw(15) << total_audio_processed << std::setw(15) << total_throughput
              << std::endl;
    std::cout << std::endl;
  }
};

}  // namespace nvidia::riva::utils

#endif  // STATS_BUILDER_H