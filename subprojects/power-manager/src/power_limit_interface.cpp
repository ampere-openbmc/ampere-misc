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

#include "power_limit_interface.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>

namespace phosphor
{
namespace Control
{
namespace Power
{
namespace Limit
{

PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

PowerLimit::PowerLimit(sdbusplus::bus_t& bus, const char* path,
                       const sdeventplus::Event& event, std::string totalPwrSrv,
                       std::string totalPwrObjectPath,
                       std::string totalPwrItf) :
    LimitItf(bus, path),
    bus(bus), objectPath(path), event(event), totalPwrSrv(totalPwrSrv),
    totalPwrObjectPath(totalPwrObjectPath), totalPwrItf(totalPwrItf),
    correctTimer(event, std::bind(&PowerLimit::callBackCorrectTimer, this)),
    samplingTimer(event, std::bind(&PowerLimit::callBackSamplingTimer, this))
{
    std::ifstream oldCfgFile(oldParametersCfgFile.c_str(),
                             std::ios::in | std::ios::binary);

    /*
     * Read the old configuration
     */
    if (oldCfgFile.is_open())
    {
        oldCfgFile.read((char*)(&currentCfg), sizeof(paramsCfg));
        active(currentCfg.actFlag);
        exceptionAction(currentCfg.exceptAct);
        powerLimit(currentCfg.powerLimit);
        correctionTime(currentCfg.correctTime);
        samplingPeriod(currentCfg.samplePeriod);
    }
    else
    {
        currentCfg.actFlag = LimitItf::active();
        currentCfg.exceptAct = LimitItf::exceptionAction();
        currentCfg.powerLimit = LimitItf::powerLimit();
        currentCfg.correctTime = LimitItf::correctionTime();
        currentCfg.samplePeriod = LimitItf::samplingPeriod();
    }
    oldCfgFile.close();
}

bool PowerLimit::active(bool value)
{
    // Get current active status
    bool currentAct = LimitItf::active();

    /*
     * The sampling timer should be enabled only the active state is changed
     * from "false" to "true"
     */
    if (true == value && value != currentAct)
    {
        /*
         * Enable sampling timer
         */
        uint16_t samplePeriod = LimitItf::samplingPeriod();
        samplePeriod =
            (samplePeriod < minSamplPeriod) ? minSamplPeriod : samplePeriod;
        samplingTimer.restart(std::chrono::seconds(samplePeriod));
    }
    else if (false == value && value != currentAct)
    {
        /*
         * Disable sampling and correction timers
         */
        samplingTimer.restart(std::nullopt);
        correctTimer.restart(std::nullopt);
    }

    currentCfg.actFlag = value;
    writeCurrentCfg();

    return LimitItf::active(value, false);
}

uint16_t PowerLimit::powerLimit(uint16_t value)
{
    currentCfg.powerLimit = value;
    writeCurrentCfg();

    return LimitItf::powerLimit(value, false);
}

LimitClass::ExceptionActions PowerLimit::exceptionAction(ExceptionActions value)
{
    currentCfg.exceptAct = value;
    writeCurrentCfg();

    return LimitItf::exceptionAction(value, false);
}

uint32_t PowerLimit::correctionTime(uint32_t value)
{
    currentCfg.correctTime = value;
    writeCurrentCfg();

    return LimitItf::correctionTime(value, false);
}

uint16_t PowerLimit::samplingPeriod(uint16_t value)
{
    value = (value < minSamplPeriod) ? minSamplPeriod : value;
    samplingTimer.setInterval(std::chrono::seconds(value));

    currentCfg.samplePeriod = value;
    writeCurrentCfg();

    return LimitItf::samplingPeriod(value, false);
}

void PowerLimit::callBackCorrectTimer()
{
    LimitClass::ExceptionActions currentExcepAct = LimitItf::exceptionAction();

    switch (currentExcepAct)
    {
        case LimitClass::ExceptionActions::NoAction:
            break;
        case LimitClass::ExceptionActions::HardPowerOff:
        {
            // Turn power off and log to SEL
            turnHardPowerOff();
            logSELEvent(true);
            break;
        }
        case LimitClass::ExceptionActions::SELLog:
        {
            // Log to SEL
            logSELEvent(true);
            break;
        }
        default:
        {
            // OEM action - call the service which handle OEM action
            handleOEMExceptionAction();
            break;
        }
    }
}

void PowerLimit::callBackSamplingTimer()
{
    /*
     * If the service which stores total power consumption is valid
     * then compare the power limit and total power consumption
     */
    if (!totalPwrSrv.empty() && !totalPwrObjectPath.empty() &&
        !totalPwrItf.empty())
    {
        /*
         * Request to get the total power consumption
         */
        auto method =
            bus.new_method_call(totalPwrSrv.c_str(), totalPwrObjectPath.c_str(),
                                "org.freedesktop.DBus.Properties", "Get");

        method.append(totalPwrItf.c_str(), "Value");

        try
        {
            std::variant<double> totalPowerVal = 0.0;
            auto response = bus.call(method);
            uint16_t powerLimit = LimitItf::powerLimit();

            response.read(totalPowerVal);

            /*
             * If the total power consumption is greater than power limit then
             * trigger one shot correction timer.
             */
            if (((uint16_t)(std::get<double>(totalPowerVal))) >= powerLimit)
            {
                if (!correctTimer.isEnabled() && !correctTimer.hasExpired())
                {
                    /*
                     * Enable correction timer
                     */
                    uint32_t correctTime = LimitItf::correctionTime();
                    correctTimer.restartOnce(
                        std::chrono::milliseconds(correctTime));
                }
            }
            else
            {
                if (correctTimer.hasExpired())
                {
                    logSELEvent(false);
                }

                /*
                 * Disable correction timer when total power is lower than power
                 * limit
                 */
                correctTimer.restart(std::nullopt);
            }
        }
        catch (const sdbusplus::exception_t& e)
        {
            error("Error when tries to get the total power");
        }
    }
}

void PowerLimit::writeCurrentCfg()
{
    std::ofstream oldCfgFile(oldParametersCfgFile.c_str(),
                             std::ios::out | std::ios::binary);
    if (oldCfgFile.is_open())
    {
        oldCfgFile.write((char*)(&currentCfg), sizeof(paramsCfg));
    }
    oldCfgFile.close();
}

void PowerLimit::logSELEvent(bool assertFlg)
{
    constexpr auto ipmiLoggingService = "xyz.openbmc_project.Logging.IPMI";
    constexpr auto ipmiLoggingObjectPath = "/xyz/openbmc_project/Logging/IPMI";
    constexpr auto ipmiLoggingItf = "xyz.openbmc_project.Logging.IPMI";
    // Indicate that event is: Upper Non-recoverable - going high
    // TODO: update the event to "Limit Exceeded"/"Limit No Exceeded"
    std::vector<uint8_t> evtData = {0x0B, 0xFF, 0xFF};
    uint16_t genID = 0x20;
    auto method = bus.new_method_call(ipmiLoggingService, ipmiLoggingObjectPath,
                                      ipmiLoggingItf, "IpmiSelAdd");
    std::string selMsg = (true == assertFlg) ?
                            "The current power greater than the limit" :
                            "The current power lower than the limit";

    method.append(selMsg.c_str());
    method.append(totalPwrObjectPath.c_str());
    method.append(evtData);
    method.append(assertFlg);
    method.append(genID);

    bus.call_noreply(method);
}

void PowerLimit::turnHardPowerOff()
{
    constexpr auto chassisStateServer = "xyz.openbmc_project.State.Chassis";
    constexpr auto chassisStateObjectPath =
        "/xyz/openbmc_project/state/chassis0";
    constexpr auto chassisStateItf = "xyz.openbmc_project.State.Chassis";
    std::variant<std::string> requestTurnPowerOff =
        "xyz.openbmc_project.State.Chassis.Transition.Off";
    auto method =
        bus.new_method_call(chassisStateServer, chassisStateObjectPath,
                            "org.freedesktop.DBus.Properties", "Set");

    method.append(chassisStateItf);
    method.append("RequestedPowerTransition");
    method.append(requestTurnPowerOff);

    bus.call_noreply(method);
}

void PowerLimit::handleOEMExceptionAction()
{
    constexpr auto SYSTEMD_SERVICE = "org.freedesktop.systemd1";
    constexpr auto SYSTEMD_OBJ_PATH = "/org/freedesktop/systemd1";
    constexpr auto SYSTEMD_INTERFACE = "org.freedesktop.systemd1.Manager";

    std::stringstream serviceStream;
    auto method = bus.new_method_call(SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
                                    SYSTEMD_INTERFACE, "StartUnit");
    auto exceptAction = (uint32_t)LimitItf::exceptionAction();

    serviceStream << "power-limit-oem@" << std::setfill ('0') <<
                     std::setw(sizeof(uint8_t) * 2) << std::hex <<
                     exceptAction << ".service";

    method.append(serviceStream.str(), "replace");

    try
    {
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("Error occur when call power-limit-oem@.service");
    }
}

} // namespace Limit
} // namespace Power
} // namespace Control
} // namespace phosphor