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

#include <error_monitors/cpu_mismatch_monitor.hpp>

#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace host_error_monitor::cpu_mismatch_monitor
{

class CPUMismatchMonitorTest : public ::testing::Test
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

TEST(CPUMismatchMonitorTypeTest, TypeExists)
{
    static_assert(std::is_class_v<CPUMismatchMonitor>);
}

TEST(CPUMismatchMonitorTypeTest, DerivedFromBaseMonitor)
{
    static_assert(
        std::is_base_of_v<host_error_monitor::base_monitor::BaseMonitor,
                          CPUMismatchMonitor>);
}

TEST(CPUMismatchMonitorTypeTest, DebugConstantAccessible)
{
    [[maybe_unused]] constexpr bool d = debug;
}

TEST_F(CPUMismatchMonitorTest, Construct_ValidWithMock)
{
    testing::internal::CaptureStderr();

    CPUMismatchMonitor monitor(*io, conn, "CPU_MISMATCH0", 0);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
}

TEST_F(CPUMismatchMonitorTest, SignalNamePreserved)
{
    CPUMismatchMonitor monitor(*io, conn, "CPU_MISMATCH1", 1);
    EXPECT_EQ(monitor.signalName, "CPU_MISMATCH1");
}

TEST_F(CPUMismatchMonitorTest, Construct_MismatchAsserted)
{
    // Set GPIO value to 1 before construction so cpuMismatchAsserted()
    // returns true, triggering cpuMismatchAssertHandler -> cpuMismatchLog
    MockGPIO::setLineValue("CPU_MISMATCH_HI", 1);
    testing::internal::CaptureStderr();

    CPUMismatchMonitor monitor(*io, conn, "CPU_MISMATCH_HI", 0);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
    EXPECT_THAT(output, ::testing::HasSubstr("CPU_MISMATCH_HI asserted"));
}

TEST_F(CPUMismatchMonitorTest, HostOn_ChecksMismatch)
{
    CPUMismatchMonitor monitor(*io, conn, "CPU_MISMATCH_HO", 0);
    EXPECT_TRUE(monitor.isValid());
    // hostOn() calls checkCPUMismatch() — value is 0, no assert
    EXPECT_NO_THROW(monitor.hostOn());
}

} // namespace host_error_monitor::cpu_mismatch_monitor
