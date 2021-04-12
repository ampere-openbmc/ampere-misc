/*
// Copyright 2022 Ampere Computing LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#include "config.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/container/flat_map.hpp>

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <regex>
#include <chrono>
#include <thread>

bool addSoCInterfaces = false;
// setup connection to dbus
boost::asio::io_service io;
auto conn = std::make_shared<sdbusplus::asio::connection>(io);
auto objectServer = sdbusplus::asio::object_server(conn);
bool slave_present = false;

std::shared_ptr<sdbusplus::asio::dbus_interface> hostIntS0 = nullptr;
std::shared_ptr<sdbusplus::asio::dbus_interface> hostIntS1 = nullptr;
std::shared_ptr<sdbusplus::asio::dbus_interface> hostPresenceS0 = nullptr;
std::shared_ptr<sdbusplus::asio::dbus_interface> hostPresenceS1 = nullptr;
const char* script_slave_present = "/usr/sbin/ampere-slave-present.sh";
constexpr const char* cpuInventoryPath =
    "/xyz/openbmc_project/inventory/system/chassis/motherboard";

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe)
    {
        std::cerr << "popen() failed!" << std::endl;
        return "failed";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
        std::cerr << " result : " << result << std::endl;
    }
    return result;
}

sdbusplus::bus::match::match
    startHostStateMonitor(std::shared_ptr<sdbusplus::asio::connection> conn)
{
    auto startEventMatcherCallback = [](sdbusplus::message::message& msg) {
        boost::container::flat_map<std::string, std::variant<std::string>>
            propertiesChanged;
        std::string interfaceName;

        msg.read(interfaceName, propertiesChanged);
        if (propertiesChanged.empty())
        {
            return;
        }

        std::string event = propertiesChanged.begin()->first;
        auto variant =
            std::get_if<std::string>(&propertiesChanged.begin()->second);

        if (event.empty() || variant == nullptr)
        {
            return;
        }
        if (event == "CurrentHostState")
        {
            if (*variant == "xyz.openbmc_project.State.Host.HostState.Running")
            {
                try
                {
                    if (!addSoCInterfaces)
                    {
                        /*
                        * After FW_BOOT_OK go high, CurrentHostState change to
                        * Running. MPro takes some time to be ready for
                        * responding PLDM commands. This delay makes sure that
                        * the MPRo terminus are only added to MCTP D-Bus
                        * interface when the MPRos are ready for PLDM commands.
                        */
                        std::cerr << "DELAY_BEFORE_ADD_TERMINUS : "
                            << static_cast<int>(DELAY_BEFORE_ADD_TERMINUS)
                            << std::endl;
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(
                                static_cast<int>(DELAY_BEFORE_ADD_TERMINUS)
                                )
                            );
                        std::cerr << "Adding interface S0" << std::endl;
                        hostIntS0 = objectServer.add_interface(
                            "/xyz/openbmc_project/mctp/2/20",
                            "xyz.openbmc_project.MCTP.Endpoint");
                        size_t val = 2;
                        hostIntS0->register_property("NetworkId", val);
                        val = 20;
                        hostIntS0->register_property("EID", val);
                        std::vector<uint8_t> messTypes = {0x01};
                        hostIntS0->register_property("SupportedMessageTypes", messTypes);
                        hostIntS0->initialize();
                        std::string socName = "CPU_1";
                        hostPresenceS0 = objectServer.add_interface(
                            cpuInventoryPath + std::string("/") + socName,
                            "xyz.openbmc_project.Inventory.Item");
                        hostPresenceS0->register_property("PrettyName", socName);
                        hostPresenceS0->register_property("Present", true);
                        hostPresenceS0->initialize();


                        if (slave_present)
                        {
                            std::cerr << "Adding interface S1" << std::endl;
                            hostIntS1 = objectServer.add_interface(
                                "/xyz/openbmc_project/mctp/2/22",
                                "xyz.openbmc_project.MCTP.Endpoint");
                            val = 2;
                            hostIntS1->register_property("NetworkId", val);
                            val = 22;
                            hostIntS1->register_property("EID", val);
                            hostIntS1->register_property("SupportedMessageTypes", messTypes);
                            hostIntS1->initialize();
                            socName = "CPU_2";
                            hostPresenceS1 = objectServer.add_interface(
                                cpuInventoryPath + std::string("/") + socName,
                                "xyz.openbmc_project.Inventory.Item");
                            hostPresenceS1->register_property("PrettyName", socName);
                            hostPresenceS1->register_property("Present", true);
                            hostPresenceS1->initialize();
                        }
                        addSoCInterfaces = true;
                    }
                }
                catch(const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                } 
            }
            else
            {
                if (addSoCInterfaces)
                {
                    try
                    {
                        std::cerr << "Removing interface S0" << std::endl;
                        objectServer.remove_interface(hostIntS0);
                        hostIntS0 = nullptr;
                        objectServer.remove_interface(hostPresenceS0);
                        hostPresenceS0 = nullptr;
                        if (slave_present)
                        {
                            std::cerr << "Removing interface S1" << std::endl;
                            objectServer.remove_interface(hostIntS1);
                            hostIntS1 = nullptr;
                            objectServer.remove_interface(hostPresenceS1);
                            hostPresenceS1 = nullptr;
                        }
                        addSoCInterfaces = false;
                    }
                    catch(const std::exception& e)
                    {
                        std::cerr << e.what() << '\n';
                    }
                }
            }
        }
    };

    sdbusplus::bus::match::match startEventMatcher(
        static_cast<sdbusplus::bus::bus&>(*conn),
        "type='signal',interface='org.freedesktop.DBus.Properties',member='"
        "PropertiesChanged',arg0namespace='xyz.openbmc_project.State.Host'",
        std::move(startEventMatcherCallback));

    return startEventMatcher;
}

int main(int argc, char** argv)
{   
    conn->request_name(
            "xyz.openbmc_project.MCTP.Control");
    objectServer.add_manager("/xyz/openbmc_project/mctp");
    slave_present = false;
    try
    {
        std::size_t found = exec(script_slave_present).find("1");
        if (found!=std::string::npos)
        {
            slave_present= true;
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    std::cerr << "Slave present : " << slave_present << std::endl;
    sdbusplus::bus::match::match hostStateMon = startHostStateMonitor(conn);
    io.run();

    return 0;
}