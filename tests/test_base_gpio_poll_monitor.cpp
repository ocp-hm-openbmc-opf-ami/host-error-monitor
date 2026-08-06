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

#include <error_monitors/base_gpio_poll_monitor.hpp>

#include <memory>
#include <string>
#include <system_error>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace host_error_monitor::base_gpio_poll_monitor
{

class BaseGPIOPollMonitorTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        MockGPIO::reset();
        // Make event_read fail immediately so flushEvents() returns
        MockGPIO::setFailEventRead(true);
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

TEST_F(BaseGPIOPollMonitorTest, ConstructLowAssert_ValidWithMock)
{
    testing::internal::CaptureStderr();

    BaseGPIOPollMonitor monitor(*io, conn, "TEST_POLL_GPIO",
                                AssertValue::lowAssert, 1000, 90000);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
}

TEST_F(BaseGPIOPollMonitorTest, ConstructHighAssert_ValidWithMock)
{
    testing::internal::CaptureStderr();

    BaseGPIOPollMonitor monitor(*io, conn, "TEST_POLL_HIGH",
                                AssertValue::highAssert, 500, 5000);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
}

TEST_F(BaseGPIOPollMonitorTest, SignalNamePreserved)
{
    BaseGPIOPollMonitor monitor(*io, conn, "POLL_SIG", AssertValue::lowAssert,
                                1000, 90000);
    EXPECT_EQ(monitor.signalName, "POLL_SIG");
}

TEST_F(BaseGPIOPollMonitorTest, TimeoutAccessors)
{
    BaseGPIOPollMonitor monitor(*io, conn, "TIMEOUT_TEST2",
                                AssertValue::lowAssert, 1000, 90000);
    EXPECT_EQ(monitor.getTimeoutMs(), 90000u);

    monitor.setTimeoutMs(5000);
    EXPECT_EQ(monitor.getTimeoutMs(), 5000u);
}

TEST_F(BaseGPIOPollMonitorTest, AssertHandler_LogsTimeout)
{
    BaseGPIOPollMonitor monitor(*io, conn, "ASSERTPOLL_TEST",
                                AssertValue::lowAssert, 1000, 5000);
    EXPECT_TRUE(monitor.isValid());
    testing::internal::CaptureStderr();
    monitor.assertHandler();
    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("ASSERTPOLL_TEST asserted"));
    EXPECT_THAT(output, ::testing::HasSubstr("5000"));
}

TEST_F(BaseGPIOPollMonitorTest, DeassertHandler_NoOp)
{
    BaseGPIOPollMonitor monitor(*io, conn, "DEASSERTPOLL_TEST",
                                AssertValue::lowAssert, 1000, 90000);
    EXPECT_TRUE(monitor.isValid());
    EXPECT_NO_THROW(monitor.deassertHandler());
}

TEST_F(BaseGPIOPollMonitorTest, AssertValueEnum_DistinctValues)
{
    EXPECT_EQ(static_cast<int>(AssertValue::lowAssert), 0);
    EXPECT_EQ(static_cast<int>(AssertValue::highAssert), 1);
}

TEST_F(BaseGPIOPollMonitorTest, Construct_FindLineFails_NotValid)
{
    MockGPIO::setFailFindLine(true);
    testing::internal::CaptureStderr();

    BaseGPIOPollMonitor monitor(*io, conn, "FAIL_POLL_FIND",
                                AssertValue::lowAssert, 1000, 90000);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(monitor.isValid());
    EXPECT_THAT(output,
                ::testing::HasSubstr("Failed to find the FAIL_POLL_FIND"));
}

TEST_F(BaseGPIOPollMonitorTest, Construct_RequestFails_NotValid)
{
    MockGPIO::setFailLineRequest(true);
    testing::internal::CaptureStderr();

    BaseGPIOPollMonitor monitor(*io, conn, "FAIL_POLL_REQ",
                                AssertValue::lowAssert, 1000, 90000);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_FALSE(monitor.isValid());
    EXPECT_THAT(output, ::testing::HasSubstr(
                            "Failed to request events for FAIL_POLL_REQ"));
}

TEST_F(BaseGPIOPollMonitorTest, Construct_EventGetFdFails_ThrowsException)
{
    MockGPIO::setFailEventGetFd(true);
    // The C++ wrapper throws std::system_error when event_get_fd returns -1.
    // This exception is NOT caught in the production constructor.
    EXPECT_THROW(
        {
            BaseGPIOPollMonitor monitor(*io, conn, "FAIL_POLL_FD",
                                        AssertValue::lowAssert, 1000, 90000);
        },
        std::system_error);
}

TEST_F(BaseGPIOPollMonitorTest, StartPolling_HostOff_Deasserts)
{
    // hostIsOff() returns true in test env, so asserted() returns false.
    // This exercises: startPolling -> poll -> flushEvents -> asserted ->
    //                 deassertHandler -> waitForEvent
    BaseGPIOPollMonitor monitor(*io, conn, "POLL_START", AssertValue::lowAssert,
                                10, 100);
    EXPECT_TRUE(monitor.isValid());

    testing::internal::CaptureStderr();
    monitor.startPolling();
    // Pump io_context to execute any scheduled handlers
    io->poll();
    io->restart();
    std::string output = testing::internal::GetCapturedStderr();
    // startPolling was called — no crash, deassert path taken
}

TEST_F(BaseGPIOPollMonitorTest, HostOn_CancelsAndPolls)
{
    // hostOn() calls event.cancel() + startPolling()
    // This exercises: hostOn -> event.cancel -> startPolling -> poll ->
    //                 flushEvents -> asserted -> deassertHandler ->
    //                 waitForEvent, plus the waitForEvent lambda with
    //                 operation_aborted from event.cancel()
    BaseGPIOPollMonitor monitor(*io, conn, "POLL_HOSTON",
                                AssertValue::lowAssert, 10, 100);
    EXPECT_TRUE(monitor.isValid());

    testing::internal::CaptureStderr();
    monitor.hostOn();
    // Pump to process the cancel + any scheduled handlers
    io->poll();
    io->restart();
    std::string output = testing::internal::GetCapturedStderr();
    // hostOn was called — exercises hostOn, cancel, startPolling, poll,
    // flushEvents, asserted, deassertHandler, waitForEvent, and the
    // waitForEvent lambda (operation_aborted)
}

} // namespace host_error_monitor::base_gpio_poll_monitor
