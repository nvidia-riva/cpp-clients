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
#include "riva/utils/wav/wav_writer.h"

using grpc::Status;
using grpc::StatusCode;

namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;
namespace nr_nmt = nvidia::riva::nmt;

typedef ClientCall<
    nr_nmt::StreamingTranslateSpeechToSpeechRequest,
    nr_nmt::StreamingTranslateSpeechToSpeechResponse>
    S2SClientCall;

class StreamingS2SClient {
 public:
  StreamingS2SClient(
      std::shared_ptr<grpc::Channel> channel, int32_t num_parallel_requests,
      const std::string& source_language_code, const std::string& target_language_code_,
      bool profanity_filter, bool automatic_punctuation, bool separate_recognition_per_channel,
      int32_t chunk_duration_ms, bool simulate_realtime, bool verbatim_transcripts,
      const std::string& boosted_phrases_file, float boosted_phrases_score,
      const std::string& tts_encoding, const std::string& tts_audio_file, int tts_sample_rate,
      const std::string& tts_voice_name);

  ~StreamingS2SClient();

  uint32_t NumActiveStreams() { return num_active_streams_.load(); }

  uint32_t NumStreamsFinished() { return num_streams_finished_.load(); }

  float TotalAudioProcessed() { return total_audio_processed_; }

  void StartNewStream(std::unique_ptr<Stream> stream);

  void GenerateRequests(std::shared_ptr<S2SClientCall> call);
  int DoStreamingFromFile(
      std::string& audio_file, int32_t num_iterations, int32_t num_parallel_requests);

  void PostProcessResults(std::shared_ptr<S2SClientCall> call, bool audio_device);

  void ReceiveResponses(std::shared_ptr<S2SClientCall> call, bool audio_device);

  int DoStreamingFromMicrophone(const std::string& audio_device, bool& request_exit);

  void PrintLatencies(std::vector<double>& latencies, const std::string& name);

  int PrintStats();

  std::mutex latencies_mutex_;

 private:
  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<nr_nmt::RivaTranslation::Stub> stub_;
  std::vector<double> latencies_;
  std::string tts_encoding_;
  std::string tts_audio_file_;
  std::string tts_voice_name_;
  std::string source_language_code_;
  std::string target_language_code_;
  int tts_sample_rate_;

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

  std::string model_name_;
  bool simulate_realtime_;
  bool verbatim_transcripts_;

  std::vector<std::string> boosted_phrases_;
  float boosted_phrases_score_;
};
