#include <gpiod.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define JTAG_DEVICE0           "/dev/jtag0"

#define __JTAG_IOCTL_MAGIC  0xb2
#define JTAG_SIOCMODE  _IOW(__JTAG_IOCTL_MAGIC, 5, unsigned int)
#define JTAG_SIOCTRST _IOW(__JTAG_IOCTL_MAGIC, 7, unsigned int)

#define S1_PRESENCE 151
#define S0_PCP_PWRGD 150
#define S1_PCP_PWRGD 179

#define UNUSED(x)               (void)(x)

static int event_callback(int event_type, unsigned int line_offset,
        const struct timespec *timestamp, void *data)
{
  UNUSED(data);
  UNUSED(line_offset);
  UNUSED(timestamp);
  UNUSED(event_type);

  /* exit at first rising edge */
  return GPIOD_CTXLESS_EVENT_CB_RET_STOP;
}

int startMonitorGpio(const char *device, unsigned int gpio_number,
  int event_type, int mon_time)
{
  bool active_low = false;
  int rv;
  struct timespec timeout = { (time_t)mon_time, 0 };

  rv = gpiod_ctxless_event_monitor( device, event_type, gpio_number,
          active_low, "jtag-trst-wa", &timeout, NULL, event_callback, NULL);

  if (rv)
  {
    perror("error waiting for events");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

bool isSlavePresent(const char *device, unsigned int gpio_number)
{
  bool active_low = false;
  int value;

  value = gpiod_ctxless_get_value( device, gpio_number, active_low,
          "jtag-trst-wa");
  /* slave_presence active low */
  if (value == 0)
  {
    return true;
  }

  return false;
}

/**
 * jtag_set_trst
 *
 * @lvl: Level of GPIO
 *       0 = low, 1 = high
 */
static void jtag_set_trst(int fd, unsigned int lvl)
{
  if (fd == -1)
    return;

  if (ioctl(fd, JTAG_SIOCTRST, &lvl) == -1) {
    perror("ioctl Failed to set JTAG_SIOCTRST.\n");
    return;
  }

  return;
}

int main(){
  int delayTime = 1;
  int jtag_fd;
  int ret;
  bool slave_present = isSlavePresent("gpiochip0", S1_PRESENCE);

  jtag_fd = open(JTAG_DEVICE0, O_RDWR);
  if (jtag_fd == -1) {
    perror("Can't open jtag driver, please install driver!! \n");
    return -1;
  }

  /* wait for S0_PCP_PWRGD */
  ret = startMonitorGpio("gpiochip0", S0_PCP_PWRGD,
          GPIOD_CTXLESS_EVENT_RISING_EDGE, 60);
  /* toggle S0_JTAG_TRST */
  if (ret == EXIT_SUCCESS)
  {
    jtag_set_trst(jtag_fd, 0);

    usleep(delayTime*1000);

    jtag_set_trst(jtag_fd, 1);
  }
  else
  {
    perror("jtag-trst time out\n");
    close(jtag_fd);
    return ret;
  }

  if (slave_present)
  {
    /* wait for S1_PCP_PWRGD */
    ret = startMonitorGpio("gpiochip0", S1_PCP_PWRGD,
            GPIOD_CTXLESS_EVENT_RISING_EDGE, 60);
    /* toggle S1_JTAG_TRST */
    if (ret == EXIT_SUCCESS)
    {
      jtag_set_trst(jtag_fd, 0);

      usleep(delayTime*1000);

      jtag_set_trst(jtag_fd, 1);
    }
    else
    {
        perror("jtag-trst time out\n");
        close(jtag_fd);
        return ret;
    }
  }

  close(jtag_fd);

  return ret;
}
