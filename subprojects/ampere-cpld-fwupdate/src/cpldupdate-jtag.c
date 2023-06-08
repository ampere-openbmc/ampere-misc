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
	int program; /* enable/disable program  */
	int erase; /* enable/disable erase flag */
	int get_version; /* get cpld version flag */
	int get_device; /* get cpld ID code flag */
	int checksum; /* get checksum flag */
	int type;
} cpld_t;

static void usage(FILE *fp, char **argv)
{
	fprintf(fp,
		"\nampere_cpldupdate_i2c v0.0.2 Copyright 2022.\n\n"
		"Usage: %s -t <type> -d <jtag_device> [options]\n\n"
		"Type:\n"
		" 0 - LCMXO2\n"
		" 1 - LCMXO3\n"
		" 2 - ANLOGIC\n"
		" 3 - YZBB\n"
		"Jtag Device: Default is jtag0\n"
		" 0 - /dev/jtag0\n"
		" 1 - /dev/jtag1\n"
		"Options:\n"
		" -h | --help                   Print this message\n"
		" -p | --program                Erase, program and verify cpld\n"
		" -v | --get-cpld-version       Get current cpld version\n"
		" -i | --get-cpld-idcode        Get cpld idcode\n"
		" -c | --checksum               Calculate CPLD checksum\n"
		"",
		argv[0]);
}

static const char short_options[] = "hvip:c:t:d:";

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
	uint8_t cpld_var[4] = { 0 };
	char key[32] = { 0 };
	cpld_t cpld;
	int rc = -1;
	unsigned int crc = 0;
	cpld_intf_info_t cpld_info;

	memset(&cpld, 0, sizeof(cpld));
	memset(&cpld_info, 0, sizeof(cpld_info));

	while ((option = getopt_long(argc, argv, short_options, long_options,
				     NULL)) != (char)-1) {
		switch (option) {
		case 'h':
			usage(stdout, argv);
			exit(EXIT_SUCCESS);
			break;
		case 't':
			strcpy(in_name, optarg);
			cpld.type = (uint8_t)strtoul(in_name, NULL, 0);
			break;
		case 'd':
			strcpy(in_name, optarg);
			cpld_info.jtag_device =
				(uint8_t)strtoul(in_name, NULL, 0);
			if ((cpld_info.jtag_device != 0) &&
			    (cpld_info.jtag_device != 1)) {
				printf("Wrong jtag device!\n");
				usage(stdout, argv);
				exit(EXIT_FAILURE);
			}
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

	/* No different between LCMX02 and LCMX03 now */
	if (cpld_intf_open(LCMXO2, INTF_JTAG, &cpld_info)) {
		printf("CPLD_INTF Open failed!\n");
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
	cpld_intf_close(INTF_JTAG);
	if (rc == 0) {
		printf_pass();
		return 0;
	} else {
		printf_failure();
		return -1;
	}
}
