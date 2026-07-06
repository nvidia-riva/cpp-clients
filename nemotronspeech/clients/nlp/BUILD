"""
SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: MIT
"""

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "nemotron_nlp_client",
    hdrs = ["nemotron_nlp_client.h"],
    deps = [
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_nlp",
        "@com_github_gflags_gflags//:gflags",
        "@glog//:glog",
        "@com_github_grpc_grpc//:grpc++"
    ]
)

cc_binary(
    name = "nemotron_nlp_punct",
    srcs = ["nemotron_nlp_punct.cc","nemotron_nlp_client.h"],
    deps = [
        ":nemotron_nlp_client",
        "//nemotronspeech/clients/utils:grpc",
        "//nemotronspeech/utils:stamping",
        "//nemotronspeech/utils/files:files"
    ]
)

