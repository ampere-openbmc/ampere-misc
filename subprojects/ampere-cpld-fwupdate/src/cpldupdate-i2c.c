/*
* main - CPLD upgrade utility via BMC's JTAG master
*/

#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <termios.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <pthread.h>
#include "cpld.h"

typedef struct {
	int fd; /* file description  */
	int bus; /* bus */
	int slave; /* slave address  */
	int program; /* enable/disable program  */
	int erase; /* enable/disable erase flag */
	int get_version; /* get cpld version flag */
	int get_device; /* get cpld ID code flag */
	int checksum; /* get cpld checksum flag */
} cpld_t;

static void usage(FILE *fp, char **argv)
{
	fprintf(fp,
		"\nampere_cpldupdate_i2c v0.0.2 Copyright 2022.\n\n"
		"Usage: %s -b <bus> -s <slave> [options]\n\n"
		"Options:\n"
		" -h | --help                   Print this message\n"
		" -p | --program                Erase, program and verify cpld\n"
		" -v | --get-cpld-version       Get current cpld version\n"
		" -i | --get-cpld-idcode        Get cpld idcode\n"
		" -c | --checksum               Calculate CPLD checksum\n"
		"",
		argv[0]);
}

static const char short_options[] = "hvic:p:b:s:t:";

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "program", required_argument, NULL, 'p' },
	{ "get-cpld-version", no_argument, NULL, 'v' },
	{ "get-cpld-idcode", no_argument, NULL, 'i' },
	{ "checksum", required_argument, NULL, 'c' },
	{ 0, 0, 0, 0 }
};

static void printf_pass()
{
	printf("+=======+\n");
	printf("| PASS! |\n");
	printf("+=======+\n\n");
}

static void printf_failure()
{
	printf("+=======+\n");
	printf("| FAIL! |\n");
	printf("+=======+\n\n");
}

int main(int argc, char *argv[])
{
	char option;
	char in_name[100] = "";
	uint8_t cpld_var[16] = { 0 };
	char key[32] = { 0 };
	cpld_t cpld;
	int rc = -1;
	cpld_intf_info_t cpld_info;
	uint32_t crc;

	memset(&cpld, 0, sizeof(cpld));
	memset(&cpld_info, 0, sizeof(cpld_info));

	while ((option = getopt_long(argc, argv, short_options, long_options,
				     NULL)) != (char)-1) {
		switch (option) {
		case 'h':
			usage(stdout, argv);
			exit(EXIT_SUCCESS);
			break;
		case 'b':
			strcpy(in_name, optarg);
			cpld_info.bus = (uint8_t)strtoul(in_name, NULL, 0);
			break;
		case 's':
			strcpy(in_name, optarg);
			cpld_info.slave = (uint8_t)strtoul(in_name, NULL, 0);
			break;
		case 'p':
			cpld.program = 1;
			strcpy(in_name, optarg);
			if (!strcmp(in_name, "")) {
				printf("No input file name!\n");
				usage(stdout, argv);
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			cpld.get_version = 1;
			break;
		case 'i':
			cpld.get_device = 1;
			break;
		case 'c':
			cpld.checksum = 1;
			strcpy(in_name, optarg);
			if (!strcmp(in_name, "")) {
				printf("No input file name!\n");
				usage(stdout, argv);
				exit(EXIT_FAILURE);
			}
			break;
		default:
			usage(stdout, argv);
			exit(EXIT_FAILURE);
		}
	}

	if ((cpld_info.bus == 0) || (cpld_info.slave == 0)) {
		printf("Need bus or slave!\n");
		usage(stdout, argv);
		exit(EXIT_FAILURE);
	}

	/* No different between LCMX02 and LCMX03 now */
	cpld_info.intf = INTF_I2C;
	if (cpld_probe(INTF_I2C, &cpld_info)) {
		printf("CPLD_INTF probe failed!\n");
		exit(EXIT_FAILURE);
	}

	if (cpld_scan(INTF_I2C)) {
		printf("CPLD_INTF scan failed!\n");
		exit(EXIT_FAILURE);
	}

	if (cpld.get_version) {
		if (cpld_get_ver((unsigned int *)&cpld_var)) {
			printf("CPLD Version: NA\n");
		}
		exit(EXIT_SUCCESS);
	}

	if (cpld.get_device) {
		if (cpld_get_device_id((unsigned int *)&cpld_var)) {
			printf("CPLD DeviceID: NA\n");
		}
		exit(EXIT_SUCCESS);
	}

	if (cpld.checksum) {
		if (cpld_get_checksum(in_name, &crc)) {
			printf("CPLD Checksum: NA\n");
		} else {
			printf("CPLD Checksum: %X\n", crc);
		}
		exit(EXIT_SUCCESS);
	}

	if (cpld.program) {
		// Print CPLD Version
		if (cpld_get_ver((unsigned int *)&cpld_var)) {
			printf("CPLD Version: NA\n");
		}
		// Print CPLD Device ID
		if (cpld_get_device_id((unsigned int *)&cpld_var)) {
			printf("CPLD DeviceID: NA\n");
		}
		rc = cpld_program(in_name, key, 0);
		if (rc < 0) {
			printf("Failed to program cpld\n");
			goto end_of_func;
		}
	}

end_of_func:
	cpld_intf_close(INTF_I2C);
	if (rc == 0) {
		printf_pass();
		return 0;
	} else {
		printf_failure();
		return -1;
	}
}
