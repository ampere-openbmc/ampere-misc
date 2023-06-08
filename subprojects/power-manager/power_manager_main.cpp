/**
 * Copyright © 2022 Ampere Computing LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "power_manager.hpp"

#include <sdbusplus/bus.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/exception.hpp>

int main(int, char *[])
{
	constexpr auto BUSPATH_PW_MNG =
		"/xyz/openbmc_project/control/power/manager";
	constexpr auto BUSNAME_PW_MNG =
		"xyz.openbmc_project.Control.power.manager";
	auto bus = sdbusplus::bus::new_default();
	auto event = sdeventplus::Event::get_default();

	bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);

	sdbusplus::server::manager_t objManager(bus, BUSPATH_PW_MNG);
	phosphor::Control::Power::Manager::PowerManager powerManager(
		bus, BUSPATH_PW_MNG, event);

	// Add sdbusplus ObjectManager
	bus.request_name(BUSNAME_PW_MNG);

	event.loop();

	return 0;
}