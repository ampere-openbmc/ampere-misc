
#ifndef CPLDUPDATE_I2C_LIB_H_
#define CPLDUPDATE_I2C_LIB_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int i2c_open(uint8_t bus_num, uint8_t addr);
int i2c_rdwr_msg_transfer(int file, uint8_t addr, uint8_t *tbuf,
			  uint16_t tcount, uint8_t *rbuf, uint16_t rcount);

#endif /* CPLDUPDATE_I2C_LIB_H_ */
