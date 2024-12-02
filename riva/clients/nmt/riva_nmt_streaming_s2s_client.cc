/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include <alsa/asoundlib.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpcpp/grpcpp.h>
#include <strings.h>

#include <atomic>
#include <cctype>
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
#include "riva/clients/utils/grpc.h"
#include "riva/proto/riva_nmt.grpc.pb.h"
#include "riva/utils/files/files.h"
#include "riva/utils/stamping.h"
#include "riva/utils/wav/wav_reader.h"
#include "streaming_s2s_client.h"

using grpc::Status;
using grpc::StatusCode;
namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;

bool g_request_exit = false;

DEFINE_string(
    audio_file, "", "Folder that contains audio files to transcribe or individual audio file name");
DEFINE_bool(
    profanity_filter, false,
    "Flag that controls if generated transcripts should be filtered for the profane words");
DEFINE_bool(automatic_punctuation, true, "Flag that controls if transcript should be punctuated");
DEFINE_bool(
    simulate_realtime, false, "Flag that controls if audio files should be sent in realtime");
DEFINE_string(audio_device, "", "Name of audio device to use");
DEFINE_string(riva_uri, "localhost:50051", "URI to access riva-server");
DEFINE_int32(num_iterations, 1, "Number of times to loop over audio files");
DEFINE_int32(num_parallel_requests, 1, "Number of parallel requests to keep in flight");
DEFINE_int32(chunk_duration_ms, 100, "Chunk duration in milliseconds");
DEFINE_string(source_language_code, "en-US", "Language code for the input speech");
DEFINE_string(target_language_code, "en-US", "Language code for the output speech");
DEFINE_string(
    dnt_phrases_file, "",
    "File with a list of words and phrases to do not translate. One line per word or phrase.");
DEFINE_bool(list_models, false, "List available models on server");
DEFINE_string(boosted_words_file, "", "File with a list of words to boost. One line per word.");
DEFINE_double(boosted_words_score, 10., "Score by which to boost the boosted words");
DEFINE_bool(
    verbatim_transcripts, true,
    "True returns text exactly as it was said with no normalization.  False applies text inverse "
    "normalization");
DEFINE_string(ssl_cert, "", "Path to SSL client certificates file");
DEFINE_string(tts_encoding, "", "TTS output encoding, currently either PCM or OPUS");
DEFINE_string(
    tts_audio_file, "s2s_output.wav", "File containing translated audio for input speech");
DEFINE_int32(tts_sample_rate, 44100, "TTS sample rate hz");
DEFINE_string(tts_voice_name, "English-US.Female-1", "Desired TTS voice name");
DEFINE_bool(
    use_ssl, false,
    "Whether to use SSL credentials or not. If ssl_cert is specified, "
    "this is assumed to be true");
DEFINE_string(metadata, "", "Comma separated key-value pair(s) of metadata to be sent to server");
DEFINE_string(tts_prosody_rate, "", "Speech rate for TTS output");
DEFINE_string(tts_prosody_pitch, "", "Speech pitch for TTS output");
DEFINE_string(tts_prosody_volume, "", "Speech volume for TTS output");

void
signal_handler(int signal_num)
{
  static int count;
  if (count > 0) {
    std::cout << "Force exit\n";
    exit(1);
  }
  std::cout << "Stopping capture\n";
  g_request_exit = true;
  count++;
}

bool
is_numeric(const std::string& str)
{
  if (str.empty())
    return false;
  size_t pos = str.find_first_not_of("0123456789.-+");
  if (pos != std::string::npos && pos != str.length()) {
    return false;
  }
  try {
    std::stod(str);
    return true;
  }
  catch (const std::invalid_argument&) {
    return false;
  }
  catch (const std::out_of_range&) {
    return false;
  }
}

bool
in_range_or_error(std::string numeric_part, double min_value, double max_value, std::string type)
{
  double numeric_value = std::stod(numeric_part);
  if (numeric_value < min_value || numeric_value > max_value) {
    std::cerr << "Value not in range [" << min_value << "," << max_value << "] for " << type
              << std::endl;
    return false;
  }
  return true;
}

bool
validate_tts_prosody_pitch(std::string& value)
{
  if (value.empty()) {
    return true;
  }

  int len = value.size();
  if (value == "default" || value == "x-low" || value == "low" || value == "medium" ||
      value == "high" || value == "x-high") {
    return true;
  } else if (
      (len > 2 && ((value[len - 2] == 'H' && value[len - 1] == 'z') ||
                   (value[len - 2] == 'h' && value[len - 1] == 'Z'))) &&
      is_numeric(value.substr(0, len - 2)) &&
      in_range_or_error(value.substr(0, len - 2), -150.0, 150.0, "tts_prosody_pitch")) {
    return true;
  } else if (is_numeric(value) && in_range_or_error(value, -3, 3, "tts_prosody_pitch")) {
    return true;
  }

  std::cerr << "Invalid value for tts_prosody_pitch: " << value << std::endl;
  return false;
}

bool
validate_tts_prosody_rate(std::string& value)
{
  if (value.empty()) {
    return true;
  }

  int len = value.size();
  if (value == "default" || value == "x-low" || value == "low" || value == "medium" ||
      value == "high" || value == "x-high") {
    return true;
  } else if (
      len > 1 && value[len - 1] == '%' && is_numeric(value.substr(0, len - 1)) &&
      in_range_or_error(value.substr(0, len - 1), 25.0, 250.0, "tts_prosody_rate")) {
    return true;
  } else if (is_numeric(value) && in_range_or_error(value, 25.0, 250.0, "tts_prosody_rate")) {
    return true;
  }

  std::cerr << "Invalid value for tts_prosody_rate: " << value << std::endl;
  return false;
}

bool
validate_tts_prosody_volume(std::string& value)
{
  if (value.empty()) {
    return true;
  }

  int len = value.size();
  if (value == "default" || value == "silent" || value == "x-soft" || value == "soft" ||
      value == "medium" || value == "loud" || value == "x-loud") {
    return true;
  } else if (
      len >= 2 && (value[len - 2] == 'd' && value[len - 1] == 'B') &&
      is_numeric(value.substr(0, len - 2)) &&
      in_range_or_error(value.substr(0, len - 2), -13.0, 8.0, "tts_prosody_volume")) {
    return true;
  } else if (is_numeric(value) && in_range_or_error(value, -13.0, 8.0, "tts_prosody_volume")) {
    return true;
  }

  std::cerr << "Invalid value for tts_prosody_volume: " << value << std::endl;
  return false;
}

main(int argc, char** argv)
{
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  std::stringstream str_usage;
  str_usage << "Usage: riva_nmt_streaming_s2s_client " << std::endl;
  str_usage << "           --audio_file=<filename or folder> " << std::endl;
  str_usage << "           --audio_device=<device_id (such as hw:5,0)> " << std::endl;
  str_usage << "           --automatic_punctuation=<true|false>" << std::endl;
  str_usage << "           --profanity_filter=<true|false>" << std::endl;
  str_usage << "           --riva_uri=<server_name:port> " << std::endl;
  str_usage << "           --chunk_duration_ms=<integer> " << std::endl;
  str_usage << "           --simulate_realtime=<true|false> " << std::endl;
  str_usage << "           --num_iterations=<integer> " << std::endl;
  str_usage << "           --num_parallel_requests=<integer> " << std::endl;
  str_usage << "           --verbatim_transcripts=<true|false>" << std::endl;
  str_usage << "           --source_language_code=<bcp 47 language code (such as en-US)>"
            << std::endl;
  str_usage << "           --target_language_code=<bcp 47 language code (such as en-US)>"
            << std::endl;
  str_usage << "           --dnt_phrases_file=<string>" << std::endl;
  str_usage << "           --list_models" << std::endl;
  str_usage << "           --boosted_words_file=<string>" << std::endl;
  str_usage << "           --boosted_words_score=<float>" << std::endl;
  str_usage << "           --ssl_cert=<filename>" << std::endl;
  str_usage << "           --tts_encoding=<opus|pcm>" << std::endl;
  str_usage << "           --tts_audio_file=<filename>" << std::endl;
  str_usage << "           --tts_sample_rate=<rate hz>" << std::endl;
  str_usage << "           --tts_voice_name=<voice name>" << std::endl;
  str_usage << "           --metadata=<key,value,...>" << std::endl;
  str_usage << "           --tts_prosody_rate=<output speech rate>" << std::endl;
  str_usage << "           --tts_prosody_pitch=<output speech pitch>" << std::endl;
  str_usage << "           --tts_prosody_volume=<output speech volume>" << std::endl;

  gflags::SetUsageMessage(str_usage.str());
  gflags::SetVersionString(::riva::utils::kBuildScmRevision);

  if (argc < 2) {
    std::cout << gflags::ProgramUsage();
    return 1;
  }

  std::signal(SIGINT, signal_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc > 1) {
    std::cout << gflags::ProgramUsage();
    return 1;
  }

  bool flag_set = gflags::GetCommandLineFlagInfoOrDie("riva_uri").is_default;
  const char* riva_uri = getenv("RIVA_URI");

  if (riva_uri && flag_set) {
    std::cout << "Using environment for " << riva_uri << std::endl;
    FLAGS_riva_uri = riva_uri;
  }

  std::shared_ptr<grpc::Channel> grpc_channel;
  try {
    auto creds =
        riva::clients::CreateChannelCredentials(FLAGS_use_ssl, FLAGS_ssl_cert, FLAGS_metadata);
    grpc_channel = riva::clients::CreateChannelBlocking(FLAGS_riva_uri, creds);
  }
  catch (const std::exception& e) {
    std::cerr << "Error creating GRPC channel: " << e.what() << std::endl;
    std::cerr << "Exiting." << std::endl;
    return 1;
  }

  if (FLAGS_list_models) {
    std::unique_ptr<nr_nmt::RivaTranslation::Stub> nmt_s2s(
        nr_nmt::RivaTranslation::NewStub(grpc_channel));
    grpc::ClientContext context;
    nr_nmt::AvailableLanguageRequest request;
    nr_nmt::AvailableLanguageResponse response;

<<<<<<< HEAD
    request.set_model("s2s_model");  // get only S2S supported languages
=======
    request.set_model("s2s_model"); // this is optional, if empty returns all available models/languages
>>>>>>> 9a8b03b (Add list_models option for s2s/s2t clients (#93))
    nmt_s2s->ListSupportedLanguagePairs(&context, request, &response);
    std::cout << response.DebugString() << std::endl;
    return 0;
  }

  if (!FLAGS_tts_encoding.empty() && FLAGS_tts_encoding != "pcm" && FLAGS_tts_encoding != "opus") {
    std::cerr << "Unsupported encoding: \'" << FLAGS_tts_encoding << "\'" << std::endl;
    return -1;
  }

  if (!validate_tts_prosody_rate(FLAGS_tts_prosody_rate) ||
      !validate_tts_prosody_pitch(FLAGS_tts_prosody_pitch) ||
      !validate_tts_prosody_volume(FLAGS_tts_prosody_volume)) {
    std::cerr << "Invalid prosody parameters, exiting." << std::endl;
    return 1;
  }

  StreamingS2SClient recognize_client(
      grpc_channel, FLAGS_num_parallel_requests, FLAGS_source_language_code,
      FLAGS_target_language_code, FLAGS_dnt_phrases_file, FLAGS_profanity_filter,
      FLAGS_automatic_punctuation,
      /* separate_recognition_per_channel*/ false, FLAGS_chunk_duration_ms, FLAGS_simulate_realtime,
      FLAGS_verbatim_transcripts, FLAGS_boosted_words_file, FLAGS_boosted_words_score,
      FLAGS_tts_encoding, FLAGS_tts_audio_file, FLAGS_tts_sample_rate, FLAGS_tts_voice_name,
      FLAGS_tts_prosody_rate, FLAGS_tts_prosody_pitch, FLAGS_tts_prosody_volume);

  if (FLAGS_audio_file.size()) {
    return recognize_client.DoStreamingFromFile(
        FLAGS_audio_file, FLAGS_num_iterations, FLAGS_num_parallel_requests);

  } else if (FLAGS_audio_device.size()) {
    if (FLAGS_num_parallel_requests != 1) {
      std::cout << "num_parallel_requests must be set to 1 with microphone input" << std::endl;
      return 1;
    }

    if (FLAGS_simulate_realtime) {
      std::cout << "simulate_realtime must be set to false with microphone input" << std::endl;
      return 1;
    }

    if (FLAGS_num_iterations != 1) {
      std::cout << "num_iterations must be set to 1 with microphone input" << std::endl;
      return 1;
    }

    return recognize_client.DoStreamingFromMicrophone(FLAGS_audio_device, g_request_exit);

  } else {
    std::cout << "No audio files or audio device specified, exiting" << std::endl;
  }

  return 0;
}
