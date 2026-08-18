/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "transcript_exporter.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace {

std::string
Lowercase(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string
Extension(const std::string& filename)
{
  const size_t dot = filename.find_last_of('.');
  if (dot == std::string::npos || dot + 1 == filename.size()) {
    return "";
  }
  return Lowercase(filename.substr(dot + 1));
}

void
WriteJsonString(std::ostream& output, const std::string& value)
{
  output << '"';
  for (unsigned char character : value) {
    switch (character) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (character < 0x20) {
          output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                 << static_cast<int>(character) << std::dec << std::setfill(' ');
        } else {
          output << character;
        }
    }
  }
  output << '"';
}

void
WriteWord(std::ostream& output, const nr_asr::WordInfo& word)
{
  output << "{\"word\":";
  WriteJsonString(output, word.word());
  output << ",\"start_time_ms\":" << word.start_time()
         << ",\"end_time_ms\":" << word.end_time() << ",\"confidence\":"
         << word.confidence() << ",\"speaker_tag\":" << word.speaker_tag()
         << ",\"language_code\":";
  WriteJsonString(output, word.language_code());
  output << '}';
}

void
WriteAlternative(std::ostream& output, const nr_asr::SpeechRecognitionAlternative& alternative)
{
  output << "{\"transcript\":";
  WriteJsonString(output, alternative.transcript());
  output << ",\"confidence\":" << alternative.confidence() << ",\"words\":[";
  for (int word_index = 0; word_index < alternative.words_size(); ++word_index) {
    if (word_index > 0) {
      output << ',';
    }
    WriteWord(output, alternative.words(word_index));
  }
  output << "],\"language_codes\":[";
  for (int language_index = 0; language_index < alternative.language_code_size();
       ++language_index) {
    if (language_index > 0) {
      output << ',';
    }
    WriteJsonString(output, alternative.language_code(language_index));
  }
  output << "]}";
}

std::string
TranscriptText(const Results& result)
{
  std::string transcript;
  for (const auto& segment : result.segments) {
    if (segment.alternatives.empty() || segment.alternatives.front().transcript().empty()) {
      continue;
    }
    if (!transcript.empty()) {
      transcript += ' ';
    }
    transcript += segment.alternatives.front().transcript();
  }
  if (transcript.empty() && !result.final_transcripts.empty()) {
    transcript = result.final_transcripts.front();
  }
  return transcript;
}

void
WriteJson(std::ostream& output, const Results& result, const std::string& audio_filename)
{
  output << "{\"schema_version\":\"1.0\",\"audio_filepath\":";
  WriteJsonString(output, audio_filename);
  const std::string transcript = TranscriptText(result);
  output << ",\"text\":";
  WriteJsonString(output, transcript);
  output << ",\"transcript\":";
  WriteJsonString(output, transcript);
  output << ",\"segments\":[";
  bool first_segment = true;
  for (const auto& segment : result.segments) {
    if (segment.alternatives.empty()) {
      continue;
    }
    if (!first_segment) {
      output << ',';
    }
    first_segment = false;
    const auto& top_alternative = segment.alternatives.front();
    output << "{\"transcript\":";
    WriteJsonString(output, top_alternative.transcript());
    if (top_alternative.words_size() > 0) {
      output << ",\"start_time_ms\":" << top_alternative.words(0).start_time()
             << ",\"end_time_ms\":"
             << top_alternative.words(top_alternative.words_size() - 1).end_time();
    } else {
      output << ",\"start_time_ms\":null,\"end_time_ms\":null";
    }
    output << ",\"confidence\":" << top_alternative.confidence() << ",\"words\":[";
    for (int word_index = 0; word_index < top_alternative.words_size(); ++word_index) {
      if (word_index > 0) {
        output << ',';
      }
      WriteWord(output, top_alternative.words(word_index));
    }
    output << "],\"alternatives\":[";
    for (size_t alternative_index = 0; alternative_index < segment.alternatives.size();
         ++alternative_index) {
      if (alternative_index > 0) {
        output << ',';
      }
      WriteAlternative(output, segment.alternatives[alternative_index]);
    }
    output << "]}";
  }
  output << "]}" << std::endl;
}

std::string
CollapseWhitespace(const std::string& text)
{
  std::string collapsed;
  bool pending_space = false;
  for (unsigned char character : text) {
    if (std::isspace(character)) {
      pending_space = !collapsed.empty();
    } else {
      if (pending_space) {
        collapsed += ' ';
      }
      collapsed += static_cast<char>(character);
      pending_space = false;
    }
  }
  return collapsed;
}

std::string
FormatTimestamp(int64_t timestamp_ms, bool webvtt)
{
  const int64_t hours = timestamp_ms / 3600000;
  timestamp_ms %= 3600000;
  const int64_t minutes = timestamp_ms / 60000;
  timestamp_ms %= 60000;
  const int64_t seconds = timestamp_ms / 1000;
  const int64_t milliseconds = timestamp_ms % 1000;
  std::ostringstream formatted;
  formatted << std::setfill('0') << std::setw(2) << hours << ':' << std::setw(2) << minutes << ':'
            << std::setw(2) << seconds << (webvtt ? '.' : ',') << std::setw(3) << milliseconds;
  return formatted.str();
}

bool
ValidateSubtitles(const Results& result, std::string* error)
{
  for (const auto& segment : result.segments) {
    if (segment.alternatives.empty() || segment.alternatives.front().transcript().empty()) {
      continue;
    }
    const auto& alternative = segment.alternatives.front();
    if (alternative.words_size() == 0) {
      *error = "SRT and VTT export require word time offsets, but a finalized transcript segment "
               "did not contain them.";
      return false;
    }
    const int64_t start_time = alternative.words(0).start_time();
    const int64_t end_time = alternative.words(alternative.words_size() - 1).end_time();
    if (start_time < 0 || end_time < start_time) {
      *error = "SRT and VTT export require non-negative, ordered timestamps.";
      return false;
    }
  }
  return true;
}

void
WriteSubtitles(std::ostream& output, const Results& result, bool webvtt, size_t* cue_index)
{
  for (const auto& segment : result.segments) {
    if (segment.alternatives.empty()) {
      continue;
    }
    const auto& alternative = segment.alternatives.front();
    const std::string text = CollapseWhitespace(alternative.transcript());
    if (text.empty()) {
      continue;
    }
    const int64_t start_time = alternative.words(0).start_time();
    const int64_t end_time =
        std::max<int64_t>(alternative.words(alternative.words_size() - 1).end_time(), start_time + 1);
    output << (*cue_index)++ << '\n'
           << FormatTimestamp(start_time, webvtt) << " --> " << FormatTimestamp(end_time, webvtt)
           << '\n'
           << text << "\n\n";
  }
}

}  // namespace

bool
ParseTranscriptOutputFormat(
    const std::string& output_filename, const std::string& requested_format,
    TranscriptOutputFormat* output_format, std::string* error)
{
  const std::string format =
      requested_format.empty() ? Extension(output_filename) : Lowercase(requested_format);
  if (format == "json") {
    *output_format = TranscriptOutputFormat::kJson;
  } else if (format == "srt") {
    *output_format = TranscriptOutputFormat::kSrt;
  } else if (format == "vtt") {
    *output_format = TranscriptOutputFormat::kVtt;
  } else {
    *error = "Unable to determine transcript output format. Use a .json, .srt, or .vtt file "
             "extension, or pass --output_format.";
    return false;
  }
  return true;
}

bool
TranscriptOutputRequiresWordTimeOffsets(TranscriptOutputFormat output_format)
{
  return output_format == TranscriptOutputFormat::kSrt || output_format == TranscriptOutputFormat::kVtt;
}

void
InitializeTranscriptOutput(std::ostream& output, TranscriptOutputFormat output_format)
{
  if (output_format == TranscriptOutputFormat::kVtt) {
    output << "WEBVTT\n\n";
  }
}

bool
WriteTranscriptOutput(
    std::ostream& output, TranscriptOutputFormat output_format, const Results& result,
    const std::string& audio_filename, size_t* cue_index, std::string* error)
{
  if (!output.good()) {
    *error = "Transcript output file is not writable.";
    return false;
  }
  if (output_format == TranscriptOutputFormat::kJson) {
    WriteJson(output, result, audio_filename);
  } else {
    if (!ValidateSubtitles(result, error)) {
      return false;
    }
    WriteSubtitles(output, result, output_format == TranscriptOutputFormat::kVtt, cue_index);
  }
  if (!output.good()) {
    *error = "Failed while writing transcript output.";
    return false;
  }
  return true;
}
