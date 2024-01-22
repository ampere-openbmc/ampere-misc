#include "cpu.h"

static tap_state_t state = TAP_RESET;
static int jtag_fd = -1;

void tap_set_state(tap_state_t goal_state)
{
    state = goal_state;
}

tap_state_t tap_get_state(void)
{
    return state;
}

enum jtag_endstate state_conversion(tap_state_t state)
{
    enum jtag_endstate endstate;

    switch (state)
    {
        case TAP_DREXIT2:
            endstate = JTAG_STATE_EXIT2DR;
            break;
        case TAP_DREXIT1:
            endstate = JTAG_STATE_EXIT1DR;
            break;
        case TAP_DRSHIFT:
            endstate = JTAG_STATE_SHIFTDR;
            break;
        case TAP_DRPAUSE:
            endstate = JTAG_STATE_PAUSEDR;
            break;
        case TAP_IRSELECT:
            endstate = JTAG_STATE_SELECTIR;
            break;
        case TAP_DRUPDATE:
            endstate = JTAG_STATE_UPDATEDR;
            break;
        case TAP_DRCAPTURE:
            endstate = JTAG_STATE_CAPTUREDR;
            break;
        case TAP_DRSELECT:
            endstate = JTAG_STATE_SELECTDR;
            break;
        case TAP_IREXIT2:
            endstate = JTAG_STATE_EXIT2IR;
            break;
        case TAP_IREXIT1:
            endstate = JTAG_STATE_EXIT1IR;
            break;
        case TAP_IRSHIFT:
            endstate = JTAG_STATE_SHIFTIR;
            break;
        case TAP_IRPAUSE:
            endstate = JTAG_STATE_PAUSEIR;
            break;
        case TAP_IDLE:
            endstate = JTAG_STATE_IDLE;
            break;
        case TAP_IRUPDATE:
            endstate = JTAG_STATE_UPDATEIR;
            break;
        case TAP_IRCAPTURE:
            endstate = JTAG_STATE_CAPTUREIR;
            break;
        case TAP_RESET:
            endstate = JTAG_STATE_TLRESET;
            break;
        default:
            endstate = JTAG_STATE_IDLE;
    }

    return endstate;
}

/**
 * Retrieves @c num bits from @c _buffer, starting at the @c first bit,
 * returning the bits in a 32-bit word.  This routine fast-paths reads
 * of little-endian, byte-aligned, 32-bit words.
 * @param _buffer The buffer whose bits will be read.
 * @param first The bit offset in @c _buffer to start reading (0-31).
 * @param num The number of bits from @c _buffer to read (1-32).
 * @returns Up to 32-bits that were read from @c _buffer.
 */
static inline uint32_t buf_get_u32(const uint8_t* _buffer, unsigned first,
                                   unsigned num)
{
    const uint8_t* buffer = _buffer;

    if ((num == 32) && (first == 0))
    {
        return (((uint32_t)buffer[3]) << 24) | (((uint32_t)buffer[2]) << 16) |
               (((uint32_t)buffer[1]) << 8) | (((uint32_t)buffer[0]) << 0);
    }
    else
    {
        uint32_t result = 0;
        for (unsigned i = first; i < first + num; i++)
        {
            if (((buffer[i / 8] >> (i % 8)) & 1) == 1)
                result |= 1U << (i - first);
        }
        return result;
    }
}

static int jtag_cpu_open(int jtag_device, unsigned int mode)
{
    struct jtag_mode m;

    if (jtag_device == 0)
    {
        jtag_fd = open(JTAG_DEVICE0, O_RDWR);
    }
    else if (jtag_device == 1)
    {
        jtag_fd = open(JTAG_DEVICE1, O_RDWR);
    }
    else
    {
        perror("Can't open jtag driver, please install driver!! \n");
        return -1;
    }
    if (jtag_fd == -1)
    {
        perror("Can't open jtag driver, please install driver!! \n");
        return -1;
    }

    if (mode != 0)
    {
        m.feature = JTAG_XFER_MODE;
        m.mode = mode;
        // Set to desired mode
        if (ioctl(jtag_fd, JTAG_SIOCMODE, &m) < 0)
        {
            perror("Failed to set JTAG mode!\n");
            return -1;
        }
    }

    return 0;
}

static void jtag_cpu_close(void)
{
    close(jtag_fd);
}

static int jtag_cpu_tdo_xfer(uint8_t* tdio)
{
    int retval = 0;
    struct jtag_xfer xfer;
    uint8_t* addr = tdio;
    int num_bits = BIT_COUNT;

    if (tdio == NULL || jtag_fd == -1)
    {
        return -1;
    }

    xfer.type = JTAG_SDR_XFER;
    xfer.direction = JTAG_READ_XFER;

    while (num_bits > 0)
    {
        xfer.tdio = (__u64)(uintptr_t)addr;
        if (num_bits > JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED)
        {
            xfer.length = (__u32)JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED;
            xfer.endstate = state_conversion(TAP_DRPAUSE);
        }
        else
        {
            xfer.length = (__u32)num_bits;
            xfer.endstate = state_conversion(TAP_DRPAUSE);
        }

        retval = ioctl(jtag_fd, JTAG_IOCXFER, &xfer);
        if (retval < 0)
        {
            perror("ioctl JTAG data xfer fail!\n");
            break;
        }
        else
        {
            if (num_bits > JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED)
            {
                tap_set_state(TAP_DRPAUSE);
            }
        }

        num_bits -= MIN(num_bits, JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED);
        addr += JTAG_MAX_XFER_DATA_LEN_BYTE_ALIGNED_DIV8;
    }

    return retval;
}

static int jtag_cpu_run_test_idle(unsigned char reset, unsigned char end,
                                  unsigned char tck)
{
    int retval = 0;
    struct jtag_end_tap_state run_idle;
    if (jtag_fd == -1)
        return -1;

    run_idle.reset = reset;
    run_idle.endstate = end;
    run_idle.tck = tck;

    retval = ioctl(jtag_fd, JTAG_SIOCSTATE, &run_idle);
    if (retval == -1)
    {
        perror("ioctl JTAG run reset fail!\n");
    }

    return retval;
}

int jtag_cpu_get_id()
{
    int retval = 0;
    unsigned int bit_count = 0;
    uint32_t idcode = 0;
    uint8_t* dr_data = calloc(MAX_TAP, 4);

    retval = jtag_cpu_run_test_idle(JTAG_FORCE_RESET, JTAG_STATE_TLRESET, 0);
    if (retval < 0)
    {
        goto error;
    }

    retval = jtag_cpu_tdo_xfer(dr_data);
    if (retval < 0)
    {
        goto error;
    }

    for (unsigned i = 0; i < MAX_TAP; i++)
    {
        assert(bit_count < MAX_TAP * 32);
        idcode = buf_get_u32((uint8_t*)dr_data, bit_count, 32);

        if ((idcode & 1) == 0)
        {
            /* Zero for LSB indicates a device in bypass */
            printf("TAP does not have valid idcode %x\n", idcode);
            bit_count += 1;
        }
        else
        {
            /* Friendly devices support IDCODE */
            printf("TAP has valid idcode %x\n", idcode);
            bit_count += 32;
        }
    }

    retval = jtag_cpu_run_test_idle(JTAG_FORCE_RESET, JTAG_STATE_TLRESET, 0);
    if (retval < 0)
    {
        goto error;
    }

    free(dr_data);
    return 0;

error:
    free(dr_data);
    return -1;
}

int cpu_probe(int jtag_device)
{
    return jtag_cpu_open(jtag_device, JTAG_XFER_SW_MODE);
}

void cpu_close()
{
    jtag_cpu_close();
}

int cpu_get_id()
{
    return jtag_cpu_get_id();
}
