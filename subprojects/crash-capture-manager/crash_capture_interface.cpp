#include "config.h"

#include "crash_capture_interface.hpp"

#include "bert.hpp"
#include "utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <iostream>
#include <string>

namespace crashcapture
{
PHOSPHOR_LOG2_USING;
using namespace sdbusplus::bus::match::rules;

CrashCapture::CrashCapture(sdbusplus::bus::bus& bus, const char* objPath) :
    CrashCaptureInherit(bus, objPath), bus(bus), objectPath(objPath)
{
    handleBootProgressMatch();
    initBertHostOnEvent();
    bertClaimSPITimeOut();
    handleBmcUnavailable();
};

CrashCaptureBase::TriggerAction
    CrashCapture::triggerActions(TriggerAction value)
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
    if (value)
    {
        maskPowerControl(true);
        bertPowerLockTimer->start(
            std::chrono::milliseconds(BERT_POWER_LOCK_TIMEOUT));
        bertHandler(bus, HOST_OFF);
        maskPowerControl(false);
        bertPowerLockTimer->stop();
        CrashCaptureInherit::triggerActions(
            CrashCaptureInherit::TriggerAction::Done);
    }

    return CrashCaptureInherit::triggerProcess(value, false);
}

void CrashCapture::executeTransition(TriggerAction value)
{
    if (value == CrashCaptureInherit::TriggerAction::Bert)
    {
        info("BERT is trigger");
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

void CrashCapture::handleBootProgressMatch()
{
    constexpr auto bootUEFICompleted =
        "xyz.openbmc_project.State.Boot.Progress.ProgressStages.OSStart";
    bootProgessMatch = std::make_unique<sdbusplus::bus::match_t>(
        bus,
        propertiesChanged("/xyz/openbmc_project/state/host0",
                          "xyz.openbmc_project.State.Boot.Progress"),
        [&](sdbusplus::message::message& msg) {
            try
            {
                std::string statusInterface;
                std::map<std::string, std::variant<std::string>> msgData;
                msg.read(statusInterface, msgData);
                if (onceTimeReadBERT)
                {
                    return;
                }
                auto propertyMap = msgData.find("BootProgress");
                if (propertyMap != msgData.end())
                {
                    // Extract the BootProgress
                    auto& bootProgress =
                        std::get<std::string>(propertyMap->second);
                    if (bootProgress == bootUEFICompleted)
                    {
                        info("UEFI boot completed. Read BERT");
                        onceTimeReadBERT = true;
                        bertHostFailTimer->stop();
                        bertHandler(bus, HOST_ON);
                        return;
                    }
                }
            }
            catch (const std::exception& e)
            {
                error("Failed to match BootProgress property changed."
                      "ERROR = {ERR_EXCEP}",
                      "ERR_EXCEP", e.what());
            }
        });
}

void CrashCapture::bertHostFailTimeOutHdl(void)
{
    info("Host boot fail. Read BERT");
    bertHostFailTimer->stop();
    bertHandler(bus, HOST_ON);
}

void CrashCapture::bertPowerLockTimeOutHdl(void)
{
    info("Time out, BERT process is still not completed. Unlock power control");
    maskPowerControl(false);
}

void CrashCapture::initBertHostOnEvent(void)
{
    bertHostFailTimer = std::make_unique<sdbusplus::Timer>([&](void) {
        bertHostFailTimeOutHdl();
    });
    bertPowerLockTimer = std::make_unique<sdbusplus::Timer>([&](void) {
        bertPowerLockTimeOutHdl();
    });
}

void CrashCapture::handleBertHostOnEvent(void)
{
    constexpr auto bootStateSrv = "xyz.openbmc_project.State.Host";
    constexpr auto bootStateInterface =
        "xyz.openbmc_project.State.Boot.Progress";
    constexpr auto bootStatePath = "/xyz/openbmc_project/state/host0";
    constexpr auto bootUEFICompleted =
        "xyz.openbmc_project.State.Boot.Progress.ProgressStages.OSStart";

    try
    {
        auto propVal = crashcapture::utils::getDbusProperty(
            bus, bootStateSrv, bootStatePath, bootStateInterface,
            "BootProgress");
        const auto& currBootProgress = std::get<std::string>(propVal);

        if (currBootProgress == bootUEFICompleted)
        {
            info("UEFI has already boot completed. Read BERT");
            onceTimeReadBERT = true;
            bertHandler(bus, HOST_ON);
            return;
        }
    }
    catch (const std::exception& e)
    {
        error("Failed to get Boot Value. ERROR = {ERR_EXCEP}", "ERR_EXCEP",
              e.what());
    }

    /* 120 seconds timer for checking host boot fails */
    bertHostFailTimer->start(std::chrono::milliseconds(BERT_HOSTFAIL_TIMEOUT));
}

void CrashCapture::handleBmcUnavailable(void)
{
    constexpr auto hostStateSrv = "xyz.openbmc_project.State.Host";
    constexpr auto hostStateInterface = "xyz.openbmc_project.State.Host";
    constexpr auto hostStatePath = "/xyz/openbmc_project/state/host0";

    try
    {
        auto propVal = crashcapture::utils::getDbusProperty(
            bus, hostStateSrv, hostStatePath, hostStateInterface,
            "CurrentHostState");
        const auto& currHostState = std::get<std::string>(propVal);
        if ((currHostState == "xyz.openbmc_project.State.Host.HostState.Off"))
        {
            info("Host is off. Read SPI to check valid BERT");
            bertHandler(bus, HOST_OFF);
        }
        else if ((currHostState ==
                  "xyz.openbmc_project.State.Host.HostState.Running"))
        {
            handleBertHostOnEvent();
        }
        else
        {
            info("Host is in unavailable state");
        }
    }
    catch (const std::exception& e)
    {
        error("Failed to get CurrentHostState. ERROR = {ERR_EXCEP}",
              "ERR_EXCEP", e.what());
    }
}

} // namespace crashcapture
