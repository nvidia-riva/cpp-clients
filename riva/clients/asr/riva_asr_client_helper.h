/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <alsa/asoundlib.h>
#include <grpcpp/grpcpp.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "absl/strings/str_replace.h"
#include "riva/proto/riva_asr.grpc.pb.h"

namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;

std::vector<std::string> ReadBoostedPhrases(const std::string& boosted_phrases_file);

bool WaitUntilReady(
    std::shared_ptr<grpc::Channel> channel, std::chrono::system_clock::time_point& deadline);

bool OpenAudioDevice(
    const char* devicename, snd_pcm_t** handle, snd_pcm_stream_t stream_type, int channels,
    int rate, unsigned int latency);

bool CloseAudioDevice(snd_pcm_t** handle);

std::string static inline EscapeTranscript(const std::string& input_str)
{
  return absl::StrReplaceAll(input_str, {{"\"", "\\\""}});
}

struct Results {
  std::vector<std::string> final_transcripts;
  std::vector<float> final_scores;
  std::string partial_transcript;
  std::vector<std::vector<nr_asr::WordInfo>> final_time_stamps;
  std::vector<nr_asr::WordInfo> partial_time_stamps;
  int request_cnt;
  float audio_processed;
};

void AppendResult(
    Results& output_result, const nr_asr::SpeechRecognitionResult& result, bool word_time_offsets,
    bool speaker_diarization);
void PrintResult(
    Results& output_result, const std::string& filename, bool word_time_offsets,
    bool speaker_diarization);
