/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include <alsa/asoundlib.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpcpp/grpcpp.h>
#include <strings.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>

#include "riva/clients/utils/grpc.h"
#include "riva/proto/riva_asr.grpc.pb.h"
#include "riva/utils/files/files.h"
#include "riva/utils/stamping.h"
#include "riva/utils/wav/wav_reader.h"
#include "riva_asr_client_helper.h"

using grpc::Status;
using grpc::StatusCode;
namespace nr = nvidia::riva;
namespace nr_asr = nvidia::riva::asr;

#define clear_screen() printf("\033[H\033[J")
#define gotoxy(x, y) printf("\033[%d;%dH", (y), (x))

DEFINE_string(
    audio_file, "", "Folder that contains audio files to transcribe or individual audio file name");
DEFINE_int32(
    max_alternatives, 1,
    "Maximum number of alternative transcripts to return (up to limit configured on server)");
DEFINE_bool(
    profanity_filter, false, "Flag to control profanity filtering for the generated transcripts");
DEFINE_bool(automatic_punctuation, true, "Flag that controls if transcript should be punctuated");
DEFINE_bool(word_time_offsets, true, "Flag that controls if word time stamps are requested");
DEFINE_string(riva_uri, "localhost:50051", "URI to access riva-server");
DEFINE_int32(num_iterations, 1, "Number of times to loop over audio files");
DEFINE_int32(num_parallel_requests, 10, "Number of parallel requests to keep in flight");
DEFINE_bool(print_transcripts, true, "Print final transcripts");
DEFINE_string(output_filename, "", "Filename to write output transcripts");
DEFINE_string(model_name, "", "Name of the TRTIS model to use");
DEFINE_bool(list_models, false, "List available models on server");
DEFINE_bool(output_ctm, false, "If true, output format should be NIST CTM");
DEFINE_string(language_code, "en-US", "Language code of the model to use");
DEFINE_string(boosted_words_file, "", "File with a list of words to boost. One line per word.");
DEFINE_double(boosted_words_score, 10., "Score by which to boost the boosted words");
DEFINE_bool(
    verbatim_transcripts, true,
    "True returns text exactly as it was said with no normalization.  False applies text inverse "
    "normalization");
DEFINE_string(ssl_root_cert, "", "Path to SSL root certificates file");
DEFINE_string(ssl_client_key, "", "Path to SSL client certificates key");
DEFINE_string(ssl_client_cert, "", "Path to SSL client certificates file");
DEFINE_bool(
    use_ssl, false,
    "Whether to use SSL credentials or not. If ssl_root_cert is specified, "
    "this is assumed to be true");
DEFINE_bool(speaker_diarization, false, "Flag that controls if speaker diarization is requested");
DEFINE_int32(
    diarization_max_speakers, 3,
    "Max number of speakers to detect when performing speaker diarization");
DEFINE_string(metadata, "", "Comma separated key-value pair(s) of metadata to be sent to server");
DEFINE_int32(start_history, -1, "Value to detect and initiate start of speech utterance");
DEFINE_double(
    start_threshold, -1.,
    "Threshold value to determine at what percentage start of speech is initiated");
DEFINE_int32(stop_history, -1, "Value to detect endpoint and reset decoder");
DEFINE_int32(
    stop_history_eou, -1, "Value to detect endpoint and generate an intermediate final transcript");
DEFINE_double(stop_threshold, -1., "Threshold value to determine when endpoint detected");
DEFINE_double(
    stop_threshold_eou, -1.,
    "Threshold value for likelihood of blanks before detecting end of utterance");
DEFINE_string(
    custom_configuration, "",
    "Custom configurations to be sent to the server as key value pairs <key:value,key:value,...>");
DEFINE_uint64(timeout_ms, 10000, "Timeout for GRPC channel creation");
DEFINE_uint64(max_grpc_message_size, MAX_GRPC_MESSAGE_SIZE, "Max GRPC message size");

class RecognizeClient {
 public:
  RecognizeClient(
      std::shared_ptr<grpc::Channel> channel, const std::string& language_code,
      int32_t max_alternatives, bool profanity_filter, bool word_time_offsets,
      bool automatic_punctuation, bool separate_recognition_per_channel, bool print_transcripts,
      std::string output_filename, std::string model_name, bool ctm, bool verbatim_transcripts,
      const std::string& boosted_phrases_file, float boosted_phrases_score,
      bool speaker_diarization, int32_t diarization_max_speakers, int32_t start_history,
      float start_threshold, int32_t stop_history, int32_t stop_history_eou, float stop_threshold,
      float stop_threshold_eou, std::string custom_configuration)
      : stub_(nr_asr::RivaSpeechRecognition::NewStub(channel)), language_code_(language_code),
        max_alternatives_(max_alternatives), profanity_filter_(profanity_filter),
        word_time_offsets_(word_time_offsets), automatic_punctuation_(automatic_punctuation),
        separate_recognition_per_channel_(separate_recognition_per_channel),
        speaker_diarization_(speaker_diarization),
        diarization_max_speakers_(diarization_max_speakers), print_transcripts_(print_transcripts),
        done_sending_(false), num_requests_(0), num_responses_(0), num_failed_requests_(0),
        total_audio_processed_(0.), model_name_(model_name), output_filename_(output_filename),
        verbatim_transcripts_(verbatim_transcripts), boosted_phrases_score_(boosted_phrases_score),
        start_history_(start_history), start_threshold_(start_threshold),
        stop_history_(stop_history), stop_history_eou_(stop_history_eou),
        stop_threshold_(stop_threshold), stop_threshold_eou_(stop_threshold_eou),
        custom_configuration_(custom_configuration)
  {
    if (!output_filename.empty()) {
      output_file_.open(output_filename);
      if (ctm) {
        write_fn_ = &RecognizeClient::WriteCTM;
      } else {
        write_fn_ = &RecognizeClient::WriteJSON;
      }
    }

    boosted_phrases_ = ReadPhrasesFromFile(boosted_phrases_file);
  }

  ~RecognizeClient()
  {
    if (output_file_.is_open()) {
      output_file_.close();
    }
  }

  uint32_t NumActiveTasks()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return curr_tasks_.size();
  }

  uint32_t NumFailedRequests() { return num_failed_requests_; }

  float TotalAudioProcessed() { return total_audio_processed_; }

  void WriteCTM(const Results& result, const std::string& filename)
  {
    std::string bname(basename(filename.c_str()));
    std::string side = (bname.find("-B-") == std::string::npos) ? "A" : "B";
    if (result.final_transcripts.size() > 0) {
      // we only use the top result for now
      for (size_t w = 0; w < result.final_time_stamps.at(0).size(); ++w) {
        auto& word_info = result.final_time_stamps.at(0).at(w);
        output_file_ << bname << " "
                     << (speaker_diarization_
                             ? std::string("speaker_") + std::to_string(word_info.speaker_tag())
                             : side) /* channel */
                     << " " << (float)word_info.start_time() / 1000. << " "
                     << (float)(word_info.end_time() - word_info.start_time()) / 1000. << " "
                     << word_info.word() << " "
                     << word_info.confidence() /* confidence */ << std::endl;
      }
    }
  }

  void WriteJSON(const Results& result, const std::string& filename)
  {
    if (result.final_transcripts.size() == 0) {
      output_file_ << "{\"audio_filepath\": \"" << filename << "\",";
      output_file_ << "\"text\": \"\"}" << std::endl;
    } else {
      for (size_t a = 0; a < result.final_transcripts.size(); ++a) {
        if (a == 0) {
          output_file_ << "{\"audio_filepath\": \"" << filename << "\",";
          output_file_ << "\"text\": \"" << EscapeTranscript(result.final_transcripts.at(a))
                       << "\"}" << std::endl;
        }
      }
    }
  }

  void PrintStats()
  {
    std::sort(latencies_.begin(), latencies_.end());
    double nresultsf = static_cast<double>(latencies_.size());
    size_t per50i = static_cast<size_t>(std::floor(50. * nresultsf / 100.));
    size_t per90i = static_cast<size_t>(std::floor(90. * nresultsf / 100.));
    size_t per95i = static_cast<size_t>(std::floor(95. * nresultsf / 100.));
    size_t per99i = static_cast<size_t>(std::floor(99. * nresultsf / 100.));

    double median = latencies_[per50i];
    double lat_90 = latencies_[per90i];
    double lat_95 = latencies_[per95i];
    double lat_99 = latencies_[per99i];

    double avg = std::accumulate(latencies_.begin(), latencies_.end(), 0.0) / latencies_.size();

    std::cout << std::setprecision(5);
    std::cout << "Latencies (ms):\n";
    std::cout << "\t\tMedian\t\t90th\t\t95th\t\t99th\t\tAvg\n";
    std::cout << "\t\t" << median << "\t\t" << lat_90 << "\t\t" << lat_95 << "\t\t" << lat_99
              << "\t\t" << avg << std::endl;
  }

  void DoneSending()
  {
    done_sending_ = true;
    return;
  }

  // Assembles the client's payload and sends it to the server.
  void Recognize(std::unique_ptr<Stream> stream)
  {
    // Data we are sending to the server.
    nr_asr::RecognizeRequest request;

    std::shared_ptr<WaveData> wav = stream->wav;

    auto config = request.mutable_config();
    config->set_sample_rate_hertz(wav->sample_rate);
    config->set_encoding(wav->encoding);
    config->set_language_code(language_code_);
    config->set_max_alternatives(max_alternatives_);
    config->set_profanity_filter(profanity_filter_);
    config->set_audio_channel_count(wav->channels);
    config->set_enable_word_time_offsets(word_time_offsets_);
    config->set_enable_automatic_punctuation(automatic_punctuation_);
    config->set_verbatim_transcripts(verbatim_transcripts_);
    config->set_enable_separate_recognition_per_channel(separate_recognition_per_channel_);
    auto custom_config = config->mutable_custom_configuration();
    std::unordered_map<std::string, std::string> custom_configuration_map =
        ReadCustomConfiguration(custom_configuration_);
    for (auto& it : custom_configuration_map) {
      (*custom_config)[it.first] = it.second;
    }

    auto speaker_diarization_config = config->mutable_diarization_config();
    speaker_diarization_config->set_enable_speaker_diarization(speaker_diarization_);
    speaker_diarization_config->set_max_speaker_count(diarization_max_speakers_);

    if (model_name_ != "") {
      config->set_model(model_name_);
    }

    nr_asr::SpeechContext* speech_context = config->add_speech_contexts();
    *(speech_context->mutable_phrases()) = {boosted_phrases_.begin(), boosted_phrases_.end()};
    speech_context->set_boost(boosted_phrases_score_);

    request.set_audio(&wav->data[0], wav->data.size());

    // Set the endpoint parameters
    UpdateEndpointingConfig(config);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      curr_tasks_.emplace(stream->corr_id);
      num_requests_++;
    }

    // Call object to store rpc data
    AsyncClientCall* call = new AsyncClientCall;

    call->stream = std::move(stream);

    // stub_->PrepareAsyncSayHello() creates an RPC object, returning
    // an instance to store in "call" but does not actually start the RPC
    // Because we are using the asynchronous API, we need to hold on to
    // the "call" instance in order to get updates on the ongoing RPC.
    call->response_reader = stub_->PrepareAsyncRecognize(&call->context, request, &cq_);

    call->start_time = std::chrono::steady_clock::now();
    // StartCall initiates the RPC call
    call->response_reader->StartCall();

    // Request that, upon completion of the RPC, "reply" be updated with the
    // server's response; "status" with the indication of whether the operation
    // was successful. Tag the request with the memory address of the call object.
    call->response_reader->Finish(&call->response, &call->status, (void*)call);
  }

  // Set the endpoint parameters
  // Get a mutable reference to the Endpointing config message
  void UpdateEndpointingConfig(nr_asr::RecognitionConfig* config)
  {
    if (!(start_history_ > 0 || start_threshold_ > 0 || stop_history_ > 0 ||
          stop_history_eou_ > 0 || stop_threshold_ > 0 || stop_threshold_eou_ > 0)) {
      return;
    }

    auto* endpointing_config = config->mutable_endpointing_config();

    if (start_history_ > 0) {
      endpointing_config->set_start_history(start_history_);
    }
    if (start_threshold_ > 0) {
      endpointing_config->set_start_threshold(start_threshold_);
    }
    if (stop_history_ > 0) {
      endpointing_config->set_stop_history(stop_history_);
    }
    if (stop_threshold_ > 0) {
      endpointing_config->set_stop_threshold(stop_threshold_);
    }
    if (stop_history_eou_ > 0) {
      endpointing_config->set_stop_history_eou(stop_history_eou_);
    }
    if (stop_threshold_eou_ > 0) {
      endpointing_config->set_stop_threshold_eou(stop_threshold_eou_);
    }
  }
  // Loop while listening for completed responses.
  // Prints out the response from the server.
  void AsyncCompleteRpc()
  {
    void* got_tag;
    bool ok = false;

    // Block until the next result is available in the completion queue "cq".
    bool stop_flag = false;
    while (!stop_flag && cq_.Next(&got_tag, &ok)) {
      // The tag in this example is the memory location of the call object
      AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);

      // Verify that the request was completed successfully. Note that "ok"
      // corresponds solely to the request for updates introduced by Finish().
      GPR_ASSERT(ok);

      if (call->status.ok()) {
        auto end_time = std::chrono::steady_clock::now();
        double lat = std::chrono::duration<double, std::milli>(end_time - call->start_time).count();
        latencies_.push_back(lat);

        Results output_result;
        if (call->response.results_size()) {
          const auto& last_result = call->response.results(call->response.results_size() - 1);
          total_audio_processed_ += last_result.audio_processed();

          for (int r = 0; r < call->response.results_size(); ++r) {
            AppendResult(
                output_result, call->response.results(r), word_time_offsets_, speaker_diarization_);
          }
        }

        if (print_transcripts_) {
          PrintResult(
              output_result, call->stream->wav->filename, word_time_offsets_, speaker_diarization_);
        }
        if (!output_filename_.empty()) {
          (this->*write_fn_)(output_result, call->stream->wav->filename);
        }
      } else {
        std::cout << "RPC failed: " << call->status.error_message() << std::endl;
        // This means that receiving thread will never finish
        num_failed_requests_++;
      }

      // Remove the element from the map
      {
        std::lock_guard<std::mutex> lock(mutex_);
        curr_tasks_.erase(call->stream->corr_id);
        num_responses_++;
      }

      // Once we're complete, deallocate the call object.
      delete call;

      if (num_responses_ == num_requests_ && done_sending_) {
        stop_flag = true;
        std::cout << "Done processing " << num_responses_ << " responses" << std::endl;
      }
    }
  }

 private:
  // struct for keeping state and data information
  struct AsyncClientCall {
    // Container for the data we expect from the server.
    nr_asr::RecognizeResponse response;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    grpc::ClientContext context;

    // Storage for the status of the RPC upon completion.
    grpc::Status status;

    std::unique_ptr<grpc::ClientAsyncResponseReader<nr_asr::RecognizeResponse>> response_reader;

    std::unique_ptr<Stream> stream;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
  };

  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<nr_asr::RivaSpeechRecognition::Stub> stub_;

  // The producer-consumer queue we use to communicate asynchronously with the
  // gRPC runtime.
  grpc::CompletionQueue cq_;

  std::set<uint32_t> curr_tasks_;
  std::vector<double> latencies_;

  std::string language_code_;
  int32_t max_alternatives_;
  bool profanity_filter_;
  int32_t channels_;
  bool word_time_offsets_;
  bool automatic_punctuation_;
  bool separate_recognition_per_channel_;
  bool speaker_diarization_;
  int32_t diarization_max_speakers_;
  bool print_transcripts_;


  std::mutex mutex_;
  bool done_sending_;
  uint32_t num_requests_;
  uint32_t num_responses_;
  uint32_t num_failed_requests_;

  std::ofstream output_file_;

  float total_audio_processed_;

  std::string model_name_;
  std::string output_filename_;
  bool verbatim_transcripts_;

  std::vector<std::string> boosted_phrases_;
  float boosted_phrases_score_;
  void (RecognizeClient::*write_fn_)(const Results& result, const std::string& filename);

  int32_t start_history_;
  float start_threshold_;
  int32_t stop_history_;
  int32_t stop_history_eou_;
  float stop_threshold_;
  float stop_threshold_eou_;
  std::string custom_configuration_;
};

int
main(int argc, char** argv)
{
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  std::stringstream str_usage;
  str_usage << "Usage: riva_asr_client " << std::endl;
  str_usage << "           --audio_file=<filename or folder> " << std::endl;
  str_usage << "           --automatic_punctuation=<true|false>" << std::endl;
  str_usage << "           --max_alternatives=<integer>" << std::endl;
  str_usage << "           --profanity_filter=<true|false>" << std::endl;
  str_usage << "           --word_time_offsets=<true|false>" << std::endl;
  str_usage << "           --riva_uri=<server_name:port> " << std::endl;
  str_usage << "           --num_iterations=<integer> " << std::endl;
  str_usage << "           --num_parallel_requests=<integer> " << std::endl;
  str_usage << "           --print_transcripts=<true|false> " << std::endl;
  str_usage << "           --output_filename=<string>" << std::endl;
  str_usage << "           --output-ctm=<true|false>" << std::endl;
  str_usage << "           --verbatim_transcripts=<true|false>" << std::endl;
  str_usage << "           --language_code=<bcp 47 language code (such as en-US)>" << std::endl;
  str_usage << "           --boosted_words_file=<string>" << std::endl;
  str_usage << "           --boosted_words_score=<float>" << std::endl;
  str_usage << "           --ssl_root_cert=<filename>" << std::endl;
  str_usage << "           --ssl_client_key=<filename>" << std::endl;
  str_usage << "           --ssl_client_cert=<filename>" << std::endl;
  str_usage << "           --speaker_diarization=<true|false>" << std::endl;
  str_usage << "           --diarization_max_speakers=<int>" << std::endl;
  str_usage << "           --model_name=<model>" << std::endl;
  str_usage << "           --list_models" << std::endl;
  str_usage << "           --metadata=<key,value,...>" << std::endl;
  str_usage << "           --start_history=<int>" << std::endl;
  str_usage << "           --start_threshold=<float>" << std::endl;
  str_usage << "           --stop_history=<int>" << std::endl;
  str_usage << "           --stop_history_eou=<int>" << std::endl;
  str_usage << "           --stop_threshold=<float>" << std::endl;
  str_usage << "           --stop_threshold_eou=<float>" << std::endl;
  str_usage << "           --custom_configuration=<key:value,key:value,...>" << std::endl;
  str_usage << "           --timeout_ms=<uint64_t>" << std::endl;
  str_usage << "           --max_grpc_message_size=<uint64_t>" << std::endl;
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

  if (FLAGS_max_alternatives < 1) {
    std::cerr << "max_alternatives must be greater than or equal to 1." << std::endl;
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
    auto creds = riva::clients::CreateChannelCredentials(
        FLAGS_use_ssl, FLAGS_ssl_root_cert, FLAGS_ssl_client_key, FLAGS_ssl_client_cert,
        FLAGS_metadata);
    grpc_channel = riva::clients::CreateChannelBlocking(
        FLAGS_riva_uri, creds, FLAGS_timeout_ms, FLAGS_max_grpc_message_size);
  }
  catch (const std::exception& e) {
    std::cerr << "Error creating GRPC channel: " << e.what() << std::endl;
    std::cerr << "Exiting." << std::endl;
    return 1;
  }

  if (FLAGS_list_models) {
    std::unique_ptr<nr_asr::RivaSpeechRecognition::Stub> asr_stub_(
        nr_asr::RivaSpeechRecognition::NewStub(grpc_channel));
    grpc::ClientContext asr_context;
    nr_asr::RivaSpeechRecognitionConfigRequest asr_request;
    nr_asr::RivaSpeechRecognitionConfigResponse asr_response;
    asr_stub_->GetRivaSpeechRecognitionConfig(&asr_context, asr_request, &asr_response);

    for (int i = 0; i < asr_response.model_config_size(); i++) {
      if (asr_response.model_config(i).parameters().find("type")->second == "offline") {
        std::cout << "'" << asr_response.model_config(i).parameters().find("language_code")->second
                  << "': '" << asr_response.model_config(i).model_name() << "'" << std::endl;
      }
    }

    return 0;
  }

  RecognizeClient recognize_client(
      grpc_channel, FLAGS_language_code, FLAGS_max_alternatives, FLAGS_profanity_filter,
      FLAGS_word_time_offsets, FLAGS_automatic_punctuation,
      /* separate_recognition_per_channel*/ false, FLAGS_print_transcripts, FLAGS_output_filename,
      FLAGS_model_name, FLAGS_output_ctm, FLAGS_verbatim_transcripts, FLAGS_boosted_words_file,
      (float)FLAGS_boosted_words_score, FLAGS_speaker_diarization, FLAGS_diarization_max_speakers,
      FLAGS_start_history, FLAGS_start_threshold, FLAGS_stop_history, FLAGS_stop_history_eou,
      FLAGS_stop_threshold, FLAGS_stop_threshold_eou, FLAGS_custom_configuration);

  // Preload all wav files, sort by size to reduce tail effects
  std::vector<std::shared_ptr<WaveData>> all_wav;
  try {
    LoadWavData(all_wav, FLAGS_audio_file);
  }
  catch (const std::exception& e) {
    std::cerr << "Unable to load audio file(s): " << e.what() << std::endl;
    return 1;
  }
  if (all_wav.size() == 0) {
    std::cout << "No audio files specified. Exiting." << std::endl;
    return 1;
  }

  uint32_t all_wav_max = all_wav.size() * FLAGS_num_iterations;
  std::vector<std::shared_ptr<WaveData>> all_wav_repeated;
  all_wav_repeated.reserve(all_wav_max);
  for (uint32_t file_id = 0; file_id < all_wav.size(); file_id++) {
    for (int iter = 0; iter < FLAGS_num_iterations; iter++) {
      all_wav_repeated.push_back(all_wav[file_id]);
    }
  }

  // Spawn reader thread that loops indefinitely
  std::thread thread_ = std::thread(&RecognizeClient::AsyncCompleteRpc, &recognize_client);

  // Ensure there's also num_parallel_requests in flight
  uint32_t all_wav_i = 0;
  auto start_time = std::chrono::steady_clock::now();
  while (true) {
    while (recognize_client.NumActiveTasks() < (uint32_t)FLAGS_num_parallel_requests &&
           all_wav_i < all_wav_max) {
      std::unique_ptr<Stream> stream(new Stream(all_wav_repeated[all_wav_i], all_wav_i));
      recognize_client.Recognize(std::move(stream));
      ++all_wav_i;
    }

    if (all_wav_i == all_wav_max) {
      break;
    }
  }

  recognize_client.DoneSending();
  thread_.join();

  if (recognize_client.NumFailedRequests()) {
    std::cout << "Some requests failed to complete properly, not printing performance stats"
              << std::endl;
  } else {
    recognize_client.PrintStats();

    auto current_time = std::chrono::steady_clock::now();
    double diff_time = std::chrono::duration<double, std::milli>(current_time - start_time).count();

    std::cout << "Run time: " << diff_time / 1000. << " sec." << std::endl;
    std::cout << "Total audio processed: " << recognize_client.TotalAudioProcessed() << " sec."
              << std::endl;
    std::cout << "Throughput: " << recognize_client.TotalAudioProcessed() * 1000. / diff_time
              << " RTFX" << std::endl;
    if (!FLAGS_output_filename.empty()) {
      std::cout << "Final transcripts written to " << FLAGS_output_filename << std::endl;
    }
  }

  return 0;
}
