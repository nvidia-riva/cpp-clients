"""
Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto.  Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
"""

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "asr_client_helper",
    srcs = ["riva_asr_client_helper.h", "riva_asr_client_helper.cc"],
    deps = select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        "@com_github_grpc_grpc//:grpc++",
        "@nvriva_common//riva/proto:riva_grpc_asr",
    ],
)

cc_library(
    name = "client_call",
    srcs = ["client_call.h", "client_call.cc"],
    deps = select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }) + [
        ":asr_client_helper",
        "@com_github_grpc_grpc//:grpc++",
        "@nvriva_common//riva/proto:riva_grpc_asr",
        "@glog//:glog",
        "//riva/utils/wav:reader",
    ],
)

cc_library(
    name = "streaming_recognize_client",
    srcs = ["streaming_recognize_client.h",
            "streaming_recognize_client.cc"
    ],
    deps = [
        ":asr_client_helper",
        ":client_call",
        "//riva/utils/wav:reader",
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
        "@nvriva_common//riva/proto:riva_grpc_asr",
        "//riva/utils:thread_pool",
    ],
)

cc_binary(
    name = "riva_asr_client",
    srcs = ["riva_asr_client.cc"],
    deps = [
        ":asr_client_helper",
        ":client_call",
        "@nvriva_common//riva/proto:riva_grpc_asr",
        "//riva/utils:stamping",
        "//riva/utils/files:files",
        "//riva/utils/wav:reader",
        "@glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_gflags_gflags//:gflags",
        "//riva/clients/utils:grpc",
    ]
)

cc_binary(
    name = "riva_streaming_asr_client",
    srcs = ["riva_streaming_asr_client.cc"],
    deps = [
        ":asr_client_helper",
        ":client_call",
        ":streaming_recognize_client",
        "@nvriva_common//riva/proto:riva_grpc_asr",
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

cc_test(
    name = "streaming_recognize_client_test",
    srcs = ["streaming_recognize_client_test.cc"],
    copts = ["-Iexternal/gtest/include"],
    deps = [
        ":streaming_recognize_client",
        "@googletest//:gtest_main",
        "@nvriva_common//riva/proto:riva_grpc_asr",
    ],
    tags = ["needs_alsa"]
)
