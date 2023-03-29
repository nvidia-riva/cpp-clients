#
# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
#

package(
    default_visibility = ["//visibility:public"],
)
cc_binary(
    name = "riva_nmt_client",
    srcs = ["riva_nmt.cc"],
    deps = [
        "//riva/clients/utils:grpc",
        "@nvriva_common//riva/proto:riva_grpc_nmt",
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
        "//riva/clients/asr:asr_client_helper",
        "@com_github_grpc_grpc//:grpc++",
        "@nvriva_common//riva/proto:riva_grpc_nmt",
        "//riva/utils/wav:reader",
    ],
)

cc_library(
    name = "streaming_s2s_client",
    srcs = ["streaming_s2s_client.h",
            "streaming_s2s_client.cc"
    ],
    deps = [
        "//riva/clients/asr:asr_client_helper",
        ":client_call",
        "//riva/utils/wav:reader",
        "//riva/utils/wav:writer",
        "//riva/utils/opus",
        "@glog//:glog",
    ] + select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "//riva/utils:stamping",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_gflags_gflags//:gflags",
        "@nvriva_common//riva/proto:riva_grpc_nmt",
        "//riva/utils:thread_pool",
    ],
)
cc_library(
    name = "streaming_speech_translate_client",
    srcs = ["streaming_speech_translate_client.h",
            "streaming_speech_translate_client.cc"
    ],
    deps = [
        "//riva/clients/asr:asr_client_helper",
        ":client_call",
        "//riva/utils/wav:reader",
        "@glog//:glog",
    ] + select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "//riva/utils:stamping",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_gflags_gflags//:gflags",
        "@nvriva_common//riva/proto:riva_grpc_nmt",
        "//riva/utils:thread_pool",
    ],
)

cc_binary(
    name = "riva_streaming_speech_translate_client",
    srcs = ["riva_streaming_speech_translate_client.cc"],
    deps = [
        "//riva/clients/asr:asr_client_helper",
        ":client_call",
        ":streaming_speech_translate_client",
        "@nvriva_common//riva/proto:riva_grpc_nmt",
        "//riva/utils/files:files",
        "//riva/utils/wav:reader",
        "@glog//:glog",
        "//riva/clients/utils:grpc",
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
    name = "riva_streaming_s2s_client",
    srcs = ["riva_streaming_s2s_client.cc"],
    deps = [
        "//riva/clients/asr:asr_client_helper",
        ":client_call",
        ":streaming_s2s_client",
        "@nvriva_common//riva/proto:riva_grpc_nmt",
        "//riva/utils/files:files",
        "@glog//:glog",
        "//riva/clients/utils:grpc",
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

