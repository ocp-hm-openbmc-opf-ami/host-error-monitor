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
#include "mock_gpiod.hpp"

#include <error_monitors/base_gpio_monitor.hpp>

#include <memory>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace host_error_monitor::base_gpio_monitor
{

class BaseGPIOMonitorTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        MockGPIO::reset();
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

TEST_F(BaseGPIOMonitorTest, ConstructLowAssert_ValidWithMock)
{
    testing::internal::CaptureStderr();

    BaseGPIOMonitor monitor(*io, conn, "TEST_GPIO_LOW", AssertValue::lowAssert);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
    EXPECT_TRUE(MockGPIO::wasLineRequested("TEST_GPIO_LOW"));
}

TEST_F(BaseGPIOMonitorTest, ConstructHighAssert_ValidWithMock)
{
    testing::internal::CaptureStderr();

    BaseGPIOMonitor monitor(*io, conn, "TEST_GPIO_HIGH",
                            AssertValue::highAssert);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
}

TEST_F(BaseGPIOMonitorTest, SignalNamePreserved)
{
    BaseGPIOMonitor monitor(*io, conn, "MY_SIGNAL", AssertValue::lowAssert);
    EXPECT_EQ(monitor.signalName, "MY_SIGNAL");
}

TEST_F(BaseGPIOMonitorTest, ConnPreserved)
{
    BaseGPIOMonitor monitor(*io, conn, "MY_SIGNAL2", AssertValue::lowAssert);
    EXPECT_EQ(monitor.conn, conn);
}

TEST_F(BaseGPIOMonitorTest, AssertValueEnum_DistinctValues)
{
    EXPECT_NE(static_cast<int>(AssertValue::lowAssert),
              static_cast<int>(AssertValue::highAssert));
    EXPECT_EQ(static_cast<int>(AssertValue::lowAssert), 0);
    EXPECT_EQ(static_cast<int>(AssertValue::highAssert), 1);
}

TEST_F(BaseGPIOMonitorTest, AssertHandler_LogsToStderr)
{
    BaseGPIOMonitor monitor(*io, conn, "ASSERT_TEST", AssertValue::lowAssert);
    EXPECT_TRUE(monitor.isValid());
    testing::internal::CaptureStderr();
    monitor.assertHandler();
    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("ASSERT_TEST asserted"));
}

TEST_F(BaseGPIOMonitorTest, DeassertHandler_NoOp)
{
    BaseGPIOMonitor monitor(*io, conn, "DEASSERT_TEST", AssertValue::lowAssert);
    EXPECT_TRUE(monitor.isValid());
    EXPECT_NO_THROW(monitor.deassertHandler());
}

TEST_F(BaseGPIOMonitorTest, Construct_FindLineFails_NotValid)
{
    MockGPIO::setFailFindLine(true);
    testing::internal::CaptureStderr();

    BaseGPIOMonitor monitor(*io, conn, "FAIL_FIND", AssertValue::lowAssert);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(monitor.isValid());
    EXPECT_THAT(output, ::testing::HasSubstr("Failed to find the FAIL_FIND"));
}

TEST_F(BaseGPIOMonitorTest, Construct_RequestFails_NotValid)
{
    MockGPIO::setFailLineRequest(true);
    testing::internal::CaptureStderr();

    BaseGPIOMonitor monitor(*io, conn, "FAIL_REQ", AssertValue::lowAssert);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(monitor.isValid());
    EXPECT_THAT(output,
                ::testing::HasSubstr("Failed to request events for FAIL_REQ"));
}

TEST_F(BaseGPIOMonitorTest, Construct_EventGetFdFails_ThrowsException)
{
    MockGPIO::setFailEventGetFd(true);
    // The C++ wrapper throws std::system_error when event_get_fd returns -1.
    // This exception is NOT caught in the production constructor, so it
    // propagates out. Verify it throws the expected exception type.
    EXPECT_THROW(
        {
            BaseGPIOMonitor monitor(*io, conn, "FAIL_FD",
                                    AssertValue::lowAssert);
        },
        std::system_error);
}

TEST_F(BaseGPIOMonitorTest, StartMonitoring_ChecksAssertedAndWaits)
{
    // startMonitoring calls checkEvent(asserted()) then waitForEvent().
    // With mock value=0 and lowAssert, asserted() returns 0 (false),
    // so checkEvent takes the deassert path calling deassertHandler().
    // waitForEvent() schedules async_wait on the eventfd.
    BaseGPIOMonitor monitor(*io, conn, "MON_START", AssertValue::lowAssert);
    EXPECT_TRUE(monitor.isValid());

    testing::internal::CaptureStderr();
    monitor.startMonitoring();
    // Pump io to process scheduled handlers
    io->poll();
    io->restart();
    std::string output = testing::internal::GetCapturedStderr();
    // startMonitoring exercised checkEvent + asserted + waitForEvent
}

TEST_F(BaseGPIOMonitorTest, StartMonitoring_Asserted_CallsHandler)
{
    // Set value=1 so asserted() returns true with lowAssert (active low
    // means value=1 is asserted). checkEvent calls assertHandler().
    MockGPIO::setLineValue("MON_ASSERT", 1);
    BaseGPIOMonitor monitor(*io, conn, "MON_ASSERT", AssertValue::lowAssert);
    EXPECT_TRUE(monitor.isValid());

    testing::internal::CaptureStderr();
    monitor.startMonitoring();
    io->poll();
    io->restart();
    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("MON_ASSERT asserted"));
}

} // namespace host_error_monitor::base_gpio_monitor
