/* Copyright (C)
* 2009 - John Melton, G0ORX/N6LYT
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

/**
* @file ozyio.h
* @brief USB I/O with Ozy
* @author John Melton, G0ORX/N6LYT
* @version 0.1
* @date 2009-10-13
*/

/*
* modified by Bob Wisdom VK4YA May 2015 to create ozymetis
* modified further Laurence Barker G8NJJ to add USB functionality to pihpsdr
*/

/*
 * code modified from that in Ozy_Metis_RPI_Gateway
 * Laurence Barker, G8NJJ December 2016
 * this gathers all Ozy functionality in one file (merging code
 * from ozy.c).
 * Further modified to add a "discover" function
 *
*/

#if !defined __OZYIO_H__
#define __OZYIO_H__

//
// TX/RX data for up to 2 Mercury Cards
// - firmware version queried (once) through ozy_i2c_readvars
// - everything else queried (periodically) through ozy_i2c_readpwr
//
extern unsigned int penny_fp;                 // Penny Forward Power
extern unsigned int penny_rp;                 // Penny Reverse Power
extern unsigned int penny_alc;                // Penny ALC
extern unsigned int penny_fw;                 // Penny Firmware Version
extern unsigned int mercury_overload[2];      // Mercury ADC overload
extern unsigned int mercury_fw[2];            // Mercury Firmware Version
extern unsigned char ozy_firmware_version[9]; // OZY firmware version

//
// Functions to be called from "outside"
//

extern int ozy_write(int ep, unsigned char* buffer, int buffer_size);
extern int ozy_read(int ep, unsigned char* buffer, int buffer_size);

extern void writepenny(int reset, int mode);   // Init TLV320 on Penelope board
extern int ozy_initialise(void);
extern int ozy_discover(void);           // returns 1 if a device found on USB
extern void ozy_i2c_readpwr(int addr);   // should be executed periodically
extern void ozy_i2c_readvars(void);      // should be executed once

//
// Constants used for I2C operations
// (partly used when calling ozy_i2c_readpwr)
//

#define I2C_MERC1_FW  0x10 // Mercury1 firmware version      (read 2 bytes)
#define I2C_MERC2_FW  0x11 // Mercury2 firmware version      (read 2 bytes)
#define I2C_MERC3_FW  0x12 // Mercury3 firmware version      (read 2 bytes)
#define I2C_MERC4_FW  0x13 // Mercury4 firmware version      (read 2 bytes)

#define I2C_MERC1_ADC_OFS 0x10 // adc1 overflow status       (read 2 bytes)
#define I2C_MERC2_ADC_OFS 0x11 // adc2 overflow status       (read 2 bytes)
#define I2C_MERC3_ADC_OFS 0x12 // adc3 overflow status       (read 2 bytes)
#define I2C_MERC4_ADC_OFS 0x13 // adc4 overflow status       (read 2 bytes)

#define I2C_PENNY_FW  0x15 // Penny firmware version         (read 2 bytes)
#define I2C_PENNY_ALC 0x16 // Penny forward power            (read 2 bytes)
#define I2C_PENNY_FWD 0x17 // Penny forward power from Alex  (read 2 bytes)
#define I2C_PENNY_REV 0x18 // Penny reverse power from Alex  (read 2 bytes)
#define I2C_PENNY_TLV320 0x1B // Penny TLV320 data           (eight times write 2 bytes)

#define VRT_VENDOR_IN  0xC0
#define VRQ_I2C_READ   0x81     // i2c address; length; how much to read
#define VRT_VENDOR_OUT 0x40
#define VRQ_I2C_WRITE  0x08

#endif
