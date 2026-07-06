#
# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
#

package(
    default_visibility = ["//visibility:public"],
)
cc_binary(
    name = "nemotron_nmt_t2t_client",
    srcs = ["nemotron_nmt_t2t_client.cc"],
    deps = [
        "//nemotronspeech/clients/utils:grpc",
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_nmt",
        "@com_github_gflags_gflags//:gflags",
        "@glog//:glog",
        "@com_github_grpc_grpc//:grpc++"
    ]

)

cc_library(
    name = "client_call",
    srcs = ["client_call.h"],
    deps = select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "//nemotronspeech/clients/asr:asr_client_helper",
        "@com_github_grpc_grpc//:grpc++",
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_nmt",
        "//nemotronspeech/utils/wav:reader",
    ],
)

cc_library(
    name = "streaming_s2s_client",
    srcs = ["streaming_s2s_client.h",
            "streaming_s2s_client.cc"
    ],
    deps = [
        "//nemotronspeech/clients/asr:asr_client_helper",
        ":client_call",
        "//nemotronspeech/utils/wav:reader",
        "//nemotronspeech/utils/wav:writer",
        "//nemotronspeech/utils/opus",
        "@glog//:glog",
    ] + select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "//nemotronspeech/utils:stamping",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_gflags_gflags//:gflags",
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_nmt",
        "//nemotronspeech/utils:thread_pool",
    ],
)
cc_library(
    name = "streaming_s2t_client",
    srcs = ["streaming_s2t_client.h",
            "streaming_s2t_client.cc"
    ],
    deps = [
        "//nemotronspeech/clients/asr:asr_client_helper",
        ":client_call",
        "//nemotronspeech/utils/wav:reader",
        "@glog//:glog",
    ] + select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "//nemotronspeech/utils:stamping",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_gflags_gflags//:gflags",
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_nmt",
        "//nemotronspeech/utils:thread_pool",
    ],
)

cc_binary(
    name = "nemotron_nmt_streaming_s2t_client",
    srcs = ["nemotron_nmt_streaming_s2t_client.cc"],
    deps = [
        "//nemotronspeech/clients/asr:asr_client_helper",
        ":client_call",
        ":streaming_s2t_client",
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_nmt",
        "//nemotronspeech/utils/files:files",
        "//nemotronspeech/utils/wav:reader",
        "@glog//:glog",
        "//nemotronspeech/clients/utils:grpc",
    ] + select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_gflags_gflags//:gflags",
    ],
)
cc_binary(
    name = "nemotron_nmt_streaming_s2s_client",
    srcs = ["nemotron_nmt_streaming_s2s_client.cc"],
    deps = [
        "//nemotronspeech/clients/asr:asr_client_helper",
        ":client_call",
        ":streaming_s2s_client",
        "@nvriva_common//nemotronspeech/proto:nemotron_grpc_nmt",
        "//nemotronspeech/utils/files:files",
        "@glog//:glog",
        "//nemotronspeech/clients/utils:grpc",
    ] + select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_gflags_gflags//:gflags",
    ],
)

