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
#include <error_monitors/mem_thermtrip_monitor.hpp>

#include <type_traits>

#include <gtest/gtest.h>

namespace host_error_monitor::mem_thermtrip_monitor
{

TEST(MemThermtripMonitorTest, HeaderCompiles)
{
    SUCCEED();
}

TEST(MemThermtripMonitorTest, TypeExists)
{
    static_assert(std::is_class_v<MemThermtripMonitor>);
}

TEST(MemThermtripMonitorTest, DerivedFromBaseGPIOMonitor)
{
    static_assert(std::is_base_of_v<
                  host_error_monitor::base_gpio_monitor::BaseGPIOMonitor,
                  MemThermtripMonitor>);
}

} // namespace host_error_monitor::mem_thermtrip_monitor
