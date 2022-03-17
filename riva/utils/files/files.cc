/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "files.h"

#include <fstream>
#include <sstream>
#include <string>

namespace riva::utils::files {

std::string
ReadFileContentAsString(const std::string filename)
{
  if (access(filename.c_str(), F_OK) == -1) {
    std::string err = "File " + filename + " does not exist";
    throw std::runtime_error(err);
  } else {
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }
}

}  // namespace riva::utils::files
