/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef BASE_REALTIME_CLIENT_H
#define BASE_REALTIME_CLIENT_H

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

#include "audio_chunks.h"

namespace nvidia::riva::realtime {
class WebSocketClientBase {
 protected:
  typedef websocketpp::client<websocketpp::config::asio_client> websocketpp_client;
  typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

  websocketpp_client wsClient_;
  websocketpp::connection_hdl connectionHdl_;

  std::string uri_;
  bool connected_;
  std::mutex mutex_;

  // Connection state
  bool connectionClosedByServer_;
  std::condition_variable connectionCv_;
  std::mutex connectionMutex_;
  std::size_t connectionTimeoutMs_;

  // Protected access to websocket client for derived classes
  websocketpp_client& GetWsClient() { return wsClient_; }
  websocketpp::connection_hdl& GetConnection() { return connectionHdl_; }
  std::mutex& GetConnectionMutex() { return connectionMutex_; }

 public:
  WebSocketClientBase(const std::string& uri);
  ~WebSocketClientBase() = default;

  // Connection timeout
  void SetConnectionTimeout(const std::size_t connectionTimeoutMs);
  std::size_t GetConnectionTimeout();

  // Connection status
  bool IsConnected() const { return connected_; }
  bool IsConnectionClosedByServer() const { return connectionClosedByServer_; }
  bool IsConnectionOpen() const { return connected_ && !connectionClosedByServer_; }
  bool IsConnectionClosed() const { return !connected_ || connectionClosedByServer_; }

  // Control logging verbosity
  void SetVerboseLogging(bool verbose);

  // Connection management
  void Connect(const std::string& uri);
  void Run();
  void Send(const std::string& message);
  void Close();
  void SendJsonMessage(const std::string& type, const std::string& data = "");

  // Connection waiting methods
  bool WaitForConnection();
  bool WaitForDisconnection();
  bool WaitForServerClose();

  // Event handlers
  void OnOpen(websocketpp::connection_hdl hdl);
  void OnClose(websocketpp::connection_hdl hdl);
  void OnFail(websocketpp::connection_hdl hdl);
  void OnMessage(websocketpp::connection_hdl hdl, message_ptr msg);
  virtual void HandleMessage(const std::string& message) = 0;
};
}  // namespace nvidia::riva::realtime
#endif  // BASE_REALTIME_CLIENT_H