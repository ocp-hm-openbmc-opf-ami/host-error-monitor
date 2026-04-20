/*
// Copyright (c) 2026 AMI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include <boost/asio/io_context.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <memory>

#include <gmock/gmock.h>

/// Lightweight D-Bus test environment inspired by openbmc/telemetry.
/// Sets up a private session-bus connection that the test code can use for
/// async_method_call / property operations without touching the real system
/// bus.
class DbusEnvironment : public ::testing::Environment
{
  public:
    ~DbusEnvironment() override;

    void SetUp() override;
    void TearDown() override;

    static boost::asio::io_context& getIoc();
    static std::shared_ptr<sdbusplus::asio::connection> getBus();
    static std::shared_ptr<sdbusplus::asio::object_server> getObjServer();

    /// Run all pending handlers on the io_context (non-blocking).
    static void synchronizeIoc()
    {
        ioc.poll();
        ioc.restart();
    }

  private:
    static inline boost::asio::io_context ioc;
    static inline std::shared_ptr<sdbusplus::asio::connection> bus;
    static inline std::shared_ptr<sdbusplus::asio::object_server> objServer;
    static inline bool setUp = false;
};
