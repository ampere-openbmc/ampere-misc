#pragma once

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Control/Power/Limit/server.hpp>

#include <chrono>

using LimitClass =
    sdbusplus::xyz::openbmc_project::Control::Power::server::Limit;
using LimitItf = sdbusplus::server::object_t<LimitClass>;

namespace phosphor
{
namespace Control
{
namespace Power
{
namespace Limit
{
class PowerLimit : public LimitItf
{
  public:
    PowerLimit() = delete;
    PowerLimit(const PowerLimit&) = delete;
    PowerLimit& operator=(const PowerLimit&) = delete;
    PowerLimit(PowerLimit&&) = delete;
    PowerLimit& operator=(PowerLimit&&) = delete;
    virtual ~PowerLimit() = default;

    /*  @brief Constructor to put object onto bus at a dbus path.
     *  @param[in] bus - sdbusplus D-Bus to attach to.
     *  @param[in] path - Path to attach to.
     */
    PowerLimit(sdbusplus::bus_t& bus, const char* path,
               const sdeventplus::Event& event, std::string totalPwrSrv,
               std::string totalPwrObjectPath, std::string totalPwrItf);

    /*  @brief Request to set the Active property of the interface.
     *  @param[in] value - the value of Active property.
     *  @return - The active property of the interface.
     */
    bool active(bool value) override;

    /*  @brief Request to set the power limt property of the interface.
     *  @param[in] value - the value of power limit property.
     *  @return - The power limit property of the interface.
     */
    uint16_t powerLimit(uint16_t value) override;

    /*  @brief Request to set the exception action property of the interface.
     *  @param[in] value - the value of exception action property.
     *  @return - The exception action property of the interface.
     */
    LimitClass::ExceptionActions
        exceptionAction(ExceptionActions value) override;

    /*  @brief Request to set the Correction Time property of the interface.
     *  @param[in] value - the value of Correction Time property.
     *  @return - The Correction Time property of the interface.
     */
    uint32_t correctionTime(uint32_t value) override;

    /*  @brief Request to set the Sampling Periodic property of the interface.
     *  @param[in] value - the value of Sampling Periodic property.
     *  @return - The Sampling Periodic property of the interface.
     */
    uint16_t samplingPeriod(uint16_t value) override;

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

    /** @brief minimun sampling periodic in seconds */
    const uint16_t minSamplPeriod = 1;

    /** @brief timer event */
    const sdeventplus::Event& event;

    /** @brief struct to store old configuration */
    typedef struct
    {
        bool actFlag;
        LimitClass::ExceptionActions exceptAct;
        uint16_t powerLimit;
        uint32_t correctTime;
        uint16_t samplePeriod;
    } paramsCfg;

    /** @brief the current configuration */
    paramsCfg currentCfg;

    /** @brief the file to store old configuration */
    std::string oldParametersCfgFile =
        "/usr/share/power-manager/powerLimit.cfg";

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

    /** @brief the function to log SEL event
     *  @param[in] assertFlg - Direction of event
     *                         true: Total power exceed the limit
     *                         false: Total power non-exceed the limit
     */
    void logSELEvent(bool assertFlg);

    /** @brief the function to turn off the power */
    void turnHardPowerOff();

    /** @brief the function to handle OEM exception action */
    void handleOEMExceptionAction();
};
} // namespace Limit
} // namespace Power
} // namespace Control
} // namespace phosphor