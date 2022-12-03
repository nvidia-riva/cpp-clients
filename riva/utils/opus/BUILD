"""
Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
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
    name = "opus",
    srcs = [
        "opus_encoder.cc",
        "opus_decoder.cc",
    ],
    hdrs = [
        "opus_encoder.h",
        "opus_decoder.h",
    ],
    deps = [
        "@glog//:glog",
        "//riva/utils/wav:reader",
    ],
    copts = ["-I/usr/include/opus"],
    linkstatic = False,
    linkopts = ["-lm"]
)

cc_test(
    name = "opus_test",
    srcs = ["opus_test.cc"],
    copts = ["-I/usr/include/opus"],
    linkopts = ["-lopus",
                "-lopusfile" ],
    linkstatic = True,
    deps = [
        ":opus",
        "@speexdsp//:resample",
        "@googletest//:gtest_main",
    ],
     data = [
        "//test_files/asr/public:public_wavs",
    ],
)
