/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#ifdef GPIO
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <gtk/gtk.h>
#include "i2c.h"
#include "actions.h"
#include "gpio.h"
#include "band.h"
#include "band_menu.h"
#include "bandstack.h"
#include "radio.h"
#include "toolbar.h"
#include "vfo.h"
#include "ext.h"
#include "message.h"

char *i2c_device = "/dev/i2c-1";
unsigned int i2c_address_1 = 0X20;
unsigned int i2c_address_2 = 0X23;

static int i2cfd;
//
// When reading the flags and ints registers of the
// MCP23017, it is important that no other thread
// (e.g., another instance of the interrupt service
// routine), does this concurrently.
// i2c_mutex guarantees this.
//
static GMutex i2c_mutex;

#define SW_2  0X8000
#define SW_3  0X4000
#define SW_4  0X2000
#define SW_5  0X1000
#define SW_6  0X0008
#define SW_7  0X0004
#define SW_8  0X0002
#define SW_9  0X0001
#define SW_10 0X0010
#define SW_11 0X0020
#define SW_12 0X0040
#define SW_13 0X0080
#define SW_14 0X0800
#define SW_15 0X0400
#define SW_16 0X0200
#define SW_17 0X0100

unsigned int i2c_sw[16] = {
  SW_2, SW_3, SW_4, SW_5, SW_6, SW_14, SW_15, SW_13,
  SW_12, SW_11, SW_10, SW_9, SW_7, SW_8, SW_16, SW_17
};

static int write_byte_data(unsigned char reg, unsigned char data) {
  int rc;

  if ((rc = i2c_smbus_write_byte_data(i2cfd, reg, data & 0xFF)) < 0) {
    t_print("%s: write REG_GCONF config failed: addr=%02X %s\n", __FUNCTION__, i2c_address_1, g_strerror(errno));
  }

  return rc;
}

#if 0
//NOTUSED
static unsigned char read_byte_data(unsigned char reg) {
  __s32 data;
  data = i2c_smbus_read_byte_data(i2cfd, reg);
  return data & 0xFF;
}

#endif

static unsigned int read_word_data(unsigned char reg) {
  __s32 data;
  data = i2c_smbus_read_word_data(i2cfd, reg);
  return data & 0xFFFF;
}

void i2c_interrupt() {
  unsigned int flags;
  int i;
  g_mutex_lock(&i2c_mutex);

  for (;;) {
    flags = read_word_data(0x0E);

    // bits in "flags" indicate which input lines triggered an interrupt
    // Two interrupts occuring at about the same time can lead to multiple bits
    // set in "flags" (or no bit set if interrupt has already been processed
    // by another interrupt service routine). If we enter here (protected by
    // the mutex), we handle all interrupts until no one is left (flags==0)
    if (flags == 0) { break; }

    unsigned int ints = read_word_data(0x10);

    //t_print("%s: flags=%04X ints=%04X\n",__FUNCTION__,flags,ints);
    // only those bits in "ints" matter where the corresponding position
    // in "flags" is set. We have a PRESSED or RELEASED event depending on
    // whether the bit in "ints" is set or clear.
    for (i = 0; i < 16 && flags; i++) { // leave loop if no bits left in "flags"
      if (i2c_sw[i] & flags) {
        //t_print("%s: switches=%p sw=%d action=%d\n",__FUNCTION__,switches,i,switches[i].switch_function);
        // The input line associated with switch #i has triggered an interrupt
        // clear *this* bit in flags
        flags &= ~i2c_sw[i];
        schedule_action(switches[i].switch_function, (ints & i2c_sw[i]) ? PRESSED : RELEASED, 0);
      }
    }
  }

  g_mutex_unlock(&i2c_mutex);
}

void i2c_init() {
  int flags;
  t_print("%s: open i2c device %s\n", __FUNCTION__, i2c_device);
  i2cfd = open(i2c_device, O_RDWR);

  if (i2cfd < 0) {
    t_print("%s: open i2c device %s failed: %s\n", __FUNCTION__, i2c_device, g_strerror(errno));
    return;
  }

  t_print("%s: open i2c device %s fd=%d\n", __FUNCTION__, i2c_device, i2cfd);

  if (ioctl(i2cfd, I2C_SLAVE, i2c_address_1) < 0) {
    t_print("%s: ioctl i2c slave %d failed: %s\n", __FUNCTION__, i2c_address_1, g_strerror(errno));
    return;
  }

  g_mutex_init(&i2c_mutex);

  // setup i2c
  if (write_byte_data(0x0A, 0x44) < 0) { return; }

  if (write_byte_data(0x0B, 0x44) < 0) { return; }

  // disable interrupt
  if (write_byte_data(0x04, 0x00) < 0) { return; }

  if (write_byte_data(0x05, 0x00) < 0) { return; }

  // clear defaults
  if (write_byte_data(0x06, 0x00) < 0) { return; }

  if (write_byte_data(0x07, 0x00) < 0) { return; }

  // OLAT
  if (write_byte_data(0x14, 0x00) < 0) { return; }

  if (write_byte_data(0x15, 0x00) < 0) { return; }

  // set GPIOA for pullups
  if (write_byte_data(0x0C, 0xFF) < 0) { return; }

  if (write_byte_data(0x0D, 0xFF) < 0) { return; }

  // reverse polarity
  if (write_byte_data(0x02, 0xFF) < 0) { return; }

  if (write_byte_data(0x03, 0xFF) < 0) { return; }

  // set GPIOA/B for input
  if (write_byte_data(0x00, 0xFF) < 0) { return; }

  if (write_byte_data(0x01, 0xFF) < 0) { return; }

  // INTCON
  if (write_byte_data(0x08, 0x00) < 0) { return; }

  if (write_byte_data(0x09, 0x00) < 0) { return; }

  // setup for an MCP23017 interrupt
  if (write_byte_data(0x04, 0xFF) < 0) { return; }

  if (write_byte_data(0x05, 0xFF) < 0) { return; }

  // flush any interrupts
  g_mutex_lock(&i2c_mutex);
  int count = 0;

  do {
    flags = read_word_data(0x0E);

    if (flags) {
      (void) read_word_data(0x10);
      count++;

      if (count == 10) {
        return;
      }
    }
  } while (flags != 0);

  g_mutex_unlock(&i2c_mutex);
}

#endif
