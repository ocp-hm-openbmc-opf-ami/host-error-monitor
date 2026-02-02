/*
// Copyright (c) 2021 Intel Corporation
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
#include <error_monitors/base_gpio_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace host_error_monitor::mem_thermtrip_monitor
{
class MemThermtripMonitor :
    public host_error_monitor::base_gpio_monitor::BaseGPIOMonitor
{
    const static host_error_monitor::base_gpio_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_monitor::AssertValue::lowAssert;
    std::shared_ptr<sdbusplus::asio::dbus_interface> assertInterface;
    size_t cpuNum;

    void logEvent() override
    {
        std::string cpuNumber = "CPU " + std::to_string(cpuNum);
        std::string msg = cpuNumber + " Memory Thermal trip.";

        std::string severity =
            "xyz.openbmc_project.Logging.Entry.Level.Critical";
        auto bus = sdbusplus::bus::new_default_system();
        sdbusplus::message::message m = bus.new_method_call(
            "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
            "xyz.openbmc_project.Logging.Create", "Create");
        std::string messageID = "OpenBMC.0.1.CPUError";
        std::string arg = "IERR";
        std::string journalMsg = messageID + "," + arg;

        std::map<std::string, std::string> additionalData;
        additionalData["REDFISH_MESSAGE_ID"] = messageID;
        additionalData["REDFISH_MESSAGE_ARGS"] = arg;
        m.append(journalMsg, severity, additionalData);
        try
        {
            bus.call(m);
        }
        catch (const sdbusplus::exception_t& e)
        {
            std::cerr << "Failed to create log entry: " << e.what()
                      << std::endl;
        }
        assertInterface->set_property("Asserted", true);
    }

    void deassertHandler() override
    {
        assertInterface->set_property("Asserted", false);
    }

  public:
    MemThermtripMonitor(boost::asio::io_context& io,
                        std::shared_ptr<sdbusplus::asio::connection> conn,
                        const std::string& signalName, const size_t cpuNum,
                        const std::string& customName = std::string()) :
        BaseGPIOMonitor(io, conn, signalName, assertValue), cpuNum(cpuNum)
    {
        sdbusplus::asio::object_server server =
            sdbusplus::asio::object_server(conn);
        std::string objectName = customName.empty() ? signalName : customName;
        std::string path =
            "/xyz/openbmc_project/host_error_monitor/processor/" + objectName;

        assertInterface = server.add_interface(
            path, "xyz.openbmc_project.HostErrorMonitor.Processor.ThermalTrip");
        assertInterface->register_property("Asserted", false);
        assertInterface->initialize();
        if (valid)
        {
            startMonitoring();
        }
    }
};
} // namespace host_error_monitor::mem_thermtrip_monitor
