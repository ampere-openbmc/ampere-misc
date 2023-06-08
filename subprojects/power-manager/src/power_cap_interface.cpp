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

#include "power_cap_interface.hpp"

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
		namespace Cap
		{

			PHOSPHOR_LOG2_USING;
			using namespace phosphor::logging;
			using namespace sdbusplus::xyz::openbmc_project::
				Common::Error;

			constexpr auto SYSTEMD_SERVICE =
				"org.freedesktop.systemd1";
			constexpr auto SYSTEMD_OBJ_PATH =
				"/org/freedesktop/systemd1";
			constexpr auto SYSTEMD_INTERFACE =
				"org.freedesktop.systemd1.Manager";

			PowerCap::PowerCap(sdbusplus::bus_t &bus,
					   const char *path,
					   const sdeventplus::Event &event,
					   std::string totalPwrSrv,
					   std::string totalPwrObjectPath,
					   std::string totalPwrItf)
				: CapItf(bus, path), bus(bus), objectPath(path),
				  event(event), totalPwrSrv(totalPwrSrv),
				  totalPwrObjectPath(totalPwrObjectPath),
				  totalPwrItf(totalPwrItf),
				  correctTimer(
					  event,
					  std::bind(
						  &PowerCap::callBackCorrectTimer,
						  this)),
				  samplingTimer(
					  event,
					  std::bind(
						  &PowerCap::callBackSamplingTimer,
						  this))
			{
				std::ifstream oldCfgFile(
					oldParametersCfgFile.c_str(),
					std::ios::in | std::ios::binary);

				/*
     * Read the old configuration
     */
				if (oldCfgFile.is_open()) {
					oldCfgFile.read((char *)(&currentCfg),
							sizeof(paramsCfg));
					powerCapEnable(currentCfg.enableFlag);
					exceptionAction(currentCfg.exceptAct);
					powerCap(currentCfg.powerCap);
					correctionTime(currentCfg.correctTime);
					samplingPeriod(currentCfg.samplePeriod);
				} else {
					currentCfg.enableFlag =
						CapItf::powerCapEnable();
					currentCfg.exceptAct =
						CapItf::exceptionAction();
					currentCfg.powerCap =
						CapItf::powerCap();
					currentCfg.correctTime =
						CapItf::correctionTime();
					currentCfg.samplePeriod =
						CapItf::samplingPeriod();
				}
				oldCfgFile.close();
			}

			bool PowerCap::powerCapEnable(bool value)
			{
				// Get current Enable status
				bool currentAct = CapItf::powerCapEnable();

				/*
     * The sampling timer should be enabled only the Enable state is
     * changed from "false" to "true"
     */
				if (true == value && value != currentAct) {
					/*
         * Enable sampling timer
         */
					uint64_t samplePeriod =
						CapItf::samplingPeriod();
					samplePeriod = (samplePeriod <
							minSamplPeriod) ?
							       minSamplPeriod :
							       samplePeriod;
					samplingTimer.restart(
						std::chrono::microseconds(
							samplePeriod));
				} else if (false == value &&
					   value != currentAct) {
					/*
         * Disable sampling and correction timers
         */
					samplingTimer.restart(std::nullopt);
					correctTimer.restart(std::nullopt);
				}

				currentCfg.enableFlag = value;
				writeCurrentCfg();

				return CapItf::powerCapEnable(value, false);
			}

			uint32_t PowerCap::powerCap(uint32_t value)
			{
				currentCfg.powerCap = value;
				writeCurrentCfg();

				return CapItf::powerCap(value, false);
			}

			CapClass::ExceptionActions
			PowerCap::exceptionAction(ExceptionActions value)
			{
				currentCfg.exceptAct = value;
				writeCurrentCfg();

				return CapItf::exceptionAction(value, false);
			}

			uint64_t PowerCap::correctionTime(uint64_t value)
			{
				currentCfg.correctTime = value;
				writeCurrentCfg();

				return CapItf::correctionTime(value, false);
			}

			uint64_t PowerCap::samplingPeriod(uint64_t value)
			{
				value = (value < minSamplPeriod) ?
						minSamplPeriod :
						value;
				samplingTimer.setInterval(
					std::chrono::microseconds(value));

				currentCfg.samplePeriod = value;
				writeCurrentCfg();

				return CapItf::samplingPeriod(value, false);
			}

			void PowerCap::callBackCorrectTimer()
			{
				CapClass::ExceptionActions currentExcepAct =
					CapItf::exceptionAction();

				switch (currentExcepAct) {
				case CapClass::ExceptionActions::NoAction:
					break;
				case CapClass::ExceptionActions::HardPowerOff: {
					// Turn power off and log the event to Redfish
					turnHardPowerOff();
					logPowerLimitEvent(true);
					break;
				}
				case CapClass::ExceptionActions::LogEventOnly: {
					// Log the event to Redfish
					logPowerLimitEvent(true);
					break;
				}
				case CapClass::ExceptionActions::Oem: {
					// OEM action - call the service which handle OEM action
					handleOEMExceptionAction();
					break;
				}
				}

				notifyTotalPowerExceedPowerCap();
			}

			void PowerCap::callBackSamplingTimer()
			{
				/*
     * If the service which stores total power consumption is valid
     * then compare the power cap and total power consumption
     */
				if (!totalPwrSrv.empty() &&
				    !totalPwrObjectPath.empty() &&
				    !totalPwrItf.empty()) {
					/*
         * Request to get the total power consumption
         */
					auto method = bus.new_method_call(
						totalPwrSrv.c_str(),
						totalPwrObjectPath.c_str(),
						"org.freedesktop.DBus.Properties",
						"Get");

					method.append(totalPwrItf.c_str(),
						      "Value");

					try {
						std::variant<double>
							totalPowerVal = 0.0;
						auto response =
							bus.call(method);
						uint32_t powerCap =
							CapItf::powerCap();

						response.read(totalPowerVal);
						currentPower =
							(uint32_t)(std::get<
								   double>(
								totalPowerVal));

						/*
             * If the total power consumption is greater than power cap then
             * trigger one shot correction timer.
             */
						if (((uint32_t)(std::get<double>(
							    totalPowerVal))) >=
						    powerCap) {
							if (!correctTimer
								     .isEnabled() &&
							    !correctTimer
								     .hasExpired()) {
								/*
                     * Enable correction timer
                     */
								uint64_t correctTime =
									CapItf::correctionTime();
								correctTimer.restartOnce(
									std::chrono::microseconds(
										correctTime));
							}
						} else {
							if (correctTimer
								    .hasExpired()) {
								auto currentExcepAct =
									CapItf::exceptionAction();

								if ((currentExcepAct ==
								     CapClass::ExceptionActions::
									     HardPowerOff) ||
								    (currentExcepAct ==
								     CapClass::ExceptionActions::
									     LogEventOnly)) {
									logPowerLimitEvent(
										false);
								}

								notifyTotalPowerDropBelowPowerCap();
							}

							/*
                 * Disable correction timer when total power is lower than power
                 * cap
                 */
							correctTimer.restart(
								std::nullopt);
						}
					} catch (const sdbusplus::exception_t
							 &e) {
						error("Error when tries to get the total power");
					}
				}
			}

			void PowerCap::writeCurrentCfg()
			{
				std::ofstream oldCfgFile(
					oldParametersCfgFile.c_str(),
					std::ios::out | std::ios::binary);
				if (oldCfgFile.is_open()) {
					oldCfgFile.write((char *)(&currentCfg),
							 sizeof(paramsCfg));
				}
				oldCfgFile.close();
			}

			void PowerCap::logPowerLimitEvent(bool assertFlg)
			{
				std::string redfishMsgId;
				std::string message;
				uint32_t powerCap = CapItf::powerCap();
				std::string msgArgs =
					std::to_string(currentPower) + "," +
					std::to_string(powerCap);

				if (true == assertFlg) {
					redfishMsgId =
						"OpenBMC.0.1.TotalPowerConsumptionExceedTheLimit";
					message = "Limit Exceeded";
				} else {
					redfishMsgId =
						"OpenBMC.0.1.TotalPowerConsumptionDropBelowTheLimit";
					message = "Limit No Exceeded";
				}

				lg2::info(message.c_str(), "REDFISH_MESSAGE_ID",
					  redfishMsgId.c_str(),
					  "REDFISH_MESSAGE_ARGS",
					  msgArgs.c_str());
			}

			void PowerCap::turnHardPowerOff()
			{
				constexpr auto chassisStateServer =
					"xyz.openbmc_project.State.Chassis";
				constexpr auto chassisStateObjectPath =
					"/xyz/openbmc_project/state/chassis0";
				constexpr auto chassisStateItf =
					"xyz.openbmc_project.State.Chassis";
				std::variant<std::string> requestTurnPowerOff =
					"xyz.openbmc_project.State.Chassis.Transition.Off";
				auto method = bus.new_method_call(
					chassisStateServer,
					chassisStateObjectPath,
					"org.freedesktop.DBus.Properties",
					"Set");

				method.append(chassisStateItf);
				method.append("RequestedPowerTransition");
				method.append(requestTurnPowerOff);

				bus.call_noreply(method);
			}

			void PowerCap::handleOEMExceptionAction()
			{
				constexpr auto oemActionService =
					"power-cap-action-oem.service";

				auto method = bus.new_method_call(
					SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
					SYSTEMD_INTERFACE, "StartUnit");
				method.append(oemActionService, "replace");

				try {
					bus.call_noreply(method);
				} catch (const sdbusplus::exception_t &e) {
					error("Error occur when call power-cap-action-oem.service");
				}
			}

			void PowerCap::notifyTotalPowerExceedPowerCap()
			{
				constexpr auto exceedService =
					"power-cap-exceeds-limit.service";

				auto method = bus.new_method_call(
					SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
					SYSTEMD_INTERFACE, "StartUnit");
				method.append(exceedService, "replace");

				try {
					bus.call_noreply(method);
				} catch (const sdbusplus::exception_t &e) {
					error("Error occur when call power-cap-exceeds-limit.service");
				}
			}

			void PowerCap::notifyTotalPowerDropBelowPowerCap()
			{
				constexpr auto dropBelowService =
					"power-cap-drops-below-limit.service";

				auto method = bus.new_method_call(
					SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
					SYSTEMD_INTERFACE, "StartUnit");
				method.append(dropBelowService, "replace");

				try {
					bus.call_noreply(method);
				} catch (const sdbusplus::exception_t &e) {
					error("Error occur when call power-cap-drops-below-limit.service");
				}
			}

		} // namespace Cap
	} // namespace Power
} // namespace Control
} // namespace phosphor