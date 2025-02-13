/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#pragma once

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

#include "riva/proto/riva_asr.grpc.pb.h"
#include "riva/utils/wav/wav_reader.h"
#include "riva_asr_client_helper.h"

using grpc::Status;
using grpc::StatusCode;

namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;

class ClientCall {
 public:
  ClientCall(uint32_t _corr_id, bool word_time_offsets, bool speaker_diarization);
  ~ClientCall();

  void AppendResult(const nr_asr::StreamingRecognitionResult& result);

  void PrintResult(bool audio_device, std::ofstream& output_file);

  // Container for the data we expect from the server.
  nr_asr::StreamingRecognizeResponse response;
  std::queue<nr_asr::StreamingRecognizeRequest> requests;

  // std::mutex request_mutex;
  // Context for the client. It could be used to convey extra information to
  // the server and/or tweak certain RPC behaviors.
  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReaderWriterInterface<
      nr_asr::StreamingRecognizeRequest, nr_asr::StreamingRecognizeResponse>>
      streamer;

  std::unique_ptr<Stream> stream;
  std::chrono::time_point<std::chrono::steady_clock> send_time;

  uint32_t corr_id_;
  bool word_time_offsets_;
  bool speaker_diarization_;

  Results latest_result_;

  std::vector<std::chrono::time_point<std::chrono::steady_clock>> send_times, recv_times;
  std::vector<bool> recv_final_flags;

  grpc::Status finish_status;
  std::ofstream pipeline_states_logs_;

};  // ClientCall
