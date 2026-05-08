/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "base_client.h"

#include <functional>
#include <iomanip>


nvidia::riva::realtime::WebSocketClientBase::WebSocketClientBase(const std::string& uri)
    : connected_(false), connectionClosedByServer_(false), connectionTimeoutMs_(std::size_t(5000)),
      uri_(uri)
{
  // Set up logging - suppress verbose internal messages
  wsClient_.set_access_channels(websocketpp::log::alevel::connect);
  wsClient_.set_access_channels(websocketpp::log::alevel::disconnect);
  wsClient_.set_access_channels(websocketpp::log::alevel::fail);
  wsClient_.set_access_channels(websocketpp::log::alevel::app);

  // Initialize ASIO
  wsClient_.init_asio();

  // Set up handlers
  wsClient_.set_open_handler(
      std::bind(&nvidia::riva::realtime::WebSocketClientBase::OnOpen, this, std::placeholders::_1));
  wsClient_.set_close_handler(std::bind(
      &nvidia::riva::realtime::WebSocketClientBase::OnClose, this, std::placeholders::_1));
  wsClient_.set_fail_handler(
      std::bind(&nvidia::riva::realtime::WebSocketClientBase::OnFail, this, std::placeholders::_1));
  wsClient_.set_message_handler(std::bind(
      &nvidia::riva::realtime::WebSocketClientBase::OnMessage, this, std::placeholders::_1,
      std::placeholders::_2));
}

void
nvidia::riva::realtime::WebSocketClientBase::SetConnectionTimeout(
    const std::size_t connectionTimeoutMs)
{
  connectionTimeoutMs_ = connectionTimeoutMs;
}

std::size_t
nvidia::riva::realtime::WebSocketClientBase::GetConnectionTimeout()
{
  return connectionTimeoutMs_;
}

void
nvidia::riva::realtime::WebSocketClientBase::SetVerboseLogging(bool verbose)
{
  if (verbose) {
    // Enable all logging channels
    wsClient_.set_access_channels(websocketpp::log::alevel::all);
    wsClient_.clear_access_channels(websocketpp::log::alevel::frame_payload);
  } else {
    // Minimal logging - only important events
    wsClient_.clear_access_channels(websocketpp::log::alevel::all);
    wsClient_.set_access_channels(websocketpp::log::alevel::connect);
    wsClient_.set_access_channels(websocketpp::log::alevel::disconnect);
    wsClient_.set_access_channels(websocketpp::log::alevel::fail);
    wsClient_.set_access_channels(websocketpp::log::alevel::app);
  }
}

void
nvidia::riva::realtime::WebSocketClientBase::Connect(const std::string& uri)
{
  uri_ = uri;
  websocketpp::lib::error_code ec;

  websocketpp_client::connection_ptr con = wsClient_.get_connection(uri, ec);
  if (ec) {
    std::cerr << "Could not create connection: " << ec.message() << std::endl;
    return;
  }

  wsClient_.connect(con);
}

void
nvidia::riva::realtime::WebSocketClientBase::Run()
{
  wsClient_.run();
}

void
nvidia::riva::realtime::WebSocketClientBase::Send(const std::string& message)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (connected_) {
    websocketpp::lib::error_code ec;
    wsClient_.send(connectionHdl_, message, websocketpp::frame::opcode::text, ec);
    if (ec) {
      std::cerr << "Send failed: " << ec.message() << std::endl;
    }
  }
}

void
nvidia::riva::realtime::WebSocketClientBase::Close()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (connected_) {
    websocketpp::lib::error_code ec;
    wsClient_.close(connectionHdl_, websocketpp::close::status::normal, "Client closing", ec);
  }
}

void
nvidia::riva::realtime::WebSocketClientBase::SendJsonMessage(
    const std::string& type, const std::string& data)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (connected_) {
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();

    doc.AddMember("type", rapidjson::Value(type.c_str(), allocator), allocator);
    if (!data.empty()) {
      doc.AddMember("data", rapidjson::Value(data.c_str(), allocator), allocator);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    websocketpp::lib::error_code ec;
    wsClient_.send(connectionHdl_, buffer.GetString(), websocketpp::frame::opcode::text, ec);
    if (ec) {
      std::cerr << "Send failed: " << ec.message() << std::endl;
    } else {
      std::cout << "Sent: " << buffer.GetString() << std::endl;
    }
  }
}

void
nvidia::riva::realtime::WebSocketClientBase::OnOpen(websocketpp::connection_hdl hdl)
{
  std::lock_guard<std::mutex> lock(mutex_);
  connectionHdl_ = hdl;
  connected_ = true;

  // Notify waiting threads that connection is established
  {
    std::lock_guard<std::mutex> conn_lock(connectionMutex_);
    connectionCv_.notify_one();
  }

  std::cout << "Connected to " << uri_ << std::endl;
}

void
nvidia::riva::realtime::WebSocketClientBase::OnClose(websocketpp::connection_hdl hdl)
{
  (void)hdl;  // Suppress unused parameter warning
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;

  // Check if this was a server-initiated close
  {
    std::lock_guard<std::mutex> conn_lock(connectionMutex_);
    connectionClosedByServer_ = true;
  }
  connectionCv_.notify_one();

  std::cout << "Connection closed" << std::endl;
}

void
nvidia::riva::realtime::WebSocketClientBase::OnFail(websocketpp::connection_hdl hdl)
{
  (void)hdl;  // Suppress unused parameter warning
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;

  // Mark as server-initiated failure
  {
    std::lock_guard<std::mutex> conn_lock(connectionMutex_);
    connectionClosedByServer_ = true;
  }
  connectionCv_.notify_one();

  std::cout << "************************ Connection failed" << std::endl;
}

void
nvidia::riva::realtime::WebSocketClientBase::OnMessage(
    websocketpp::connection_hdl hdl, message_ptr msg)
{
  (void)hdl;  // Suppress unused parameter warning
  HandleMessage(msg->get_payload());
}

bool
nvidia::riva::realtime::WebSocketClientBase::WaitForConnection()
{
  std::unique_lock<std::mutex> lock(connectionMutex_);
  return connectionCv_.wait_for(
      lock,
      std::chrono::milliseconds(connectionTimeoutMs_),  // Use the provided timeout
      [this] { return connected_; });
}

bool
nvidia::riva::realtime::WebSocketClientBase::WaitForDisconnection()
{
  std::unique_lock<std::mutex> lock(connectionMutex_);
  return connectionCv_.wait_for(
      lock,
      std::chrono::milliseconds(connectionTimeoutMs_),  // Use the provided timeout
      [this] { return !connected_; });
}

bool
nvidia::riva::realtime::WebSocketClientBase::WaitForServerClose()
{
  std::unique_lock<std::mutex> lock(connectionMutex_);
  return connectionCv_.wait_for(
      lock,
      std::chrono::milliseconds(connectionTimeoutMs_),  // Use the provided timeout
      [this] { return connectionClosedByServer_; });
}
