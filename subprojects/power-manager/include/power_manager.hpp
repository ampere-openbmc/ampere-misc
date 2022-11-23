#pragma once

#include "power_limit_interface.hpp"

namespace phosphor
{
namespace Control
{
namespace Power
{
namespace Manager
{
using powerLimitClass = phosphor::Control::Power::Limit::PowerLimit;
class PowerManager
{
  public:
    PowerManager() = delete;
    PowerManager(const PowerManager&) = delete;
    PowerManager& operator=(const PowerManager&) = delete;
    PowerManager(PowerManager&&) = delete;
    PowerManager& operator=(PowerManager&&) = delete;
    virtual ~PowerManager() = default;

    PowerManager(sdbusplus::bus_t& bus, const char* path,
                 const sdeventplus::Event& event) :
        bus(bus),
        objectPath(path)
    {
        parsePowerManagerCfg();
        limitObject = std::make_unique<powerLimitClass>(
            bus, (std::string(path) + "/limit").c_str(), event, totalPwrSrv,
            totalPwrObjectPath, totalPwrItf);
    }

  private:
    /** @brief sdbus handle */
    sdbusplus::bus_t& bus;

    /** @brief object path */
    std::string objectPath;

    std::unique_ptr<powerLimitClass> limitObject = nullptr;

    const char* powerCfgJsonFile =
        "/usr/share/power-manager/power-manager-cfg.json";

    std::string totalPwrSrv = "xyz.openbmc_project.VirtualSensor";
    std::string totalPwrObjectPath =
        "/xyz/openbmc_project/sensors/power/total_power";
    std::string totalPwrItf = "xyz.openbmc_project.Sensor.Value";

    void parsePowerManagerCfg();
};
} // namespace Manager
} // namespace Power
} // namespace Control
} // namespace phosphor