/**
 * Copyright © 2023 Ampere Computing LLC
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

#include <sdbusplus/bus.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/exception.hpp>
#include "crash_capture_interface.hpp"

int main(int, char*[])
{
    constexpr auto BUSPATH_CRASHCAPTURE = "/com/ampere/crashcapture/trigger";
    constexpr auto BUSNAME_CRASHCAPTURE = "com.ampere.CrashCapture.Trigger";
    auto bus = sdbusplus::bus::new_default();
    auto event = sdeventplus::Event::get_default();

    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);

    sdbusplus::server::manager_t objManager(bus, BUSPATH_CRASHCAPTURE);
    crashcapture::CrashCapture CrashCapture(bus, BUSPATH_CRASHCAPTURE);
    // Add sdbusplus ObjectManager
    bus.request_name(BUSNAME_CRASHCAPTURE);

    event.loop();

    return 0;
}
