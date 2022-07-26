#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ast-jtag.h"
#include "cpld.h"
#include "anlogic.h"
#include "i2c-lib.h"

static cpld_intf_info_t cpld;

//#define DEBUG
//#define VERBOSE_DEBUG
#ifdef DEBUG
#define CPLD_DEBUG(...) printf(__VA_ARGS__);
#else
#define CPLD_DEBUG(...)
#endif

#define ERR_PRINT(...) \
        fprintf(stderr, __VA_ARGS__);

#define MAX_RETRY               4000
#define LATTICE_COL_SIZE        128
#define ARRAY_SIZE(x)           (sizeof(x) / sizeof((x)[0]))
#define UNUSED(x)               (void)(x)


/******************************************************************************/
/***************************     JTAG       ***********************************/
/******************************************************************************/
static int
jtag_cpld_get_ver(unsigned int *ver)
{
    UNUSED(ver);
    return 0;
}

static int
jtag_cpld_update(FILE *jed_fd, char* key, char is_signed)
{
    UNUSED(jed_fd);
    UNUSED(key);
    UNUSED(is_signed);
    return 0;
}

static int
jtag_cpld_get_id(unsigned int *dev_id)
{
    UNUSED(dev_id);
    return 0;
}
static int
jtag_cpld_checksum(FILE *jed_fd, unsigned int *crc)
{
    UNUSED(jed_fd);
    UNUSED(crc);
    return 0;
}

/******************************************************************************/
/***************************      I2C       ***********************************/
/******************************************************************************/

static int i2c_read_fw_data(unsigned char *data_buffer, unsigned long data_size)
{
    int ret = 0;
    unsigned char prog_buf[PROGRAM_INS_LENGTH];
    unsigned char read_buf[READ_DATA_INS_LENGTH];
    unsigned int index = 0;
    unsigned int row_count, col_count;

    row_count = data_size/PAGE_SIZE + ((data_size%PAGE_SIZE != 0)?1:0);
    col_count = PAGE_SIZE;

    memset(data_buffer, 0, data_size);
    memset(prog_buf, 0, PROGRAM_INS_LENGTH);
    memset(read_buf, 0, READ_DATA_INS_LENGTH);

    //!!Read the Flash
    while (row_count > 0)
    {
        // Read 16 data from address
        // I2C 02 03 addr2 addr1 addr0 00 00 .. 00 (16 bytes) 00 STOP
        prog_buf[0] = WRITE_DATA_TO_SPI;
        prog_buf[1] = READ_DATA;
        prog_buf[2] = (index >> 16) & 0x0000FF;
        prog_buf[3] = (index >> 8) & 0x0000FF;
        prog_buf[4] = index & 0x0000FF;
        if(i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, prog_buf,
                                 PROGRAM_INS_LENGTH, NULL, 0) < 0)
        {
            ret = -1;
            printf("Can not send read cmd row fw data\n");
            return ret;
        }
        usleep(1000);

        // Read data from ram buff to i2c interface
        // I2C dummy0 dummy1 dummy2 dummy3 data0 data1 .. data15 STOP
        if(i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, NULL,
                                 0, (uint8_t *)read_buf, READ_DATA_INS_LENGTH) < 0)
        {
            ret = -1;
            printf("Can not read cmd row fw data\n");
            return ret;
        }
        memcpy(&data_buffer[index], &read_buf[4], PAGE_SIZE);

        index += col_count;
        row_count--;
    }

    return ret;
}

static int i2c_compare_data(unsigned char *read_buffer, unsigned char *compare_data, unsigned long data_size)
{
    unsigned int i;
    int ret = 0;

    if (compare_data != 0)
    {
        printf("Starting read back value, and compare with file\n");
        for (i = 0; i < data_size; i++)
        {
            if (read_buffer[i] != compare_data[i])
            {
                printf("VERIFY ERROR!! ERROR at Index %08x\n", i);
                printf("ERROR at row %d\n", (unsigned int)(i/PAGE_SIZE));
                printf("The file Value is 0x%x, Read Back Value is 0x%x\n", compare_data[i], read_buffer[i]);
                ret = -1;
                break;
            }
            else
            {
                if (compare_data[i] != 0 && read_buffer[i] != 0) {
//                    CPLD_DEBUG("At Row[%d]: The file Value is %08x,"
//                                "Read Back Value is %08x",(unsigned int)(i/PAGE_SIZE), compare_data[i], read_buffer[i]);
                }
            }
        }
    }

    return ret;
}


static int
i2c_cpld_start()
{
    uint8_t cmd[CFG_SPI_INS_LENGTH] = {CFG_SPI_INTERFACE, 0xF0};
    int ret = -1;

    //-----------------------------------------------
    //!!Config SPI Interface by I2C Master
    // SS4  SS3  SS2  SS1  SS0  DIR  CPHA  CPOL
    // 1    1    1    1    0    0    0     0
    ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, cmd,
                                sizeof(cmd), NULL, 0);
    if (ret != 0) {
        printf("i2c_cpld_start() program done failed\n");
      return ret;
    }
    usleep(100);

    return 0;
}

static int
i2c_cpld_end()
{
    uint8_t cmd[WRITE_DISABLE_INS_LENGTH] = {WRITE_DATA_TO_SPI, WRITE_DISABLE, 0x00};
    int ret = -1;

    //-----------------------------------------------
    //!!Write disable
    // I2C 02 04 00 STOP

    ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, cmd,
                                sizeof(cmd), NULL, 0);
    if (ret != 0) {
      printf("i2c_cpld_end() program done failed\n");
      return ret;
    }
    usleep(100);

    return 0;
}

static int
i2c_cpld_erase(unsigned long data_size)
{
    int ret = 0;
    unsigned int i;
    unsigned char cmd[SECTOR_ERASE_INS_LENGTH];
    unsigned int write_en = 0;
    unsigned int using_sectors = 0;
    unsigned int addr = 0;

    using_sectors = data_size/SECTOR_SIZE + ((data_size%SECTOR_SIZE != 0)?1:0);

    printf("Starting to erase device... (this will take a few seconds)\n");
    //-----------------------------------------------
    //!!Write enable
    // I2C 02 06 00 STOP
    write_en = (WRITE_ENABLE << 8) | WRITE_DATA_TO_SPI;

    //!!Erase the Flash
    // I2C 02 20 addr2 addr1 addr0 00 STOP
    cmd[0] = WRITE_DATA_TO_SPI;
    cmd[1] = SECTOR_ERASE;
    cmd[5] = 0x00;

    for (i = 0; i < using_sectors; i++)
    {
        // Write Enable
        if (i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, (uint8_t *) &write_en,
                                  WRITE_ENABLE_INS_LENGTH, NULL, 0) < 0)
        {
            ret = -1;
            return ret;
        }
        usleep(100);

        addr = i*SECTOR_SIZE;
        cmd[2] = (addr >> 16) & 0x0000FF;
        cmd[3] = (addr >> 8) & 0x0000FF;
        cmd[4] = addr & 0x0000FF;

        // Sector Erase
        if (i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, cmd,
                                  SECTOR_ERASE_INS_LENGTH, NULL, 0) < 0)
        {
            ret = -1;
            return ret;
        }
        // Delay 300ms
        usleep(300000);
    }
    //-----------------------------------------------

    return ret;
}

static int
i2c_cpld_program(unsigned char *buf, unsigned long data_size)
{
    int ret = 0;
    unsigned char prog_buf[PROGRAM_INS_LENGTH];
    unsigned int index = 0;
    unsigned int row_count, col_count;
    unsigned int write_en = 0;
    int flag = 0;

    if (data_size%PAGE_SIZE) flag = 1;
    row_count = data_size/PAGE_SIZE;
    col_count = PAGE_SIZE;

    printf("Starting to program device... (this will take a few seconds)\n");
    //-----------------------------------------------
    //!!Write enable
    // I2C 02 06 00 STOP
    write_en = (WRITE_ENABLE << 8) | WRITE_DATA_TO_SPI;

    //!!Program the Flash
    // I2C 02 02 addr2 addr1 addr0 data0 data1 .. data15 00 STOP
    prog_buf[0]   = WRITE_DATA_TO_SPI;
    prog_buf[1]   = PAGE_PROGRAM;
    prog_buf[21] = 0x00;

    while (row_count > 0)
    {
        // Write Enable
        if (i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, (uint8_t *) &write_en,
                                  WRITE_ENABLE_INS_LENGTH, NULL, 0) < 0)
        {
            ret = -1;
            return ret;
        }
        usleep(100);

        prog_buf[2] = (index >> 16) & 0x0000FF;
        prog_buf[3] = (index >> 8) & 0x0000FF;
        prog_buf[4] = index & 0x0000FF;
        memcpy(&prog_buf[5], &buf[index], PAGE_SIZE);

        // Page Program
        if (i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, prog_buf,
                                  PROGRAM_INS_LENGTH, NULL, 0) < 0)
        {
            ret = -1;
            return ret;
        }
        // Delay 1ms
        usleep(1000);

        index += col_count;
        row_count--;
    }

    if (flag)
    {
        unsigned int last_bytes = data_size%PAGE_SIZE;

        // Write Enable
        if (i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, (uint8_t *) &write_en,
                                  WRITE_ENABLE_INS_LENGTH, NULL, 0) < 0)
        {
            ret = -1;
            return ret;
        }
        usleep(100);

        prog_buf[2] = (index >> 16) & 0x0000FF;
        prog_buf[3] = (index >> 8) & 0x0000FF;
        prog_buf[4] = index & 0x0000FF;
        memset(&prog_buf[5], 0, PAGE_SIZE);
        memcpy(&prog_buf[5], &buf[index], last_bytes);

        // Page Program
        if (i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, prog_buf,
                                  PROGRAM_INS_LENGTH, NULL, 0) < 0)
        {
            ret = -1;
            return ret;
        }
        // Delay 1ms
        usleep(1000);
    }
    //-----------------------------------------------

    return ret;
}

static int
i2c_cpld_verify(unsigned char *buf, unsigned long data_size)
{
    int ret = 0;
    unsigned char *pCompare;

    pCompare = (unsigned char *)malloc(data_size);
    if (pCompare == NULL)
    {
        printf("Unable to allocate memory\n");
        return -1;
    }
    memset(pCompare, 0xff, data_size);

    printf("Starting to verify device... (this will take a few seconds)\n");
    //-----------------------------------------------
    i2c_read_fw_data(pCompare, data_size);

    //!!Compare
    ret = i2c_compare_data(pCompare, buf, data_size);
    if (ret == 0)
        printf("Verify Done\n");
    //-----------------------------------------------

    free(pCompare);

    return ret;

}

static int
i2c_cpld_get_ver(unsigned int *ver)
{
    uint8_t cmd[READ_VER_INS_LENGTH] = {READ_VERSION};
    uint8_t dr_data[VERSION_DATA_LENGTH];
    int ret = -1;

    ret = i2c_rdwr_msg_transfer(cpld.fd, ANLOGIC_CPLD_SLAVE << 1, (uint8_t *) &cmd,
                  READ_VER_INS_LENGTH, (uint8_t *) &dr_data, VERSION_DATA_LENGTH);
    if (ret != 0) {
      printf("read_device_id() failed\n");
      return ret;
    }
#ifdef DEBUG
    printf("Version = ");
    for (int i = 0; i < VERSION_DATA_LENGTH; i++ )
    {
        printf(" 0x%X ", dr_data[i]);
    }
    printf("\n");
#endif
    /* Byte 1 is version */
    *ver = dr_data[1] & 0xFF;
   printf("CPLD Version: %02X\n", *ver);

    return 0;
}

static int
i2c_cpld_update(FILE *jed_fd, char* key, char is_signed)
{
    int ret;
    int totalSize =0;
    int transCount = 0;
    unsigned char *pMemBuffer = NULL;

    UNUSED(key);
    UNUSED(is_signed);
    fseek(jed_fd, 0, SEEK_END);
    totalSize = ftell(jed_fd);
    printf("Total file size (%d)\n", totalSize);
    fseek(jed_fd, 0, SEEK_SET);
    // Read the image
    pMemBuffer = (unsigned char *)malloc(totalSize);
    if (pMemBuffer == NULL)
    {
        printf("Unable to allocate memory\n");
        return -1;
    }
    memset(pMemBuffer, 0xff, totalSize);
    transCount = fread(pMemBuffer, 1, totalSize, jed_fd);
    printf("Total read size (%d)\n", transCount);

    ret = i2c_cpld_start();
    if ( ret < 0 )
    {
      printf("[%s] Enter Program mode Error!\n", __func__);
      goto error_exit;
    }

    ret = i2c_cpld_erase(transCount);
    if ( ret < 0 )
    {
      printf("[%s] Erase failed!\n", __func__);
      goto error_exit;
    }

    ret = i2c_cpld_program(pMemBuffer, transCount);
    if ( ret < 0 )
    {
      printf("[%s] Program failed!\n", __func__);
      goto error_exit;
    }

    ret = i2c_cpld_verify(pMemBuffer, transCount);
    if ( ret < 0 )
    {
      printf("[%s] Verify Failed!\n", __func__);
      goto error_exit;
    }

    ret = i2c_cpld_end();
    if ( ret < 0 )
    {
      printf("[%s] Exit Program Mode Failed!\n", __func__);
    }
error_exit:
    free(pMemBuffer);

    return ret;

}

static int
i2c_cpld_get_id(unsigned int *dev_id)
{
    /* Get the ID in the UFM sector (address 0xCA).
     * All Anlogic CPLDs need to support this field. */
    uint8_t cmd[READ_UFM_INS_LENGTH] = {READ_UFM_ADDR, 0x00, 0x00, 0x00};
    uint8_t dr_data[UFM_DATA_LENGTH];
    int ret = -1;

    ret = i2c_rdwr_msg_transfer(cpld.fd, cpld.slave << 1, cmd,
                            sizeof(cmd), dr_data, sizeof(dr_data));
    if (ret != 0) {
      printf("read_device_id() failed\n");
      return ret;
    }
#ifdef DEBUG
    printf("Device ID = ");
    for (int i = 0; i < UFM_DATA_LENGTH; i++ )
    {
        printf(" 0x%X ", dr_data[i]);
    }
    printf("\n");
#endif

    // The ID is byte 2 to byte 12
    memcpy(dev_id, &dr_data[1], DEVICEID_LENGTH);
    printf("CPLD DeviceID: ");
    for (int i = 1; i < DEVICEID_LENGTH + 1; i++ ) {
        printf(" %02X ", dr_data[i]);
    }
    printf("\n");

    return 0;
}
static int
i2c_cpld_checksum(FILE *jed_fd, unsigned int *crc)
{
    int ret = 0;
    int i;
    int totalSize =0;
    int transCount = 0;
    unsigned char *pMemBuffer = NULL;
    unsigned char *pCompare = NULL;
    unsigned int tmpCrc = 0;


    fseek(jed_fd, 0, SEEK_END);
    totalSize = ftell(jed_fd);
    printf("Total file size (%d)\n", totalSize);
    fseek(jed_fd, 0, SEEK_SET);
    // Read the image
    pMemBuffer = (unsigned char *)malloc(totalSize);
    if (pMemBuffer == NULL)
    {
        printf("Unable to allocate memory\n");
        return -1;
    }
    memset(pMemBuffer, 0xff, totalSize);
    transCount = fread(pMemBuffer, 1, totalSize, jed_fd);
    printf("Total read size (%d)\n", transCount);
    for (i = 0; i < transCount; i++)
    {
        tmpCrc += pMemBuffer[i] & 0xFF;
    }
    tmpCrc &= 0xFFFF;
    printf("File Checksum is %X\n", tmpCrc);

    pCompare = (unsigned char *)malloc(transCount);
    if (pCompare == NULL)
    {
        printf("Unable to allocate memory\n");
        return -1;
    }
    memset(pCompare, 0xff, transCount);
    i2c_read_fw_data(pCompare, transCount);
    for (i = 0; i < transCount; i++)
    {
        *crc += pMemBuffer[i] & 0xFF;
    }
    *crc &= 0xFFFF;
    printf("Fw Checksum is %X\n", *crc);

    free(pCompare);
    free(pMemBuffer);

    return ret;
}

/******************************************************************************/
/***************************      Common       ********************************/
/******************************************************************************/

static int
ANLOGICFamily_cpld_get_ver(unsigned int *ver)
{
  return (cpld.intf == INTF_JTAG) ? jtag_cpld_get_ver(ver):
                                    i2c_cpld_get_ver(ver);
}

static int
ANLOGICFamily_cpld_update(FILE *jed_fd, char* key, char is_signed)
{
  return (cpld.intf == INTF_JTAG) ? jtag_cpld_update(jed_fd, key, is_signed):
                                    i2c_cpld_update(jed_fd, key, is_signed);
}

static int
ANLOGICFamily_cpld_get_id(unsigned int *dev_id)
{
  return (cpld.intf == INTF_JTAG) ? jtag_cpld_get_id(dev_id):
                                    i2c_cpld_get_id(dev_id);
}

static int
ANLOGICFamily_cpld_checksum(FILE *jed_fd, uint32_t *crc)
{
  return (cpld.intf == INTF_JTAG) ? jtag_cpld_checksum(jed_fd, crc):
                                    i2c_cpld_checksum(jed_fd, crc);
}

static int
ANLOGICFamily_cpld_dev_open(cpld_intf_t intf, uint8_t id, cpld_intf_info_t *attr)
{
    int rc = 0;
    UNUSED(id);

    cpld.intf = intf;
    if (attr != NULL) {
      cpld.bus = attr->bus;
      cpld.slave = attr->slave;
      cpld.mode = attr->mode;
    }

    if (intf == INTF_JTAG) {
      ast_jtag_set_mode(JTAG_XFER_HW_MODE);
      rc = ast_jtag_open(cpld.jtag_device);
    } else if (intf == INTF_I2C) {
      cpld.fd = i2c_open(cpld.bus, cpld.slave);
      if (cpld.fd < 0)
       rc = -1;
    } else {
      printf("[%s] Interface type %d is not supported\n", __func__, intf);
      rc = -1;
    }

    return rc;

}

static int
ANLOGICFamily_cpld_dev_close(cpld_intf_t intf)
{
    if (intf == INTF_JTAG) {
      ast_jtag_close();
    } else if (intf == INTF_I2C) {
      close(cpld.fd);
    } else {
      printf("[%s] Interface type %d is not supported\n", __func__, intf);
    }

    return 0;
}

/******************************************************************************/
struct cpld_dev_info anlogic_dev_list[] = {
    [0] = {
      .name = "ANLOGIC-Family",
      .cpld_open = ANLOGICFamily_cpld_dev_open,
      .cpld_close = ANLOGICFamily_cpld_dev_close,
      .cpld_ver = ANLOGICFamily_cpld_get_ver,
      .cpld_program = ANLOGICFamily_cpld_update,
      .cpld_dev_id = ANLOGICFamily_cpld_get_id,
      .cpld_checksum = ANLOGICFamily_cpld_checksum,
    }
};
