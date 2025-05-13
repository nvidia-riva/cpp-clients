/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "client_call.h"

ClientCall::ClientCall(uint32_t corr_id, bool word_time_offsets, bool speaker_diarization)
    : corr_id_(corr_id), word_time_offsets_(word_time_offsets),
      speaker_diarization_(speaker_diarization)
{
  send_times.reserve(1000);
  recv_times.reserve(1000);
  recv_final_flags.reserve(1000);
}

ClientCall::~ClientCall()
{
  if (pipeline_states_logs_.is_open()) {
    pipeline_states_logs_.close();
  }
}

void
ClientCall::AppendResult(const nr_asr::StreamingRecognitionResult& result)
{
  if (latest_result_.final_transcripts.size() < 1) {
    latest_result_.final_transcripts.resize(1);
    latest_result_.final_transcripts[0] = "";
  }
  if (result.has_pipeline_states()) {
    auto pipeline_states = result.pipeline_states();
    size_t prob_states_count = pipeline_states.vad_probabilities_size();
    std::string vad_log = "";
    for (size_t i = 0; i < prob_states_count; i++) {
      vad_log += std::to_string(pipeline_states.vad_probabilities(i)) + " ";
    }
    if (!pipeline_states_logs_.is_open()) {
      pipeline_states_logs_.open("riva_asr_pipeline_states.log");
    }
    pipeline_states_logs_ << "VAD states: " << vad_log << std::endl;
  } else {
    bool is_final = result.is_final();
    if (is_final) {
      int num_alternatives = result.alternatives_size();
      latest_result_.final_transcripts.resize(num_alternatives);
      latest_result_.final_scores.resize(num_alternatives);
      latest_result_.final_time_stamps.resize(num_alternatives);
      for (int a = 0; a < num_alternatives; ++a) {
        // Append to transcript
        latest_result_.final_transcripts[a] += result.alternatives(a).transcript();
        latest_result_.final_scores[a] += result.alternatives(a).confidence();
        for (auto& lang_code : result.alternatives(a).language_code()) {
          if (std::find(
                  latest_result_.language_codes.begin(), latest_result_.language_codes.end(),
                  lang_code) == latest_result_.language_codes.end()) {
            latest_result_.language_codes.push_back(lang_code);
          }
        }
      }
      VLOG(1) << "Final transcript: " << result.alternatives(0).transcript();

      if (word_time_offsets_ || speaker_diarization_) {
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
        if (result.stability() == 1) {
          VLOG(1) << "Intermediate transcript: " << result.alternatives(0).transcript();
        } else {
          VLOG(1) << "Partial transcript: " << result.alternatives(0).transcript();
          latest_result_.partial_transcript += result.alternatives(0).transcript();
          if (word_time_offsets_) {
            for (int w = 0; w < result.alternatives(0).words_size(); ++w) {
              latest_result_.partial_time_stamps.emplace_back(result.alternatives(0).words(w));
            }
          }
        }
      }
    }
  }
}

void
ClientCall::PrintResult(bool audio_device, std::ofstream& output_file)
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

      if (word_time_offsets_ || speaker_diarization_) {
        std::cout << "Timestamps: " << std::endl;
        std::cout << std::setw(40) << std::left << "Word";
        if (word_time_offsets_) {
          std::cout << std::setw(16) << std::left << "Start (ms)";
          std::cout << std::setw(16) << std::left << "End (ms)";
        }
        if (latest_result_.language_codes.size() > 0) {
          std::cout << std::setw(16) << std::left << "Language";
        }
        std::cout << std::setw(16) << std::left << "Confidence";
        if (a == 0 && speaker_diarization_) {
          std::cout << std::setw(16) << std::left << "Speaker";
        }
        std::cout << std::endl;
        std::cout << std::endl;
        for (uint32_t w = 0; a < latest_result_.final_time_stamps.size() &&
                             w < latest_result_.final_time_stamps[a].size();
             ++w) {
          auto& word_info = latest_result_.final_time_stamps[a][w];
          std::cout << std::setw(40) << std::left << word_info.word();
          if (word_time_offsets_) {
            std::cout << std::setw(16) << std::left << word_info.start_time();
            std::cout << std::setw(16) << std::left << word_info.end_time();
            if (latest_result_.language_codes.size() > 0) {
              std::cout << std::setw(16) << std::left << word_info.language_code();
            }
          }
          std::cout << std::setw(16) << std::setprecision(4) << std::scientific
                    << word_info.confidence();
          if (a == 0 && speaker_diarization_) {
            std::cout << std::setw(16) << std::left << word_info.speaker_tag();
          }
          std::cout << std::endl;
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
    if (latest_result_.language_codes.size() > 0) {
      std::cout << "Language codes detected in the audio: " << std::endl;
      for (auto& lang_code : latest_result_.language_codes) {
        std::cout << lang_code << " ";
      }
      std::cout << std::endl;
    }
  }
  std::cout << std::endl;
  std::cout << "Audio processed: " << latest_result_.audio_processed << " sec." << std::endl;
  std::cout << "-----------------------------------------------------------" << std::endl;
  std::cout << std::endl;
}
