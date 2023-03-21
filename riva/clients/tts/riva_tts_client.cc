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
#include <string>

#include "riva/clients/utils/grpc.h"
#include "riva/proto/riva_tts.grpc.pb.h"
#include "riva/utils/files/files.h"
#include "riva/utils/opus/opus_client_decoder.h"
#include "riva/utils/stamping.h"
#include "riva/utils/wav/wav_writer.h"

using grpc::Status;
using grpc::StatusCode;
namespace nr = nvidia::riva;
namespace nr_tts = nvidia::riva::tts;

DEFINE_string(text, "", "Text to be synthesized");
DEFINE_string(audio_file, "output.wav", "Output file");
DEFINE_string(audio_encoding, "pcm", "Audio encoding (pcm or opus)");
DEFINE_string(riva_uri, "localhost:50051", "Riva API server URI and port");
DEFINE_string(ssl_cert, "", "Path to SSL client certificatates file");
DEFINE_int32(rate, 44100, "Sample rate for the TTS output");
DEFINE_bool(online, false, "Whether synthesis should be online or batch");
DEFINE_string(
    language, "en-US",
    "Language code as per [BCP-47](https://www.rfc-editor.org/rfc/bcp/bcp47.txt) language tag.");
DEFINE_string(voice_name, "", "Desired voice name");

static const std::string LC_enUS = "en-US";

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
  str_usage << "           --ssl_cert=<filename>" << std::endl;
  gflags::SetUsageMessage(str_usage.str());
  gflags::SetVersionString(::riva::utils::kBuildScmRevision);

  if (argc < 2) {
    std::cout << gflags::ProgramUsage();
    return 1;
  }

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc > 1) {
    std::cout << gflags::ProgramUsage();
    return 1;
  }

  auto text = FLAGS_text;
  if (text.length() == 0) {
    std::cerr << "Input text cannot be empty." << std::endl;
    return -1;
  }

  bool flag_set = gflags::GetCommandLineFlagInfoOrDie("riva_uri").is_default;
  const char* riva_uri = getenv("RIVA_URI");

  if (riva_uri && flag_set) {
    std::cout << "Using environment for " << riva_uri << std::endl;
    FLAGS_riva_uri = riva_uri;
  }
  std::shared_ptr<grpc::ChannelCredentials> creds;
  if (FLAGS_ssl_cert.size() > 0) {
    try {
      auto cacert = riva::utils::files::ReadFileContentAsString(FLAGS_ssl_cert);
      grpc::SslCredentialsOptions ssl_opts;
      ssl_opts.pem_root_certs = cacert;
      LOG(INFO) << "Using SSL Credentials";
      creds = grpc::SslCredentials(ssl_opts);
    }
    catch (const std::exception& e) {
      std::cout << "Failed to load SSL certificate: " << e.what() << std::endl;
      return 1;
    }
  } else {
    LOG(INFO) << "Using Insecure Server Credentials";
    creds = grpc::InsecureChannelCredentials();
  }

  auto channel = riva::clients::CreateChannelBlocking(FLAGS_riva_uri, creds);

  std::unique_ptr<nr_tts::RivaSpeechSynthesis::Stub> tts(
      nr_tts::RivaSpeechSynthesis::NewStub(channel));

  // Parse command line arguments.
  nr_tts::SynthesizeSpeechRequest request;
  request.set_text(text);
  request.set_language_code(FLAGS_language);
  if (FLAGS_audio_encoding.empty() || FLAGS_audio_encoding == "pcm") {
    request.set_encoding(nr::LINEAR_PCM);
  } else if (FLAGS_audio_encoding == "opus") {
    request.set_encoding(nr::OGGOPUS);
  } else {
    std::cerr << "Unsupported encoding: \'" << FLAGS_audio_encoding << "\'" << std::endl;
    return -1;
  }

  int32_t rate = FLAGS_rate;
  if (FLAGS_audio_encoding == "opus") {
    rate = riva::utils::opus::Decoder::AdjustRateIfUnsupported(FLAGS_rate);
  }

  request.set_sample_rate_hz(rate);
  request.set_voice_name(FLAGS_voice_name);

  // Send text content using Synthesize().
  grpc::ClientContext context;
  nr_tts::SynthesizeSpeechResponse response;

  if (!FLAGS_online) {  // batch inference
    auto start = std::chrono::steady_clock::now();
    grpc::Status rpc_status = tts->Synthesize(&context, request, &response);
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cerr << "Request time: " << elapsed.count() << " s" << std::endl;

    if (!rpc_status.ok()) {
      // Report the RPC failure.
      std::cerr << rpc_status.error_message() << std::endl;
      std::cerr << "Input was: \'" << text << "\'" << std::endl;
      return -1;
    }

    auto audio = response.audio();
    std::cerr << "Got " << audio.length() << " bytes back from server" << std::endl;
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
        std::cerr << "Time to first chunk: " << elapsed_first_audio.count() << " s" << std::endl;
      }
      std::cout << "Got chunk: " << chunk.audio().size() << " bytes" << std::endl;
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
    std::cerr << "Streaming time: " << elapsed_total.count() << " s" << std::endl;

    if (!rpc_status.ok()) {
      // Report the RPC failure.
      std::cerr << rpc_status.error_message() << std::endl;
      std::cerr << "Input was: \'" << text << "\'" << std::endl;
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
