#ifndef __CPLD_H__
#define __CPLD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum { INTF_I2C, INTF_JTAG } cpld_intf_t;

typedef struct {
	uint8_t bus_id;
	uint8_t slv_addr;
	uint8_t img_type;
	uint32_t start_addr;
	uint32_t end_addr;
	uint32_t csr_base;
	uint32_t data_base;
	uint32_t boot_base;
} altera_max10_attr_t;

typedef struct {
	cpld_intf_t intf;
	int mode;
	int fd;
	int bus;
	int slave;
	int jtag_device;
} cpld_intf_info_t;

int cpld_probe(cpld_intf_t intf, cpld_intf_info_t *attr);
int cpld_scan(cpld_intf_t intf);
int cpld_intf_close(cpld_intf_t intf);
int cpld_get_ver(uint32_t *ver);
int cpld_get_checksum(char *file, uint32_t *crc);
int cpld_get_device_id(uint32_t *dev_id);
int cpld_erase(void);
int cpld_program(char *file, char *key, char is_signed);
int cpld_verify(char *file);

struct cpld_dev_info {
	const char *name;
	uint32_t dev_id;
	uint32_t dev_id2;
	uint32_t dev_id3;
	uint32_t dev_id4;
	int (*cpld_open)(cpld_intf_t intf, cpld_intf_info_t *attr);
	int (*cpld_close)(cpld_intf_t intf);
	int (*cpld_ver)(uint32_t *ver);
	int (*cpld_checksum)(FILE *fd, uint32_t *crc);
	int (*cpld_erase)(void);
	int (*cpld_program)(FILE *fd, char *key, char is_signed);
	int (*cpld_verify)(FILE *fd);
	int (*cpld_dev_id)(uint32_t *dev_id);
};

enum {
	LCMXO2 = 0,
	LCMXO3 = 1, //TBD. No different between LCMX03 and LCMX02
	ANLOGIC = 2,
	YZBB = 3,
	UNKNOWN_DEV
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __CPLD_H__ */
