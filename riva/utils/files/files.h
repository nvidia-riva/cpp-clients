/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#pragma once

#include <strings.h>
#include <unistd.h>

#include <fstream>

namespace riva::utils::files {

/// Utility function to read a file content
///
/// Retuns a string with file content
/// Throws an error if file cannot be opened

/// @param[in]: filename The file path

std::string ReadFileContentAsString(const std::string filename);

}  // namespace riva::utils::files
