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
DEFINE_string(source_language_code, "en-US", "Language code for the input text");
DEFINE_string(target_language_code, "en-US", "Language code for the output text");
DEFINE_string(model_name, "", "Model to use");
DEFINE_bool(list_models, false, "List available models on server");
DEFINE_int32(num_iterations, 1, "Number of times to loop over text");
DEFINE_int32(num_parallel_requests, 1, "Number of parallel requests");
DEFINE_string(ssl_cert, "", "Path to SSL client certificates file");
DEFINE_int32(batch_size, 8, "Batch size to use");
DEFINE_bool(
    use_ssl, false,
    "Whether to use SSL credentials or not. If ssl_cert is specified, "
    "this is assumed to be true");
DEFINE_string(metadata, "", "Comma separated key-value pair(s) of metadata to be sent to server");

int
translateBatch(
    std::unique_ptr<nr_nmt::RivaTranslation::Stub> nmt,
    std::queue<std::vector<std::pair<int, std::string>>>& work,
    const std::string target_language_code, const std::string source_language_code,
    const std::string model_name, std::mutex& mtx, std::vector<double>& latencies, std::mutex& lmtx,
    std::vector<nr_nmt::TranslateTextResponse>& responses)
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
    request.set_source_language(source_language_code);
    request.set_target_language(target_language_code);
    *request.mutable_texts() = {text.begin(), text.end()};
    // std::cout << request.DebugString() << std::endl;

    auto start = std::chrono::steady_clock::now();
    grpc::Status rpc_status = nmt->TranslateText(&context, request, &response);
    if (!rpc_status.ok()) {
      LOG(ERROR) << rpc_status.error_message();
    }
    responses.push_back(response);
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = end - start;
    {
      std::lock_guard<std::mutex> lguard(lmtx);
      latencies.push_back(duration.count());
    }
  }
}

int countWords(const std::string& text) {
    int wordCount = 0;
    bool wasSpace = true; 
    for (char c : text) {
        if (std::isspace(c)) {
            if (!wasSpace) {
                wordCount++;
            }
            wasSpace = true;
        } else {
            wasSpace = false;
        }
    }
    if (!wasSpace) {
        wordCount++;
    }
    return wordCount;
}

int
main(int argc, char** argv)
{
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  std::stringstream str_usage;
  str_usage << "Usage: riva_nmt_t2t_client" << std::endl;
  str_usage << "           --text_file=<filename> " << std::endl;
  str_usage << "           --riva_uri=<server_name:port> " << std::endl;
  str_usage << "           --num_iterations=<integer> " << std::endl;
  str_usage << "           --num_parallel_requests=<integer> " << std::endl;
  str_usage << "           --batch_size=<integer> " << std::endl;
  str_usage << "           --ssl_cert=<filename>" << std::endl;
  str_usage << "           --text=\"text to translate\"" << std::endl;
  str_usage << "           --source_language_code=<bcp 47 language code (such as en-US)>"
            << std::endl;
  str_usage << "           --target_language_code=<bcp 47 language code (such as en-US)>"
            << std::endl;
  str_usage << "           --model_name=<model>" << std::endl;
  str_usage << "           --list_models" << std::endl;
  str_usage << "           --metadata=<key,value,...>" << std::endl;
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

  if (FLAGS_batch_size <= 0) {
    LOG(ERROR) << "Invalid batch size: " << FLAGS_batch_size;
    return 1;
  }

  if (FLAGS_num_iterations <= 0) {
    LOG(ERROR) << "Invalid num iterations: " << FLAGS_num_iterations;
    return 1;
  }

  if (FLAGS_num_parallel_requests <= 0) {
    LOG(ERROR) << "Invalid num parallel requests: " << FLAGS_num_parallel_requests;
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

  // Do initialization prior to anything
  std::unique_ptr<nr_nmt::RivaTranslation::Stub> nmt(
      nr_nmt::RivaTranslation::NewStub(grpc_channel));

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
    request.set_source_language(FLAGS_source_language_code);
    request.set_target_language(FLAGS_target_language_code);

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
    int count = 0, total_words = 0;
    std::vector<std::pair<int, std::string>> batch;
    std::vector<std::vector<std::pair<int, std::string>>> all_requests;
    std::ifstream nmt_file(FLAGS_text_file);
    if (nmt_file.fail()) {
      LOG(ERROR) << FLAGS_text_file << " failed to load, please check file " << std::endl;
      return 1;
    }

    while (std::getline(nmt_file, str)) {
      if ((batch.size() > 0) && ((int)batch.size() == FLAGS_batch_size)) {
        all_requests.push_back(batch);
        batch.clear();
      }
      if (!str.empty()) {
        total_words += countWords(str);
        batch.push_back(make_pair(count, str));
        count++;
      }
    }

    if (batch.size() > 0) {
      all_requests.push_back(batch);
    }

    if (!all_requests.size()) {
      LOG(ERROR) << "No text to process";
      return 1;
    }

    auto request_count = all_requests.size();

    auto start = std::chrono::steady_clock::now();
    std::mutex mtx;   // queue
    std::mutex lmtx;  // latency vector
    std::vector<double> latencies;

    for (int iters = 0; iters < FLAGS_num_iterations; iters++) {
      std::queue<std::vector<std::pair<int, std::string>>> request_queue;
      std::vector<std::thread> workers;
      std::vector<std::vector<nr_nmt::TranslateTextResponse>> responses(
          FLAGS_num_parallel_requests);

      for (auto& request : all_requests) {
        request_queue.push(request);
      }

      for (int i = 0; i < FLAGS_num_parallel_requests; i++) {
        workers.push_back(std::thread([&, i]() {
          std::unique_ptr<nr_nmt::RivaTranslation::Stub> nmt2(
              nr_nmt::RivaTranslation::NewStub(grpc_channel));
          translateBatch(
              std::move(nmt2), request_queue, FLAGS_target_language_code,
              FLAGS_source_language_code, FLAGS_model_name, mtx, latencies, lmtx, responses.at(i));
        }));
      }

      std::for_each(workers.begin(), workers.end(), [](std::thread& worker) { worker.join(); });

      for (int i = 0; i < FLAGS_num_parallel_requests; i++) {
        for (auto response : responses.at(i))
          for (auto i : response.translations()) {
            std::cout << i.text() << std::endl;
          }
      }
    }
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> total = end - start;
    LOG(INFO) << FLAGS_model_name << "-" << FLAGS_batch_size << "-" << FLAGS_source_language_code
              << "-" << FLAGS_target_language_code << ",count:" << count
              << ",total words: " << total_words 
              << ",total time: " << total.count()
              << ",requests/second: " << FLAGS_num_iterations * request_count / total.count()
              << ",tokens/second: " << FLAGS_num_iterations * total_words /total.count();

    std::sort(latencies.begin(), latencies.end());
    auto size = latencies.size();

    LOG(INFO) << "P90: " << latencies[static_cast<int>(0.9 * size)]
              << ",P95: " << latencies[static_cast<int>(0.95 * size)]
              << ",P99: " << latencies[static_cast<int>(0.99 * size)];
  }


  // std::vector<std::thread> workers;
}
