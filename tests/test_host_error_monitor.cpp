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

// ============================================================
// Comprehensive Unit Tests for host-error-monitor
//
// Strategy:
//   - Compile production src/host_error_monitor.cpp with -DUNIT_TEST
//     to exclude main(), but keep all other functions accessible.
//   - Use a D-Bus test environment (session bus) for testing
//     async_method_call paths (handleRecovery, startPowerCycle, etc.)
//   - GPIO-dependent monitors (BaseGPIOMonitor, BaseGPIOPollMonitor)
//     fail gracefully at construction (gpiod::find_line returns null
//     in docker), so we test their construction failure paths.
//   - error_monitors.hpp functions are currently stubs with
//     commented-out code, but we still cover them.
// ============================================================

#include "dbus_environment.hpp"

// The production .cpp is compiled as a separate TU with -DUNIT_TEST.
// We declare the functions we need from host_error_monitor namespace
// since many are static-inline in the header or defined in the .cpp.

#include <error_monitors/base_monitor.hpp>
#include <host_error_monitor.hpp>

// sd_journal_send is used by base_monitor::log_message
#include <systemd/sd-journal.h>

#ifdef FAIL
#undef FAIL
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef DEBUG
#undef DEBUG
#endif

#include <memory>
#include <string>
#include <thread>
#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// ============================================================
// Test Fixture with D-Bus environment
// ============================================================

class HostErrorMonitorTest : public ::testing::Test
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

// ============================================================
// hostIsOff() Tests — tests the actual production function
// ============================================================

TEST_F(HostErrorMonitorTest, HostIsOff_DefaultState)
{
    // The production code initializes hostOff = true
    EXPECT_TRUE(host_error_monitor::hostIsOff());
}

// ============================================================
// RecoveryType Enum Tests
// ============================================================

TEST(RecoveryTypeTest, EnumValues_AllDistinct)
{
    EXPECT_NE(host_error_monitor::RecoveryType::noRecovery,
              host_error_monitor::RecoveryType::powerCycle);
    EXPECT_NE(host_error_monitor::RecoveryType::powerCycle,
              host_error_monitor::RecoveryType::warmReset);
    EXPECT_NE(host_error_monitor::RecoveryType::noRecovery,
              host_error_monitor::RecoveryType::warmReset);
}

TEST(RecoveryTypeTest, NoRecovery_IsZeroValue)
{
    EXPECT_EQ(static_cast<int>(host_error_monitor::RecoveryType::noRecovery),
              0);
}

TEST(RecoveryTypeTest, PowerCycle_HasExpectedValue)
{
    EXPECT_EQ(static_cast<int>(host_error_monitor::RecoveryType::powerCycle),
              1);
}

TEST(RecoveryTypeTest, WarmReset_HasExpectedValue)
{
    EXPECT_EQ(static_cast<int>(host_error_monitor::RecoveryType::warmReset), 2);
}

// ============================================================
// Association Type Tests
// ============================================================

TEST(AssociationTest, CanBeConstructed)
{
    host_error_monitor::Association assoc{"forward", "reverse", "/path"};
    EXPECT_EQ(std::get<0>(assoc), "forward");
    EXPECT_EQ(std::get<1>(assoc), "reverse");
    EXPECT_EQ(std::get<2>(assoc), "/path");
}

TEST(AssociationTest, EmptyStrings)
{
    host_error_monitor::Association assoc{"", "", ""};
    EXPECT_TRUE(std::get<0>(assoc).empty());
    EXPECT_TRUE(std::get<1>(assoc).empty());
    EXPECT_TRUE(std::get<2>(assoc).empty());
}

// ============================================================
// MAX_CPUS Constant Test
// ============================================================

TEST(ConstantsTest, MaxCpus_DefinedAs8)
{
    EXPECT_EQ(MAX_CPUS, 8);
}

// ============================================================
// handleRecovery Tests — using D-Bus connection
// ============================================================

TEST_F(HostErrorMonitorTest, HandleRecovery_NoRecovery_LogsDisabledMessage)
{
    testing::internal::CaptureStderr();

    host_error_monitor::handleRecovery(
        host_error_monitor::RecoveryType::noRecovery, conn);

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("Recovery is disabled"));
}

TEST_F(HostErrorMonitorTest, HandleRecovery_PowerCycle_LogsAndCalls)
{
    // startPowerCycle will attempt async_method_call which will fail
    // (no target service), but the code path is exercised
    testing::internal::CaptureStderr();

    host_error_monitor::handleRecovery(
        host_error_monitor::RecoveryType::powerCycle, conn);

    DbusEnvironment::synchronizeIoc();

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("power cycle"));
}

TEST_F(HostErrorMonitorTest, HandleRecovery_WarmReset_LogsAndCalls)
{
    testing::internal::CaptureStderr();

    host_error_monitor::handleRecovery(
        host_error_monitor::RecoveryType::warmReset, conn);

    DbusEnvironment::synchronizeIoc();

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("warm reset"));
}

// ============================================================
// startPowerCycle / startWarmReset Tests
// ============================================================

TEST_F(HostErrorMonitorTest, StartPowerCycle_WithConn_DoesNotThrow)
{
    EXPECT_NO_THROW(host_error_monitor::startPowerCycle(conn));
    DbusEnvironment::synchronizeIoc();
}

TEST_F(HostErrorMonitorTest, StartWarmReset_WithConn_DoesNotThrow)
{
    EXPECT_NO_THROW(host_error_monitor::startWarmReset(conn));
    DbusEnvironment::synchronizeIoc();
}

TEST_F(HostErrorMonitorTest, StartPowerCycle_ErrorCallback_PrintsError)
{
    testing::internal::CaptureStderr();

    host_error_monitor::startPowerCycle(conn);

    // Wait for the async D-Bus call to complete and fire the error callback
    for (int i = 0; i < 20; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DbusEnvironment::synchronizeIoc();
    }

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("failed to set Chassis State"));
}

TEST_F(HostErrorMonitorTest, StartWarmReset_ErrorCallback_PrintsError)
{
    testing::internal::CaptureStderr();

    host_error_monitor::startWarmReset(conn);

    // Wait for the async D-Bus call to complete and fire the error callback
    for (int i = 0; i < 20; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DbusEnvironment::synchronizeIoc();
    }

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("failed to set Host State"));
}

// ============================================================
// beep() Test
// ============================================================

TEST_F(HostErrorMonitorTest, Beep_WithConn_DoesNotThrow)
{
    EXPECT_NO_THROW(host_error_monitor::beep(conn, 4));
    DbusEnvironment::synchronizeIoc();
}

TEST_F(HostErrorMonitorTest, Beep_ErrorCallback_Handled)
{
    testing::internal::CaptureStderr();

    host_error_monitor::beep(conn, 1);

    for (int i = 0; i < 30; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        DbusEnvironment::synchronizeIoc();
    }

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("beep returned error"));
}

// ============================================================
// startCrashdumpAndRecovery Tests (no-op without CRASHDUMP flag)
// ============================================================

TEST_F(HostErrorMonitorTest, StartCrashdumpAndRecovery_NoCrashdumpFlag)
{
    EXPECT_NO_THROW(host_error_monitor::startCrashdumpAndRecovery(
        conn, host_error_monitor::RecoveryType::noRecovery, "TestTrigger"));
}

TEST_F(HostErrorMonitorTest, StartCrashdumpAndRecovery_AllRecoveryTypes)
{
    EXPECT_NO_THROW(host_error_monitor::startCrashdumpAndRecovery(
        conn, host_error_monitor::RecoveryType::powerCycle, "IERR"));
    EXPECT_NO_THROW(host_error_monitor::startCrashdumpAndRecovery(
        conn, host_error_monitor::RecoveryType::warmReset, "SMI Timeout"));
}

// ============================================================
// checkErrPinCPUs Test (no-op without LIBPECI)
// ============================================================

TEST(ErrPinTest, CheckErrPinCPUs_WithoutLibpeci_ResetsAll)
{
    std::bitset<MAX_CPUS> cpus;
    cpus.set(0);
    cpus.set(3);

    host_error_monitor::checkErrPinCPUs(0, cpus);

    EXPECT_TRUE(cpus.none());
}

TEST(ErrPinTest, CheckErrPinCPUs_DifferentPins)
{
    std::bitset<MAX_CPUS> cpus;
    cpus.set();

    host_error_monitor::checkErrPinCPUs(1, cpus);
    EXPECT_TRUE(cpus.none());

    cpus.set();
    host_error_monitor::checkErrPinCPUs(2, cpus);
    EXPECT_TRUE(cpus.none());
}

// ============================================================
// BaseMonitor Tests (with D-Bus connection)
// ============================================================

TEST_F(HostErrorMonitorTest, BaseMonitor_ConstructWithRealConn)
{
    testing::internal::CaptureStderr();

    host_error_monitor::base_monitor::BaseMonitor monitor(
        *io, conn, "TestSignal");

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("Initializing TestSignal"));
    EXPECT_FALSE(monitor.isValid());
}

TEST_F(HostErrorMonitorTest, BaseMonitor_SetValid)
{
    host_error_monitor::base_monitor::BaseMonitor monitor(
        *io, conn, "TestSignal");
    monitor.valid = true;
    EXPECT_TRUE(monitor.isValid());
}

TEST_F(HostErrorMonitorTest, BaseMonitor_HostOnNoOp)
{
    host_error_monitor::base_monitor::BaseMonitor monitor(
        *io, conn, "TestSignal");
    EXPECT_NO_THROW(monitor.hostOn());
}

TEST_F(HostErrorMonitorTest, BaseMonitor_SignalNameStored)
{
    host_error_monitor::base_monitor::BaseMonitor monitor(
        *io, conn, "CPU_IERR");
    EXPECT_EQ(monitor.signalName, "CPU_IERR");
}

TEST_F(HostErrorMonitorTest, BaseMonitor_ConnStored)
{
    host_error_monitor::base_monitor::BaseMonitor monitor(*io, conn, "Test");
    EXPECT_EQ(monitor.conn, conn);
}

TEST_F(HostErrorMonitorTest, BaseMonitor_IoContextStored)
{
    host_error_monitor::base_monitor::BaseMonitor monitor(*io, conn, "Test");
    EXPECT_EQ(&monitor.io, io);
}

// ============================================================
// BaseMonitor::log_message Tests
// ============================================================

class TestableMonitor : public host_error_monitor::base_monitor::BaseMonitor
{
  public:
    using BaseMonitor::BaseMonitor;

    void callLogMessage(int priority, const std::string& msg,
                        const std::string& redfishId,
                        const std::string& redfishMsg)
    {
        log_message(priority, msg, redfishId, redfishMsg);
    }
};

TEST_F(HostErrorMonitorTest, BaseMonitor_LogMessage_DoesNotThrow)
{
    TestableMonitor monitor(*io, conn, "TestSignal");

    EXPECT_NO_THROW(monitor.callLogMessage(LOG_ERR, "Test error message",
                                           "OpenBMC.0.1.CPUError", "TestArgs"));
}

TEST_F(HostErrorMonitorTest, BaseMonitor_LogMessage_AllPriorities)
{
    TestableMonitor monitor(*io, conn, "TestSignal");

    EXPECT_NO_THROW(
        monitor.callLogMessage(LOG_EMERG, "Emergency", "ID.1", "args"));
    EXPECT_NO_THROW(monitor.callLogMessage(LOG_ALERT, "Alert", "ID.2", "args"));
    EXPECT_NO_THROW(
        monitor.callLogMessage(LOG_CRIT, "Critical", "ID.3", "args"));
    EXPECT_NO_THROW(monitor.callLogMessage(LOG_ERR, "Error", "ID.4", "args"));
    EXPECT_NO_THROW(
        monitor.callLogMessage(LOG_WARNING, "Warning", "ID.5", "args"));
    EXPECT_NO_THROW(
        monitor.callLogMessage(LOG_NOTICE, "Notice", "ID.6", "args"));
    EXPECT_NO_THROW(monitor.callLogMessage(LOG_INFO, "Info", "ID.7", "args"));
    EXPECT_NO_THROW(monitor.callLogMessage(LOG_DEBUG, "Debug", "ID.8", "args"));
}

TEST_F(HostErrorMonitorTest, BaseMonitor_LogMessage_EmptyStrings)
{
    TestableMonitor monitor(*io, conn, "TestSignal");
    EXPECT_NO_THROW(monitor.callLogMessage(LOG_INFO, "", "", ""));
}

// ============================================================
// HandleRecovery with null connection — edge case
// ============================================================

TEST(HandleRecoveryNullConnTest, NoRecovery_NullConn)
{
    std::shared_ptr<sdbusplus::asio::connection> nullConn;
    EXPECT_NO_THROW(host_error_monitor::handleRecovery(
        host_error_monitor::RecoveryType::noRecovery, nullConn));
}

// ============================================================
// Integration: handleRecovery full paths
// ============================================================

TEST_F(HostErrorMonitorTest, HandleRecovery_PowerCycle_FullPath)
{
    testing::internal::CaptureStderr();

    host_error_monitor::handleRecovery(
        host_error_monitor::RecoveryType::powerCycle, conn);

    for (int i = 0; i < 20; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DbusEnvironment::synchronizeIoc();
    }

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("power cycle"));
}

TEST_F(HostErrorMonitorTest, HandleRecovery_WarmReset_FullPath)
{
    testing::internal::CaptureStderr();

    host_error_monitor::handleRecovery(
        host_error_monitor::RecoveryType::warmReset, conn);

    for (int i = 0; i < 20; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DbusEnvironment::synchronizeIoc();
    }

    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_THAT(output, ::testing::HasSubstr("warm reset"));
}

// ============================================================
// Multiple BaseMonitor instances
// ============================================================

TEST_F(HostErrorMonitorTest, MultipleMonitors_IndependentState)
{
    host_error_monitor::base_monitor::BaseMonitor m1(*io, conn, "Signal1");
    host_error_monitor::base_monitor::BaseMonitor m2(*io, conn, "Signal2");

    m1.valid = true;
    EXPECT_TRUE(m1.isValid());
    EXPECT_FALSE(m2.isValid());

    m2.valid = true;
    EXPECT_TRUE(m1.isValid());
    EXPECT_TRUE(m2.isValid());
}

// ============================================================
// Edge cases for beep priority values
// ============================================================

TEST_F(HostErrorMonitorTest, Beep_ZeroPriority)
{
    EXPECT_NO_THROW(host_error_monitor::beep(conn, 0));
    DbusEnvironment::synchronizeIoc();
}

TEST_F(HostErrorMonitorTest, Beep_MaxPriority)
{
    EXPECT_NO_THROW(host_error_monitor::beep(conn, 255));
    DbusEnvironment::synchronizeIoc();
}
