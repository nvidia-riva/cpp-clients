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
        "opus_client_decoder.cc",
    ],
    hdrs = [
        "opus_client_decoder.h",
    ],
    deps = [
        "@glog//:glog",
        "@libopus",
        "@libopusfile",
        "//riva/utils/wav:reader",
    ],
    linkopts = [
        "-logg"
    ],
    linkstatic = False,
)
