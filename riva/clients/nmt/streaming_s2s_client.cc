/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "streaming_s2s_client.h"

#include "riva/utils/opus/opus_client_decoder.h"

#define clear_screen() printf("\033[H\033[J")
#define gotoxy(x, y) printf("\033[%d;%dH", (y), (x))

static void
MicrophoneThreadMain(
    std::shared_ptr<S2SClientCall> call, snd_pcm_t* alsa_handle, int samplerate, int numchannels,
    nr::AudioEncoding& encoding, int32_t chunk_duration_ms, bool& request_exit)
{
  nr_nmt::StreamingTranslateSpeechToSpeechRequest request;
  int total_samples = 0;

  // Read 0.1s of audio
  const size_t chunk_size = (samplerate * chunk_duration_ms / 1000) * sizeof(int16_t);
  std::vector<char> chunk(chunk_size);

  while (true) {
    // Read another chunk from the mic stream
    std::streamsize bytes_read = 0;
    size_t bytes_to_read = chunk_size;
    if (alsa_handle) {
      int rc = snd_pcm_readi(alsa_handle, &chunk[0], chunk_size / sizeof(int16_t));
      bytes_read = rc * sizeof(int16_t);  // convert frames to bytes
      if (rc < 0) {
        std::cerr << "read failed : " << snd_strerror(rc) << std::endl;
        bytes_read = 0;  // reset counter
      }
    }

    // And write the chunk to the stream.
    request.set_audio_content(&chunk[0], bytes_read);

    total_samples += (bytes_read / sizeof(int16_t));

    call->send_times.push_back(std::chrono::steady_clock::now());
    call->streamer->Write(request);
    if ((bytes_read < (std::streamsize)bytes_to_read) || request_exit) {
      // Done reading everything from the file, so done writing to the stream.
      call->streamer->WritesDone();
      break;
    }
  }
}

StreamingS2SClient::StreamingS2SClient(
    std::shared_ptr<grpc::Channel> channel, int32_t num_parallel_requests,
    const std::string& source_language_code, const std::string& target_language_code,
    const std::string& dnt_phrases_file, bool profanity_filter, bool automatic_punctuation,
    bool separate_recognition_per_channel, int32_t chunk_duration_ms, bool simulate_realtime,
    bool verbatim_transcripts, const std::string& boosted_phrases_file, float boosted_phrases_score,
    const std::string& tts_encoding, const std::string& tts_audio_file, int tts_sample_rate,
    const std::string& tts_voice_name, std::string& tts_prosody_rate,
    std::string& tts_prosody_pitch, std::string& tts_prosody_volume)
    : stub_(nr_nmt::RivaTranslation::NewStub(channel)), source_language_code_(source_language_code),
      target_language_code_(target_language_code), profanity_filter_(profanity_filter),
      automatic_punctuation_(automatic_punctuation),
      separate_recognition_per_channel_(separate_recognition_per_channel),
      chunk_duration_ms_(chunk_duration_ms), total_audio_processed_(0.), num_streams_started_(0),
      simulate_realtime_(simulate_realtime), verbatim_transcripts_(verbatim_transcripts),
      boosted_phrases_score_(boosted_phrases_score), tts_encoding_(tts_encoding),
      tts_audio_file_(tts_audio_file), tts_voice_name_(tts_voice_name),
      tts_sample_rate_(tts_sample_rate), tts_prosody_rate_(tts_prosody_rate),
      tts_prosody_pitch_(tts_prosody_pitch), tts_prosody_volume_(tts_prosody_volume)
{
  num_active_streams_.store(0);
  num_streams_finished_.store(0);
  thread_pool_.reset(new ThreadPool(4 * num_parallel_requests));

  boosted_phrases_ = ReadPhrasesFromFile(boosted_phrases_file);
  dnt_phrases_ = ReadPhrasesFromFile(dnt_phrases_file);
}

StreamingS2SClient::~StreamingS2SClient() {}

void
StreamingS2SClient::StartNewStream(std::unique_ptr<Stream> stream)
{
  std::cout << "starting a new stream!" << std::endl;
  std::shared_ptr<S2SClientCall> call = std::make_shared<S2SClientCall>(stream->corr_id, false);
  call->streamer = stub_->StreamingTranslateSpeechToSpeech(&call->context);
  call->stream = std::move(stream);

  num_active_streams_++;
  num_streams_started_++;

  auto gen_func = std::bind(&StreamingS2SClient::GenerateRequests, this, call);
  auto recv_func =
      std::bind(&StreamingS2SClient::ReceiveResponses, this, call, false /*audio_device*/);

  thread_pool_->Enqueue(gen_func);
  thread_pool_->Enqueue(recv_func);
}

void
StreamingS2SClient::GenerateRequests(std::shared_ptr<S2SClientCall> call)
{
  float audio_processed = 0.;

  bool first_write = true;
  bool done = false;
  auto start_time = std::chrono::steady_clock::now();
  while (!done) {
    nr_nmt::StreamingTranslateSpeechToSpeechRequest request;
    if (first_write) {
      auto streaming_s2s_config = request.mutable_config();

      // set nmt config
      auto translation_config = streaming_s2s_config->mutable_translation_config();
      translation_config->set_source_language_code(source_language_code_);
      translation_config->set_target_language_code(target_language_code_);
      *(translation_config->mutable_dnt_phrases()) = {dnt_phrases_.begin(), dnt_phrases_.end()};

      // set tts config
      auto tts_config = streaming_s2s_config->mutable_tts_config();
      if (tts_encoding_.empty() || tts_encoding_ == "pcm") {
        tts_config->set_encoding(nr::LINEAR_PCM);
      } else if (tts_encoding_ == "opus") {
        tts_config->set_encoding(nr::OGGOPUS);
      }
      int32_t rate = tts_sample_rate_;
      if (tts_encoding_ == "opus") {
        rate = riva::utils::opus::Decoder::AdjustRateIfUnsupported(tts_sample_rate_);
      }
      tts_config->set_sample_rate_hz(rate);
      tts_config->set_voice_name(tts_voice_name_);
      tts_config->set_language_code(target_language_code_);
      tts_config->set_prosody_rate(tts_prosody_rate_);
      tts_config->set_prosody_pitch(tts_prosody_pitch_);
      tts_config->set_prosody_volume(tts_prosody_volume_);

      // set asr config
      auto streaming_asr_config = streaming_s2s_config->mutable_asr_config();
      streaming_asr_config->set_interim_results(false);
      auto config = streaming_asr_config->mutable_config();
      config->set_sample_rate_hertz(call->stream->wav->sample_rate);
      config->set_language_code(source_language_code_);
      config->set_encoding(call->stream->wav->encoding);
      config->set_max_alternatives(1);
      config->set_profanity_filter(profanity_filter_);
      config->set_audio_channel_count(call->stream->wav->channels);
      config->set_enable_word_time_offsets(false);
      config->set_enable_automatic_punctuation(automatic_punctuation_);
      config->set_enable_separate_recognition_per_channel(separate_recognition_per_channel_);
      auto custom_config = config->mutable_custom_configuration();
      (*custom_config)["test_key"] = "test_value";
      config->set_verbatim_transcripts(verbatim_transcripts_);

      nr_asr::SpeechContext* speech_context = config->add_speech_contexts();
      *(speech_context->mutable_phrases()) = {boosted_phrases_.begin(), boosted_phrases_.end()};
      speech_context->set_boost(boosted_phrases_score_);
      call->streamer->Write(request);
      first_write = false;
    }

    size_t chunk_size =
        (call->stream->wav->sample_rate * chunk_duration_ms_ / 1000) * sizeof(int16_t);
    size_t& offset = call->stream->offset;
    long header_size = (offset == 0U) ? call->stream->wav->data_offset : 0L;
    size_t bytes_to_send =
        std::min(call->stream->wav->data.size() - offset, chunk_size + header_size);
    double current_wait_time =
        1000. * (bytes_to_send - header_size) / (sizeof(int16_t) * call->stream->wav->sample_rate);
    audio_processed += current_wait_time / 1000.F;
    request.set_audio_content(&call->stream->wav->data[offset], bytes_to_send);
    offset += bytes_to_send;

    if (simulate_realtime_) {
      auto current_time = std::chrono::steady_clock::now();
      double diff_time =
          std::chrono::duration<double, std::milli>(current_time - start_time).count();
      double wait_time =
          current_wait_time - (diff_time - call->send_times.size() * chunk_duration_ms_);

      // To nanoseconds
      wait_time *= 1.e3;
      wait_time = std::max(wait_time, 0.);
      // Round to nearest integer
      wait_time = wait_time + 0.5 - (wait_time < 0);
      int64_t usecs = (int64_t)wait_time;
      // Sleep
      if (usecs > 0) {
        usleep(usecs);
      }
    }
    call->send_times.push_back(std::chrono::steady_clock::now());
    call->streamer->Write(request);

    // Set write done to true so next call will lead to WritesDone
    if (offset == call->stream->wav->data.size()) {
      done = true;
      call->streamer->WritesDone();
    }
  }

  {
    std::lock_guard<std::mutex> lock(latencies_mutex_);
    total_audio_processed_ += audio_processed;
  }
}

int
StreamingS2SClient::DoStreamingFromFile(
    std::string& audio_file, int32_t num_iterations, int32_t num_parallel_requests)
{
  // Preload all wav files, sort by size to reduce tail effects
  std::vector<std::shared_ptr<WaveData>> all_wav;
  try {
    LoadWavData(all_wav, audio_file);
  }
  catch (const std::exception& e) {
    std::cerr << "Unable to load audio file(s): " << e.what() << std::endl;
    return 1;
  }
  if (all_wav.size() == 0) {
    std::cout << "No audio files specified. Exiting." << std::endl;
    return 1;
  }

  uint32_t all_wav_max = all_wav.size() * num_iterations;
  std::vector<std::shared_ptr<WaveData>> all_wav_repeated;
  all_wav_repeated.reserve(all_wav_max);
  for (uint32_t file_id = 0; file_id < all_wav.size(); file_id++) {
    for (int iter = 0; iter < num_iterations; iter++) {
      all_wav_repeated.push_back(all_wav[file_id]);
    }
  }

  // Ensure there's also num_parallel_requests in flight
  uint32_t all_wav_i = 0;
  auto start_time = std::chrono::steady_clock::now();
  while (true) {
    while (NumActiveStreams() < (uint32_t)num_parallel_requests && all_wav_i < all_wav_max) {
      std::unique_ptr<Stream> stream(new Stream(all_wav_repeated[all_wav_i], all_wav_i));
      StartNewStream(std::move(stream));
      ++all_wav_i;
    }

    // Break if no more tasks to add
    if (NumStreamsFinished() == all_wav_max) {
      break;
    }
  }


  auto current_time = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(latencies_mutex_);

    PrintStats();
    std::cout << std::flush;
    double diff_time = std::chrono::duration<double, std::milli>(current_time - start_time).count();

    std::cout << "Run time: " << diff_time / 1000. << " sec." << std::endl;
    std::cout << "Total audio processed: " << TotalAudioProcessed() << " sec." << std::endl;
    std::cout << "Throughput: " << TotalAudioProcessed() * 1000. / diff_time << " RTFX"
              << std::endl;
  }

  return 0;
}

void
StreamingS2SClient::PostProcessResults(std::shared_ptr<S2SClientCall> call, bool audio_device)
{
  std::lock_guard<std::mutex> lock(latencies_mutex_);
  // the latency for the s2s would be for an individual file as the difference between the last
  // chunk sent to the first chunk of audio received.
  if (simulate_realtime_) {
    double lat =
        std::chrono::duration<double, std::milli>(call->recv_times[0] - call->send_times.back())
            .count();
    VLOG(1) << "Latency:" << lat << std::endl;
    latencies_.push_back(lat);
  }
}

void
StreamingS2SClient::ReceiveResponses(std::shared_ptr<S2SClientCall> call, bool audio_device)
{
  if (audio_device) {
    clear_screen();
    std::cout << "ASR started... press `Ctrl-C' to stop recording\n\n";
    gotoxy(0, 5);
  }

  std::vector<int16_t> pcm_buffer;
  std::vector<unsigned char> opus_buffer;
  while (call->streamer->Read(&call->response)) {  // Returns false when no more to read.
    if (!call->response.speech().audio().length()) {
      // If the audio size is zero continue the loop for next sentence.
      VLOG(1) << "Got 0 bytes back from server.Sentence Completed.";
      continue;
    }
    call->recv_times.push_back(std::chrono::steady_clock::now());
    auto audio = call->response.speech().audio();
    if (audio_device) {
      clear_screen();
      std::cout << "ASR started... press `Ctrl-C' to stop recording\n\n";
      gotoxy(0, 5);
    }

    std::cout << "Got " << audio.length() << " bytes back from server" << std::endl;
    if (tts_encoding_.empty() || tts_encoding_ == "pcm") {
      int16_t* pcm_data = (int16_t*)audio.data();
      size_t len = audio.length() / sizeof(int16_t);
      std::copy(pcm_data, pcm_data + len, std::back_inserter(pcm_buffer));
    } else if (tts_encoding_ == "opus") {
      const unsigned char* opus_data = (unsigned char*)audio.data();
      size_t len = audio.length();
      std::copy(opus_data, opus_data + len, std::back_inserter(opus_buffer));
    }
  }

  // Write to WAV file
  if (tts_encoding_.empty() || tts_encoding_ == "pcm") {
    ::riva::utils::wav::Write(
        tts_audio_file_, tts_sample_rate_, pcm_buffer.data(), pcm_buffer.size());
    pcm_buffer.clear();
  } else if (tts_encoding_ == "opus") {
    int32_t rate = riva::utils::opus::Decoder::AdjustRateIfUnsupported(tts_sample_rate_);
    riva::utils::opus::Decoder decoder(rate, 1);
    auto pcm = decoder.DecodePcm(decoder.DeserializeOpus(opus_buffer));
    ::riva::utils::wav::Write(tts_audio_file_, rate, pcm.data(), pcm.size());
    opus_buffer.clear();
  }

  grpc::Status status = call->streamer->Finish();
  if (!status.ok()) {
    // Report the RPC failure.
    std::cerr << status.error_message() << std::endl;
  } else {
    PostProcessResults(call, audio_device);
  }
  // A stream would be marked as complete when both ASR and TTS are complete
  num_active_streams_--;
  num_streams_finished_++;
}

int
StreamingS2SClient::DoStreamingFromMicrophone(const std::string& audio_device, bool& request_exit)
{
  nr::AudioEncoding encoding = nr::LINEAR_PCM;
  int samplerate = 16000;
  int channels = 1;
  snd_pcm_t* alsa_handle = nullptr;

  bool ret = OpenAudioDevice(
      audio_device.c_str(), &alsa_handle, SND_PCM_STREAM_CAPTURE, channels, samplerate,
      100000 /* latency in us */);

  if (ret == false) {
    std::cerr << "Error opening capture device " << audio_device << std::endl;
    return 1;
  }
  std::cout << "Using device:" << audio_device << std::endl;

  std::shared_ptr<S2SClientCall> call = std::make_shared<S2SClientCall>(1, false);

  call->streamer = stub_->StreamingTranslateSpeechToSpeech(&call->context);

  // Send first request
  nr_nmt::StreamingTranslateSpeechToSpeechRequest request;
  auto s2s_config = request.mutable_config();

  // set nmt config
  auto translation_config = s2s_config->mutable_translation_config();
  translation_config->set_source_language_code(source_language_code_);
  translation_config->set_target_language_code(target_language_code_);

  // set tts config
  auto tts_config = s2s_config->mutable_tts_config();
  if (tts_encoding_.empty() || tts_encoding_ == "pcm") {
    tts_config->set_encoding(nr::LINEAR_PCM);
  } else if (tts_encoding_ == "opus") {
    tts_config->set_encoding(nr::OGGOPUS);
  }
  int32_t rate = tts_sample_rate_;
  if (tts_encoding_ == "opus") {
    rate = riva::utils::opus::Decoder::AdjustRateIfUnsupported(tts_sample_rate_);
  }
  tts_config->set_sample_rate_hz(rate);
  tts_config->set_voice_name(tts_voice_name_);
  tts_config->set_language_code(target_language_code_);
  tts_config->set_prosody_rate(tts_prosody_rate_);
  tts_config->set_prosody_pitch(tts_prosody_pitch_);
  tts_config->set_prosody_volume(tts_prosody_volume_);


  auto streaming_config = s2s_config->mutable_asr_config();
  streaming_config->set_interim_results(false);
  auto config = streaming_config->mutable_config();
  config->set_sample_rate_hertz(samplerate);
  config->set_language_code(source_language_code_);
  config->set_encoding(encoding);
  config->set_max_alternatives(1);
  config->set_profanity_filter(profanity_filter_);
  config->set_audio_channel_count(channels);
  config->set_enable_word_time_offsets(false);
  config->set_enable_automatic_punctuation(automatic_punctuation_);
  config->set_enable_separate_recognition_per_channel(separate_recognition_per_channel_);
  config->set_verbatim_transcripts(verbatim_transcripts_);
  call->streamer->Write(request);

  std::thread microphone_thread(
      &MicrophoneThreadMain, call, alsa_handle, samplerate, channels, std::ref(encoding),
      chunk_duration_ms_, std::ref(request_exit));

  ReceiveResponses(call, true /*audio_device*/);
  microphone_thread.join();

  CloseAudioDevice(&alsa_handle);

  std::cout << "\nExiting with 0" << std::flush << std::endl;
  return 0;
}

void
StreamingS2SClient::PrintLatencies(std::vector<double>& latencies, const std::string& name)
{
  if (latencies.size() > 0) {
    std::sort(latencies.begin(), latencies.end());
    double nresultsf = static_cast<double>(latencies.size());
    size_t per50i = static_cast<size_t>(std::floor(50. * nresultsf / 100.));
    size_t per90i = static_cast<size_t>(std::floor(90. * nresultsf / 100.));
    size_t per95i = static_cast<size_t>(std::floor(95. * nresultsf / 100.));
    size_t per99i = static_cast<size_t>(std::floor(99. * nresultsf / 100.));

    double median = latencies[per50i];
    double lat_90 = latencies[per90i];
    double lat_95 = latencies[per95i];
    double lat_99 = latencies[per99i];

    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

    std::cout << std::setprecision(5);
    std::cout << name << " (ms):\n";
    std::cout << "\t\tMedian\t\t90th\t\t95th\t\t99th\t\tAvg\n";
    std::cout << "\t\t" << median << "\t\t" << lat_90 << "\t\t" << lat_95 << "\t\t" << lat_99
              << "\t\t" << avg << std::endl;
  }
}

int
StreamingS2SClient::PrintStats()
{
  if (simulate_realtime_) {
    PrintLatencies(latencies_, "Latencies");
    return 0;
  } else {
    std::cout << "To get latency statistics, run with --simulate_realtime "
                 "and set the --chunk_duration_ms to be the same as the server chunk duration"
              << std::endl;
    return 1;
  }
}
