/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */


#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpcpp/grpcpp.h>
#include <strings.h>

#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "riva/clients/utils/grpc.h"
#include "riva/proto/riva_nmt.grpc.pb.h"
#include "riva/utils/files/files.h"
using grpc::Status;
using grpc::StatusCode;

namespace nr = nvidia::riva;
namespace nr_nmt = nvidia::riva::nmt;

DEFINE_string(
    text_file, "", "Text file with list of sentences to be TRANSLATED. Ignored if 'text' is set.");
DEFINE_string(riva_uri, "localhost:50051", "Riva API server URI and port");
DEFINE_string(text, "", "Text to translate");
DEFINE_string(src_language, "en", "Source language");
DEFINE_string(tgt_language, "zh", "Target langugae");
DEFINE_string(model_name, "riva-nmt", "Model to use");
DEFINE_bool(list_models, false, "returns available models from server");
DEFINE_bool(print_line_numbers, false, "prepend line number to translated texts");
DEFINE_int32(num_iterations, 1, "Number of times to loop over text");
DEFINE_int32(num_parallel_requests, 1, "Number of parallel requests");
DEFINE_string(ssl_cert, "", "Path to SSL client certificatates file");
DEFINE_int32(batch_size, 8, "batch size to use");


int
translateBatch(
    std::unique_ptr<nr_nmt::RivaTranslation::Stub> nmt,
    std::queue<std::vector<std::pair<int, std::string>>>& work, const std::string tgt_lang,
    const std::string src_lang, const std::string model_name, std::mutex& mtx,
    std::vector<double>& latencies, std::mutex& lmtx)
{
  while (1) {
    std::vector<std::pair<int, std::string>> pairs;
    {
      std::lock_guard<std::mutex> guard(mtx);
      if (work.empty()) {
        return 1;
      }
      pairs = work.front();
      work.pop();
    }

    std::vector<std::string> text;
    for (auto it = pairs.begin(); it != pairs.end(); ++it) {
      text.push_back(it->second);
    }
    grpc::ClientContext context;
    nr_nmt::TranslateTextRequest request;
    nr_nmt::TranslateTextResponse response;
    request.set_model(model_name);
    request.set_source_language(src_lang);
    request.set_target_language(tgt_lang);
    *request.mutable_texts() = {text.begin(), text.end()};
    // std::cout << request.DebugString() << std::endl;

    auto start = std::chrono::steady_clock::now();
    grpc::Status rpc_status = nmt->TranslateText(&context, request, &response);
    if (!rpc_status.ok()) {
      LOG(ERROR) << rpc_status.error_message();
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = end - start;
    {
      std::lock_guard<std::mutex> lguard(lmtx);
      latencies.push_back(duration.count());
    }
    // auto untranslated = text.begin();

    auto current_line = pairs[0].first;
    for (auto i : response.translations()) {
      // std::cout << *untranslated << "->" << i.text() << std::endl;
      // untranslated++;
      if (FLAGS_print_line_numbers) {
        std::cout << current_line << ":" << i.text() << std::endl;
      } else {
        std::cout << i.text() << std::endl;
      }
      current_line++;
    }
  }
}


int
main(int argc, char** argv)
{
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  std::stringstream str_usage;
  str_usage << "Usage: riva_nmt" << std::endl;
  str_usage << "           --text_file=<filename> " << std::endl;
  str_usage << "           --riva_uri=<server_name:port> " << std::endl;
  str_usage << "           --num_iterations=<integer> " << std::endl;
  str_usage << "           --num_parallel_requests=<integer> " << std::endl;
  str_usage << "           --batch_size=<integer> " << std::endl;
  str_usage << "           --ssl_cert=<filename>" << std::endl;
  str_usage << "           --text=\"text to translate\"" << std::endl;
  str_usage << "           --src_language=<lang>" << std::endl;
  str_usage << "           --tgt_language=<lang>" << std::endl;
  str_usage << "           --model_name=<model>" << std::endl;
  str_usage << "           --list_models" << std::endl;
  gflags::SetUsageMessage(str_usage.str());

  if (argc < 2) {
    std::cout << gflags::ProgramUsage();
    return 1;
  }

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc > 1) {
    std::cout << argc << std::endl;
    std::cout << gflags::ProgramUsage();
    return 1;
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
    creds = grpc::InsecureChannelCredentials();
  }

  // Do initialization prior to anytiming

  auto channel = riva::clients::CreateChannelBlocking(FLAGS_riva_uri, creds);
  std::unique_ptr<nr_nmt::RivaTranslation::Stub> nmt(nr_nmt::RivaTranslation::NewStub(channel));

  grpc::ClientContext context;

  if (FLAGS_list_models) {
    nr_nmt::AvailableLanguageRequest request;
    nr_nmt::AvailableLanguageResponse response;

    nmt->ListSupportedLanguagePairs(&context, request, &response);
    std::cout << response.DebugString() << std::endl;
    return 0;
  }


  if (FLAGS_text != "") {
    nr_nmt::TranslateTextRequest request;
    nr_nmt::TranslateTextResponse response;
    request.set_model(FLAGS_model_name);
    request.set_source_language(FLAGS_src_language);
    request.set_target_language(FLAGS_tgt_language);

    request.add_texts(FLAGS_text);
    grpc::Status rpc_status = nmt->TranslateText(&context, request, &response);
    if (!rpc_status.ok()) {
      LOG(ERROR) << rpc_status.error_message();
      return 0;
    }
    std::cout << response.translations(0).text() << std::endl;
    return 0;
  }

  if (FLAGS_text_file != "") {
    // pull strings into vectors per parallel request
    /*
     *  parallel requests->  batches -> of strings
     *
     */
    // std::vector<std::vector<std::vector<std::string>>> inputs;

    std::string str;
    int count = 0;
    std::vector<std::pair<int, std::string>> batch;
    std::queue<std::vector<std::pair<int, std::string>>> inputs;
    std::ifstream nmt_file(FLAGS_text_file);
    if (nmt_file.fail()) {
      LOG(ERROR) << FLAGS_text_file << " failed to load, please check file " << std::endl;
      return 1;
    }

    int bs = FLAGS_batch_size;

    while (std::getline(nmt_file, str)) {
      if (count && count % bs == 0) {
        inputs.push(batch);
        batch.clear();
      }
      batch.push_back(make_pair(count, str));
      count++;
    }


    if (batch.size() > 0) {
      inputs.push(batch);
    }
    auto batch_count = inputs.size();

    auto start = std::chrono::steady_clock::now();
    std::mutex mtx;   // queue
    std::mutex lmtx;  // latency vector
    std::vector<double> latencies;

    for (int iters = 0; iters < FLAGS_num_iterations; iters++) {
      std::vector<std::thread> workers;

      for (int i = 0; i < FLAGS_num_parallel_requests; i++) {
        workers.push_back(std::thread([&, i]() {
          std::unique_ptr<nr_nmt::RivaTranslation::Stub> nmt2(
              nr_nmt::RivaTranslation::NewStub(channel));
          translateBatch(
              std::move(nmt2), inputs, FLAGS_tgt_language, FLAGS_src_language, FLAGS_model_name,
              mtx, latencies, lmtx);
        }));
      }

      std::for_each(workers.begin(), workers.end(), [](std::thread& worker) { worker.join(); });
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> total = end - start;
    std::cerr << FLAGS_model_name << "-" << FLAGS_batch_size << "-" << FLAGS_src_language << "-"
              << FLAGS_tgt_language << ",count:" << count << ",total time: " << total.count()
              << ",requests/second: " << batch_count / total.count()
              << ",translations/second: " << count / total.count() << std::endl;

    std::sort(latencies.begin(), latencies.end());
    auto size = latencies.size();

    std::cerr << "P90: " << latencies[static_cast<int>(0.9 * size)]
              << ",P95: " << latencies[static_cast<int>(0.95 * size)]
              << ",P99: " << latencies[static_cast<int>(0.99 * size)] << std::endl;
  }


  // std::vector<std::thread> workers;
}
