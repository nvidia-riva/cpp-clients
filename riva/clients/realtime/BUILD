"""
Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
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
    name = "realtime_audio_client_lib",
    srcs = [
        "audio_chunks.cpp",
        "base_client.cpp", 
        "realtime_client.cpp",
    ],
    hdrs = [
        "audio_chunks.h",
        "base_client.h",
        "realtime_client.h",
    ],
    deps = [
        "//riva/utils/wav:reader",
        "//riva/utils/stats_builder:stats_builder_lib",
        "@websocketpp//:websocketpp",
        "@rapidjson//:rapidjson",
        "@glog//:glog",
        "@com_github_gflags_gflags//:gflags",
    ],
) 

cc_binary(
    name = "riva_realtime_asr_client",
    srcs = ["riva_realtime_asr_client.cc"],
    includes = ["-Irealtime"],
    deps = [
        ":realtime_audio_client_lib",
        "@websocketpp//:websocketpp",
        "@rapidjson//:rapidjson",
        "//riva/utils/stats_builder:stats_builder_lib",
        "//riva/utils/wav:reader",
    ] + select({
        "@platforms//cpu:aarch64": [
            "@alsa_aarch64//:libasound"
        ],
        "//conditions:default": [
            "@alsa//:libasound"
        ],
    }),
    linkopts = [
        "-lssl",
        "-lcrypto",
        "-lboost_system",
    ]
)