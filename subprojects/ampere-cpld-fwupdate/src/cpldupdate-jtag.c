/*
 * main - CPLD upgrade utility via BMC's JTAG master
 */

#include "cpld.h"
#include "cpu.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

typedef struct
{
    int program;     /* enable/disable program  */
    int erase;       /* enable/disable erase flag */
    int get_version; /* get cpld version flag */
    int get_device;  /* get cpld ID code flag */
    int get_cpuid;   /* get CPU ID code flag */
    int checksum;    /* get checksum flag */
    int type;
} cpld_t;

static void usage(FILE* fp, char** argv)
{
    fprintf(fp,
            "\nampere_cpldupdate_jtag v0.0.2 Copyright 2022.\n\n"
            "Usage: %s -d <jtag_device> [options]\n\n"
            "Jtag Device: Default is jtag0\n"
            " 0 - /dev/jtag0\n"
            " 1 - /dev/jtag1\n"
            "Options:\n"
            " -h | --help                   Print this message\n"
            " -p | --program                Erase, program and verify cpld\n"
            " -v | --get-cpld-version       Get current cpld version\n"
            " -i | --get-cpld-idcode        Get cpld idcode\n"
            " -u | --get-cpu-idcode         Get cpu idcode\n"
            " -c | --checksum               Calculate CPLD checksum\n"
            "",
            argv[0]);
}

static const char short_options[] = "hviup:c:t:d:";
static char jdev_file_lck[50];

static const struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"program", required_argument, NULL, 'p'},
    {"get-cpld-version", no_argument, NULL, 'v'},
    {"get-cpld-idcode", no_argument, NULL, 'i'},
    {"get-cpu-idcode", no_argument, NULL, 'u'},
    {"checksum", required_argument, NULL, 'c'},
    {0, 0, 0, 0}};

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

static void handle_signal(int sig)
{
    remove(jdev_file_lck);
    printf("Terminated by signal %d\n", sig);
    exit(EXIT_FAILURE);
}

static int lock_device(int jtag_device)
{
    FILE* fptr;
    char pid[50];
    sprintf(jdev_file_lck, "%s%d", JTAG_FILE_LOCK, jtag_device);

    if (access(jdev_file_lck, F_OK) == 0)
    {
        /* The lock file exists */
        printf("Error: The /dev/jtag%d is locked by process ", jtag_device);
        /* Read the pid from the file */
        fptr = fopen(jdev_file_lck, "r");
        while (fgets(pid, 50, fptr) != NULL)
        {
            printf("%s", pid);
        }
        printf("\n");
        goto busy;
    }
    else
    {
        /* The lock file doesn't exist */
        /* Support interupt signal handler
         *    SIGINT : Keyboard press ctrl-C
         *    SIGTERM : Kill process
         *    SIGKILL : Kill signal
         *    SIGHUP : Hangup the process
         *.   SIGQUIT : Core dumped
         */
        signal(SIGINT, handle_signal);
        signal(SIGTERM, handle_signal);
        signal(SIGKILL, handle_signal);
        signal(SIGHUP, handle_signal);
        signal(SIGQUIT, handle_signal);

        /* Create the lock file with pid */
        fptr = fopen(jdev_file_lck, "w");
        /* Store pid to lock file */
        fprintf(fptr, "%d", getpid());
        goto notBusy;
    }

busy:
    fclose(fptr);
    return -1;
notBusy:
    fclose(fptr);
    return 0;
}

int main(int argc, char* argv[])
{
    char option;
    char in_name[100] = "";
    uint8_t cpld_var[4] = {0};
    char key[32] = {0};
    cpld_t cpld;
    int rc = -1;
    unsigned int crc = 0;
    cpld_intf_info_t cpld_info;

    memset(&cpld, 0, sizeof(cpld));
    memset(&cpld_info, 0, sizeof(cpld_info));

    while ((option = getopt_long(argc, argv, short_options, long_options,
                                 NULL)) != (char)-1)
    {
        switch (option)
        {
            case 'h':
                usage(stdout, argv);
                exit(EXIT_SUCCESS);
                break;
            case 'd':
                strcpy(in_name, optarg);
                cpld_info.jtag_device = (uint8_t)strtoul(in_name, NULL, 0);
                if ((cpld_info.jtag_device != 0) &&
                    (cpld_info.jtag_device != 1))
                {
                    printf("Wrong jtag device!\n");
                    usage(stdout, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                cpld.program = 1;
                strcpy(in_name, optarg);
                if (!strcmp(in_name, ""))
                {
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
            case 'u':
                cpld.get_cpuid = 1;
                break;
            case 'c':
                cpld.checksum = 1;
                strcpy(in_name, optarg);
                if (!strcmp(in_name, ""))
                {
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

    if (lock_device(cpld_info.jtag_device))
    {
        exit(EXIT_FAILURE);
    }

    if (cpld.get_cpuid)
    {
        rc = cpu_probe(cpld_info.jtag_device);
        if (rc)
        {
            printf("CPU probe failed!\n");
            goto unlock_device;
        }

        rc = cpu_get_id();
        if (rc)
        {
            printf("CPU IDcode: NA\n");
        }
        cpu_close();
        goto unlock_device;
    }

    rc = cpld_probe(INTF_JTAG, &cpld_info);
    if (rc)
    {
        printf("CPLD_INTF probe failed!\n");
        goto unlock_device;
    }

    rc = cpld_scan(INTF_JTAG);
    if (rc)
    {
        printf("CPLD_INTF scan failed!\n");
        goto end_of_func;
    }

    if (cpld.get_version)
    {
        rc = cpld_get_ver((unsigned int*)&cpld_var);
        if (rc)
        {
            printf("CPLD Version: NA\n");
        }
        goto end_of_func;
    }

    if (cpld.get_device)
    {
        rc = cpld_get_device_id((unsigned int*)&cpld_var);
        if (rc)
        {
            printf("CPLD DeviceID: NA\n");
        }
        goto end_of_func;
    }

    if (cpld.checksum)
    {
        rc = cpld_get_checksum(in_name, &crc);
        if (rc)
        {
            printf("CPLD Checksum: NA\n");
        }
        else
        {
            printf("CPLD Checksum: %X\n", crc);
        }
        goto end_of_func;
    }

    if (cpld.program)
    {
        // Print CPLD Version
        rc = cpld_get_ver((unsigned int*)&cpld_var);
        if (rc)
        {
            printf("CPLD Version: NA\n");
            goto end_of_func;
        }
        // Print CPLD Device ID
        rc = cpld_get_device_id((unsigned int*)&cpld_var);
        if (rc)
        {
            printf("CPLD DeviceID: NA\n");
            goto end_of_func;
        }
        rc = cpld_program(in_name, key, 0);
        if (rc < 0)
        {
            printf("Failed to program cpld\n");
            goto end_of_func;
        }
    }

end_of_func:
    cpld_intf_close(INTF_JTAG);
unlock_device:
    remove(jdev_file_lck);
    if (rc == 0)
    {
        printf_pass();
        return 0;
    }
    else
    {
        printf_failure();
        return -1;
    }
}
