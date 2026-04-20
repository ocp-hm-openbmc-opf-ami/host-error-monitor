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

#include <error_monitors/cpu_presence_monitor.hpp>

#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace host_error_monitor::cpu_presence_monitor
{

class CPUPresenceMonitorTest : public ::testing::Test
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

TEST(CPUPresenceMonitorTypeTest, TypeExists)
{
    static_assert(std::is_class_v<CPUPresenceMonitor>);
}

TEST(CPUPresenceMonitorTypeTest, DerivedFromBaseMonitor)
{
    static_assert(
        std::is_base_of_v<host_error_monitor::base_monitor::BaseMonitor,
                          CPUPresenceMonitor>);
}

TEST_F(CPUPresenceMonitorTest, Construct_ValidWithMock)
{
    // Set value=1 so CPU is "present" and no assert path is triggered
    MockGPIO::setLineValue("CPU0_PRESENCE", 1);
    testing::internal::CaptureStderr();

    CPUPresenceMonitor monitor(*io, conn, "CPU0_PRESENCE", 0);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
}

TEST_F(CPUPresenceMonitorTest, Construct_CPUMissing_Asserts)
{
    // Set GPIO value to 0 (CPU not present with ACTIVE_LOW) before
    // construction. getCPUPresence reads the value, then checkCPUPresence
    // calls CPUPresenceAssertHandler -> beep + logEvent
    MockGPIO::setLineValue("CPU_PRES_MISS", 0);
    testing::internal::CaptureStderr();

    CPUPresenceMonitor monitor(*io, conn, "CPU_PRES_MISS", 0);
    // Let beep async call complete
    DbusEnvironment::synchronizeIoc();

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(monitor.isValid());
    EXPECT_THAT(output, ::testing::HasSubstr("CPU_PRES_MISS asserted"));
}

TEST_F(CPUPresenceMonitorTest, HostOn_CPUPresent_NoAssert)
{
    // Default value is 0, but with ACTIVE_LOW flag the mock returns 0
    // which means "not present". Let's set it to 1 (present).
    MockGPIO::setLineValue("CPU_PRES_HO", 1);
    CPUPresenceMonitor monitor(*io, conn, "CPU_PRES_HO", 0);
    EXPECT_TRUE(monitor.isValid());
    // hostOn calls checkCPUPresence — cpuPresent was set during construction
    EXPECT_NO_THROW(monitor.hostOn());
    DbusEnvironment::synchronizeIoc();
}

} // namespace host_error_monitor::cpu_presence_monitor
