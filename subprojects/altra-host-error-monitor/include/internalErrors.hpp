/*
 * Copyright (c) 2023 Ampere Computing LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <map>
#include <string>
#include <vector>

namespace ampere
{
namespace internalErrors
{

	const static constexpr u_int8_t NUM_IMAGE_CODES = 10;
	const char *imageCodes[NUM_IMAGE_CODES] = {
		"Executing ROM image",
		"Executing boot strap image",
		"Executing ROM normal (non-secure) boot path",
		"Executing ROM secure boot path",
		"Executing ROM asymmetric secure boot path",
		"Executing ROM symmetric secure boot path",
		"Executing runtime image",
		"Executing asymmetric secure runtime image",
		"Executing symmetric secure runtime image",
		"All others"
	};

	const static constexpr u_int8_t NUM_DIRS = 2;
	const char *directions[NUM_DIRS] = { "ENTER", "EXIT" };

	const static constexpr u_int8_t NUM_LOCAL_CODES = 78;
	const char *localCodes[NUM_LOCAL_CODES] = {
		"Unknown",
		"Main routine",
		"Interrupt controller (NVIC)",
		"AHB to AXI mapping",
		"Efuse initialization",
		"Efuse loading",
		"Efuse read fields",
		"Efuse write fields",
		"Crypto authentication",
		"Cryptocell initialization",
		"Certificate reading by Crytocell library",
		"Loading from I2C EEPROM to IRAM using IICDMA controller",
		"ROM main",
		"ROM dead",
		"N/A",
		"N/A",
		"N/A",
		"BL1 and PMpro Secure boot",
		"Slimimg file operations",
		"ROM jump to runtime firmware",
		"AVS module",
		"PMpro booting",
		"OCM initialization",
		"Media booting",
		"SPI NOR read",
		"Memory repair",
		"Console",
		"Board module",
		"DDR ZQCS",
		"Armv8 Initialization",
		"PCP power down",
		"Application processor PLL initialization",
		"PMD PLL initialization",
		"PCP power up",
		"PCP CPM initialization",
		"Mesh clock reset initialization",
		"PMD initialization",
		"PSCI CPU power management",
		"PSCI PMD power management",
		"PSCI PCP power management",
		"L3C clock reset initialization",
		"PCP initialization",
		"Thermal protection circuit",
		"Mainloop",
		"Mainloop non-secure message processing",
		"Mainloop secure message processing",
		"Mainloop console proxy buffer processing",
		"Mainloop PSCI request processing",
		"Mainloop CLI request processing",
		"Mainloop thermal protection request processing",
		"Mainloop board sensor processing",
		"Mainloop RAS request processing",
		"SCP Yielding routine",
		"Mainloop PMpro watchdog monitoring",
		"Mainloop Turbo processing",
		"Mainloop Alert module processing",
		"Mainloop warm reset bottom half processing",
		"I2C proxy",
		"Mainloop DVFS request",
		"Hard Fault handler",
		"PCC AXI access",
		"CPPC AXI access",
		"I2C AXI access",
		"VRM monitor",
		"DDR scrubbing",
		"SPI-NOR Write",
		"SPI-NOR Erase",
		"RAS BERT storage",
		"CCIX initialization",
		"CCIX ESM initialization",
		"GIC initialization",
		"Mesh initialization",
		"POST module",
		"DDR initialization",
		"NVPARAM initialization",
		"TPM initialization",
		"TPM Extend",
		"BMC module",
	};

	struct ScpErrCode {
		const char *ledDefault;
		const char *description;
	};

	const static constexpr u_int8_t NUM_ERROR_CODES = 146;
	ScpErrCode errorCodes[NUM_ERROR_CODES] = {
		{ "N/A", "No_Error" },
		{ "GPIO_INVALID_LCS", "IPP_FAULT_TMMCFG_FAIL" },
		{ "GPIO_FILE_HDR_INVALID", "IPP_FAULT_FILE_NOT_FOUND" },
		{ "GPIO_FILE_HDR_INVALID", "IPP_FAULT_FILE_SIZE_ZERO" },
		{ "N/A", "IPP_FAULT_INVALID_FILE" },
		{ "N/A", "IPP_FAULT_INVALID_KEYCERT" },
		{ "N/A", "IPP_FAULT_INVALID_CNTCERT" },
		{ "GPIO_FILE_INTEGRITY_INVALID", "IPP_FAULT_SLIM_HDRCRC_FAIL" },
		{ "GPIO_FILE_INTEGRITY_INVALID",
		  "IPP_FAULT_SLIM_BOOTHDR_FAIL" },
		{ "GPIO_FILE_INTEGRITY_INVALID",
		  "IPP_FAULT_SLIM_BOOTCRC_FAIL" },
		{ "GPIO_KEY_CERT_AUTH_ERR", "IPP_FAULT_KEY_CERT_AUTH_ERR" },
		{ "GPIO_CNT_CERT_AUTH_ERR", "IPP_FAULT_CNT_CERT_AUTH_ERR" },
		{ "N/A", "IPP_FAULT_SOC_HW_FAIL" },
		{ "GPIO_I2C_HARDWARE_ERR", "IPP_FAULT_IIDMA_TO" },
		{ "N/A", "IPP_FAULT_SOC_BOOTDEV_INIT_FAIL" },
		{ "GPIO_CRYPTO_ENGINE_ERR", "IPP_FAULT_CRYPTO_RST_FAIL" },
		{ "GPIO_CRYPTO_ENGINE_ERR", "IPP_FAULT_CRYPTO_INIT_FAIL" },
		{ "GPIO_CRYPTO_ENGINE_ERR", "IPP_FAULT_CRYPTO_LCS_INIT" },
		{ "GPIO_CRYPTO_ENGINE_ERR", "IPP_FAULT_CRYPTO_CERT_CHAIN" },
		{ "N/A", "IPP_FAULT_CRYPTO_AUTH_FAIL" },
		{ "GPIO_I2C_HARDWARE_ERR", "IPP_FAULT_FILE_READ_FAIL" },
		{ "GPIO_ROTPK_EFUSE_INVALID", "IPP_FAULT_INVALID_ROTPK_EFUSE" },
		{ "GPIO_SEED_EFUSE_INVALID",
		  "IPP_FAULT_INVALID_SEED_FROM_EFUSE" },
		{ "GPIO_LCS_FROM_EFUSE_INVALID",
		  "IPP_FAULT_INVALID_LCS_FROM_EFUSE" },
		{ "GPIO_PRIM_ROLLBACK_EFUSE_INVALID",
		  "IPP_FAULT_INVALID_PRIM_ROLLBACK_EFUSE" },
		{ "GPIO_SEC_ROLLBACK_EFUSE_INVALID",
		  "IPP_FAULT_INVALID_SEC_ROLLBACK_EFUSE" },
		{ "GPIO_HUK_EFUSE_INVALID", "IPP_FAULT_INVALID_HUK_EFUSE" },
		{ "GPIO_CERT_DATA_INVALID",
		  "IPP_FAULT_INVALID_PRIM_ROLLBACK_CERT" },
		{ "GPIO_CERT_DATA_INVALID", "IPP_FAULT_INVALID_HUK_FROM_CERT" },
		{ "GPIO_CERT_DATA_INVALID",
		  "IPP_FAULT_INVALID_SEED_FROM_CERT" },
		{ "GPIO_CERT_DATA_INVALID",
		  "IPP_FAULT_INVALID_SECOND_ROLLBACK_CERT" },
		{ "GPIO_CERT_DATA_INVALID", "IPP_FAULT_INVALID_CERT_TYPE" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_ERR_PMPRO_FAIL" },
		{ "N/A", "IPP_FAULT_SW_ERROR" },
		{ "GPIO_CERT_DATA_INVALID", "IPP_FAULT_INVALID_DBG_DIS_CERT" },
		{ "GPIO_CERT_DATA_INVALID",
		  "IPP_FAULT_INVALID_ANTIROLLBACK_CERT" },
		{ "N/A", "SLIM_OPEN_FILEHDL_MAXED_OUT" },
		{ "N/A", "IPP_FAULT_CONSOLE_FIFO_TIMEOUT" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_EFUSE_OPS_TIMEOUT" },
		{ "GPIO_CERT_DATA_INVALID",
		  "IPP_FAULT_ANTIROLLBACK_VER_MISMATCH" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_NMI_EXCEP" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_HF_EXCEP" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_MEM_EXCEP" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_BUS_EXCEP" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_USE_EXCEP" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_EFUSE_COPY_FAIL" },
		{ "N/A", "IPP_FAULT_SECJMP_FAIL" },
		{ "N/A", "IPP_FAULT_PBAC_FAIL" },
		{ "N/A", "IPP_FAULT_NO_LCS_EMU" },
		{ "N/A", "IPP_FAULT_SEC_INTF_INIT_FAIL" },
		{ "N/A", "IPP_FAULT_LOADER_INTF_INIT_FAIL" },
		{ "N/A", "IPP_FAULT_SKIP_ERROR_CM_LCS" },
		{ "N/A", "IPP_FAULT_CERT_SZ_OVERFLOW" },
		{ "GPIO_FILE_INTEGRITY_INVALID", "IPP_FAULT_FILE_SZ_ZERO" },
		{ "GPIO_FILE_INTEGRITY_INVALID",
		  "IPP_FAULT_FILE_OFFSET_MISMATCH" },
		{ "GPIO_FILE_INTEGRITY_INVALID",
		  "IPP_INVALID_FILESZ_MORE_THAN_MAX" },
		{ "N/A", "IPP_FAULT_INVALID_VAL" },
		{ "GPIO_INTERNAL_HW_ERR", "IPP_FAULT_ASEC_AUTH_AP_BL1" },
		{ "N/A", "IPP_FAULT_APPLLLCK_FAIL" },
		{ "N/A", "ERR_DDR_ZQCS_MC0" },
		{ "N/A", "ERR_DDR_ZQCS_MC1" },
		{ "N/A", "ERR_DDR_ZQCS_MC2" },
		{ "N/A", "ERR_DDR_ZQCS_MC3" },
		{ "N/A", "ERR_DDR_ZQCS_MC4" },
		{ "N/A", "ERR_DDR_ZQCS_MC5" },
		{ "N/A", "ERR_DDR_ZQCS_MC6" },
		{ "N/A", "ERR_DDR_ZQCS_MC7" },
		{ "N/A", "IPP_FAULT_PMDx_TIBDFT_FAIL" },
		{ "N/A", "IPP_FAULT_PMDxPLLLCK_FAIL" },
		{ "N/A", "AVS_ERR_VOLTAGE" },
		{ "N/A", "AVS_ERR_VRM" },
		{ "N/A", "AVS_ERR_PCP_PLL" },
		{ "N/A", "AVS_ERR_PMD_PLL" },
		{ "N/A", "PSCI_LPI_RUN_FAIL" },
		{ "N/A", "PSCI_LPI_STANDBY_FAIL" },
		{ "N/A", "PSCI_LPI_RETENTION_FAIL" },
		{ "N/A", "PSCI_LPI_POWERDOWN_FAIL" },
		{ "N/A", "IPP_BOARD_CFG" },
		{ "N/A", "IPP_BOARD_CFG_VER" },
		{ "N/A", "IPP_BOARD_CFG_SIZE" },
		{ "N/A", "IPP_BOARD_CFG_DATA" },
		{ "N/A", "IPP_FAULT_PCPPWR_FAIL" },
		{ "N/A", "IPP_FAULT_CSW_TIBDFT_FAIL" },
		{ "N/A", "IPP_FAULT_L3C_DFT_FAIL" },
		{ "N/A", "IPP_FAULT_L3C_INIT_FAIL" },
		{ "N/A", "IPP_FAULT_PCP_INIT_FAIL" },
		{ "N/A", "IPP_FAULT_SPI_BUSERR" },
		{ "N/A", "IPP_FAULT_SPI_NODEV" },
		{ "N/A", "IPP_FAULT_SPI_READ_INCOMPLETE" },
		{ "N/A", "ERR_CSR_MEM_NOT_RDY" },
		{ "N/A", "ERR_TPC" },
		{ "N/A", "ERR_ALERT" },
		{ "N/A", "ERR_WRST_FAIL" },
		{ "N/A", "IPP_AXI_RESP_ERR" },
		{ "N/A", "ERR_AXI_NON_FATAL" },
		{ "N/A", "VRM_MONITOR_FAIL" },
		{ "N/A", "ERR_DDR_SCRUB_MC0" },
		{ "N/A", "ERR_DDR_SCRUB_MC1" },
		{ "N/A", "ERR_DDR_SCRUB_MC2" },
		{ "N/A", "ERR_DDR_SCRUB_MC3" },
		{ "N/A", "ERR_DDR_SCRUB_MC4" },
		{ "N/A", "ERR_DDR_SCRUB_MC5" },
		{ "N/A", "ERR_DDR_SCRUB_MC6" },
		{ "N/A", "ERR_DDR_SCRUB_MC7" },
		{ "N/A", "BERT_STORE_FAIL" },
		{ "N/A", "ERR_DDR_SERVICE_ZQCS" },
		{ "N/A", "ERR_CCIX_RSB" },
		{ "N/A", "ERR_CCIX_MEMRDY_FAIL" },
		{ "N/A", "ERR_CCIX_TCVC_FAIL" },
		{ "N/A", "ERR_CCIX_NOT_COMPLIANT" },
		{ "N/A", "ERR_CCIX_GEN1_FAIL" },
		{ "N/A", "ERR_CCIX_L1_PWR_FAIL" },
		{ "N/A", "ERR_CCIX_L0_PWR_FAIL" },
		{ "N/A", "ERR_CCIX_ESM_FAIL" },
		{ "N/A", "ERR_CCIX_DR1_FAIL" },
		{ "N/A", "ERR_CCIX_GEN4_FAIL" },
		{ "N/A", "ERR_CCIX_RCA_LINKUP_FAIL" },
		{ "N/A", "ERR_GIC_FAIL" },
		{ "N/A", "ERR_MESH_CCIX_LINKUP_FAIL" },
		{ "N/A", "IPP_ERR_IOB_SOC_WAKE" },
		{ "N/A", "IPP_CONSOLE_OVERFLOW" },
		{ "N/A", "IPP_FAULT_ASEC_AUTH_PMPRO" },
		{ "N/A", "IPP_FAULT_NO_IIC_PROXY_DEV" },
		{ "N/A", "IPP_POST_MSG" },
		{ "N/A", "ERR_DDR_SPD_READ_FAIL" },
		{ "N/A", "ERR_PCP_MEM_REPAIR_FAIL" },
		{ "N/A", "ERR_INVALID_OPERATION" },
		{ "N/A", "ERR_NO_CPM_AVAIL" },
		{ "N/A", "ERR_NO_MCU_AVAIL" },
		{ "N/A", "IPP_FAULT_LOAD_AP_IMAGE" },
		{ "N/A", "IPP_FAULT_LOAD_PMPRO" },
		{ "N/A", "IPP_FAULT_PMD0DFT_FAIL" },
		{ "N/A", "ERR_DDR_GET_DIMM_INFO" },
		{ "N/A", "IPP_FAULT_OB2P_SLAVE_NOT_RDY" },
		{ "N/A", "IPP_FAULT_PCP_PMPRO_INIT_FAIL" },
		{ "N/A", "IPP_FAULT_TPM_INIT_FAIL" },
		{ "N/A", "ERR_HOB_UPDATE_FAIL" },
		{ "N/A", "SKU_NOT_VALID" },
		{ "N/A", "IPP_FAULT_TPM_EXTEND_FAIL" },
		{ "N/A", "ERR_BMC_OVERFLOW" },
		{ "N/A", "ERR_MESH_FAIL" },
		{ "N/A", "ERR_DDR_INVALID_MCU_MASK" },
		{ "N/A", "IPP_FAULT_EFUSE_WR_TIMEOUT" },
		{ "N/A", "IPP_FUSE_WR_DATA_MISTMATCH" },
		{ "N/A", "IPP_FUSE_UNSUPPORTED_OPERATION" },
		{ "N/A", "ERR_DDR_TRAINING_FAILED" },
	};

} /* namespace internalErrors */
} /* namespace ampere */
