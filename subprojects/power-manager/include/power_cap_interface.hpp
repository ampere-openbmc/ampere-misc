#pragma once

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Control/Power/Cap/server.hpp>

#include <chrono>

using CapClass =
    sdbusplus::xyz::openbmc_project::Control::Power::server::Cap;
using CapItf = sdbusplus::server::object_t<CapClass>;

namespace phosphor
{
namespace Control
{
namespace Power
{
namespace Cap
{
class PowerCap : public CapItf
{
  public:
    PowerCap() = delete;
    PowerCap(const PowerCap&) = delete;
    PowerCap& operator=(const PowerCap&) = delete;
    PowerCap(PowerCap&&) = delete;
    PowerCap& operator=(PowerCap&&) = delete;
    virtual ~PowerCap() = default;

    /*  @brief Constructor to put object onto bus at a dbus path.
     *  @param[in] bus - sdbusplus D-Bus to attach to.
     *  @param[in] path - Path to attach to.
     */
    PowerCap(sdbusplus::bus_t& bus, const char* path,
               const sdeventplus::Event& event, std::string totalPwrSrv,
               std::string totalPwrObjectPath, std::string totalPwrItf);

    /*  @brief Request to set the Enable property of the interface.
     *  @param[in] value - the value of Enable property.
     *  @return - The Enable property of the interface.
     */
    bool powerCapEnable(bool value) override;

    /*  @brief Request to set the power Cap property of the interface.
     *  @param[in] value - the value of power Cap property.
     *  @return - The power Cap property of the interface.
     */
    uint32_t powerCap(uint32_t value) override;

    /*  @brief Request to set the exception action property of the interface.
     *  @param[in] value - the value of exception action property.
     *  @return - The exception action property of the interface.
     */
    CapClass::ExceptionActions
        exceptionAction(ExceptionActions value) override;

    /*  @brief Request to set the Correction Time property of the interface.
     *  @param[in] value - the value of Correction Time property.
     *  @return - The Correction Time property of the interface.
     */
    uint64_t correctionTime(uint64_t value) override;

    /*  @brief Request to set the Sampling Periodic property of the interface.
     *  @param[in] value - the value of Sampling Periodic property.
     *  @return - The Sampling Periodic property of the interface.
     */
    uint64_t samplingPeriod(uint64_t value) override;

  private:
    /** @brief sdbus handle */
    sdbusplus::bus_t& bus;

    /** @brief object path */
    std::string objectPath;

    /** @brief The Service, object path and interface which handles total power
     */
    std::string totalPwrSrv;
    std::string totalPwrObjectPath;
    std::string totalPwrItf;

    /** @brief minimun sampling periodic in microseconds */
    const uint64_t minSamplPeriod = 1000000;

    /** @brief timer event */
    const sdeventplus::Event& event;

    // TODO: move the power actions to utils files.
    /** @brief current total power consumption */
    uint32_t currentPower;

    /** @brief struct to store old configuration */
    typedef struct
    {
        bool enableFlag;
        CapClass::ExceptionActions exceptAct;
        uint32_t powerCap;
        uint64_t correctTime;
        uint64_t samplePeriod;
    } paramsCfg;

    /** @brief the current configuration */
    paramsCfg currentCfg;

    /** @brief the file to store old configuration */
    std::string oldParametersCfgFile =
        "/usr/share/power-manager/powerCap.cfg";

    /** @brief the correction timer */
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> correctTimer;
    /** @brief the sampling timer */
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> samplingTimer;

    /** @brief the call-back function when correction timer is expried */
    void callBackCorrectTimer();

    /** @brief the call-back function when sampling timer is expried */
    void callBackSamplingTimer();

    /** @brief the function to store current configuration */
    void writeCurrentCfg();

    /** @brief the function to log power limit event
     *  @param[in] assertFlg - Direction of event
     *                         true: Total power exceed the limit
     *                         false: Total power non-exceed the limit
     */
    void logPowerLimitEvent(bool assertFlg);

    /** @brief the function to turn off the power */
    void turnHardPowerOff();

    /** @brief the function to handle OEM exception action */
    void handleOEMExceptionAction();

    /** @brief the function to notify the total power consumption exceeds
     *         the power cap */
    void notifyTotalPowerExceedPowerCap();

    /** @brief the function to notify the total power consumption drops below
     *         power cap */
    void notifyTotalPowerDropBelowPowerCap();
};
} // namespace Cap
} // namespace Power
} // namespace Control
} // namespace phosphor