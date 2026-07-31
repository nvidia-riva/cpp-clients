/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "transcript_exporter.h"

#include <sstream>
#include <string>

#include "gtest/gtest.h"

namespace {

Results
MakeResults()
{
  Results result;
  Results::Segment segment;
  nr_asr::SpeechRecognitionAlternative alternative;
  alternative.set_transcript("hello \"world\"");
  alternative.set_confidence(0.9F);
  auto* first_word = alternative.add_words();
  first_word->set_word("hello");
  first_word->set_start_time(1000);
  first_word->set_end_time(1200);
  first_word->set_confidence(0.8F);
  auto* second_word = alternative.add_words();
  second_word->set_word("world");
  second_word->set_start_time(1300);
  second_word->set_end_time(1500);
  second_word->set_confidence(0.7F);
  segment.alternatives.push_back(alternative);
  result.segments.push_back(segment);
  result.final_transcripts.push_back(alternative.transcript());
  return result;
}

TEST(TranscriptExporterTest, InfersOutputFormat)
{
  TranscriptOutputFormat format;
  std::string error;
  EXPECT_TRUE(ParseTranscriptOutputFormat("transcript.SRT", "", &format, &error));
  EXPECT_EQ(format, TranscriptOutputFormat::kSrt);
  EXPECT_TRUE(ParseTranscriptOutputFormat("transcript.data", "vtt", &format, &error));
  EXPECT_EQ(format, TranscriptOutputFormat::kVtt);
  EXPECT_FALSE(ParseTranscriptOutputFormat("transcript.txt", "", &format, &error));
}

TEST(TranscriptExporterTest, WritesSrt)
{
  std::ostringstream output;
  size_t cue_index = 1;
  std::string error;

  EXPECT_TRUE(WriteTranscriptOutput(
      output, TranscriptOutputFormat::kSrt, MakeResults(), "audio.wav", &cue_index, &error));
  EXPECT_EQ(
      output.str(),
      "1\n00:00:01,000 --> 00:00:01,500\nhello \"world\"\n\n");
}

TEST(TranscriptExporterTest, WritesWebVtt)
{
  std::ostringstream output;
  InitializeTranscriptOutput(output, TranscriptOutputFormat::kVtt);
  size_t cue_index = 1;
  std::string error;

  EXPECT_TRUE(WriteTranscriptOutput(
      output, TranscriptOutputFormat::kVtt, MakeResults(), "audio.wav", &cue_index, &error));
  EXPECT_EQ(
      output.str(),
      "WEBVTT\n\n1\n00:00:01.000 --> 00:00:01.500\nhello \"world\"\n\n");
}

TEST(TranscriptExporterTest, WritesStructuredJson)
{
  std::ostringstream output;
  size_t cue_index = 1;
  std::string error;

  EXPECT_TRUE(WriteTranscriptOutput(
      output, TranscriptOutputFormat::kJson, MakeResults(), "audio.wav", &cue_index, &error));
  EXPECT_NE(output.str().find("\"schema_version\":\"1.0\""), std::string::npos);
  EXPECT_NE(output.str().find("\"text\":\"hello \\\"world\\\"\""), std::string::npos);
  EXPECT_NE(output.str().find("\"transcript\":\"hello \\\"world\\\"\""), std::string::npos);
  EXPECT_NE(output.str().find("\"start_time_ms\":1000"), std::string::npos);
}

TEST(TranscriptExporterTest, RejectsSubtitlesWithoutWordTimes)
{
  Results result;
  Results::Segment segment;
  nr_asr::SpeechRecognitionAlternative alternative;
  alternative.set_transcript("hello");
  segment.alternatives.push_back(alternative);
  result.segments.push_back(segment);
  std::ostringstream output;
  size_t cue_index = 1;
  std::string error;

  EXPECT_FALSE(WriteTranscriptOutput(
      output, TranscriptOutputFormat::kSrt, result, "audio.wav", &cue_index, &error));
  EXPECT_NE(error.find("require word time offsets"), std::string::npos);
}

TEST(TranscriptExporterTest, IgnoresJsonSegmentsWithoutAlternatives)
{
  Results result;
  result.segments.emplace_back();
  std::ostringstream output;
  size_t cue_index = 1;
  std::string error;

  EXPECT_TRUE(WriteTranscriptOutput(
      output, TranscriptOutputFormat::kJson, result, "audio.wav", &cue_index, &error));
  EXPECT_NE(output.str().find("\"segments\":[]"), std::string::npos);
}

}  // namespace
