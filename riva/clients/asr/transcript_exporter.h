/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <ostream>
#include <string>

#include "riva_asr_client_helper.h"

enum class TranscriptOutputFormat { kJson, kSrt, kVtt };

bool ParseTranscriptOutputFormat(
    const std::string& output_filename, const std::string& requested_format,
    TranscriptOutputFormat* output_format, std::string* error);

bool TranscriptOutputRequiresWordTimeOffsets(TranscriptOutputFormat output_format);

void InitializeTranscriptOutput(std::ostream& output, TranscriptOutputFormat output_format);

bool WriteTranscriptOutput(
    std::ostream& output, TranscriptOutputFormat output_format, const Results& result,
    const std::string& audio_filename, size_t* cue_index, std::string* error);
