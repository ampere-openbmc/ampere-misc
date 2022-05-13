#ifndef _ANLOGIC_H_
#define _ANLOGIC_H_

// Anlogic CPLD Getting ID
#define READ_UFM_INS_LENGTH 4
#define READ_UFM_ADDR       0xCA
#define UFM_DATA_LENGTH     16
#define DEVICEID_LENGTH     12

#define ANLOGIC_CPLD_SLAVE  0x11

#define READ_VER_INS_LENGTH         1
#define CFG_SPI_INS_LENGTH          2
#define WRITE_ENABLE_INS_LENGTH     3
#define SECTOR_ERASE_INS_LENGTH     6
#define WRITE_DISABLE_INS_LENGTH    3
#define PROGRAM_INS_LENGTH          22
#define READ_DATA_INS_LENGTH        20

#define VERSION_DATA_LENGTH 2

// Anlogic Command Types
#define READ_VERSION        0x00
#define CFG_SPI_INTERFACE   0x01
#define WRITE_DATA_TO_SPI   0x02

// Anlogic CPLD Programming Commands
#define WRITE_ENABLE    0x06
#define SECTOR_ERASE    0x20
#define PAGE_PROGRAM    0x02
#define READ_DATA       0x03
#define WRITE_DISABLE   0x04

// Anlogic Device Info
#define PAGE_SIZE       16
#define SECTOR_SIZE     4096


extern struct cpld_dev_info anlogic_dev_list[1];

#endif /* _ANLOGIC_H_ */
