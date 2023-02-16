/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#pragma once

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

#include "riva/clients/asr/riva_asr_client_helper.h"
#include "riva/proto/riva_asr.grpc.pb.h"
#include "riva/proto/riva_nmt.grpc.pb.h"
#include "riva/utils/wav/wav_reader.h"

using grpc::Status;
using grpc::StatusCode;

namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;
namespace nr_nmt = nvidia::riva::nmt;

// struct Results {
//   std::vector<std::string> final_transcripts;
//   std::vector<float> final_scores;
//   std::string partial_transcript;
//   std::vector<std::vector<nr_asr::WordInfo>> final_time_stamps;
//   std::vector<nr_asr::WordInfo> partial_time_stamps;
//   int request_cnt;
//   float audio_processed;
// };


template <typename Request, typename Response>
class ClientCall {
 public:
  ClientCall(uint32_t _corr_id, bool word_time_offsets)
      : corr_id_(_corr_id), word_time_offsets_(word_time_offsets)
  {
    send_times.reserve(1000);
    recv_times.reserve(1000);
    recv_final_flags.reserve(1000);
  }

  Response response;
  std::queue<Request> requests;

  // Context for the client. It could be used to convey extra information to
  // the server and/or tweak certain RPC behaviors.
  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReaderWriterInterface<Request, Response>> streamer;

  std::unique_ptr<Stream> stream;
  std::chrono::time_point<std::chrono::steady_clock> send_time;

  uint32_t corr_id_;
  bool word_time_offsets_;

  Results latest_result_;

  std::vector<std::chrono::time_point<std::chrono::steady_clock>> send_times, recv_times;
  std::vector<bool> recv_final_flags;

  grpc::Status finish_status;

  // template<typename Request, typename Response>
  void AppendResult(const nr_asr::StreamingRecognitionResult& result)
  {
    bool is_final = result.is_final();
    if (latest_result_.final_transcripts.size() < 1) {
      latest_result_.final_transcripts.resize(1);
      latest_result_.final_transcripts[0] = "";
    }

    if (is_final) {
      int num_alternatives = result.alternatives_size();
      latest_result_.final_transcripts.resize(num_alternatives);
      latest_result_.final_scores.resize(num_alternatives);
      latest_result_.final_time_stamps.resize(num_alternatives);
      for (int a = 0; a < num_alternatives; ++a) {
        // Append to transcript
        latest_result_.final_transcripts[a] += result.alternatives(a).transcript();
        latest_result_.final_scores[a] += result.alternatives(a).confidence();
      }
      if (word_time_offsets_) {
        if (num_alternatives > 0) {
          for (int a = 0; a < num_alternatives; ++a) {
            for (int w = 0; w < result.alternatives(a).words_size(); ++w) {
              latest_result_.final_time_stamps[a].push_back(result.alternatives(a).words(w));
            }
          }
        }
      }
    } else {
      if (result.alternatives_size() > 0) {
        latest_result_.partial_transcript += result.alternatives(0).transcript();
        if (word_time_offsets_) {
          for (int w = 0; w < result.alternatives(0).words_size(); ++w) {
            latest_result_.partial_time_stamps.emplace_back(result.alternatives(0).words(w));
          }
        }
      }
    }
  }
  void PrintResult(bool audio_device, std::ofstream& output_file)
  {
    std::cout << "-----------------------------------------------------------" << std::endl;

    std::string filename = "microphone";
    if (!audio_device) {
      filename = this->stream->wav->filename;
      std::cout << "File: " << filename << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Final transcripts: " << std::endl;
    if (latest_result_.final_transcripts.size() == 0) {
      output_file << "{\"audio_filepath\": \"" << filename << "\",";
      output_file << "\"text\": \"\"}" << std::endl;
    } else {
      for (uint32_t a = 0; a < latest_result_.final_transcripts.size(); ++a) {
        if (a == 0) {
          output_file << "{\"audio_filepath\": \"" << filename << "\",";
          output_file << "\"text\": \"" << EscapeTranscript(latest_result_.final_transcripts[a])
                      << "\"}" << std::endl;
        }
        std::cout << a << " : " << latest_result_.final_transcripts[a]
                  << latest_result_.partial_transcript << std::endl;
        std::cout << std::endl;

        if (word_time_offsets_) {
          std::cout << "Timestamps: " << std::endl;
          std::cout << std::setw(40) << std::left << "Word";
          std::cout << std::setw(16) << std::left << "Start (ms)";
          std::cout << std::setw(16) << std::left << "End (ms)";
          std::cout << std::setw(16) << std::left << "Confidence";
          std::cout << std::endl;
          std::cout << std::endl;
          for (uint32_t w = 0; a < latest_result_.final_time_stamps.size() &&
                               w < latest_result_.final_time_stamps[a].size();
               ++w) {
            auto& word_info = latest_result_.final_time_stamps[a][w];
            std::cout << std::setw(40) << std::left << word_info.word();
            std::cout << std::setw(16) << std::left << word_info.start_time();
            std::cout << std::setw(16) << std::left << word_info.end_time();
            std::cout << std::setw(16) << std::setprecision(4) << std::scientific
                      << word_info.confidence() << std::endl;
          }

          for (uint32_t w = 0; w < latest_result_.partial_time_stamps.size(); ++w) {
            auto& word_info = latest_result_.partial_time_stamps[w];
            std::cout << std::setw(40) << std::left << word_info.word();
            std::cout << std::setw(16) << std::left << word_info.start_time();
            std::cout << std::setw(16) << std::left << word_info.end_time() << std::endl;
            std::cout << std::setw(16) << std::setprecision(4) << std::scientific
                      << word_info.confidence() << std::endl;
          }
        }
        std::cout << std::endl;
      }
    }
    std::cout << std::endl;
    std::cout << "Audio processed: " << latest_result_.audio_processed << " sec." << std::endl;
    std::cout << "-----------------------------------------------------------" << std::endl;
    std::cout << std::endl;
  }

};  // ClientCall
