/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */


#pragma once

#include <iostream>
#include <sstream>

#include "wav_data.h"

// Header length
static inline constexpr std::size_t OPUS_HEADER_LENGTH = 8192U;

void LoadWavData(std::vector<std::shared_ptr<WaveData>>& all_wav, std::string& path);
int ParseWavHeader(std::istream& wavfile, WAVHeader& header, bool read_header);
std::string AudioToString(nr::AudioEncoding& encoding);
