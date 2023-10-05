/*
Please get the JEDEC file format before you read the code
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpld.h"
#include "lattice.h"
#include "anlogic.h"

struct cpld_dev_info *cur_dev = NULL;

int cpld_probe(cpld_intf_t intf, cpld_intf_info_t *attr)
{
	//.No difference between other CPLDs
	// JTAG: Set JTAG hardware mode
	// I2C: Open I2C port
	cur_dev = &lattice_dev_list[0];

	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_open) {
		printf("Open not supported\n");
		return -1;
	}

	return cur_dev->cpld_open(intf, attr);
}

static int cpld_remove(cpld_intf_t intf)
{
	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_close) {
		printf("Close not supported\n");
		return -1;
	}

	return cur_dev->cpld_close(intf);
}

int cpld_scan(cpld_intf_t intf)
{
	unsigned int idcode = 0;

	// Check for LATTICE device (Jtag and I2C)
	cpld_get_device_id(&idcode);
	for (int i = 0; i < LATTICE_MAX_DEVICE_SUPPORT; i++) {
		if (idcode == lattice_dev_list[i].dev_id) {
			cur_dev = &lattice_dev_list[i];
			printf("Detected: %s\n", cur_dev->name);
			return 0;
		}
	}

	// Check for YZBB JTAG device
	// Todo: Have not supported YZBB Jtag device

	// Check for ANALOGIC Jtag device
	// Todo: Have not supported ANALOGIC Jtag device

	if (intf == INTF_I2C) {
		uint8_t idcode_i2c[16] = { 0 };
		// Check for YZBB I2C device
		cur_dev = &lattice_dev_list[LATTICE_MAX_DEVICE_SUPPORT - 1];
		cpld_get_device_id((unsigned int *)&idcode_i2c);
		if (!memcmp(&idcode_i2c,
			    &lattice_dev_list[LATTICE_MAX_DEVICE_SUPPORT - 1]
				     .dev_id,
			    YZBB_DEVICEID_LENGTH)) {
			printf("Detected: %s\n", cur_dev->name);
			return 0;
		}

		// Check for ANALOGIC I2C device
		cur_dev = &anlogic_dev_list[0];
		cpld_get_device_id((unsigned int *)&idcode_i2c);
		if (!memcmp(&idcode_i2c, &anlogic_dev_list[0].dev_id,
			    DEVICEID_LENGTH)) {
			printf("Detected: %s\n", cur_dev->name);
			return 0;
		}
	}

	printf("The CPLD device is not supported list\n");
	return 1;
}

int cpld_intf_close(cpld_intf_t intf)
{
	int ret;

	ret = cpld_remove(intf);
	cur_dev = NULL;

	return ret;
}

int cpld_get_ver(uint32_t *ver)
{
	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_ver) {
		printf("Get version not supported\n");
		return -1;
	}

	return cur_dev->cpld_ver(ver);
}

int cpld_get_checksum(char *file, uint32_t *crc)
{
	int ret;
	FILE *fp_in = NULL;

	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_checksum) {
		printf("Program CPLD not supported\n");
		return -1;
	}

	fp_in = fopen(file, "r");
	if (NULL == fp_in) {
		printf("[%s] Cannot Open File %s!\n", __func__, file);
		return -1;
	}

	ret = cur_dev->cpld_checksum(fp_in, crc);
	fclose(fp_in);

	return ret;
}

int cpld_get_device_id(uint32_t *dev_id)
{
	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_dev_id) {
		printf("Get device id not supported\n");
		return -1;
	}

	return cur_dev->cpld_dev_id(dev_id);
}

int cpld_erase(void)
{
	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_erase) {
		printf("Erase CPLD not supported\n");
		return -1;
	}

	return cur_dev->cpld_erase();
}

int cpld_program(char *file, char *key, char is_signed)
{
	int ret;
	FILE *fp_in = NULL;

	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_program) {
		printf("Program CPLD not supported\n");
		return -1;
	}

	fp_in = fopen(file, "r");
	if (NULL == fp_in) {
		printf("[%s] Cannot Open File %s!\n", __func__, file);
		return -1;
	}

	ret = cur_dev->cpld_program(fp_in, key, is_signed);
	fclose(fp_in);

	return ret;
}

int cpld_verify(char *file)
{
	int ret;
	FILE *fp_in = NULL;

	if (cur_dev == NULL)
		return -1;

	if (!cur_dev->cpld_verify) {
		printf("Verify CPLD not supported\n");
		return -1;
	}

	fp_in = fopen(file, "r");
	if (NULL == fp_in) {
		printf("[%s] Cannot Open File %s!\n", __func__, file);
		return -1;
	}

	cur_dev->cpld_verify(fp_in);
	fclose(fp_in);

	return ret;
}
