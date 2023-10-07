#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ast-jtag.h"
#include "cpld.h"
#include "lattice.h"
#include "i2c-lib.h"

//#define DEBUG
//#define VERBOSE_DEBUG
#ifdef DEBUG
#define CPLD_DEBUG(...) printf(__VA_ARGS__);
#else
#define CPLD_DEBUG(...)
#endif

#define ERR_PRINT(...) fprintf(stderr, __VA_ARGS__);

#define MAX_RETRY	 4000
#define LATTICE_COL_SIZE 128
#define ARRAY_SIZE(x)	 (sizeof(x) / sizeof((x)[0]))
#define UNUSED(x)	 (void)(x)

typedef struct {
	unsigned long int QF;
	unsigned int *CF;
	unsigned int CF_Line;
	unsigned int *UFM;
	unsigned int UFM_Line;
	unsigned int *EndCF;
	unsigned int EndCF_Line;
	unsigned int Version;
	unsigned int CheckSum;
	unsigned int FEARBits;
	unsigned int FeatureRow;

} CPLDInfo;

//#define CPLD_DEBUG //enable debug message
//#define VERBOSE_DEBUG //enable detail debug message

enum {
	CHECK_BUSY = 0,
	CHECK_STATUS = 1,
};

enum {
	Only_CF = 0,
	Both_CF_UFM = 1,
};

static cpld_intf_info_t cpld;

/******************************************************************************/
/***************************      Common Functions      ***********************/
/******************************************************************************/

/*search the index of char in string*/
static int indexof(const char *str, const char *c)
{
	char *ptr = strstr(str, c);
	int index = 0;

	if (ptr) {
		index = ptr - str;
	} else {
		index = -1;
	}

	return index;
}

/*identify the str start with a specific str or not*/
static int startWith(const char *str, const char *c)
{
	int len = strlen(c);
	int i;

	for (i = 0; i < len; i++) {
		if (str[i] != c[i]) {
			return 0;
		}
	}
	return 1;
}

/*convert bit data to byte data*/
static unsigned int ShiftData(char *data, unsigned int *result, int len)
{
	int i;
	int ret = 0;
	int result_index = 0, data_index = 0;
	int bit_count = 0;

#ifdef VERBOSE_DEBUG
	printf("[%s][%s]\n", __func__, data);

	for (i = 0; i < len; i++) {
		printf("%c", data[i]);

		if (0 == ((i + 1) % 8)) {
			printf("\n");
		}
	}
#endif

	for (i = 0; i < len; i++) {
		data[i] = data[i] - 0x30;

		result[result_index] |= ((unsigned char)data[i] << data_index);

#ifdef VERBOSE_DEBUG
		printf("[%s]%x %d %x\n", __func__, data[i], data_index,
		       result[result_index]);
#endif
		data_index++;

		bit_count++;

		if (0 == ((i + 1) % 32)) {
			data_index = 0;
#ifdef VERBOSE_DEBUG
			printf("[%s]%x\n", __func__, result[result_index]);
#endif
			result_index++;
		}
	}

	if (bit_count != len) {
		printf("[%s] Expected Data Length is [%d] but not [%d] ",
		       __func__, bit_count, len);

		ret = -1;
	}

	return ret;
}

static unsigned int byte_to_int(uint8_t *data)
{
	return (((data[0] & 0xFF) << 24) | ((data[1] & 0xFF) << 16) |
		((data[2] & 0xFF) << 8) | ((data[3] & 0xFF)));
}

static unsigned int byte_swap(unsigned int dword)
{
	return (((dword & 0xFF) << 24) | (((dword >> 8) & 0xFF) << 16) |
		(((dword >> 16) & 0xFF) << 8) | ((dword >> 24) & 0xFF));
}

static void swap_bit_byte(uint8_t *data, unsigned int len)
{
	unsigned int i, j;
	uint8_t byte, swap_byte;

	for (i = 0; i < len; i++) {
		byte = data[i];
		swap_byte = 0;
		for (j = 0; j < 8; j++) {
			if (byte & (1 << j)) {
				swap_byte |= 1 << (7 - j);
			}
		}
		data[i] = swap_byte;
	}
}

/*check the size of cf and ufm*/
static int jed_update_data_size(FILE *jed_fd, int *cf_size, int *ufm_size,
				int *endcfg_size)
{
	const char TAG_CF_START[] = "L000";
	int ReadLineSize = LATTICE_COL_SIZE + 2;
	char tmp_buf[ReadLineSize];
	unsigned int CFStart = 0;
	unsigned int UFMStart = 0;
	unsigned int CFEnd = 0;
	const char TAG_UFM[] = "NOTE TAG DATA";
	const char TAG_CF_END[] = "NOTE END CONFIG DATA";
	int ret = 0;

	while (NULL != fgets(tmp_buf, ReadLineSize, jed_fd)) {
		if (startWith(tmp_buf, TAG_CF_START /*"L000"*/)) {
			CFStart = 1;
		} else if (startWith(tmp_buf, TAG_UFM /*"NOTE TAG DATA"*/)) {
			UFMStart = 1;
		} else if (startWith(tmp_buf,
				     TAG_CF_END /*"NOTE END CONFIG DATA"*/)) {
			CFEnd = 1;
		}

		if (CFStart) {
			if (!startWith(tmp_buf, TAG_CF_START /*"L000"*/) &&
			    strlen(tmp_buf) != 1) {
				if (startWith(tmp_buf, "0") ||
				    startWith(tmp_buf, "1")) {
					(*cf_size)++;
				} else {
					CFStart = 0;
				}
			}
		} else if (UFMStart) {
			if (!startWith(tmp_buf, TAG_UFM /*"NOTE TAG DATA"*/) &&
			    !startWith(tmp_buf, "L") && strlen(tmp_buf) != 1) {
				if (startWith(tmp_buf, "0") ||
				    startWith(tmp_buf, "1")) {
					(*ufm_size)++;
				} else {
					UFMStart = 0;
				}
			}
		} else if (CFEnd) {
			if (!startWith(tmp_buf,
				       TAG_CF_END /*"NOTE END CONFIG DATA"*/) &&
			    !startWith(tmp_buf, "L") && strlen(tmp_buf) != 1) {
				if (startWith(tmp_buf, "0") ||
				    startWith(tmp_buf, "1")) {
					(*endcfg_size)++;
				} else {
					CFEnd = 0;
				}
			}
		}
	}

	//cf must greater than 0
	if (!(*cf_size)) {
		ret = -1;
	}

	return ret;
}

static int jed_file_parser(FILE *jed_fd, CPLDInfo *dev_info, int cf_size,
			   int ufm_size, int endcfg_size)
{
	/**TAG Information**/
	const char TAG_QF[] = "QF";
	const char TAG_CF_START[] = "L000";
	const char TAG_UFM[] = "NOTE TAG DATA";
	const char TAG_CF_END[] = "NOTE END CONFIG DATA";
	const char TAG_ROW[] = "NOTE FEATURE";
	const char TAG_CHECKSUM[] = "C";
	const char TAG_USERCODE[] = "NOTE User Electronic";
	/**TAG Information**/

	int ReadLineSize =
		LATTICE_COL_SIZE +
		2; //the len of 128 only contain data size, '\n' need to be considered, too.
	char tmp_buf[ReadLineSize];
	char data_buf[LATTICE_COL_SIZE];
	unsigned int CFStart = 0;
	unsigned int UFMStart = 0;
	unsigned int CFEnd = 0;
	unsigned int ROWStart = 0;
	unsigned int VersionStart = 0;
	unsigned int ChkSUMStart = 0;
	unsigned int JED_CheckSum = 0;
	int copy_size;
	int current_addr = 0;
	unsigned int i;
	int ret = 0;
	int cf_size_used = (cf_size * LATTICE_COL_SIZE) / 8; // unit: bytes
	int ufm_size_used = (ufm_size * LATTICE_COL_SIZE) / 8; // unit: bytes
	int endcfg_size_used =
		(endcfg_size * LATTICE_COL_SIZE) / 8; // unit: bytes

	dev_info->CF = (unsigned int *)malloc(cf_size_used);
	memset(dev_info->CF, 0, cf_size_used);

	if (ufm_size_used) {
		dev_info->UFM = (unsigned int *)malloc(ufm_size_used);
		memset(dev_info->UFM, 0, ufm_size_used);
	}

	if (endcfg_size_used) {
		dev_info->EndCF = (unsigned int *)malloc(endcfg_size_used);
		memset(dev_info->EndCF, 0, endcfg_size_used);
	}

	dev_info->CF_Line = 0;
	dev_info->UFM_Line = 0;
	dev_info->EndCF_Line = 0;

	while (NULL != fgets(tmp_buf, ReadLineSize, jed_fd)) {
		if (startWith(tmp_buf, TAG_QF /*"QF"*/)) {
			copy_size = indexof(tmp_buf, "*") -
				    indexof(tmp_buf, "F") - 1;

			memset(data_buf, 0, sizeof(data_buf));

			memcpy(data_buf, &tmp_buf[2], copy_size);

			dev_info->QF = atol(data_buf);

			CPLD_DEBUG("[QF]%ld\n", dev_info->QF);
		} else if (startWith(tmp_buf, TAG_CF_START /*"L000"*/)) {
			CPLD_DEBUG("[CFStart]\n");
			CFStart = 1;
		} else if (startWith(tmp_buf, TAG_UFM /*"NOTE TAG DATA"*/)) {
			CPLD_DEBUG("[UFMStart]\n");
			UFMStart = 1;
		} else if (startWith(tmp_buf, TAG_ROW /*"NOTE FEATURE"*/)) {
			CPLD_DEBUG("[ROWStart]\n");
			ROWStart = 1;
		} else if (startWith(tmp_buf,
				     TAG_USERCODE /*"NOTE User Electronic"*/)) {
			CPLD_DEBUG("[VersionStart]\n");
			VersionStart = 1;
		} else if (startWith(tmp_buf, TAG_CHECKSUM /*"C"*/)) {
			CPLD_DEBUG("[ChkSUMStart]\n");
			ChkSUMStart = 1;
		} else if (startWith(tmp_buf,
				     TAG_CF_END /*"NOTE END CONFIG DATA"*/)) {
			CPLD_DEBUG("[CFEnd]\n");
			CFEnd = 1;
		}

		if (CFStart) {
#ifdef VERBOSE_DEBUG
			printf("[%s][%d][%c]", __func__, strlen(tmp_buf),
			       tmp_buf[0]);
#endif
			if (!startWith(tmp_buf, TAG_CF_START /*"L000"*/) &&
			    strlen(tmp_buf) != 1) {
				if (startWith(tmp_buf, "0") ||
				    startWith(tmp_buf, "1")) {
					current_addr = (dev_info->CF_Line *
							LATTICE_COL_SIZE) /
						       32;

					memset(data_buf, 0, sizeof(data_buf));

					memcpy(data_buf, tmp_buf,
					       LATTICE_COL_SIZE);

					/*convert string to byte data*/
					ShiftData(data_buf,
						  &dev_info->CF[current_addr],
						  LATTICE_COL_SIZE);

#ifdef VERBOSE_DEBUG
					//          printf("[%d]%x %x %x %x\n",dev_info->CF_Line, dev_info->CF[current_addr],dev_info->CF[current_addr+1],dev_info->CF[current_addr+2],dev_info->CF[current_addr+3]);
					printf("[%d] ", dev_info->CF_Line);
					for (i = 0; i < sizeof(unsigned int);
					     i++) {
						printf("%x ",
						       (dev_info->CF[current_addr +
								     i]) &
							       0xff);
						printf("%x ",
						       (dev_info->CF[current_addr +
								     i] >>
							8) & 0xff);
						printf("%x ",
						       (dev_info->CF[current_addr +
								     i] >>
							16) & 0xff);
						printf("%x ",
						       (dev_info->CF[current_addr +
								     i] >>
							24) & 0xff);
					}
					printf("\n");
#endif
					//each data has 128bits(4*unsigned int), so the for-loop need to be run 4 times

					for (i = 0; i < sizeof(unsigned int);
					     i++) {
						JED_CheckSum +=
							(dev_info->CF
								 [current_addr +
								  i] >>
							 24) &
							0xff;
						JED_CheckSum +=
							(dev_info->CF
								 [current_addr +
								  i] >>
							 16) &
							0xff;
						JED_CheckSum +=
							(dev_info->CF
								 [current_addr +
								  i] >>
							 8) &
							0xff;
						JED_CheckSum +=
							(dev_info->CF
								 [current_addr +
								  i]) &
							0xff;
					}

					dev_info->CF_Line++;
				} else {
					CPLD_DEBUG("[%s]CF Line: %d\n",
						   __func__, dev_info->CF_Line);
					CFStart = 0;
				}
			}
		} else if (ChkSUMStart && strlen(tmp_buf) != 1) {
			ChkSUMStart = 0;

			copy_size = indexof(tmp_buf, "*") -
				    indexof(tmp_buf, "C") - 1;

			memset(data_buf, 0, sizeof(data_buf));

			memcpy(data_buf, &tmp_buf[1], copy_size);

			dev_info->CheckSum = strtoul(data_buf, NULL, 16);
			printf("JED Checksum from file: %X\n",
			       dev_info->CheckSum);
		} else if (ROWStart) {
			if (!startWith(tmp_buf, TAG_ROW /*"NOTE FEATURE"*/) &&
			    strlen(tmp_buf) != 1) {
				if (startWith(tmp_buf, "E")) {
					copy_size = strlen(tmp_buf) -
						    indexof(tmp_buf, "E") - 2;

					memset(data_buf, 0, sizeof(data_buf));

					memcpy(data_buf, &tmp_buf[1],
					       copy_size);

					dev_info->FeatureRow =
						strtoul(data_buf, NULL, 2);
				} else {
					copy_size = indexof(tmp_buf, "*") - 1;

					memset(data_buf, 0, sizeof(data_buf));

					memcpy(data_buf, &tmp_buf[2],
					       copy_size);

					dev_info->FEARBits =
						strtoul(data_buf, NULL, 2);
					CPLD_DEBUG("[FeatureROW]%x\n",
						   dev_info->FeatureRow);
					CPLD_DEBUG("[FEARBits]%x\n",
						   dev_info->FEARBits);
					ROWStart = 0;
				}
			}
		} else if (VersionStart) {
			if (!startWith(
				    tmp_buf,
				    TAG_USERCODE /*"NOTE User Electronic"*/) &&
			    strlen(tmp_buf) != 1) {
				VersionStart = 0;

				if (startWith(tmp_buf, "UH")) {
					copy_size = indexof(tmp_buf, "*") -
						    indexof(tmp_buf, "H") - 1;

					memset(data_buf, 0, sizeof(data_buf));

					memcpy(data_buf, &tmp_buf[2],
					       copy_size);

					dev_info->Version =
						strtoul(data_buf, NULL, 16);
					CPLD_DEBUG("[UserCode]%x\n",
						   dev_info->Version);
				}
			}
		} else if (UFMStart) {
			if (!startWith(tmp_buf, TAG_UFM /*"NOTE TAG DATA"*/) &&
			    !startWith(tmp_buf, "L") && strlen(tmp_buf) != 1) {
				if (startWith(tmp_buf, "0") ||
				    startWith(tmp_buf, "1")) {
					current_addr = (dev_info->UFM_Line *
							LATTICE_COL_SIZE) /
						       32;

					memset(data_buf, 0, sizeof(data_buf));

					memcpy(data_buf, tmp_buf,
					       LATTICE_COL_SIZE);

					ShiftData(data_buf,
						  &dev_info->UFM[current_addr],
						  LATTICE_COL_SIZE);
#ifdef VERBOSE_DEBUG
					printf("%x %x %x %x\n",
					       dev_info->UFM[current_addr],
					       dev_info->UFM[current_addr + 1],
					       dev_info->UFM[current_addr + 2],
					       dev_info->UFM[current_addr + 3]);
#endif
					//each data has 128bits(4*unsigned int), so the for-loop need to be run 4 times

					for (i = 0; i < sizeof(unsigned int);
					     i++) {
						JED_CheckSum +=
							(dev_info->UFM
								 [current_addr +
								  i] >>
							 24) &
							0xff;
						JED_CheckSum +=
							(dev_info->UFM
								 [current_addr +
								  i] >>
							 16) &
							0xff;
						JED_CheckSum +=
							(dev_info->UFM
								 [current_addr +
								  i] >>
							 8) &
							0xff;
						JED_CheckSum +=
							(dev_info->UFM
								 [current_addr +
								  i]) &
							0xff;
					}

					dev_info->UFM_Line++;
				} else {
					CPLD_DEBUG("[%s]UFM Line: %d\n",
						   __func__,
						   dev_info->UFM_Line);
					UFMStart = 0;
				}
			}
		} else if (CFEnd) {
			if (!startWith(tmp_buf,
				       TAG_CF_END /*"NOTE END CONFIG DATA"*/) &&
			    !startWith(tmp_buf, "L") && strlen(tmp_buf) != 1) {
				if (startWith(tmp_buf, "0") ||
				    startWith(tmp_buf, "1")) {
					current_addr = (dev_info->EndCF_Line *
							LATTICE_COL_SIZE) /
						       32;
					memset(data_buf, 0, sizeof(data_buf));
					memcpy(data_buf, tmp_buf,
					       LATTICE_COL_SIZE);
					ShiftData(
						data_buf,
						&dev_info->EndCF[current_addr],
						LATTICE_COL_SIZE);
#ifdef VERBOSE_DEBUG
					printf("dev_info->EndCF_Line : %d | %x %x %x %x\n",
					       dev_info->EndCF_Line,
					       dev_info->EndCF[current_addr],
					       dev_info->EndCF[current_addr + 1],
					       dev_info->EndCF[current_addr + 2],
					       dev_info->EndCF[current_addr +
							       3]);
#endif
					//each data has 128bits(4*unsigned int), so the for-loop need to be run 4 times
					for (i = 0; i < sizeof(unsigned int);
					     i++) {
						JED_CheckSum +=
							(dev_info->EndCF
								 [current_addr +
								  i] >>
							 24) &
							0xff;
						JED_CheckSum +=
							(dev_info->EndCF
								 [current_addr +
								  i] >>
							 16) &
							0xff;
						JED_CheckSum +=
							(dev_info->EndCF
								 [current_addr +
								  i] >>
							 8) &
							0xff;
						JED_CheckSum +=
							(dev_info->EndCF
								 [current_addr +
								  i]) &
							0xff;
					}

					dev_info->EndCF_Line++;
				} else {
					CPLD_DEBUG("[%s]EndCF Line: %d\n",
						   __func__,
						   dev_info->EndCF_Line);
					CFEnd = 0;
				}
			}
		}
	}

	JED_CheckSum = JED_CheckSum & 0xffff;

	if (dev_info->CheckSum != JED_CheckSum || dev_info->CheckSum == 0) {
		printf("[%s] JED File CheckSum Error\n", __func__);
		ret = -1;
	} else {
		CPLD_DEBUG("[%s] JED File CheckSum OKay\n", __func__);
	}
	return ret;
}

void jed_file_parse_header(FILE *jed_fd)
{
	//File
	char tmp_buf[160];

	//Header paser
	while (fgets(tmp_buf, 120, jed_fd) != NULL) {
#ifdef VERBOSE_DEBUG
		printf("%s \n", tmp_buf);
#endif
		if (tmp_buf[0] == 0x4C) { // "L"
			break;
		}
	}
}

/******************************************************************************/
/***************************     JTAG       ***********************************/
/******************************************************************************/
static int jtag_cpld_get_id(unsigned int *dev_id)
{
	unsigned int dr_data[4] = { 0 };

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	//Check the IDCODE_PUB
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_IDCODE_PUB);
	dr_data[0] = 0x0;
	ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, 32, dr_data);

	*dev_id = dr_data[0];
	printf("CPLD DeviceID: %X\n", *dev_id);

	return 0;
}

static unsigned int jtag_check_device_status(int mode)
{
	int RETRY = MAX_RETRY;
	unsigned int buf[4] = { 0 };

	switch (mode) {
	case CHECK_BUSY:
		//      ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
		do {
			ast_jtag_sir_xfer(JTAG_STATE_TLRESET,
					  LATTICE_INS_LENGTH,
					  LCMXO2_LSC_CHECK_BUSY);
			usleep(1000);
			buf[0] = 0x0;
			ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, 8, &buf[0]);
			buf[0] = (buf[0] >> 7) & 0x1;
			if (buf[0] == 0x0) {
				break;
			}
			RETRY--;
		} while (RETRY);
		break;

	case CHECK_STATUS:
		//      ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
		do {
			ast_jtag_sir_xfer(JTAG_STATE_TLRESET,
					  LATTICE_INS_LENGTH,
					  LCMXO2_LSC_READ_STATUS);
			usleep(1000);
			buf[0] = 0x0;
			ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, 32, &buf[0]);
			buf[0] = (buf[0] >> 12) & 0x3;
			if (buf[0] == 0x0) {
				break;
			}
			RETRY--;
		} while (RETRY);
		break;

	default:
		break;
	}

	return buf[0];
}

static int jtag_cpld_start()
{
	unsigned int dr_data[4] = { 0 };
	int ret = 0;

	//Enable the Flash (Transparent Mode)
	CPLD_DEBUG("[%s] Enter transparent mode!\n", __func__);

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_ISC_ENABLE_X);
	dr_data[0] = 0x08;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH, dr_data);

	//LSC_CHECK_BUSY(0xF0) instruction
	dr_data[0] = jtag_check_device_status(CHECK_BUSY);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	CPLD_DEBUG("[%s] READ_STATUS(0x3C)!\n", __func__);
	//READ_STATUS(0x3C) instruction
	dr_data[0] = jtag_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	return ret;
}

static int jtag_cpld_end()
{
	int ret = 0;
	unsigned int status;

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_ISC_PROGRAM_DONE);

	CPLD_DEBUG("[%s] Program DONE bit\n", __func__);

	//Read CHECK_BUSY

	status = jtag_check_device_status(CHECK_BUSY);
	if (status != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, status);
		ret = -1;
	}

	CPLD_DEBUG("[%s] READ_STATUS: %x\n", __func__, ret);

	status = jtag_check_device_status(CHECK_STATUS);
	if (status != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, status);
		ret = -1;
	}

	CPLD_DEBUG("[%s] READ_STATUS: %x\n", __func__, ret);

	//Exit the programming mode
	//Shift in ISC DISABLE(0x26) instruction
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_ISC_DISABLE);

	return ret;
}

static int jtag_cpld_check_id()
{
#if 0
  unsigned int dr_data[4] = {0};
  int ret = -1;
  unsigned int i;

  //RUNTEST IDLE
  ast_jtag_run_test_idle(1, JTAG_STATE_TLRESET, 3);

  CPLD_DEBUG("[%s] RUNTEST IDLE\n", __func__);

  //Check the IDCODE_PUB
  ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH, LCMXO2_IDCODE_PUB);
  dr_data[0] = 0x0;
  ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, 32, dr_data);

  CPLD_DEBUG("[%s] ID Code: %x\n", __func__, dr_data[0]);

  for (i = 0; i < ARRAY_SIZE(lattice_dev_list); i++)
  {
    if (dr_data[0] == lattice_dev_list[i].dev_id)
    {
      ret = 0;
      break;
    }
  }

  return ret;
#else
	return 0;
#endif
}

/*write cf data*/
static int jtag_sendCFdata(CPLDInfo *dev_info)
{
	int ret = 0;
	int CurrentAddr = 0;
	unsigned int i;
	unsigned status;

	for (i = 0; i < dev_info->CF_Line; i++) {
		printf("Writing Data: %d/%d (%.2f%%) \r", (i + 1),
		       dev_info->CF_Line,
		       (((i + 1) / (float)dev_info->CF_Line) * 100));

		CurrentAddr = (i * LATTICE_COL_SIZE) / 32;

		//RUNTEST    IDLE    15 TCK    1.00E-003 SEC;
		//    ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);

		//set page to program page
		ast_jtag_sir_xfer(JTAG_STATE_PAUSEIR, LATTICE_INS_LENGTH,
				  LCMXO2_LSC_PROG_INCR_NV);

		//send data
		ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LATTICE_COL_SIZE,
				  &dev_info->CF[CurrentAddr]);

		//usleep(1000);

		status = jtag_check_device_status(CHECK_BUSY);
		if (status != 0) {
			printf("[%s]Write CF Error, status = %x\n", __func__,
			       status);
			ret = -1;
			break;
		}
	}

	printf("\n");

	return ret;
}

/*write ufm data if need*/
static int jtag_sendUFMdata(CPLDInfo *dev_info)
{
	int ret = 0;
	int CurrentAddr = 0;
	unsigned int i;
	unsigned int status;

	for (i = 0; i < dev_info->UFM_Line; i++) {
		CurrentAddr = (i * LATTICE_COL_SIZE) / 32;

		//RUNTEST    IDLE    15 TCK    1.00E-003 SEC;
		//    ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);

		//set page to program page
		ast_jtag_sir_xfer(JTAG_STATE_PAUSEIR, LATTICE_INS_LENGTH,
				  LCMXO2_LSC_PROG_INCR_NV);

		//send data
		ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LATTICE_COL_SIZE,
				  &dev_info->UFM[CurrentAddr]);

		//usleep(1000);

		status = jtag_check_device_status(CHECK_BUSY);
		if (status != 0) {
			printf("[%s]Write UFM Error, status = %x\n", __func__,
			       status);
			ret = -1;
			break;
		}
	}

	return ret;
}

static int jtag_cpld_get_ver(unsigned int *ver)
{
	int ret;
	unsigned int dr_data[4] = { 0 };

	ret = jtag_cpld_check_id();

	if (ret < 0) {
		printf("[%s] Unknown Device ID!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_start();
	if (ret < 0) {
		printf("[%s] Enter Transparent mode Error!\n", __func__);
		goto error_exit;
	}

	//Shift in READ USERCODE(0xC0) instruction;
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_USERCODE);

	CPLD_DEBUG("[%s] READ USERCODE(0xC0)\n", __func__);

	//Read UserCode
	dr_data[0] = 0;
	ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, 32, dr_data);
	*ver = dr_data[0];

	printf("CPLD Version: %X\n", *ver);
	ret = jtag_cpld_end();
	if (ret < 0) {
		printf("[%s] Exit Transparent Mode Failed!\n", __func__);
	}

error_exit:
	return ret;
}

static int jtag_cpld_checksum(FILE *jed_fd, unsigned int *crc)
{
	CPLDInfo dev_info = { 0 };
	int cf_size = 0;
	int endcfg_size = 0;
	int ufm_size = 0;
	int ret;
	unsigned int i, j;
	unsigned int buff[4] = { 0 };

	CPLD_DEBUG("[%s]\n", __func__);

	*crc = 0;

	ret = jtag_cpld_check_id();
	if (ret < 0) {
		printf("[%s] Unknown Device ID!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//get update data size
	ret = jed_update_data_size(jed_fd, &cf_size, &ufm_size, &endcfg_size);
	if (ret < 0) {
		printf("[%s] Update Data Size Error!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//parse info from JED file and calculate checksum
	ret = jed_file_parser(jed_fd, &dev_info, cf_size, ufm_size,
			      endcfg_size);
	if (ret < 0) {
		printf("[%s] JED file CheckSum Error!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_start();
	if (ret < 0) {
		printf("[%s] Enter Transparent mode Error!\n", __func__);
		goto error_exit;
	}

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_INIT_ADDRESS);

	buff[0] = 0x04;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH, &buff[0]);
	usleep(1000);

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_READ_INCR_NV);
	usleep(1000);

	//  CPLD_DEBUG("[%s] dev_info->CF_Line: %d\n", __func__, dev_info->CF_Line);

	for (i = 0; i < dev_info.CF_Line; i++) {
		memset(buff, 0, sizeof(buff));
		ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, LATTICE_COL_SIZE, buff);
#ifdef VERBOSE_DEBUG
		printf("[%d] ", i);
		for (j = 0; j < 4; j++) {
			printf("%x ", buff[j]);
		}
		printf("\n");
#endif
		for (j = 0; j < sizeof(unsigned int); j++) {
			*crc += (buff[j] >> 24) & 0xff;
			*crc += (buff[j] >> 16) & 0xff;
			*crc += (buff[j] >> 8) & 0xff;
			*crc += (buff[j]) & 0xff;
		}
	}

	for (i = 0; i < dev_info.EndCF_Line; i++) {
#ifdef VERBOSE_DEBUG
		printf("[%d] ", i);
		for (j = 0; j < 4; j++) {
			printf("%x ", dev_info.EndCF[j]);
		}
		printf("\n");
#endif
		for (j = 0; j < sizeof(unsigned int); j++) {
			*crc += (dev_info.EndCF[i + j] >> 24) & 0xff;
			*crc += (dev_info.EndCF[i + j] >> 16) & 0xff;
			*crc += (dev_info.EndCF[i + j] >> 8) & 0xff;
			*crc += (dev_info.EndCF[i + j]) & 0xff;
		}
	}

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_INIT_ADDR_UFM);

	buff[0] = 0x04;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH, &buff[0]);
	usleep(1000);

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_READ_INCR_NV);
	usleep(1000);

	for (i = 0; i < dev_info.UFM_Line; i++) {
		memset(buff, 0, sizeof(buff));
		ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, LATTICE_COL_SIZE, buff);
#ifdef VERBOSE_DEBUG
		printf("[%d] ", i);
		for (j = 0; j < 4; j++) {
			printf("%x ", buff[j]);
		}
		printf("\n");
#endif
		for (j = 0; j < sizeof(unsigned int); j++) {
			*crc += (buff[j] >> 24) & 0xff;
			*crc += (buff[j] >> 16) & 0xff;
			*crc += (buff[j] >> 8) & 0xff;
			*crc += (buff[j]) & 0xff;
		}
	}

	*crc &= 0xFFFF;

	ret = jtag_cpld_end();
	if (ret < 0) {
		printf("[%s] Exit Transparent Mode Failed!\n", __func__);
		goto error_exit;
	}

error_exit:
	if (NULL != dev_info.CF) {
		free(dev_info.CF);
	}

	if (NULL != dev_info.EndCF) {
		free(dev_info.EndCF);
	}

	if (NULL != dev_info.UFM) {
		free(dev_info.UFM);
	}

	return ret;
}

static int jtag_cpld_verify(CPLDInfo *dev_info)
{
	unsigned int i;
	int result;
	int current_addr = 0;
	unsigned int buff[4] = { 0 };
	int ret = 0;

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_INIT_ADDRESS);

	buff[0] = 0x04;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH, &buff[0]);
	usleep(1000);

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_READ_INCR_NV);
	usleep(1000);

	CPLD_DEBUG("[%s] dev_info->CF_Line: %d\n", __func__, dev_info->CF_Line);

	for (i = 0; i < dev_info->CF_Line; i++) {
		printf("Verify Data: %d/%d (%.2f%%) \r", (i + 1),
		       dev_info->CF_Line,
		       (((i + 1) / (float)dev_info->CF_Line) * 100));

		current_addr = (i * LATTICE_COL_SIZE) / 32;

		memset(buff, 0, sizeof(buff));

		ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, LATTICE_COL_SIZE, buff);

		result = memcmp(buff, &dev_info->CF[current_addr],
				sizeof(unsigned int));

		if (result) {
			CPLD_DEBUG(
				"\nPage#%d (%x %x %x %x) did not match with CF (%x %x %x %x)\n",
				i, buff[0], buff[1], buff[2], buff[3],
				dev_info->CF[current_addr],
				dev_info->CF[current_addr + 1],
				dev_info->CF[current_addr + 2],
				dev_info->CF[current_addr + 3]);
			ret = -1;
			break;
		}
	}

	printf("\n");

	if (-1 == ret) {
		printf("\n[%s] Verify CPLD FW Error\n", __func__);
	} else {
		CPLD_DEBUG("\n[%s] Verify CPLD FW Pass\n", __func__);
	}

	return ret;
}

static int jtag_cpld_lcm3d_verify(CPLDInfo *dev_info)
{
	unsigned int i;
	int result;
	int current_addr = 0;
	unsigned int buff[4] = { 0 };
	int ret = 0;
	unsigned int operand;

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_INIT_ADDRESS);

	operand = 0x000100;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LCMXO3D_INIT_ADD_BITS_LEN,
			  &operand);
	usleep(1000);

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_READ_INCR_NV);
	usleep(1000);

	CPLD_DEBUG("[%s] dev_info->CF_Line: %d\n", __func__, dev_info->CF_Line);

	for (i = 0; i < dev_info->CF_Line; i++) {
		printf("Verify Data: %d/%d (%.2f%%) \r", (i + 1),
		       dev_info->CF_Line,
		       (((i + 1) / (float)dev_info->CF_Line) * 100));

		current_addr = (i * LATTICE_COL_SIZE) / 32;

		memset(buff, 0, sizeof(buff));

		ast_jtag_tdo_xfer(JTAG_STATE_TLRESET, LATTICE_COL_SIZE, buff);

		result = memcmp(buff, &dev_info->CF[current_addr],
				sizeof(unsigned int));

		if (result) {
			CPLD_DEBUG(
				"\nPage#%d (%x %x %x %x) did not match with CF (%x %x %x %x)\n",
				i, buff[0], buff[1], buff[2], buff[3],
				dev_info->CF[current_addr],
				dev_info->CF[current_addr + 1],
				dev_info->CF[current_addr + 2],
				dev_info->CF[current_addr + 3]);
			ret = -1;
			break;
		}
	}

	printf("\n");

	if (-1 == ret) {
		printf("\n[%s] Verify CPLD FW Error\n", __func__);
	} else {
		CPLD_DEBUG("\n[%s] Verify CPLD FW Pass\n", __func__);
	}

	return ret;
}

static int jtag_cpld_erase(int erase_type)
{
	unsigned int dr_data[4] = { 0 };
	int ret = 0;

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	//Erase the Flash
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_ISC_ERASE);

	CPLD_DEBUG("[%s] ERASE(0x0E)!\n", __func__);

	switch (erase_type) {
	case Only_CF:
		dr_data[0] = 0x04; //0x4=CF
		break;

	case Both_CF_UFM:
		dr_data[0] = 0x0C; //0xC=CF and UFM
		break;
	}

	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH, dr_data);

	dr_data[0] = jtag_check_device_status(CHECK_BUSY);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	//Shift in LSC_READ_STATUS(0x3C) instruction
	CPLD_DEBUG("[%s] READ_STATUS!\n", __func__);

	dr_data[0] = jtag_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("Erase Failed, status = %x\n", dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	CPLD_DEBUG("[%s] Erase Done!\n", __func__);

	return ret;
}

static int jtag_cpld_lcm3d_erase()
{
	unsigned int dr_data[4] = { 0 };
	unsigned int operand;
	int ret = 0;

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	//Erase the Flash
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_ISC_ERASE);

	CPLD_DEBUG("[%s] ERASE(0x0E)!\n", __func__);

	operand = 0x000100;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LCMXO3D_ERASE_BITS_LEN, &operand);

	dr_data[0] = jtag_check_device_status(CHECK_BUSY);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	//Shift in LSC_READ_STATUS(0x3C) instruction
	CPLD_DEBUG("[%s] READ_STATUS!\n", __func__);

	dr_data[0] = jtag_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("Erase Failed, status = %x\n", dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	CPLD_DEBUG("[%s] Erase Done!\n", __func__);

	return ret;
}

static int jtag_cpld_program(CPLDInfo *dev_info)
{
	int ret = 0;
	unsigned int dr_data[4] = { 0 };

	//Program CFG
	CPLD_DEBUG("[%s] Program CFG \n", __func__);
	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	//Shift in LSC_INIT_ADDRESS(0x46) instruction
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_INIT_ADDRESS);

	CPLD_DEBUG("[%s] INIT_ADDRESS(0x46) \n", __func__);

	ret = jtag_sendCFdata(dev_info);
	if (ret < 0) {
		goto error_exit;
	}

	if (dev_info->UFM_Line) {
		//    ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
		//program UFM
		ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
				  LCMXO2_LSC_INIT_ADDR_UFM);

		ret = jtag_sendUFMdata(dev_info);
		if (ret < 0) {
			goto error_exit;
		}
	}

	CPLD_DEBUG("[%s] Update CPLD done \n", __func__);

	//Read the status bit
	dr_data[0] = jtag_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
		goto error_exit;
	}

	//Program USERCODE
	CPLD_DEBUG("[%s] Program USERCODE\n", __func__);

	//Write UserCode
	dr_data[0] = dev_info->Version;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, 32, dr_data);

	CPLD_DEBUG("[%s] Write USERCODE: %x\n", __func__, dr_data[0]);

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_ISC_PROGRAM_USERCOD);
	usleep(2000);

	CPLD_DEBUG("[%s] PROGRAM USERCODE(0xC2)\n", __func__);

	//Read the status bit
	dr_data[0] = jtag_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
		goto error_exit;
	}

	CPLD_DEBUG("[%s] READ_STATUS: %x\n", __func__, dr_data[0]);

error_exit:
	return ret;
}

static int jtag_cpld_lcm3d_program(CPLDInfo *dev_info)
{
	int ret = 0;
	unsigned int dr_data[4] = { 0 };
	unsigned int operand;

	//Program CFG
	CPLD_DEBUG("[%s] Program CFG \n", __func__);
	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	//Shift in LSC_INIT_ADDRESS(0x46) instruction
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_LSC_INIT_ADDRESS);
	operand = 0x000100;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, LCMXO3D_INIT_ADD_BITS_LEN,
			  &operand);

	CPLD_DEBUG("[%s] INIT_ADDRESS(0x46) \n", __func__);

	ret = jtag_sendCFdata(dev_info);
	if (ret < 0) {
		goto error_exit;
	}

	if (dev_info->UFM_Line) {
		//    ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
		//program UFM
		ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
				  LCMXO2_LSC_INIT_ADDR_UFM);

		ret = jtag_sendUFMdata(dev_info);
		if (ret < 0) {
			goto error_exit;
		}
	}

	CPLD_DEBUG("[%s] Update CPLD done \n", __func__);

	//Read the status bit
	dr_data[0] = jtag_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
		goto error_exit;
	}

	//Program USERCODE
	CPLD_DEBUG("[%s] Program USERCODE\n", __func__);

	//Write UserCode
	dr_data[0] = dev_info->Version;
	ast_jtag_tdi_xfer(JTAG_STATE_TLRESET, 32, dr_data);

	CPLD_DEBUG("[%s] Write USERCODE: %x\n", __func__, dr_data[0]);

	//  ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
	ast_jtag_sir_xfer(JTAG_STATE_TLRESET, LATTICE_INS_LENGTH,
			  LCMXO2_ISC_PROGRAM_USERCOD);
	usleep(2000);

	CPLD_DEBUG("[%s] PROGRAM USERCODE(0xC2)\n", __func__);

	//Read the status bit
	dr_data[0] = jtag_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
		goto error_exit;
	}

	CPLD_DEBUG("[%s] READ_STATUS: %x\n", __func__, dr_data[0]);

error_exit:
	return ret;
}

static int jtag_cpld_update(FILE *jed_fd, char *key, char is_signed)
{
	CPLDInfo dev_info = { 0 };
	int cf_size = 0;
	int endcfg_size = 0;
	int ufm_size = 0;
	int erase_type = 0;
	int ret;

	CPLD_DEBUG("[%s]\n", __func__);
	UNUSED(key);
	UNUSED(is_signed);
	ret = jtag_cpld_check_id();
	if (ret < 0) {
		printf("[%s] Unknown Device ID!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//get update data size
	ret = jed_update_data_size(jed_fd, &cf_size, &ufm_size, &endcfg_size);
	if (ret < 0) {
		printf("[%s] Update Data Size Error!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//parse info from JED file and calculate checksum
	ret = jed_file_parser(jed_fd, &dev_info, cf_size, ufm_size,
			      endcfg_size);
	if (ret < 0) {
		printf("[%s] JED file CheckSum Error!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_start();
	if (ret < 0) {
		printf("[%s] Enter Transparent mode Error!\n", __func__);
		goto error_exit;
	}

	if (dev_info.UFM_Line) {
		erase_type = Both_CF_UFM;
	} else {
		erase_type = Only_CF;
	}

	ret = jtag_cpld_erase(erase_type);
	if (ret < 0) {
		printf("[%s] Erase failed!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_program(&dev_info);
	if (ret < 0) {
		printf("[%s] Program failed!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_verify(&dev_info);
	if (ret < 0) {
		printf("[%s] Verify Failed!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_end();
	if (ret < 0) {
		printf("[%s] Exit Transparent Mode Failed!\n", __func__);
		goto error_exit;
	}

error_exit:
	if (NULL != dev_info.CF) {
		free(dev_info.CF);
	}

	if (NULL != dev_info.EndCF) {
		free(dev_info.EndCF);
	}

	if (NULL != dev_info.UFM) {
		free(dev_info.UFM);
	}

	return ret;
}

static int jtag_cpld_lcm3d_update(FILE *jed_fd, char *key, char is_signed)
{
	CPLDInfo dev_info = { 0 };
	int cf_size = 0;
	int ufm_size = 0;
	int endcfg_size = 0;
	int ret;

	CPLD_DEBUG("[%s]\n", __func__);
	UNUSED(key);
	UNUSED(is_signed);
	ret = jtag_cpld_check_id();
	if (ret < 0) {
		printf("[%s] Unknown Device ID!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//get update data size
	ret = jed_update_data_size(jed_fd, &cf_size, &ufm_size, &endcfg_size);
	if (ret < 0) {
		printf("[%s] Update Data Size Error!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//parse info from JED file and calculate checksum
	ret = jed_file_parser(jed_fd, &dev_info, cf_size, ufm_size,
			      endcfg_size);
	if (ret < 0) {
		printf("[%s] JED file CheckSum Error!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_start();
	if (ret < 0) {
		printf("[%s] Enter Transparent mode Error!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_lcm3d_erase();
	if (ret < 0) {
		printf("[%s] Erase failed!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_lcm3d_program(&dev_info);
	if (ret < 0) {
		printf("[%s] Program failed!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_lcm3d_verify(&dev_info);
	if (ret < 0) {
		printf("[%s] Verify Failed!\n", __func__);
		goto error_exit;
	}

	ret = jtag_cpld_end();
	if (ret < 0) {
		printf("[%s] Exit Transparent Mode Failed!\n", __func__);
		goto error_exit;
	}

error_exit:
	if (NULL != dev_info.CF) {
		free(dev_info.CF);
	}

	if (NULL != dev_info.EndCF) {
		free(dev_info.EndCF);
	}

	if (NULL != dev_info.UFM) {
		free(dev_info.UFM);
	}

	return ret;
}

static int yzbb_jtag_cpld_get_ver(unsigned int *ver)
{
	UNUSED(ver);
	return 0;
}

static int yzbb_jtag_cpld_get_id(unsigned int *dev_id)
{
	UNUSED(dev_id);
	return 0;
}

/******************************************************************************/
/***************************      I2C       ***********************************/
/******************************************************************************/
static int i2c_cpld_get_id(unsigned int *dev_id)
{
	uint8_t cmd[4] = { 0xE0, 0x00, 0x00, 0x00 };
	uint8_t dr_data[4];
	int ret = -1;

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, cmd, sizeof(cmd),
				    dr_data, sizeof(dr_data));
	if (ret != 0) {
		printf("read_device_id() failed\n");
		return ret;
	}
	CPLD_DEBUG("Read Device ID = 0x%X 0x%X 0x%X 0x%X -\n", dr_data[0],
		   dr_data[1], dr_data[2], dr_data[3]);
	*dev_id = byte_to_int(dr_data);
	printf("CPLD DeviceID: %X\n", *dev_id);

	return 0;
}

static unsigned int i2c_check_device_status(int mode)
{
	int RETRY = MAX_RETRY;
	uint8_t flag[1];
	uint8_t status[4];
	uint8_t busy_flag_cmd[4] = { 0xF0, 0x00, 0x00, 0x00 };
	uint8_t status_cmd[4] = { 0x3C, 0x00, 0x00, 0x00 };
	int ret = -1;
	unsigned int rc = 1;
	unsigned int tmp;

	switch (mode) {
	case CHECK_BUSY:
		do {
			ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
						    busy_flag_cmd,
						    sizeof(busy_flag_cmd), flag,
						    sizeof(flag));
			if (ret != 0) {
				ERR_PRINT("read_busy_flag()");
				break;
			}
			if (!(flag[0] & 0x80)) {
				rc = 0;
				break;
			}
			usleep(1000);
			RETRY--;
		} while (RETRY);
		break;

	case CHECK_STATUS:
		do {
			ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
						    status_cmd,
						    sizeof(status_cmd), status,
						    sizeof(status));
			if (ret != 0) {
				ERR_PRINT("read_status_flag()");
				break;
			}
			tmp = byte_to_int(status);
			tmp = (tmp >> 12) & 0x3;
			if (!tmp) {
				rc = 0;
				break;
			}
			usleep(1000);
			RETRY--;
		} while (RETRY);
		break;

	default:
		break;
	}

	return rc;
}

static int i2c_cpld_start()
{
	uint8_t enable_program_cmd[3] = { 0x74, 0x08, 0x00 };
	unsigned int dr_data[4] = { 0 };
	int ret = 0;

	//Enable the Flash (Transparent Mode)
	CPLD_DEBUG("[%s] Enter transparent mode!\n", __func__);

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
				    enable_program_cmd,
				    sizeof(enable_program_cmd), NULL, 0);
	if (ret != 0) {
		printf("Enable the Flash (Transparent Mode) failed\n");
		return ret;
	}

	//LSC_CHECK_BUSY(0xF0) instruction
	dr_data[0] = i2c_check_device_status(CHECK_BUSY);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	CPLD_DEBUG("[%s] READ_STATUS(0x3C)!\n", __func__);
	//READ_STATUS(0x3C) instruction
	dr_data[0] = i2c_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	return ret;
}

static int i2c_cpld_end()
{
	int ret = 0;
	unsigned int status;
	uint8_t program_done_cmd[4] = { 0x5E, 0x00, 0x00, 0x00 };
	uint8_t refresh_cmd[3] = { 0x79, 0x00, 0x00 };
	uint8_t disable_cmd[4] = { 0x26, 0x00, 0x00, 0x00 };

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, program_done_cmd,
				    sizeof(program_done_cmd), NULL, 0);
	if (ret != 0) {
		printf("i2c_cpld_end() program done failed\n");
		return ret;
	}

	CPLD_DEBUG("[%s] Program DONE bit\n", __func__);

	//Read CHECK_BUSY

	status = i2c_check_device_status(CHECK_BUSY);
	if (status != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, status);
		ret = -1;
	}

	CPLD_DEBUG("[%s] READ_STATUS: %x\n", __func__, ret);

	status = i2c_check_device_status(CHECK_STATUS);
	if (status != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, status);
		ret = -1;
	}

	CPLD_DEBUG("[%s] READ_STATUS: %x\n", __func__, ret);

	// Refresh CPLD
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, refresh_cmd,
				    sizeof(refresh_cmd), NULL, 0);
	if (ret != 0) {
		ERR_PRINT("i2c_refresh(): Refresh CPLD failed");
		return ret;
	}
	usleep(1000000);

	//Exit the programming mode
	//Shift in ISC DISABLE(0x26) instruction
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, disable_cmd,
				    sizeof(disable_cmd), NULL, 0);
	if (ret != 0) {
		printf("i2c_cpld_end() disable failed\n");
		return ret;
	}

	return ret;
}

static int i2c_cpld_check_id()
{
#if 0
  unsigned int dr_data[4] = {0};
  int ret = -1;
  unsigned int i;

  i2c_cpld_get_id(dr_data);

  CPLD_DEBUG("[%s] ID Code: %x\n", __func__, dr_data[0]);

  for (i = 0; i < ARRAY_SIZE(lattice_dev_list); i++)
  {
    if (dr_data[0] == lattice_dev_list[i].dev_id)
    {
      ret = 0;
      break;
    }
  }

  return ret;
#else
	return 0;
#endif
}

/*write cf data*/
static int i2c_sendCFdata(CPLDInfo *dev_info)
{
	uint8_t write_page_cmd[4] = { 0x70, 0x00, 0x00, 0x01 };
	uint8_t program_page_cmd[1 + 3 + 16] = {
		0
	}; /* 1B Command + 3B Operands + 16B Write Data */
	int ret = 0;
	int CurrentAddr = 0;
	unsigned int i;
	unsigned status;

	memcpy(&program_page_cmd[0], write_page_cmd, 4);
	for (i = 0; i < dev_info->CF_Line; i++) {
		printf("Writing Data: %d/%d (%.2f%%) \r", (i + 1),
		       dev_info->CF_Line,
		       (((i + 1) / (float)dev_info->CF_Line) * 100));

		CurrentAddr = (i * LATTICE_COL_SIZE) / 32;

		memcpy(&program_page_cmd[4], &dev_info->CF[CurrentAddr], 16);
		swap_bit_byte(&program_page_cmd[4], 16);
		ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
					    program_page_cmd, 1 + 3 + 16, NULL,
					    0);
		if (ret != 0) {
			ERR_PRINT("program_flash(): Program Page Data");
			return ret;
		}
		//usleep(1000);

		status = i2c_check_device_status(CHECK_BUSY);
		if (status != 0) {
			printf("[%s]Write CF Error, status = %x\n", __func__,
			       status);
			ret = -1;
			break;
		}
	}

	printf("\n");

	return ret;
}

/*write ufm data if need*/
static int i2c_sendUFMdata(CPLDInfo *dev_info)
{
	uint8_t write_page_cmd[4] = { 0x70, 0x00, 0x00, 0x01 };
	uint8_t program_page_cmd[1 + 3 + 16] = {
		0
	}; /* 1B Command + 3B Operands + 16B Write Data */
	int ret = 0;
	int CurrentAddr = 0;
	unsigned int i;
	unsigned int status;

	memcpy(&program_page_cmd[0], write_page_cmd, 4);
	for (i = 0; i < dev_info->UFM_Line; i++) {
		CurrentAddr = (i * LATTICE_COL_SIZE) / 32;

		memcpy(&program_page_cmd[4], &dev_info->UFM[CurrentAddr], 16);
		swap_bit_byte(&program_page_cmd[4], 16);
		ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
					    program_page_cmd,
					    sizeof(program_page_cmd), NULL, 0);
		if (ret != 0) {
			ERR_PRINT("program_flash(): Program Page Data");
			return ret;
		}
		//usleep(1000);

		status = i2c_check_device_status(CHECK_BUSY);
		if (status != 0) {
			printf("[%s]Write UFM Error, status = %x\n", __func__,
			       status);
			ret = -1;
			break;
		}
	}

	return ret;
}

static int i2c_cpld_get_ver(unsigned int *ver)
{
	int ret;
	uint8_t user_code_cmd[4] = { 0xC0, 0x00, 0x00, 0x00 };
	uint8_t dr_data[4];

	ret = i2c_cpld_check_id();

	if (ret < 0) {
		printf("[%s] Unknown Device ID!\n", __func__);
		goto error_exit;
	}

	ret = i2c_cpld_start();
	if (ret < 0) {
		printf("[%s] Enter Transparent mode Error!\n", __func__);
		goto error_exit;
	}

	//Shift in READ USERCODE(0xC0) instruction;
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, user_code_cmd,
				    sizeof(user_code_cmd), dr_data,
				    sizeof(dr_data));
	if (ret != 0) {
		printf("i2c_cpld_get_ver() read usercode failed\n");
		return ret;
	}
	CPLD_DEBUG("[%s] READ USERCODE(0xC0)\n", __func__);

	//Read UserCode
	*ver = byte_to_int(dr_data);
	CPLD_DEBUG("USERCODE= 0x%X\n", *ver);
	printf("CPLD Version: %X\n", *ver);

	ret = i2c_cpld_end();
	if (ret < 0) {
		printf("[%s] Exit Transparent Mode Failed!\n", __func__);
	}

error_exit:
	return ret;
}

static int i2c_cpld_checksum(FILE *jed_fd, unsigned int *crc)
{
	unsigned int i, j;
	uint8_t buff[16] = { 0 };
	int ret = 0;
	uint8_t reset_addr_cmd[4] = { 0x46, 0x00, 0x00, 0x00 };
	/* 0x73 0x00: i2c, 0x73 0x10: JTAG/SSPI */
	uint8_t read_page_cmd[4] = { 0x73, 0x00, 0x00, 0x01 };

	CPLDInfo dev_info = { 0 };
	int cf_size = 0;
	int endcfg_size = 0;
	int ufm_size = 0;

	CPLD_DEBUG("[%s]\n", __func__);

	*crc = 0;

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//get update data size
	ret = jed_update_data_size(jed_fd, &cf_size, &ufm_size, &endcfg_size);
	if (ret < 0) {
		printf("[%s] Update Data Size Error!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//parse info from JED file and calculate checksum
	ret = jed_file_parser(jed_fd, &dev_info, cf_size, ufm_size,
			      endcfg_size);
	if (ret < 0) {
		printf("[%s] JED file CheckSum Error!\n", __func__);
		goto error_exit;
	}

	ret = i2c_cpld_start();
	if (ret < 0) {
		printf("[%s] Enter Transparent mode Error!\n", __func__);
		goto error_exit;
	}

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, reset_addr_cmd,
				    sizeof(reset_addr_cmd), NULL, 0);
	if (ret != 0) {
		ERR_PRINT("i2c_cpld_checksum(): Reset Page Address");
		return ret;
	}

	for (i = 0; i < dev_info.CF_Line; i++) {
		memset(buff, 0, sizeof(buff));
		ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
					    read_page_cmd,
					    sizeof(read_page_cmd), buff,
					    sizeof(buff));
		if (ret != 0) {
			ERR_PRINT("i2c_cpld_checksum(): Read Data fail");
			return ret;
		}
#ifdef VERBOSE_DEBUG
		printf("[%d] ", i);
		for (j = 0; j < 16; j++) {
			printf("%x ", buff[j]);
		}
		printf("\n");
#endif
		swap_bit_byte(buff, 16);
		for (j = 0; j < 16; j++) {
			*crc += buff[j] & 0xff;
		}
	}

	reset_addr_cmd[0] = 0x47;
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, reset_addr_cmd,
				    sizeof(reset_addr_cmd), NULL, 0);
	if (ret != 0) {
		ERR_PRINT("i2c_cpld_checksum(): Reset Page Address");
		return ret;
	}

	for (i = 0; i < dev_info.UFM_Line; i++) {
		memset(buff, 0, sizeof(buff));
		ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
					    read_page_cmd,
					    sizeof(read_page_cmd), buff,
					    sizeof(buff));
		if (ret != 0) {
			ERR_PRINT("i2c_cpld_checksum(): Read Data fail");
			return ret;
		}
#ifdef VERBOSE_DEBUG
		printf("[%d] ", i);
		for (j = 0; j < 16; j++) {
			printf("%x ", buff[j]);
		}
		printf("\n");
#endif
		swap_bit_byte(buff, 16);
		for (j = 0; j < 16; j++) {
			*crc += buff[j] & 0xff;
		}
	}
	*crc &= 0xFFFF;

	ret = i2c_cpld_end();
	if (ret < 0) {
		printf("[%s] Exit Transparent Mode Failed!\n", __func__);
		goto error_exit;
	}
error_exit:
	if (NULL != dev_info.CF) {
		free(dev_info.CF);
	}

	if (NULL != dev_info.EndCF) {
		free(dev_info.EndCF);
	}

	if (NULL != dev_info.UFM) {
		free(dev_info.UFM);
	}
	return ret;
}

static int i2c_cpld_verify(CPLDInfo *dev_info)
{
	unsigned int i;
	int result;
	int current_addr = 0;
	uint8_t buff[16] = { 0 };
	int ret = 0;
	uint8_t reset_addr_cmd[4] = { 0x46, 0x00, 0x00, 0x00 };
	/* 0x73 0x00: i2c, 0x73 0x10: JTAG/SSPI */
	uint8_t read_page_cmd[4] = { 0x73, 0x00, 0x00, 0x01 };

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, reset_addr_cmd,
				    sizeof(reset_addr_cmd), NULL, 0);
	if (ret != 0) {
		ERR_PRINT("i2c_cpld_verify(): Reset Page Address");
		return ret;
	}

	CPLD_DEBUG("[%s] dev_info->CF_Line: %d\n", __func__, dev_info->CF_Line);

	for (i = 0; i < dev_info->CF_Line; i++) {
		printf("Verify Data: %d/%d (%.2f%%) \r", (i + 1),
		       dev_info->CF_Line,
		       (((i + 1) / (float)dev_info->CF_Line) * 100));
		current_addr = (i * LATTICE_COL_SIZE) / 32;
		memset(buff, 0, sizeof(buff));
		ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
					    read_page_cmd,
					    sizeof(read_page_cmd), buff,
					    sizeof(buff));
		if (ret != 0) {
			ERR_PRINT("i2c_cpld_verify(): Read Data fail");
			return ret;
		}
		swap_bit_byte(buff, 16);
		result = memcmp(buff, &dev_info->CF[current_addr],
				sizeof(unsigned int));
		if (result) {
			CPLD_DEBUG(
				"\nPage#%d (%x %x %x %x) did not match with CF (%x %x %x %x)\n",
				i, buff[0], buff[1], buff[2], buff[3],
				dev_info->CF[current_addr],
				dev_info->CF[current_addr + 1],
				dev_info->CF[current_addr + 2],
				dev_info->CF[current_addr + 3]);
			ret = -1;
			break;
		}
	}

	printf("\n");

	if (-1 == ret) {
		printf("\n[%s] Verify CPLD FW Error\n", __func__);
	} else {
		CPLD_DEBUG("\n[%s] Verify CPLD FW Pass\n", __func__);
	}

	return ret;
}

static int i2c_cpld_erase(int erase_type)
{
	unsigned int dr_data[4] = { 0 };
	uint8_t erase_flash_cmd[4] = { 0x0E, 0x0, 0x00, 0x00 };
	int ret = 0;

	CPLD_DEBUG("[%s] ERASE(0x0E)!\n", __func__);

	switch (erase_type) {
	case Only_CF:
		erase_flash_cmd[1] = 0x04; //0x4=CF
		break;

	case Both_CF_UFM:
		erase_flash_cmd[1] = 0x0C; //0xC=CF and UFM
		break;
	}

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, erase_flash_cmd,
				    sizeof(erase_flash_cmd), NULL, 0);
	if (ret != 0) {
		ERR_PRINT("i2c_cpld_erase()");
		return ret;
	}

	dr_data[0] = i2c_check_device_status(CHECK_BUSY);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	//Shift in LSC_READ_STATUS(0x3C) instruction
	CPLD_DEBUG("[%s] READ_STATUS!\n", __func__);

	dr_data[0] = i2c_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("Erase Failed, status = %x\n", dr_data[0]);
		ret = -1;
	} else {
		ret = 0;
	}

	CPLD_DEBUG("[%s] Erase Done!\n", __func__);

	return ret;
}

static int i2c_cpld_program(CPLDInfo *dev_info)
{
	int ret = 0;
	unsigned int dr_data[4] = { 0 };
	uint8_t reset_addr_cmd[4] = { 0x46, 0x00, 0x00, 0x00 };
	uint8_t prog_user_code_cmd[4] = { 0xC2, 0x00, 0x00, 0x00 };
	uint8_t prog_buf[1 + 3 + 4] = { 0 };

	//Program CFG
	CPLD_DEBUG("[%s] Program CFG \n", __func__);
	//Shift in LSC_INIT_ADDRESS(0x46) instruction
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, reset_addr_cmd,
				    sizeof(reset_addr_cmd), NULL, 0);
	if (ret != 0) {
		ERR_PRINT("i2c_cpld_program(): Reset CFG Page Address");
		return ret;
	}
	CPLD_DEBUG("[%s] INIT_ADDRESS(0x46) \n", __func__);

	ret = i2c_sendCFdata(dev_info);
	if (ret < 0) {
		goto error_exit;
	}

	if (dev_info->UFM_Line) {
		//    ast_jtag_run_test_idle(0, JTAG_STATE_TLRESET, 3);
		reset_addr_cmd[0] = 0x47;
		//program UFM
		ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1,
					    reset_addr_cmd,
					    sizeof(reset_addr_cmd), NULL, 0);
		if (ret != 0) {
			ERR_PRINT("i2c_cpld_program(): Reset UFM Page Address");
			return ret;
		}

		ret = i2c_sendUFMdata(dev_info);
		if (ret < 0) {
			goto error_exit;
		}
	}

	CPLD_DEBUG("[%s] Update CPLD done \n", __func__);

	//Read the status bit
	dr_data[0] = i2c_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
		goto error_exit;
	}

	//Program USERCODE
	CPLD_DEBUG("[%s] Program USERCODE\n", __func__);

	//Write UserCode
	dr_data[0] = dev_info->Version;
	dr_data[0] = byte_swap(dr_data[0]);

	CPLD_DEBUG("[%s] Write USERCODE: %x\n", __func__, dr_data[0]);

	memcpy(&prog_buf[0], prog_user_code_cmd, 4);
	memcpy(&prog_buf[4], &dr_data[0], 4);

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, prog_buf,
				    1 + 3 + 4, NULL, 0);
	if (ret != 0) {
		ERR_PRINT("i2c_cpld_program(): Reset CFG Page Address");
		return ret;
	}
	usleep(2000);
	CPLD_DEBUG("[%s] PROGRAM USERCODE(0xC2)\n", __func__);

	//Read the status bit
	dr_data[0] = i2c_check_device_status(CHECK_STATUS);

	if (dr_data[0] != 0) {
		printf("[%s] Device Busy, status = %x\n", __func__, dr_data[0]);
		ret = -1;
		goto error_exit;
	}

	CPLD_DEBUG("[%s] READ_STATUS: %x\n", __func__, dr_data[0]);

error_exit:
	return ret;
}

static int i2c_cpld_update(FILE *jed_fd, char *key, char is_signed)
{
	CPLDInfo dev_info = { 0 };
	int cf_size = 0;
	int endcfg_size = 0;
	int ufm_size = 0;
	int erase_type = 0;
	int ret;

	CPLD_DEBUG("[%s]\n", __func__);
	UNUSED(key);
	UNUSED(is_signed);
	ret = i2c_cpld_check_id();
	if (ret < 0) {
		printf("[%s] Unknown Device ID!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//get update data size
	ret = jed_update_data_size(jed_fd, &cf_size, &ufm_size, &endcfg_size);
	if (ret < 0) {
		printf("[%s] Update Data Size Error!\n", __func__);
		goto error_exit;
	}

	//set file pointer to the beginning
	fseek(jed_fd, 0, SEEK_SET);

	//parse info from JED file and calculate checksum
	ret = jed_file_parser(jed_fd, &dev_info, cf_size, ufm_size,
			      endcfg_size);
	if (ret < 0) {
		printf("[%s] JED file CheckSum Error!\n", __func__);
		goto error_exit;
	}

	ret = i2c_cpld_start();
	if (ret < 0) {
		printf("[%s] Enter Transparent mode Error!\n", __func__);
		goto error_exit;
	}

	if (dev_info.UFM_Line) {
		erase_type = Both_CF_UFM;
	} else {
		erase_type = Only_CF;
	}

	ret = i2c_cpld_erase(erase_type);
	if (ret < 0) {
		printf("[%s] Erase failed!\n", __func__);
		goto error_exit;
	}

	ret = i2c_cpld_program(&dev_info);
	if (ret < 0) {
		printf("[%s] Program failed!\n", __func__);
		goto error_exit;
	}

	ret = i2c_cpld_verify(&dev_info);
	if (ret < 0) {
		printf("[%s] Verify Failed!\n", __func__);
		goto error_exit;
	}

	ret = i2c_cpld_end();
	if (ret < 0) {
		printf("[%s] Exit Transparent Mode Failed!\n", __func__);
		goto error_exit;
	}

error_exit:
	if (NULL != dev_info.CF) {
		free(dev_info.CF);
	}

	if (NULL != dev_info.EndCF) {
		free(dev_info.EndCF);
	}

	if (NULL != dev_info.UFM) {
		free(dev_info.UFM);
	}

	return ret;
}

static int yzbb_i2c_cpld_get_ver(unsigned int *ver)
{
	uint8_t cmd[YZBB_READ_VER_INS_LENGTH] = { YZBB_READ_VERSION };
	uint8_t dr_data[YZBB_VERSION_DATA_LENGTH];
	int ret = -1;

	ret = i2c_rdwr_msg_transfer(cpld.fd, YZBB_CPLD_SLAVE << 1,
				    (uint8_t *)&cmd, YZBB_READ_VER_INS_LENGTH,
				    (uint8_t *)&dr_data,
				    YZBB_VERSION_DATA_LENGTH);
	if (ret != 0) {
		printf("read_device_id() failed\n");
		return ret;
	}
#ifdef DEBUG
	printf("Version = ");
	for (int i = 0; i < YZBB_VERSION_DATA_LENGTH; i++) {
		printf(" 0x%X ", dr_data[i]);
	}
	printf("\n");
#endif
	/* Byte 1 is version */
	*ver = dr_data[1] & 0xFF;
	printf("CPLD Version: %02X\n", *ver);

	return 0;
}

static int yzbb_i2c_cpld_get_id(unsigned int *dev_id)
{
	/* Get the ID in the UFM sector (address 0xCA).
     * All YZBB CPLDs need to support this field. */
	uint8_t en_cfg_cmd[YZBB_CFG_INS_LENGTH] = { YZBB_ENABLE_CFG, 0x08,
						    0x00 };
	uint8_t dis_cfg_cmd[YZBB_CFG_INS_LENGTH] = { YZBB_DISABLE_CFG, 0x00,
						     0x00 };
	uint8_t status_cmd[YZBB_INS_LENGTH] = { YZBB_READ_STATUS, 0x00, 0x00,
						0x00 };
	uint8_t init_ufm_cmd[YZBB_INS_LENGTH] = { YZBB_INIT_ADRR_UFM, 0x00,
						  0x00, 0x00 };
	uint8_t ufm_cmd[YZBB_READ_UFM_INS_LENGTH] = { YZBB_READ_UFM_ADDR, 0x00,
						      0x00, 0x00 };
	uint8_t bypass_cmd[YZBB_INS_LENGTH] = { YZBB_BYPASS, 0x00, 0x00, 0x00 };
	uint8_t ufm_data[YZBB_UFM_DATA_LENGTH];
	uint8_t st_data[YZBB_READ_STATUS_LENGTH];

	int ret = -1;

	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, en_cfg_cmd,
				    sizeof(en_cfg_cmd), NULL, 0);
	if (ret != 0) {
		printf("read_device_id() Enable Configuration Interface failed\n");
		return ret;
	}
	// Delay 10ms
	usleep(10000);
	// Check the configuration status
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, status_cmd,
				    sizeof(status_cmd), st_data,
				    sizeof(st_data));
	if (ret != 0) {
		printf("read_device_id() configuration status failed\n");
		return ret;
	}
	unsigned int tmp = byte_to_int(st_data);
	if ((tmp & 0x00003000) != 0) {
		printf("read_device_id() wrong status\n");
		return -1;
	}
	// Init UFM address
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, init_ufm_cmd,
				    sizeof(init_ufm_cmd), NULL, 0);
	if (ret != 0) {
		printf("read_device_id() Init UFM address failed\n");
		return ret;
	}
	usleep(1000);
	// Get ID in the UFM sector
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, ufm_cmd,
				    sizeof(ufm_cmd), ufm_data,
				    sizeof(ufm_data));
	if (ret != 0) {
		printf("read_device_id() Get ID in the UFM sector failed\n");
		return ret;
	}
#ifdef DEBUG
	printf("Device ID = ");
	for (int i = 0; i < YZBB_UFM_DATA_LENGTH; i++) {
		printf(" 0x%X ", ufm_data[i]);
	}
	printf("\n");
#endif

	// The ID is byte 2 to byte 12
	memcpy(dev_id, &ufm_data[1], YZBB_DEVICEID_LENGTH);
	printf("CPLD DeviceID: ");
	for (int i = 1; i < YZBB_DEVICEID_LENGTH + 1; i++) {
		printf(" %02X ", ufm_data[i]);
	}
	printf("\n");

	// Disable Configuration Interface
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, dis_cfg_cmd,
				    sizeof(dis_cfg_cmd), NULL, 0);
	if (ret != 0) {
		printf("read_device_id() Disable Configuration Interface failed\n");
		return ret;
	}
	// Send Bypass
	ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, bypass_cmd,
				    sizeof(bypass_cmd), NULL, 0);
	if (ret != 0) {
		printf("read_device_id() Send Bypass failed\n");
		return ret;
	}

	return 0;
}

/******************************************************************************/
/***************************      Common       ********************************/
/******************************************************************************/

static int LCMXO2Family_cpld_get_ver(unsigned int *ver)
{
	return (cpld.intf == INTF_JTAG) ? jtag_cpld_get_ver(ver) :
					  i2c_cpld_get_ver(ver);
}

static int LCMXO2Family_cpld_update(FILE *jed_fd, char *key, char is_signed)
{
	return (cpld.intf == INTF_JTAG) ?
		       jtag_cpld_update(jed_fd, key, is_signed) :
		       i2c_cpld_update(jed_fd, key, is_signed);
}

static int LCMXO3D_cpld_update(FILE *jed_fd, char *key, char is_signed)
{
	return (cpld.intf == INTF_JTAG) ?
		       jtag_cpld_lcm3d_update(jed_fd, key, is_signed) :
		       i2c_cpld_update(jed_fd, key, is_signed);
}

static int LCMXO2Family_cpld_get_id(unsigned int *dev_id)
{
	return (cpld.intf == INTF_JTAG) ? jtag_cpld_get_id(dev_id) :
					  i2c_cpld_get_id(dev_id);
}

static int LCMXO2Family_cpld_checksum(FILE *jed_fd, uint32_t *crc)
{
	return (cpld.intf == INTF_JTAG) ? jtag_cpld_checksum(jed_fd, crc) :
					  i2c_cpld_checksum(jed_fd, crc);
}

static int LCMXO2Family_cpld_dev_open(cpld_intf_t intf, cpld_intf_info_t *attr)
{
	int rc = 0;

	cpld.intf = intf;
	if (attr != NULL) {
		cpld.bus = attr->bus;
		cpld.slave = attr->slave;
		cpld.mode = attr->mode;
		cpld.jtag_device = attr->jtag_device;
	}

	if (intf == INTF_JTAG) {
		ast_jtag_set_mode(JTAG_XFER_HW_MODE);
		rc = ast_jtag_open(cpld.jtag_device);
	} else if (intf == INTF_I2C) {
		cpld.fd = i2c_open(cpld.bus, cpld.slave);
		if (cpld.fd < 0)
			rc = -1;
	} else {
		printf("[%s] Interface type %d is not supported\n", __func__,
		       intf);
		rc = -1;
	}

	return rc;
}

static int LCMXO2Family_cpld_dev_close(cpld_intf_t intf)
{
	if (intf == INTF_JTAG) {
		ast_jtag_close();
	} else if (intf == INTF_I2C) {
		close(cpld.fd);
	} else {
		printf("[%s] Interface type %d is not supported\n", __func__,
		       intf);
	}

	return 0;
}

static int YZBBFamily_cpld_get_ver(unsigned int *ver)
{
	return (cpld.intf == INTF_JTAG) ? yzbb_jtag_cpld_get_ver(ver) :
					  yzbb_i2c_cpld_get_ver(ver);
}

static int YZBBFamily_cpld_get_id(unsigned int *dev_id)
{
	return (cpld.intf == INTF_JTAG) ? yzbb_jtag_cpld_get_id(dev_id) :
					  yzbb_i2c_cpld_get_id(dev_id);
}

/******************************************************************************/
struct cpld_dev_info lattice_dev_list[] = {
  [0] = {
    .name = "LCMXO3LF-9400",
    .dev_id = 0x612BE043,
    .cpld_open = LCMXO2Family_cpld_dev_open,
    .cpld_close = LCMXO2Family_cpld_dev_close,
    .cpld_ver = LCMXO2Family_cpld_get_ver,
    .cpld_program = LCMXO2Family_cpld_update,
    .cpld_dev_id = LCMXO2Family_cpld_get_id,
    .cpld_checksum = LCMXO2Family_cpld_checksum,
  },
  [1] = {
    .name = "LCMXO3LF-4300",
    .dev_id = 0x612BC043,
    .cpld_open = LCMXO2Family_cpld_dev_open,
    .cpld_close = LCMXO2Family_cpld_dev_close,
    .cpld_ver = LCMXO2Family_cpld_get_ver,
    .cpld_program = LCMXO2Family_cpld_update,
    .cpld_dev_id = LCMXO2Family_cpld_get_id,
    .cpld_checksum = LCMXO2Family_cpld_checksum,
  },
  [2] = {
    .name = "LCMXO3D-9400",
    .dev_id = 0x212E3043,
    .cpld_open = LCMXO2Family_cpld_dev_open,
    .cpld_close = LCMXO2Family_cpld_dev_close,
    .cpld_ver = LCMXO2Family_cpld_get_ver,
    .cpld_program = LCMXO3D_cpld_update,
    .cpld_dev_id = LCMXO2Family_cpld_get_id,
    .cpld_checksum = LCMXO2Family_cpld_checksum,
  },
  [3] = {
    .name = "YZBB-Family",
    .dev_id = 0x42425A59,
    .dev_id2 = 0x35383230,
    .dev_id3 = 0x32303136,
    .cpld_open = LCMXO2Family_cpld_dev_open,
    .cpld_close = LCMXO2Family_cpld_dev_close,
    .cpld_ver = YZBBFamily_cpld_get_ver,
    .cpld_program = LCMXO2Family_cpld_update,
    .cpld_dev_id = YZBBFamily_cpld_get_id,
    .cpld_checksum = LCMXO2Family_cpld_checksum,
  },
};
