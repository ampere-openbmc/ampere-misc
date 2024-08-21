#ifndef __CPU_H__
#define __CPU_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "ast-jtag.h"
#include "jtag.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

int cpu_probe(int jtag_device);
void cpu_close();
int cpu_get_id();

#define MAX_TAP 2
#define IDCODE_REG_SIZE_IN_BIT 32
#define BIT_COUNT (MAX_TAP * IDCODE_REG_SIZE_IN_BIT)
#define JTAG_MAX_XFER_DATA_LEN 65535

#ifndef MIN
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#endif
#ifndef MAX
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif
/**
 * Note: When calculating the 8-bit aligned address below for the maximum
 * JTAG transfer data bit length, the JTAG_MAX_XFER_DATA_LEN define is
 * adjusted by one bit to account for a code bug in the open-source Linux
 * JTAG driver. Due to the driver bug, transfers of exact size
 * JTAG_MAX_XFER_DATA_LEN bits will fail.
 */
#define JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED                                    \
    (((JTAG_MAX_XFER_DATA_LEN) - 1) & (~0x7))
#define JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED_DIV8                               \
    ((JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED) >> 3)

/**
 * Defines JTAG Test Access Port states.
 *
 * These definitions were gleaned from the ARM7TDMI-S Technical
 * Reference Manual and validated against several other ARM core
 * technical manuals.
 *
 * FIXME some interfaces require specific numbers be used, as they
 * are handed-off directly to their hardware implementations.
 * Fix those drivers to map as appropriate ... then pick some
 * sane set of numbers here (where 0/uninitialized == INVALID).
 */
typedef enum tap_state
{
    TAP_INVALID = -1,

    /* Proper ARM recommended numbers */
    TAP_DREXIT2 = 0x0,
    TAP_DREXIT1 = 0x1,
    TAP_DRSHIFT = 0x2,
    TAP_DRPAUSE = 0x3,
    TAP_IRSELECT = 0x4,
    TAP_DRUPDATE = 0x5,
    TAP_DRCAPTURE = 0x6,
    TAP_DRSELECT = 0x7,
    TAP_IREXIT2 = 0x8,
    TAP_IREXIT1 = 0x9,
    TAP_IRSHIFT = 0xa,
    TAP_IRPAUSE = 0xb,
    TAP_IDLE = 0xc,
    TAP_IRUPDATE = 0xd,
    TAP_IRCAPTURE = 0xe,
    TAP_RESET = 0x0f,
} tap_state_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __CPU_H__ */
