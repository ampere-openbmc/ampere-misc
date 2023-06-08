#include "utils.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace crashcapture
{
namespace utils
{

	using namespace phosphor::logging;

	static time_t prevTs = 0;
	static int indexId = 0;

	std::string getUniqueEntryID(std::string &prefix)
	{
		// Get the entry timestamp
		std::time_t curTs = time(0);
		std::tm timeStruct = *std::localtime(&curTs);
		char buf[80];
		// If the timestamp isn't unique, increment the index
		if (curTs == prevTs) {
			indexId++;
		} else {
			// Otherwise, reset it
			indexId = 0;
		}
		// Save the timestamp
		prevTs = curTs;
		strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &timeStruct);
		std::string uniqueId(buf);
		uniqueId = prefix + uniqueId;
		if (indexId > 0) {
			uniqueId += "_" + std::to_string(indexId);
		}
		return uniqueId;
	}

	Value getDbusProperty(sdbusplus::bus::bus &bus,
			      const std::string &service,
			      const std::string &objPath,
			      const std::string &interface,
			      const std::string &property)
	{
		Value value;
		try {
			auto method =
				bus.new_method_call(service.c_str(),
						    objPath.c_str(), PROP_INTF,
						    METHOD_GET);
			method.append(interface, property);
			auto reply = bus.call(method);
			reply.read(value);
		} catch (const std::exception &e) {
			log<level::ERR>("Failed to get property",
					entry("PROPERTY=%s", property.c_str()),
					entry("PATH=%s", objPath.c_str()),
					entry("INTERFACE=%s",
					      interface.c_str()));
		}

		return value;
	}

	void addFaultLogToRedfish(sdbusplus::bus::bus &bus,
				  std::string &primaryLogId, std::string &type)
	{
		std::map<std::string, std::variant<std::string, uint64_t> >
			params;

		params["Type"] = type;
		params["PrimaryLogId"] = primaryLogId;
		try {
			auto method =
				bus.new_method_call(faultLogBusName,
						    faultLogPath, faultLogIntf,
						    "CreateDump");
			method.append(params);

			auto response = bus.call(method);
			if (response.is_method_error()) {
				std::cerr << "createDump error\n";
			}
		} catch (const std::exception &e) {
			std::cerr << "call createDump error: " << e.what()
				  << "\n";
		}
	}

	void addOEMSelLog(sdbusplus::bus::bus &bus, std::string &msg,
			  std::vector<uint8_t> &evtData, uint8_t recordType)
	{
		try {
			auto method = bus.new_method_call(
				logBusName, logPath, logIntf, "IpmiSelAddOem");
			method.append(msg, evtData, recordType);

			auto selReply = bus.call(method);
			if (selReply.is_method_error()) {
				std::cerr << "add SEL log error\n";
			}
		} catch (const std::exception &e) {
			std::cerr << "call SEL log error: " << e.what() << "\n";
		}
	}

} // namespace utils
} // namespace crashcapture
