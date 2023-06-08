/**
 * Copyright (C) 2022 Ampere Computing LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nmi_interface.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace dbus
{
namespace nmi
{

	NMI::NMI(sdbusplus::bus_t &bus, const char *path)
		: Interface(bus, path), bus(bus), objectPath(path)
	{
	}

	void NMI::nmi()
	{
		using namespace phosphor::logging;
		constexpr auto SYSTEMD_SERVICE = "org.freedesktop.systemd1";
		constexpr auto SYSTEMD_OBJ_PATH = "/org/freedesktop/systemd1";
		constexpr auto SYSTEMD_INTERFACE =
			"org.freedesktop.systemd1.Manager";

		auto method =
			bus.new_method_call(SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
					    SYSTEMD_INTERFACE, "StartUnit");
		method.append("nmi.service", "replace");
		try {
			bus.call_noreply(method);
		} catch (const sdbusplus::exception_t &e) {
			log<level::ERR>("Error occur when call nmi.service");
		}
	}
} // namespace nmi
} // namespace dbus
