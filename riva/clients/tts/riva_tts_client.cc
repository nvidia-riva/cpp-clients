/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpcpp/grpcpp.h>
#include <strings.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

#include "riva/clients/utils/grpc.h"
#include "riva/proto/riva_tts.grpc.pb.h"
#include "riva/utils/files/files.h"
#include "riva/utils/opus/opus_client_decoder.h"
#include "riva/utils/stamping.h"
#include "riva/utils/wav/wav_reader.h"
#include "riva/utils/wav/wav_writer.h"

using grpc::Status;
using grpc::StatusCode;
namespace nr = nvidia::riva;
namespace nr_tts = nvidia::riva::tts;

DEFINE_string(text, "", "Text to be synthesized");
DEFINE_string(audio_file, "output.wav", "Output file");
DEFINE_string(audio_encoding, "pcm", "Audio encoding (pcm or opus)");
DEFINE_string(riva_uri, "localhost:50051", "Riva API server URI and port");
DEFINE_string(ssl_root_cert, "", "Path to SSL root certificates file");
DEFINE_string(ssl_client_key, "", "Path to SSL client certificates key");
DEFINE_string(ssl_client_cert, "", "Path to SSL client certificates file");
DEFINE_int32(rate, 44100, "Sample rate for the TTS output");
DEFINE_bool(online, false, "Whether synthesis should be online or batch");
DEFINE_string(
    language, "en-US",
    "Language code as per [BCP-47](https://www.rfc-editor.org/rfc/bcp/bcp47.txt) language tag.");
DEFINE_string(voice_name, "", "Desired voice name");
DEFINE_bool(
    use_ssl, false,
    "Whether to use SSL credentials or not. If ssl_root_cert is specified, "
    "this is assumed to be true");
DEFINE_string(metadata, "", "Comma separated key-value pair(s) of metadata to be sent to server");
DEFINE_string(
    zero_shot_audio_prompt, "",
    "Input audio prompt file for Zero Shot Model. Audio length should be between 3-10 seconds.");
DEFINE_int32(zero_shot_quality, 20, "Required quality of output audio, ranges between 1-40.");
DEFINE_string(custom_dictionary, "", " User dictionary containing graph-to-phone custom words");
DEFINE_string(zero_shot_transcript, "", "Transcript corresponding to Zero shot audio prompt.");
DEFINE_uint64(timeout_ms, 10000, "Timeout for GRPC channel creation");
DEFINE_uint64(max_grpc_message_size, MAX_GRPC_MESSAGE_SIZE, "Max GRPC message size");

static const std::string LC_enUS = "en-US";

std::string
ReadUserDictionaryFile(const std::string& dictionary_file)
{
  std::string dictionary_string;
  if (!dictionary_file.empty()) {
    std::ifstream infile(dictionary_file);

    if (infile.is_open()) {
      std::string line;

      while (std::getline(infile, line)) {
        // Trim leading and trailing whitespaces
        line = std::regex_replace(line, std::regex("^ +| +$"), "");
        size_t pos = line.find("  ");

        if (pos != std::string::npos) {
          std::string key = line.substr(0, pos);
          std::string value = std::regex_replace(line.substr(pos + 2), std::regex("^ +"), "");
          // Append the key-value pair to the dictionary string
          if (!dictionary_string.empty()) {
            dictionary_string += ",";
          }
          dictionary_string += key + "  " + value;
        } else {
          LOG(WARNING) << "Warning: Malformed line " << line << std::endl;
        }
      }
    } else {
      std::string err = "Could not open file " + dictionary_file;
      throw std::runtime_error(err);
    }
  }
  return dictionary_string;
}

int
main(int argc, char** argv)
{
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  std::stringstream str_usage;
  str_usage << "Usage: riva_tts_client " << std::endl;
  str_usage << "           --text=<text> " << std::endl;
  str_usage << "           --audio_file=<filename> " << std::endl;
  str_usage << "           --audio_encoding=<pcm|opus> " << std::endl;
  str_usage << "           --riva_uri=<server_name:port> " << std::endl;
  str_usage << "           --rate=<sample_rate> " << std::endl;
  str_usage << "           --language=<language-code> " << std::endl;
  str_usage << "           --voice_name=<voice-name> " << std::endl;
  str_usage << "           --online=<true|false> " << std::endl;
  str_usage << "           --ssl_root_cert=<filename>" << std::endl;
  str_usage << "           --ssl_client_key=<filename>" << std::endl;
  str_usage << "           --ssl_client_cert=<filename>" << std::endl;
  str_usage << "           --metadata=<key,value,...>" << std::endl;
  str_usage << "           --zero_shot_audio_prompt=<filename>" << std::endl;
  str_usage << "           --zero_shot_quality=<quality>" << std::endl;
  str_usage << "           --zero_shot_transcript=<text>" << std::endl;
  str_usage << "           --custom_dictionary=<filename> " << std::endl;
  str_usage << "           --timeout_ms=<timeout_ms> " << std::endl;
  str_usage << "           --max_grpc_message_size=<max_grpc_message_size> " << std::endl;
  gflags::SetUsageMessage(str_usage.str());
  gflags::SetVersionString(::riva::utils::kBuildScmRevision);

  if (argc < 2) {
    LOG(INFO) << gflags::ProgramUsage();
    return 1;
  }

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc > 1) {
    LOG(INFO) << gflags::ProgramUsage();
    return 1;
  }

  auto text = FLAGS_text;
  if (text.length() == 0) {
    LOG(ERROR) << "Input text cannot be empty." << std::endl;
    return -1;
  }

  bool flag_set = gflags::GetCommandLineFlagInfoOrDie("riva_uri").is_default;
  const char* riva_uri = getenv("RIVA_URI");

  if (riva_uri && flag_set) {
    LOG(INFO) << "Using environment for " << riva_uri << std::endl;
    FLAGS_riva_uri = riva_uri;
  }

  std::shared_ptr<grpc::Channel> grpc_channel;
  try {
    auto creds = riva::clients::CreateChannelCredentials(
        FLAGS_use_ssl, FLAGS_ssl_root_cert, FLAGS_ssl_client_key, FLAGS_ssl_client_cert,
        FLAGS_metadata);
    grpc_channel = riva::clients::CreateChannelBlocking(FLAGS_riva_uri, creds, FLAGS_timeout_ms, FLAGS_max_grpc_message_size);
  }
  catch (const std::exception& e) {
    std::cerr << "Error creating GRPC channel: " << e.what() << std::endl;
    std::cerr << "Exiting." << std::endl;
    return 1;
  }

  std::unique_ptr<nr_tts::RivaSpeechSynthesis::Stub> tts(
      nr_tts::RivaSpeechSynthesis::NewStub(grpc_channel));

  // Parse command line arguments.
  nr_tts::SynthesizeSpeechRequest request;
  request.set_text(text);
  request.set_language_code(FLAGS_language);
  if (FLAGS_audio_encoding.empty() || FLAGS_audio_encoding == "pcm") {
    request.set_encoding(nr::LINEAR_PCM);
  } else if (FLAGS_audio_encoding == "opus") {
    request.set_encoding(nr::OGGOPUS);
  } else {
    LOG(ERROR) << "Unsupported encoding: \'" << FLAGS_audio_encoding << "\'" << std::endl;
    return -1;
  }

  int32_t rate = FLAGS_rate;
  if (FLAGS_audio_encoding == "opus") {
    rate = riva::utils::opus::Decoder::AdjustRateIfUnsupported(FLAGS_rate);
  }

  std::string custom_dictionary = ReadUserDictionaryFile(FLAGS_custom_dictionary);
  request.set_custom_dictionary(custom_dictionary);

  request.set_sample_rate_hz(rate);
  request.set_voice_name(FLAGS_voice_name);
  if (not FLAGS_zero_shot_audio_prompt.empty()) {
    auto zero_shot_data = request.mutable_zero_shot_data();
    std::vector<std::shared_ptr<WaveData>> audio_prompt;
    try {
      LoadWavData(audio_prompt, FLAGS_zero_shot_audio_prompt);
    }
    catch (const std::exception& e) {
      LOG(ERROR) << "Unable to load audio file: " << e.what() << std::endl;
      return 1;
    }
    if (audio_prompt.size() != 1) {
      LOG(ERROR) << "Unsupported number of audio prompts. Need exactly 1 audio prompt."
                 << std::endl;
      return -1;
    }
    if (audio_prompt[0]->encoding != nr::LINEAR_PCM && audio_prompt[0]->encoding != nr::OGGOPUS) {
      LOG(ERROR) << "Unsupported encoding for zero shot prompt: \'" << audio_prompt[0]->encoding
                 << "\'";
      return -1;
    }
    zero_shot_data->set_audio_prompt(&audio_prompt[0]->data[0], audio_prompt[0]->data.size());
    int32_t zero_shot_sample_rate = audio_prompt[0]->sample_rate;
    zero_shot_data->set_encoding(audio_prompt[0]->encoding);
    if (audio_prompt[0]->encoding == nr::OGGOPUS) {
      zero_shot_sample_rate =
          riva::utils::opus::Decoder::AdjustRateIfUnsupported(zero_shot_sample_rate);
    }
    zero_shot_data->set_sample_rate_hz(zero_shot_sample_rate);
    zero_shot_data->set_quality(FLAGS_zero_shot_quality);
    if (not FLAGS_online and not FLAGS_zero_shot_transcript.empty()) {
      zero_shot_data->set_transcript(FLAGS_zero_shot_transcript);
    }
  }

  // Send text content using Synthesize().
  grpc::ClientContext context;
  nr_tts::SynthesizeSpeechResponse response;

  if (!FLAGS_online) {  // batch inference
    auto start = std::chrono::steady_clock::now();
    grpc::Status rpc_status = tts->Synthesize(&context, request, &response);
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    LOG(INFO) << "Request time: " << elapsed.count() << " s" << std::endl;

    if (!rpc_status.ok()) {
      // Report the RPC failure.
      LOG(ERROR) << rpc_status.error_message() << std::endl;
      LOG(ERROR) << "Input was: \'" << text << "\'" << std::endl;
      return -1;
    }

    auto audio = response.audio();
    LOG(INFO) << "Got " << audio.length() << " bytes back from server" << std::endl;
    // Write to WAV file
    if (FLAGS_audio_encoding.empty() || FLAGS_audio_encoding == "pcm") {
      ::riva::utils::wav::Write(
          FLAGS_audio_file, rate, (int16_t*)audio.data(), audio.length() / sizeof(int16_t));
    } else if (FLAGS_audio_encoding == "opus") {
      riva::utils::opus::Decoder decoder(rate, 1);
      auto ptr = reinterpret_cast<unsigned char*>(audio.data());
      auto pcm = decoder.DecodePcm(
          decoder.DeserializeOpus(std::vector<unsigned char>(ptr, ptr + audio.size())));
      ::riva::utils::wav::Write(FLAGS_audio_file, rate, pcm.data(), pcm.size());
    }
  } else {  // online inference
    if (not FLAGS_zero_shot_transcript.empty()) {
      LOG(ERROR) << "Zero shot transcript is not supported for streaming inference.";
      return -1;
    }
    std::vector<int16_t> pcm_buffer;
    std::vector<unsigned char> opus_buffer;
    size_t audio_len = 0;
    nr_tts::SynthesizeSpeechResponse chunk;
    auto start = std::chrono::steady_clock::now();
    std::unique_ptr<grpc::ClientReader<nr_tts::SynthesizeSpeechResponse>> reader(
        tts->SynthesizeOnline(&context, request));
    while (reader->Read(&chunk)) {
      // Copy chunk to local buffer
      if (audio_len == 0) {
        auto t_first_audio = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_first_audio = t_first_audio - start;
        LOG(INFO) << "Time to first chunk: " << elapsed_first_audio.count() << " s" << std::endl;
      }
      LOG(INFO) << "Got chunk: " << chunk.audio().size() << " bytes" << std::endl;
      if (FLAGS_audio_encoding.empty() || FLAGS_audio_encoding == "pcm") {
        int16_t* audio_data = (int16_t*)chunk.audio().data();
        size_t len = chunk.audio().length() / sizeof(int16_t);
        std::copy(audio_data, audio_data + len, std::back_inserter(pcm_buffer));
        audio_len += len;
      } else if (FLAGS_audio_encoding == "opus") {
        const unsigned char* opus_data = (unsigned char*)chunk.audio().data();
        size_t len = chunk.audio().length();
        std::copy(opus_data, opus_data + len, std::back_inserter(opus_buffer));
        audio_len += len;
      }
    }
    grpc::Status rpc_status = reader->Finish();
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_total = end - start;
    LOG(INFO) << "Streaming time: " << elapsed_total.count() << " s" << std::endl;

    if (!rpc_status.ok()) {
      // Report the RPC failure.
      LOG(ERROR) << rpc_status.error_message() << std::endl;
      LOG(ERROR) << "Input was: \'" << text << "\'" << std::endl;
      return -1;
    }

    if (FLAGS_audio_encoding.empty() || FLAGS_audio_encoding == "pcm") {
      ::riva::utils::wav::Write(FLAGS_audio_file, rate, pcm_buffer.data(), pcm_buffer.size());
    } else if (FLAGS_audio_encoding == "opus") {
      riva::utils::opus::Decoder decoder(rate, 1);
      auto pcm = decoder.DecodePcm(decoder.DeserializeOpus(opus_buffer));
      ::riva::utils::wav::Write(FLAGS_audio_file, rate, pcm.data(), pcm.size());
    }
  }
  return 0;
}
