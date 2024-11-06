/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
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

#ifndef _ALEX_H
#define _ALEX_H

//
// Used in new_protocol.c only.
//
// This defines the "standard" ALEX0 bits. For some gear,
// we need different definitions, but some bits are highly conserved
// Note that in the standard case, RX signals from Ant1,2,3 go through
// the TX low-pass filters!
// For ANAN-7000/8000, there are furthermore ALEX1 bits for the filter
// board of the second RX.
//
// One note about bit 11 (controlling relay K36)
//
// On older (Rev 15/16) filter boards, K36 switches an output (ByPass)
// On newer (Rev 24)    filter boards, K36 switches an input  (ByPass)
//
// That is, we use bit 11 ONLY when "RX BYPASS" is selected as the *input*
// for the PS feedback signal. If you select "RX BYPASS" for the old
// ANAN-100 boards PS will not work, you have to use EXT1.
//
//
#define ALEX_RX_ANTENNA_NONE   0x00000000               // route Ant1,2,2 to RX1
#define ALEX_RX_ANTENNA_XVTR   0x00000100               // route XVTR-in  to RX1 (bit 8)
#define ALEX_RX_ANTENNA_EXT1   0x00000200               // route EXT1     to RX1 (bit 9)
#define ALEX_RX_ANTENNA_EXT2   0x00000400               // route EXT2     to RX1 (bit 10)
#define ALEX_RX_ANTENNA_BYPASS 0x00000800               // activate BYPASS       (bit 11)
#define ANAN7000_RX_SELECT     0x00004000               // Master RX select      (bit 14)

#define ALEX_TX_ANTENNA_1      0x01000000               // route TX to ANT1      (bit 24)
#define ALEX_TX_ANTENNA_2      0x02000000               // route TX to ANT2      (bit 25)
#define ALEX_TX_ANTENNA_3      0x04000000               // route TX to ANT3      (bit 26)

//
// Note: DO NOT SET attenuator bits (13 and 14)  on ANAN-7000/8000
//       since ALEX0(14) is used for a different purpose!
//
#define ALEX_ATTENUATION_0dB   0x00000000
#define ALEX_ATTENUATION_10dB  0x00004000               // activate 10 dB attenuator  for first ADC
#define ALEX_ATTENUATION_20dB  0x00002000               // activate 20 dB attenuator  for first ADC
#define ALEX_ATTENUATION_30dB  0x00006000               // activate both  attenuators for first ADC

//
// Note: Anan 100/200: If RX signal comes from Ant1,2,3 it also goes
//       through the TX LPFs
//
#define ALEX_30_20_LPF         0x00100000               // activate 30/20m TX LPF (bit 20)
#define ALEX_60_40_LPF         0x00200000               // activate 60/40m TX LPF (bit 21)
#define ALEX_80_LPF            0x00400000               // activate 80m    TX LPF (bit 22)
#define ALEX_160_LPF           0x00800000               // activate 160m   TX LPF (bit 23)
#define ALEX_6_BYPASS_LPF      0x20000000               // activate 6m     TX LPF (bit 29)
#define ALEX_12_10_LPF         0x40000000               // activate 12/10m TX LPF (bit 30)
#define ALEX_17_15_LPF         0x80000000               // activate 17/15m TX LPF (bit 31)

//
// ALEX RX high-pass filters (Valid for ANAN-100/200 first ADC)
// NOTE: Anan-7000/8000 use band-pass filters here
//
#define ALEX_13MHZ_HPF         0x00000002               // (bit 1)
#define ALEX_20MHZ_HPF         0x00000004               // (bit 2)
#define ALEX_6M_PREAMP         0x00000008               // (bit 3) 35 MHz HPF + low-noise amplifier
#define ALEX_9_5MHZ_HPF        0x00000010               // (bit 4)
#define ALEX_6_5MHZ_HPF        0x00000020               // (bit 5)
#define ALEX_1_5MHZ_HPF        0x00000040               // (bit 6)
#define ALEX_BYPASS_HPF        0x00001000               // (bit 12)

//
// Bit that controls the RX/TX relay
//
#define ALEX_TX_RELAY          0x08000000               // (bit 27)

//
// Bit used for PureSignal
//
#define ALEX_PS_BIT            0x00040000               // (bit 18)

//
// From this point on, we define bits that have a slightly
// different meaning
//

//
// ALEX bits for ANAN-7000 and ANAN-8000
// Note that we here also have Alex1 bits which control the filters
// for the second ADC. The RX input is also very different and now used
// bit14 which is used for switching attenuators in the general case
// On the RX side, we now have band-pass filters and the RX signals
// from Ant1,2,3 DO NOT PASS the TX low-pass filters.
// The TX bits are just as for the "generic" case.
//

// These bits are valid both for ALEX0 and ALEX1 in the Anan-7000/8000

#define ALEX_ANAN7000_RX_20_15_BPF      0x00000002   // (bit  1), 11.0 - 22.0 MHz
#define ALEX_ANAN7000_RX_12_10_BPF      0x00000004   // (bit  2), 22.0 - 35.6 MHz
#define ALEX_ANAN7000_RX_6_PRE_BPF      0x00000008   // (bit  3),  > 35.6 MHz with preamp
#define ALEX_ANAN7000_RX_40_30_BPF      0x00000010   // (bit  4),  5.5 - 10.9 MHz
#define ALEX_ANAN7000_RX_80_60_BPF      0x00000020   // (bit  5),  2.1 -  5.4 MHz
#define ALEX_ANAN7000_RX_160_BPF        0x00000040   // (bit  6),  1.5 -  2.0 MHz
#define ALEX_ANAN7000_RX_BYPASS_BPF     0x00001000   // (bit 12)

#define ALEX1_ANAN7000_RX_GNDonTX       0x00000100   // (bit 8), ground second ADC input during TX

//
// ANAN7000,8000 specific bits in byte 1400 of the high-priority packet
//
#define ANAN7000_XVTR_OUT               0x00000001   //  if PTT is set, Orion-II RF output is routed to the "XVTR Port" jack (1 = enabled)
#define ANAN7000_SPKR_MUTE              0x00000002   //  Enable/mute audio (1 = mute)
#endif
