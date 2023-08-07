/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpcpp/grpcpp.h>
#include <strings.h>

#include <chrono>
#include <string>

#include "riva/utils/files/files.h"
#include "riva/utils/string_processing.h"

using grpc::Status;
using grpc::StatusCode;

namespace riva::clients {

/// Utility function to create a GRPC channel
/// This will only return when the channel has been created, thus making sure that the subsequent
/// GRPC call won't have additional latency due to channel creation
///
/// @param uri URI of server
/// @param credentials GRPC credentials
/// @param timeout_ms The maximum time (in milliseconds) to wait for channel creation. Throws
/// exception if time is exceeded

std::shared_ptr<grpc::Channel>
CreateChannelBlocking(
    const std::string& uri, const std::shared_ptr<grpc::ChannelCredentials> credentials,
    uint64_t timeout_ms = 10000);

/// Utility function to create GRPC credentials
/// Returns shared ptr to GrpcChannelCredentials
/// @param use_ssl Boolean flag that controls if ssl encryption should be used
/// @param ssl_cert Path to the certificate file
std::shared_ptr<grpc::ChannelCredentials>
CreateChannelCredentials(bool use_ssl, const std::string& ssl_cert);

void AddMetadata(grpc::ClientContext& context, std::string metadata);

}  // namespace riva::clients
