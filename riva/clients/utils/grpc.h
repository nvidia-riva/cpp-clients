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

constexpr int MAX_GRPC_MESSAGE_SIZE = 128 * 1024 * 1024;

using grpc::Status;
using grpc::StatusCode;

namespace riva::clients {

class CustomAuthenticator : public grpc::MetadataCredentialsPlugin {
 public:
  CustomAuthenticator(const std::string& metadata) : metadata_(metadata) {}

  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) override
  {
    auto key_value_pairs = split(metadata_, ',');
    if (key_value_pairs.size() % 2) {
      throw std::runtime_error("Error: metadata must contain key value pairs.");
    }
    for (size_t i = 0; i < key_value_pairs.size(); i += 2) {
      metadata->insert(std::make_pair(key_value_pairs.at(i), key_value_pairs.at(i + 1)));
    }
    return grpc::Status::OK;
  }

 private:
  std::string metadata_;
};

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
    uint64_t timeout_ms = 10000, uint64_t max_grpc_message_size = MAX_GRPC_MESSAGE_SIZE)
{
  grpc::ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(max_grpc_message_size);
  channel_args.SetMaxSendMessageSize(max_grpc_message_size);
  auto channel = grpc::CreateCustomChannel(uri, credentials, channel_args);

  auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms);
  auto reached_required_state = channel->WaitForConnected(deadline);
  auto state = channel->GetState(true);

  if (!reached_required_state) {
    DLOG(WARNING) << "Unable to establish connection to server. Current state: " << (int)state;
    throw std::runtime_error(
        "Unable to establish connection to server. Current state: " + std::to_string((int)state));
  }

  return channel;
}

/// Utility function to create GRPC credentials
/// Returns shared ptr to GrpcChannelCredentials
/// @param use_ssl Boolean flag that controls if ssl encryption should be used
/// @param ssl_cert Path to the certificate file
std::shared_ptr<grpc::ChannelCredentials>
CreateChannelCredentials(
    bool use_ssl, const std::string& ssl_root_cert, const std::string& ssl_client_key,
    const std::string& ssl_client_cert, const std::string& metadata)
{
  std::shared_ptr<grpc::ChannelCredentials> creds;

  if (use_ssl || !ssl_root_cert.empty()) {
    grpc::SslCredentialsOptions ssl_opts;
    if (!ssl_root_cert.empty()) {
      auto cacert = riva::utils::files::ReadFileContentAsString(ssl_root_cert);
      ssl_opts.pem_root_certs = cacert;
    }
    if (!ssl_client_key.empty() && !ssl_client_cert.empty()) {
      auto client_key = riva::utils::files::ReadFileContentAsString(ssl_client_key);
      auto client_cert = riva::utils::files::ReadFileContentAsString(ssl_client_cert);
      ssl_opts.pem_private_key = client_key;
      ssl_opts.pem_cert_chain = client_cert;
    }

    LOG(INFO) << "Using SSL Credentials";
    creds = grpc::SslCredentials(ssl_opts);
  } else {
    LOG(INFO) << "Using Insecure Server Credentials";
    creds = grpc::InsecureChannelCredentials();
  }

  if (!metadata.empty()) {
    auto call_creds =
        grpc::MetadataCredentialsFromPlugin(std::unique_ptr<grpc::MetadataCredentialsPlugin>(
            new riva::clients::CustomAuthenticator(metadata)));
    creds = grpc::CompositeChannelCredentials(creds, call_creds);
  }

  return creds;
}

}  // namespace riva::clients
