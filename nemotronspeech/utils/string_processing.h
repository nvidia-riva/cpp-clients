/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

static inline std::vector<std::string>
split(std::string& input_string, const char delim = ' ')
{
  std::stringstream ss(input_string);
  std::string s;
  std::vector<std::string> splat;
  while (getline(ss, s, delim)) {
    splat.push_back(s);
  }

  return splat;
}