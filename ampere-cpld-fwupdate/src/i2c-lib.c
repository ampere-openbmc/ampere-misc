
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include "i2c-lib.h"

// #define DEBUG
#ifdef DEBUG
#define CPLD_BEBUG(...) printf(__VA_ARGS__);
#else
#define CPLD_BEBUG(...)
#endif

#define ERR_PRINT(...) \
        fprintf(stderr, __VA_ARGS__);

int i2c_open(uint8_t bus_num, uint8_t addr) {
  int fd = -1, rc = -1;
  char fn[32];

  snprintf(fn, sizeof(fn), "/dev/i2c-%d", bus_num);
  fd = open(fn, O_RDWR);
  if (fd == -1) {
    ERR_PRINT("Failed to open i2c device %s", fn);
    return -1;
  }

  rc = ioctl(fd, I2C_SLAVE, addr);
  if (rc < 0) {
    ERR_PRINT("Failed to open slave @ address 0x%x", addr);
    close(fd);
    return -1;
  }
  return fd;
}

int i2c_rdwr_msg_transfer(int file, uint8_t addr, uint8_t *tbuf,
                          uint8_t tcount, uint8_t *rbuf, uint8_t rcount)
{
  struct i2c_rdwr_ioctl_data data;
  struct i2c_msg msg[2];
  int n_msg = 0;
  int rc;

  memset(&msg, 0, sizeof(msg));

  if (tcount) {
    msg[n_msg].addr = addr >> 1;
    msg[n_msg].flags = 0;
    msg[n_msg].len = tcount;
    msg[n_msg].buf = tbuf;
    n_msg++;
  }

  if (rcount) {
    msg[n_msg].addr = addr >> 1;
    msg[n_msg].flags = I2C_M_RD;
    msg[n_msg].len = rcount;
    msg[n_msg].buf = rbuf;
    n_msg++;
  }

  data.msgs = msg;
  data.nmsgs = n_msg;

  rc = ioctl(file, I2C_RDWR, &data);
  if (rc < 0) {
    // syslog(LOG_ERR, "Failed to do raw io");
    return -1;
  }
  return 0;
}
