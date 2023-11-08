/*
// Copyright 2023 Ampere Computing LLC
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
bool s0_present = false;
bool s1_present = false;

std::shared_ptr<sdbusplus::asio::dbus_interface> hostPresenceS0 = nullptr;
std::shared_ptr<sdbusplus::asio::dbus_interface> hostPresenceS1 = nullptr;
const char *script_s0_present = "/usr/sbin/ampere_utils host present s0";
const char *script_s1_present = "/usr/sbin/ampere_utils host present s1";
constexpr const char *cpuInventoryPath =
	"/xyz/openbmc_project/inventory/system/chassis/motherboard";

std::string exec(const char *cmd)
{
	std::array<char, 128> buffer;
	std::string result;
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
	if (!pipe) {
		std::cerr << "popen() failed!" << std::endl;
		return "failed";
	}
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		result += buffer.data();
	}
	return result;
}

void checkCpuPresent()
{
	try {
		/* script_s0_present exit with 0 when S1 present */
		std::size_t found = exec(script_s0_present).find("0");
		if (found != std::string::npos) {
			s0_present = true;
		}

		found = exec(script_s1_present).find("0");
		if (found != std::string::npos) {
			s1_present = true;
		}
	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
}

void addCpuPresentInterfaces()
{
	/* Update CPU present status before add Present D-Bus interfaces*/
	s0_present = false;
	s1_present = false;
	checkCpuPresent();

	try {
		if (!addSoCInterfaces) {
			if (s0_present) {
				std::string socName = "CPU_1";
				hostPresenceS0 = objectServer.add_interface(
					cpuInventoryPath + std::string("/") +
						socName,
					"xyz.openbmc_project.Inventory.Item");
				hostPresenceS0->register_property("PrettyName",
								  socName);
				hostPresenceS0->register_property("Present",
								  true);
				hostPresenceS0->initialize();
			}

			if (s1_present) {
				std::string socName = "CPU_2";
				hostPresenceS1 = objectServer.add_interface(
					cpuInventoryPath + std::string("/") +
						socName,
					"xyz.openbmc_project.Inventory.Item");
				hostPresenceS1->register_property("PrettyName",
								  socName);
				hostPresenceS1->register_property("Present",
								  true);
				hostPresenceS1->initialize();
			}
			addSoCInterfaces = true;
		}
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
	}
}

int main(int argc, char **argv)
{
	conn->request_name("xyz.openbmc_project.Ampere.Cpu");
	objectServer.add_manager("/xyz/openbmc_project/inventory");
	addCpuPresentInterfaces();
	io.run();

	return 0;
}
