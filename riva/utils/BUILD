"""
SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: MIT
"""

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "thread_pool",
    hdrs = ["thread_pool.h"],
)

cc_library(
    name = "stamping",
    hdrs = ["stamping.h"],
    linkstamp = "stamping.cc"
)

cc_library(
    name = "string_processing",
    srcs = [],
    hdrs = ["string_processing.h"],
    includes = ["."],
    linkopts = ["-lm"],
    deps = [
        "@com_google_absl//absl/strings",
    ]
)