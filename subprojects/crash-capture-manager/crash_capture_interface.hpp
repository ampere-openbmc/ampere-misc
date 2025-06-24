#pragma once

#include "com/ampere/CrashCapture/Trigger/server.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdbusplus/timer.hpp>

namespace crashcapture
{

using CrashCaptureBase = sdbusplus::com::ampere::CrashCapture::server::Trigger;
using CrashCaptureInherit = sdbusplus::server::object_t<CrashCaptureBase>;
namespace sdbusRule = sdbusplus::bus::match::rules;

/** @class BMC
 *  @brief OpenBMC CrashCapture  management implementation.
 *  @details A concrete implementation for com.Ampere.CrashCapture.Status
 *  DBus API.
 */
class CrashCapture : public CrashCaptureInherit
{
  public:
    CrashCapture() = delete;
    CrashCapture(const CrashCapture&) = delete;
    CrashCapture& operator=(const CrashCapture&) = delete;
    CrashCapture(CrashCapture&&) = delete;
    CrashCapture& operator=(CrashCapture&&) = delete;
    virtual ~CrashCapture() = default;
    /** @brief Constructs CrashCapture Manager
     *
     * @param[in] bus       - The Dbus bus object
     * @param[in] objPath   - The Dbus object path
     */
    CrashCapture(sdbusplus::bus::bus& bus, const char* objPath);

    /** @brief Set value of TriggerActions **/
    CrashCaptureBase::TriggerAction triggerActions(
        TriggerAction value) override;

    /** @brief Set value of TriggerUE **/
    bool triggerUE(bool value) override;

    /** @brief Set value of TriggerProcess **/
    bool triggerProcess(bool value) override;

  private:
    /** @brief Persistent sdbusplus DBus bus connection. **/
    sdbusplus::bus::bus& bus;

    /** @brief object path */
    std::string objectPath;

    /** @brief  D-Bus property changed signal match  **/
    std::unique_ptr<sdbusplus::bus::match_t> bootProgessMatch;

    bool onceTimeReadBERT = false;

    std::unique_ptr<sdbusplus::Timer> bertHostOffTimer, bertHostFailTimer,
        bertPowerLockTimer;

    void executeTransition(TriggerAction value);
    void handleBootProgressMatch(void);
    void handleBertHostOnEvent(void);
    void initBertHostOnEvent(void);
    void bertHostFailTimeOutHdl(void);
    void bertHostOnTimeOutHdl(void);
    void handleBmcUnavailable(void);
    void bertPowerLockTimeOutHdl(void);
};

} // namespace crashcapture
