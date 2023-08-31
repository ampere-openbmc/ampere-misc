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
bool s1_ready = false;
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

constexpr const char *hostStatePath = "/xyz/openbmc_project/state/host0";
constexpr const char *hostStateInterface = "xyz.openbmc_project.State.Host";
constexpr const char *hostStateProp = "CurrentHostState";
constexpr const char *hostStateOff =
	"xyz.openbmc_project.State.Host.HostState.Off";
constexpr const char *hostStateOn =
	"xyz.openbmc_project.State.Host.HostState.On";
constexpr const char *hostStateRunning =
	"xyz.openbmc_project.State.Host.HostState.Running";

constexpr auto MAPPER_BUSNAME = "xyz.openbmc_project.ObjectMapper";
constexpr auto MAPPER_PATH = "/xyz/openbmc_project/object_mapper";
constexpr auto MAPPER_INTERFACE = "xyz.openbmc_project.ObjectMapper";
constexpr auto PROPERTY_INTERFACE = "org.freedesktop.DBus.Properties";

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

std::string getService(sdbusplus::bus_t &bus, std::string path,
		       std::string interface)
{
	auto mapper = bus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
					  MAPPER_INTERFACE, "GetObject");

	mapper.append(path, std::vector<std::string>({ interface }));

	std::vector<std::pair<std::string, std::vector<std::string> > >
		mapperResponse;

	try {
		auto mapperResponseMsg = bus.call(mapper);

		mapperResponseMsg.read(mapperResponse);
		if (mapperResponse.empty()) {
			std::cerr << "Error no matching service " << std::endl;
			return "";
		}
	} catch (const sdbusplus::exception_t &e) {
		std::cerr << "Error no matching service " << std::endl;
		return "";
	}

	return mapperResponse.begin()->first;
}

std::string getProperty(sdbusplus::bus_t &bus, const std::string &path,
			const std::string &interface,
			const std::string &propertyName)
{
	std::variant<std::string> property;
	std::string service = getService(bus, path, interface);
	if (service == "") {
		return "";
	}

	auto method = bus.new_method_call(service.c_str(), path.c_str(),
					  PROPERTY_INTERFACE, "Get");

	method.append(interface, propertyName);

	try {
		auto reply = bus.call(method);
		reply.read(property);
	} catch (const sdbusplus::exception_t &e) {
		std::cerr << "Error in property Get " << propertyName
			  << std::endl;
		throw;
	}

	if (std::get<std::string>(property).empty()) {
		std::cerr << "Error reading property response for "
			  << propertyName << std::endl;
		throw std::runtime_error("Error reading property response");
	}

	return std::get<std::string>(property);
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

void removeCpuPresentInterfaces()
{
	if (addSoCInterfaces) {
		try {
			if (s0_present && hostPresenceS0 != nullptr) {
				std::cerr << "Removing interface S0"
					  << std::endl;
				objectServer.remove_interface(hostPresenceS0);
				hostPresenceS0 = nullptr;
			}

			if (s1_present && hostPresenceS1 != nullptr) {
				std::cerr << "Removing interface S1"
					  << std::endl;
				objectServer.remove_interface(hostPresenceS1);
				hostPresenceS1 = nullptr;
			}
			addSoCInterfaces = false;
		} catch (const std::exception &e) {
			std::cerr << e.what() << '\n';
		}
	}
}

sdbusplus::bus::match::match
startHostStateMonitor(std::shared_ptr<sdbusplus::asio::connection> conn)
{
	auto startEventMatcherCallback = [](sdbusplus::message::message &msg) {
		boost::container::flat_map<std::string,
					   std::variant<std::string> >
			propertiesChanged;
		std::string interfaceName;

		msg.read(interfaceName, propertiesChanged);
		if (propertiesChanged.empty()) {
			return;
		}

		std::string event = propertiesChanged.begin()->first;
		auto variant = std::get_if<std::string>(
			&propertiesChanged.begin()->second);

		if (event.empty() || variant == nullptr) {
			return;
		}
		if (event == hostStateProp) {
			if (*variant == hostStateRunning) {
				addCpuPresentInterfaces();
			} else {
				removeCpuPresentInterfaces();
			}
		}
	};

	sdbusplus::bus::match::match startEventMatcher(
		static_cast<sdbusplus::bus::bus &>(*conn),
		"type='signal',interface='org.freedesktop.DBus.Properties',member='"
		"PropertiesChanged',arg0namespace='xyz.openbmc_project.State.Host'",
		std::move(startEventMatcherCallback));

	return startEventMatcher;
}

int main(int argc, char **argv)
{
	conn->request_name("xyz.openbmc_project.Ampere.Cpu");
	objectServer.add_manager("/xyz/openbmc_project/inventory");

	auto state =
		getProperty(static_cast<sdbusplus::bus::bus &>(*conn),
			    hostStatePath, hostStateInterface, hostStateProp);
	if (state != "") {
		if (state == hostStateRunning) {
			addCpuPresentInterfaces();
		} else {
			removeCpuPresentInterfaces();
		}
	}

	sdbusplus::bus::match::match hostStateMon = startHostStateMonitor(conn);
	io.run();

	return 0;
}
