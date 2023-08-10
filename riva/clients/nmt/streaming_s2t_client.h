/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <alsa/asoundlib.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpcpp/grpcpp.h>
#include <strings.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#include "client_call.h"
#include "riva/clients/asr/riva_asr_client_helper.h"
#include "riva/proto/riva_asr.grpc.pb.h"
#include "riva/utils/thread_pool.h"
#include "riva/utils/wav/wav_reader.h"

using grpc::Status;
using grpc::StatusCode;

namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;
namespace nr_nmt = nvidia::riva::nmt;

typedef ClientCall<
    nr_nmt::StreamingTranslateSpeechToTextRequest, nr_nmt::StreamingTranslateSpeechToTextResponse>
    S2TClientCall;


class StreamingS2TClient {
 public:
  StreamingS2TClient(
      std::shared_ptr<grpc::Channel> channel, int32_t num_parallel_requests,
      const std::string& source_language_code, const std::string& target_language_code,
      bool profanity_filter, bool automatic_punctuation, bool separate_recognition_per_channel,
      int32_t chunk_duration_ms, bool simulate_realtime, bool verbatim_transcripts,
      const std::string& boosted_phrases_file, float boosted_phrases_score,
      const std::string& nmt_text_file);

  ~StreamingS2TClient();

  uint32_t NumActiveStreams() { return num_active_streams_.load(); }

  uint32_t NumStreamsFinished() { return num_streams_finished_.load(); }

  float TotalAudioProcessed() { return total_audio_processed_; }

  void StartNewStream(std::unique_ptr<Stream> stream);

  void GenerateRequests(std::shared_ptr<S2TClientCall> call);

  int DoStreamingFromFile(
      std::string& audio_file, int32_t num_iterations, int32_t num_parallel_requests);

  void PostProcessResults(std::shared_ptr<S2TClientCall> call, bool audio_device);

  void ReceiveResponses(std::shared_ptr<S2TClientCall> call, bool audio_device);

  int DoStreamingFromMicrophone(const std::string& auido_device, bool& request_exit);

  void PrintLatencies(std::vector<double>& latencies, const std::string& name);

  int PrintStats();

  std::mutex latencies_mutex_;

  bool print_latency_stats_;

 private:
  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<nr_nmt::RivaTranslation::Stub> stub_;
  std::vector<double> int_latencies_, final_latencies_, latencies_;

  std::string source_language_code_;
  std::string target_language_code_;
  bool profanity_filter_;
  int32_t channels_;
  bool automatic_punctuation_;
  bool separate_recognition_per_channel_;
  int32_t chunk_duration_ms_;

  std::mutex curr_tasks_mutex_;

  float total_audio_processed_;

  std::atomic<uint32_t> num_active_streams_;
  uint32_t num_streams_started_;
  std::atomic<uint32_t> num_streams_finished_;
  uint32_t num_failed_requests_;

  std::unique_ptr<ThreadPool> thread_pool_;


  bool simulate_realtime_;
  bool verbatim_transcripts_;

  std::vector<std::string> boosted_phrases_;
  float boosted_phrases_score_;
  std::string nmt_text_file_;
};
