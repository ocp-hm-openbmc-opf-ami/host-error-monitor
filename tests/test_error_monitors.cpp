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
#include "dbus_environment.hpp"

#include <gtest/gtest.h>

namespace host_error_monitor::error_monitors
{

// Forward declarations — defined in error_monitors.hpp, compiled via
// the production .cpp TU
bool checkMonitors();
bool startMonitors(boost::asio::io_context& io,
                   std::shared_ptr<sdbusplus::asio::connection> conn);
void sendHostOn();

class ErrorMonitorsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        io = &DbusEnvironment::getIoc();
        conn = DbusEnvironment::getBus();
    }

    void TearDown() override
    {
        DbusEnvironment::synchronizeIoc();
    }

    boost::asio::io_context* io = nullptr;
    std::shared_ptr<sdbusplus::asio::connection> conn;
};

TEST_F(ErrorMonitorsTest, CheckMonitors_ReturnsTrue)
{
    EXPECT_TRUE(checkMonitors());
}

TEST_F(ErrorMonitorsTest, StartMonitors_ReturnsTrue)
{
    EXPECT_TRUE(startMonitors(*io, conn));
}

TEST_F(ErrorMonitorsTest, SendHostOn_DoesNotThrow)
{
    EXPECT_NO_THROW(sendHostOn());
}

TEST_F(ErrorMonitorsTest, StartMonitors_MultipleCalls)
{
    EXPECT_TRUE(startMonitors(*io, conn));
    EXPECT_TRUE(startMonitors(*io, conn));
}

} // namespace host_error_monitor::error_monitors
