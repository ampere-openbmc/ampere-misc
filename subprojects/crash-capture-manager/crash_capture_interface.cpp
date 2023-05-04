#include "config.h"
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <phosphor-logging/lg2.hpp>
#include <string>
#include <iostream>
#include "bert.hpp"
#include "utils.hpp"
#include "crash_capture_interface.hpp"

namespace crashcapture
{

PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;

CrashCapture::CrashCapture(sdbusplus::bus::bus& bus, const char* objPath) :
    CrashCaptureInherit(bus, objPath), bus(bus), objectPath(objPath)
{
    handleDbusEventSignal();
    initBertHostOnEvent();
    bertClaimSPITimeOut();
    handleBmcUnavailable();
};

CrashCaptureBase::TriggerAction CrashCapture::triggerActions(TriggerAction value)
{
    info("Setting the TriggerActions field to {VALUE}", "VALUE", value);

    executeTransition(value);

    return CrashCaptureInherit::triggerActions(value, false);
}

bool CrashCapture::triggerUE(bool value)
{
    info("Setting the triggerUE field to {VALUE}", "VALUE", value);
    return CrashCaptureInherit::triggerUE(value, false);
}

bool CrashCapture::triggerProcess(bool value)
{
    info("Setting the triggerProcess field to {VALUE}", "VALUE", value);
    if(value && isBertTrigger)
    {
        bertHandler(bus, HOST_ON);
        isBertTrigger = false;
        maskPowerControl(false);
        bertPowerLockTimer->stop();
        CrashCaptureInherit::triggerActions(CrashCaptureInherit::TriggerAction::Done);
    }

    return CrashCaptureInherit::triggerProcess(value, false);
}

void CrashCapture::executeTransition(TriggerAction value)
{
    if (value == CrashCaptureInherit::TriggerAction::Bert)
    {
        info("BERT is trigger");
        isBertTrigger = true;
        maskPowerControl(true);
        bertPowerLockTimer->start(std::chrono::milliseconds(BERT_POWER_LOCK_TIMEOUT));
    }
    else if (value == CrashCaptureInherit::TriggerAction::Diagnostic)
    {
        info("Diagnostic is trigger");
    }
    else if (value == CrashCaptureInherit::TriggerAction::Done)
    {
        info("Crash Capture Trigger is done");
    }
    else
    {
        info("None");
    }
}

void CrashCapture::handleDbusEventSignal()
{
    handleNumericSensorEventSignal();
}

void CrashCapture::handleNumericSensorEventSignal()
{
    numericSensorEventSignal = std::make_unique<sdbusplus::bus::match_t>(
        bus, sdbusplus::bus::match::rules::type::signal() +
        sdbusplus::bus::match::rules::member("NumericSensorEvent") +
        sdbusplus::bus::match::rules::path("/xyz/openbmc_project/pldm") +
        sdbusplus::bus::match::rules::interface(
            "xyz.openbmc_project.PLDM.Event"),
        [&](sdbusplus::message::message& msg) {
            try
            {
                uint8_t tid{};
                uint16_t sensorId{};
                uint8_t eventState{};
                uint8_t preEventState{};
                uint8_t sensorDataSize{};
                uint32_t presentReading{};

                /*
                 * Read the information of event
                 */
                msg.read(tid, sensorId, eventState, preEventState,
                         sensorDataSize, presentReading);

                /*
                 * Handle Overall sensor
                 */
                if (sensorId == 175)
                {
                    handleBertHostBootEvent(tid, sensorId, presentReading);
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "handleNumericSensorEventSignal failed\n"
                          << e.what() << std::endl;
            }
        });
}

void CrashCapture::handleBertHostBootEvent([[maybe_unused]]uint8_t tid,
                [[maybe_unused]]uint16_t sensorId, uint32_t presentReading)
{
    bool failFlg = false;
    std::stringstream strStream;
    uint8_t byte3 = (presentReading & 0x000000ff);
    uint8_t byte2 = (presentReading & 0x0000ff00) >> 8;

    // Sensor report action is fail
    if (0x81 == byte2)
    {
        failFlg = true;
    }

    // Handle DDR training fail
    if ((0x96 == byte3) || (0x99 == byte3))
    {
        failFlg = true;
    }

    /* Handler BERT flow in case host on. BMC should handshake
     * with Host to accessing SPI-NOR when UEFI boot complete.
     */
    if (failFlg)
    {
        hostStatus = HOST_FAILURE;
    }
    else
    {
        hostStatus = HOST_BOOTING;
    }
    if ((byte3 == 0x03) && (byte2 == 0x10) && checkBertFlag)
    {
        info("Host is on, UEFI boot complete. Read SPI to check valid BERT");
        bertHandler(bus, HOST_ON);
        checkBertFlag = false;
        bertHostFailTimer->stop();
    }
}


void CrashCapture::bertHostFailTimeOutHdl(void)
{
    info("Host boot fail. Read BERT");
    checkBertFlag = false;
    bertHandler(bus, HOST_ON);
}

void CrashCapture::bertHostOnTimeOutHdl(void)
{
    if (hostStatus == HOST_COMPLETE)
    {
        info("UEFI already boot completed. Read BERT");
        bertHostFailTimer->stop();
        checkBertFlag = false;
        bertHandler(bus, HOST_ON);
    }
}

void CrashCapture::bertPowerLockTimeOutHdl(void)
{
    info("Time out, BERT process is still not completed. Unlock power control");
    maskPowerControl(false);
}

void CrashCapture::initBertHostOnEvent(void)
{
    bertHostOnTimer = std::make_unique<phosphor::Timer>(
                                 [&](void) { bertHostOnTimeOutHdl(); });
    bertHostFailTimer = std::make_unique<phosphor::Timer>(
                                 [&](void) { bertHostFailTimeOutHdl(); });
    bertPowerLockTimer = std::make_unique<phosphor::Timer>(
                                     [&](void) { bertPowerLockTimeOutHdl(); });
}

void CrashCapture::handleBertHostOnEvent(void)
{
    /* Need to delay about 10s to make sure host sent
     * boot progress event to BMC
     */
    bertHostOnTimer->start(std::chrono::milliseconds(BERT_HOSTON_TIMEOUT));
    /* Check bert after host boot fails 120s timeout */
    bertHostFailTimer->start(std::chrono::milliseconds(BERT_HOSTFAIL_TIMEOUT));
    checkBertFlag = true;
    hostStatus = HOST_COMPLETE;
}

void CrashCapture::handleBmcUnavailable(void)
{
    constexpr auto hostStateSrv =
                   "xyz.openbmc_project.State.Host";
    constexpr auto hostStateInterface =
                   "xyz.openbmc_project.State.Host";
    constexpr auto hostStatePath =
                   "/xyz/openbmc_project/state/host0";

    auto propVal = crashcapture::utils::getDbusProperty(bus, hostStateSrv,
                   hostStatePath, hostStateInterface, "CurrentHostState");
    const auto& currHostState = std::get<std::string>(propVal);
    if ((currHostState == "xyz.openbmc_project.State.Host.HostState.Off"))
    {
        info("Host is off. Read SPI to check valid BERT");
        bertHandler(bus, HOST_OFF);
    }
    else if ((currHostState == "xyz.openbmc_project.State.Host.HostState.Running"))
    {
        handleBertHostOnEvent();
    }
    else
    {
        info("Host is in unavailable state");
    }
}

} // namespace crashcapture
