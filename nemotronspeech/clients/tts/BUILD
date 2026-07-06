"""
SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: MIT
"""

package(
    default_visibility = ["//visibility:public"],
)

cc_binary(
    name = "nemotron_tts_client",
    srcs = ["nemotron_tts_client.cc"],
    deps = [
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_tts",
        "//nemotronspeech/utils:stamping",
        "//nemotronspeech/utils/wav:writer",
        "//nemotronspeech/utils/wav:reader",
        "//nemotronspeech/utils/opus",
        "@glog//:glog",
        "@com_google_absl//absl/time",
        "@com_github_gflags_gflags//:gflags",
        "@com_github_grpc_grpc//:grpc++",
        "//nemotronspeech/clients/utils:grpc",
    ]
)

cc_binary(
    name = "nemotron_tts_perf_client",
    srcs = ["nemotron_tts_perf_client.cc"],
    deps = [
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_tts",
        "//nemotronspeech/utils:stamping",
        "//nemotronspeech/utils/wav:writer",
        "//nemotronspeech/utils/wav:reader",
        "//nemotronspeech/utils/opus",
        "@glog//:glog",
        "@com_github_gflags_gflags//:gflags",
        "@com_github_grpc_grpc//:grpc++",
        "//nemotronspeech/clients/utils:grpc",
    ]
)
