#pragma once

#include <sdbusplus/bus.hpp>
#include <string>
#include <map>
#include <variant>

namespace crashcapture
{
namespace utils
{

	constexpr auto MAPPER_BUS_NAME = "xyz.openbmc_project.ObjectMapper";
	constexpr auto MAPPER_OBJ = "/xyz/openbmc_project/object_mapper";
	constexpr auto MAPPER_INTF = "xyz.openbmc_project.ObjectMapper";

	constexpr auto PROP_INTF = "org.freedesktop.DBus.Properties";
	constexpr auto DELETE_INTERFACE = "xyz.openbmc_project.Object.Delete";

	constexpr auto METHOD_GET = "Get";
	constexpr auto METHOD_GET_ALL = "GetAll";
	constexpr auto METHOD_SET = "Set";

	constexpr auto faultLogBusName = "xyz.openbmc_project.Dump.Manager";
	constexpr auto faultLogPath = "/xyz/openbmc_project/dump/faultlog";
	constexpr auto faultLogIntf = "xyz.openbmc_project.Dump.Create";
	constexpr auto logBusName = "xyz.openbmc_project.Logging.IPMI";
	constexpr auto logPath = "/xyz/openbmc_project/Logging/IPMI";
	constexpr auto logIntf = "xyz.openbmc_project.Logging.IPMI";

	using Association = std::tuple<std::string, std::string, std::string>;

	using Value =
		std::variant<bool, uint8_t, int16_t, uint16_t, int32_t,
			     uint32_t, int64_t, uint64_t, double, std::string,
			     std::vector<uint8_t>, std::vector<uint16_t>,
			     std::vector<uint32_t>, std::vector<std::string>,
			     std::vector<Association> >;

	/** @brief Gets the value associated with the given object
 *         and the interface.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] service - Dbus service name.
 *  @param[in] objPath - Dbus object path.
 *  @param[in] interface - Dbus interface.
 *  @param[in] property - name of the property.
 *  @return On success returns the value of the property.
 */
	Value getDbusProperty(sdbusplus::bus::bus &bus,
			      const std::string &service,
			      const std::string &objPath,
			      const std::string &interface,
			      const std::string &property);

	/** @brief Get unique entry ID
 *
 *  @param[in] prefix - prefix name
 *
 *  @return std::string unique entry ID
 */
	std::string getUniqueEntryID(std::string &prefix);

	/** @brief Log Redfish for FaultLog
 *
 *  @param[in] primaryLogId - unique name
 *  @param[in] type - Crashdump or CPER
 *
 *  @return None
 */
	void addFaultLogToRedfish(sdbusplus::bus::bus &bus,
				  std::string &primaryLogId, std::string &type);

	/** @brief Log OEM SEL for FaultLog
 *
 *  @param[in] msg - message string
 *  @param[in] evtData - event Data
 *  @param[in] recordType - record Type
 *
 *  @return None
 */
	void addOEMSelLog(sdbusplus::bus::bus &bus, std::string &msg,
			  std::vector<uint8_t> &evtData, uint8_t recordType);

} // namespace utils
} // namespace crashcapture
