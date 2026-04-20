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

DbusEnvironment::~DbusEnvironment()
{
    if (setUp)
    {
        setUp = false;
        objServer = nullptr;
        bus = nullptr;
    }
}

void DbusEnvironment::SetUp()
{
    if (!setUp)
    {
        setUp = true;
        bus = std::make_shared<sdbusplus::asio::connection>(ioc);
        objServer = std::make_shared<sdbusplus::asio::object_server>(bus);
    }
}

void DbusEnvironment::TearDown()
{
    ioc.poll();
    ioc.restart();
}

boost::asio::io_context& DbusEnvironment::getIoc()
{
    return ioc;
}

std::shared_ptr<sdbusplus::asio::connection> DbusEnvironment::getBus()
{
    return bus;
}

std::shared_ptr<sdbusplus::asio::object_server> DbusEnvironment::getObjServer()
{
    return objServer;
}
