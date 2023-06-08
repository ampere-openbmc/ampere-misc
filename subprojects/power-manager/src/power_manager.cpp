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

#include "power_manager.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/exception.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <filesystem>
#include <fstream>

namespace phosphor
{
namespace Control
{
	namespace Power
	{
		namespace Manager
		{

			PHOSPHOR_LOG2_USING;

			void PowerManager::parsePowerManagerCfg()
			{
				std::ifstream powerCfgFile(powerCfgJsonFile);

				if (!powerCfgFile.is_open()) {
					error("Can not open power configuration file");
					return;
				}

				auto data = nlohmann::json::parse(
					powerCfgFile, nullptr, false);

				if (data.is_discarded()) {
					error("Can not parse power configuration data");
					return;
				}

				/*
     * Get the information of total power consumption
     */
				if (data.contains("total_power")) {
					const auto &totalPwr =
						data.at("total_power");
					totalPwrSrv = totalPwr.value(
						"service", totalPwrSrv);
					totalPwrObjectPath = totalPwr.value(
						"object_path",
						totalPwrObjectPath);
					totalPwrItf = totalPwr.value(
						"interface", totalPwrItf);
				}
			}

		} // namespace Manager
	} // namespace Power
} // namespace Control
} // namespace phosphor