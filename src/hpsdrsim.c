/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
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

/*
 *
 * This program simulates a HPSDR board.
 * If an SDR program such as phipsdr "connects" with this program, it
 * writes to stdout what goes on. This is great for debugging.
 *
 * In addition, I have built in the following features:
 *
 * This device has four "RF sources"
 *
 * RF1: ADC noise plus a signal at 14.1 MHz at -73 dBm
 * RF2: noise floor at -130 dBm/Hz
 * RF3: TX feedback signal with some distortion.
 * RF4: normalised undistorted TX signal
 *
 * RF1 and RF2 signal strenght vary according to Preamp and Attenuator settings
 * RF3 signal strength varies according to TX-drive and TX-ATT settings
 * RF4 signal strength is normalised to amplitude of 0.407 (old protocol) or 0.2899 (new protocol)
 *     note HERMESLITEV2 old protocol: 0.23
 *
 * The connection with the ADCs are:
 * ADC0: RF1 upon receive, RF3 upon transmit
 * ADC1: RF2 (for HERMES: RF4)
 * ADC2: RF4
 *
 * RF4 is the TX DAC signal. Upon TX, it goes to RX2 for Metis, RX4 for Hermes, and RX5 beyond.
 * Since the feedback runs at the RX sample rate while the TX sample rate is fixed (48000 Hz),
 * we have to re-sample and do this in a very stupid way (linear interpolation).
 * NOTE: anan10E flag: use RX2 for TX DAC in the HERMES case.
 *
 * The "noise" is a random number
 *
 * The SDR application has to make the proper ADC settings, except for STEMlab
 * (RedPitaya based SDRs), where there is a fixed association
 * RX1=ADC1, RX2=ADC2, RX3=ADC2, RX4=TX-DAC
 * and the PureSignal feedback signal is connected to the second ADC.
 *
 * If invoked with the "-diversity" flag, broad "man-made" noise is fed to ADC1 and
 * ADC2 upon RXing. The ADC2 signal is phase shifted by 90 degrees and somewhat
 * stronger. This noise can completely be eliminated using DIVERSITY.
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#ifdef __APPLE__
  #include "MacOS.h"  // emulate clock_gettime on old MacOS systems
#endif

#define EXTERN
#include "hpsdrsim.h"

/*
 * These variables store the state of the "old protocol" SDR.
 * Whenevery they are changed, this is reported.
 */

static int              AlexTXrel = -1;
static int              alexRXout = -1;
static int              alexRXant = -1;
static int              MicTS = -1;
static int              duplex = -1;
static int              receivers = -1;
static int              rate = -1;
static int              preamp = -1;
static int              LTdither = -1;
static int              LTrandom = -1;
static int              ref10 = -1;
static int              src122 = -1;
static int              PMconfig = -1;
static int              MicSrc = -1;
static int              txdrive = 0;
static int              txatt = 0;
static int              sidetone_volume = -1;
static int              cw_internal = -1;
static int              envgain = 0;
static int              pwmmin = 0;
static int              pwmmax = 0;
static int              adc2bpf = 0;
static int              anan7kps = 0;
static int              anan7kxvtr = 0;
static int              rx_att[2] = {-1, -1};
static int              rx1_attE = -1;
static int              rx_preamp[4] = {-1, -1, -1, -1};
static int              MerTxATT0 = -1;
static int              MerTxATT1 = -1;
static int              MetisDB9 = -1;
static int              PeneSel = -1;
static int              PureSignal = -1;
static int              LineGain = -1;
static int              MicPTT = -1;
static int              tip_ring = -1;
static int              MicBias = -1;
static int              ptt = 0;
static int              AlexAtt = -1;
static int              TX_class_E = -1;
static int              OpenCollectorOutputs = -1;
static long             tx_freq = -1;
static long             rx_freq[7] = {-1, -1, -1, -1, -1, -1, -1};
static int              alex_lpf = -1;
static int              alex_hpf = -1;
static int              alex_manual = -1;
static int              alex_bypass = -1;
static int              lna6m = -1;
static int              alexTRdisable = -1;
static int              vna = -1;
static int              line_in = -1;
static int              mic_boost = -1;
static int              apollo_filter = -1;
static int              apollo_tuner = -1;
static int              apollo_auto_tune = -1;
static int              alex_apollo = -1;
static int              hl2_q5 = -1;
static int              hl2_tune = -1;
static int              hl2_pa = -1;
static int              hl2_tx_latency = 1;
static int              hl2_ptt_hang = -1;
static int              c25_ext_board_i2c_data = -1;
static int              rx_adc[7] = {-1, -1, -1, -1, -1, -1, -1};
static int              cw_hang = -1;
static int              cw_reversed = -1;
static int              cw_speed = -1;
static int              cw_mode = -1;
static int              cw_weight = -1;
static int              cw_spacing = -1;
static int              cw_delay = -1;
static int              CommonMercuryFreq = -1;
static int              freq = -1;
static int              rx2gnd = -1;
static int              TXDAC = 1;
static double           p1noisefac = 6.92820;

struct hl2word {
  unsigned char c1;
  unsigned char c2;
  unsigned char c3;
  unsigned char c4;
} hl2addr[128];

const double hl2drv[16] = { 0.421697, 0.446684, 0.473151, 0.501187, 0.530884, 0.562341, 0.595662,
                            0.630957, 0.668344, 0.707946, 0.749894, 0.794328, 0.841395, 0.891251,
                            0.944061, 1.000000
                          };

// floating-point represeners of TX att, RX att, and RX preamp settings

static double txdrv_dbl = 0.99;
static double txatt_dbl = 1.0;
static double rxatt_dbl[4] = {1.0, 1.0, 1.0, 1.0};   // this reflects both ATT and PREAMP

/*
 * Socket for communicating with the "PC side"
 */
static int sock_TCP_Server = -1;
static int sock_TCP_Client = -1;
static int sock_udp;

/*
 * These two variables monitor whether the TX thread is active
 */
static int enable_thread = 0;
static int active_thread = 0;

static void process_ep2(uint8_t *frame);
static void *handler_ep6(void *arg);

static double  last_i_sample = 0.0;
static double  last_q_sample = 0.0;
static int  txptr = -1;
static int  oldnew = 3;  // 1: only P1, 2: only P2, 3: P1 and P2,
static int  anan10e = 0; // HERMES with anan10e set behaves like METIS
int  speed = 0;   // 0: nominal speed, -1: radio is 1% too slow, +1: radio is 1% too fast

static double txlevel;

static double tonearg, tonearg2;
static double tonedelta, tonedelta2;
static int    do_tone, t3p, t3l;

static  struct termios tios_old;

static struct sigaction sigint_action;
static struct sigaction sigterm_action;

//
// Call this when the program terminates, or when
// a SIGINT or SIGTERM is received.
// This restores terminal settings on stdin
//
void restore_terminal_attributes() {
  tcsetattr(0, TCSANOW, &tios_old);
}

//
// This signal handler does the following:
// - restore terminal attributes (no matter which signal)
// - if it is SIGINT or SIGTERM:
//    --> reset signal handler and re-raise signal
// - if it is an unknown signal (should not happen):
//    --> terminate program.
//
void my_signal_handler(int sig) {
  struct sigaction sa;
  restore_terminal_attributes();
  memset(&sa, 0, sizeof(sa));
  switch (sig) {
    case SIGINT:
      sigaction(SIGINT, &sigint_action, NULL);
      raise(SIGINT);
      break;
    case SIGTERM:
      sigaction(SIGTERM, &sigterm_action, NULL);
      raise(SIGTERM);
      break;
    default:
      _exit(EXIT_FAILURE);
      // NOTREACHED
     break;
  }
}


int main(int argc, char *argv[]) {
  int i, j, size;
  int count = 0;
  pthread_t thread;
  uint8_t id[4] = { 0xef, 0xfe, 1, 6 };
  uint32_t code;
  int16_t sample;
  struct sockaddr_in addr_udp;
  uint8_t buffer[1032];
  struct timeval tv;
  int yes = 1;
  uint8_t *bp;
  unsigned long checksum = 0;
  socklen_t lenaddr;
  struct sockaddr_in addr_from;
  unsigned int seed;
  memset(hl2addr, 0, sizeof(hl2addr));
  uint32_t last_seqnum = 0xffffffff, seqnum;  // sequence number of received packet
  int udp_retries = 0;
  int bytes_read, bytes_left;
  uint32_t *code0 = (uint32_t *) buffer;  // fast access to code of first buffer
  double run, off, off2, inc;
  struct timeval tvzero = {0, 0};
  fd_set fds;
  struct sigaction sa;
  struct termios tios_new;
  /*
   *      Examples for METIS:     ATLAS bus with Mercury/Penelope boards
   *      Examples for HERMES:    ANAN10, ANAN100 (Note ANAN-10E/100B behave like METIS)
   *      Examples for ANGELIA:   ANAN100D
   *      Examples for ORION:     ANAN200D
   *      Examples for ORION2:    ANAN7000, ANAN8000
   *
   *      Examples for C25:       RedPitaya based boards with fixed ADC connections
   */
  radio_digi_changed = 0; // used  to trigger a highprio packet
  radio_ptt = 0;
  radio_dash = 0;
  radio_dot = 0;
  radio_io1 = 1;
  radio_io2 = 1;
  radio_io3 = 1;
  radio_io4 = 1;
  radio_io5 = 1;
  radio_io6 = 1;
  radio_io8 = 0;
  // seed value for random number generator
  seed = ((uintptr_t) &seed) & 0xffffff;
  tonearg = 0.0;
  tonedelta = 0.0;
  tonearg2 = 0.0;
  tonedelta2 = 0.0;
  do_tone = 0;
  diversity = 0;
  noiseblank = 0;
  nb_pulse = 0;
  nb_width = 0;
  const int MAC1 = 0x00;
  const int MAC2 = 0x1C;
  const int MAC3 = 0xC0;
  const int MAC4 = 0xA2;
  int MAC5 = 0x10;
  const int MAC6 = 0xDD;  // P1
  const int MAC6N = 0xEE; // P2
  OLDDEVICE = ODEV_ORION2;
  NEWDEVICE = NDEV_ORION2;

  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "-atlas",        6))  {OLDDEVICE = ODEV_METIS;        NEWDEVICE = NDEV_ATLAS;         MAC5 = 0x11;             continue;}

    if (!strncmp(argv[i], "-metis",        6))  {OLDDEVICE = ODEV_METIS;        NEWDEVICE = NDEV_ATLAS;         MAC5 = 0x12;             continue;}

    if (!strncmp(argv[i], "-hermeslite2", 12))  {OLDDEVICE = ODEV_HERMES_LITE2; NEWDEVICE = NDEV_HERMES_LITE2;  MAC5 = 0x13; oldnew = 1; continue;}

    if (!strncmp(argv[i], "-hermeslite",  11))  {OLDDEVICE = ODEV_HERMES_LITE;  NEWDEVICE = NDEV_HERMES_LITE;   MAC5 = 0x14; oldnew = 1; continue;}

    if (!strncmp(argv[i], "-hermes",       7))  {OLDDEVICE = ODEV_HERMES;       NEWDEVICE = NDEV_HERMES;        MAC5 = 0x15;             continue;}

    if (!strncmp(argv[i], "-griffin",      8))  {OLDDEVICE = ODEV_GRIFFIN;      NEWDEVICE = NDEV_HERMES2;       MAC5 = 0x16;             continue;}

    if (!strncmp(argv[i], "-angelia",      8))  {OLDDEVICE = ODEV_ANGELIA;      NEWDEVICE = NDEV_ANGELIA;       MAC5 = 0x17;             continue;}

    if (!strncmp(argv[i], "-orion2",       7))  {OLDDEVICE = ODEV_ORION2;       NEWDEVICE = NDEV_ORION2;        MAC5 = 0x18;             continue;}

    if (!strncmp(argv[i], "-g2",           3))  {OLDDEVICE = ODEV_NONE;         NEWDEVICE = NDEV_SATURN;        MAC5 = 0x19; oldnew = 2; continue;}

    if (!strncmp(argv[i], "-orion",        6))  {OLDDEVICE = ODEV_ORION;        NEWDEVICE = NDEV_ORION;         MAC5 = 0x1A;             continue;}

    if (!strncmp(argv[i], "-c25",          4))  {OLDDEVICE = ODEV_C25;          NEWDEVICE = NDEV_C25;           MAC5 = 0x1B; oldnew = 1; continue;}

    if (!strncmp(argv[i], "-diversity",   10))  {diversity = 1; continue;}

    if (!strncmp(argv[i], "-P1",           3))  {oldnew = 1; continue;}

    if (!strncmp(argv[i], "-P2",           3))  {oldnew = 2; continue;}

    if (!strncmp(argv[i], "-anan10e",      8))  {anan10e = 1; continue;}

    if (!strncmp(argv[i], "-fast",         5))  {speed = 1; continue;}

    if (!strncmp(argv[i], "-slow",         5))  {speed = -1; continue;}

    if (!strncmp(argv[i], "-nb",           3))  {
      noiseblank = 1;

      if (i < argc - 1) { sscanf(argv[++i], "%d", &nb_pulse); }

      if (i < argc - 1) { sscanf(argv[++i], "%d", &nb_width); }

      if (nb_pulse < 1 || nb_pulse > 200) { nb_pulse = 5; }

      if (nb_width < 1 || nb_width > 200) { nb_width = 100; }

      continue;
    }

    t_print("Unknown option: %s\n", argv[i]);
    t_print("Valid options are: -atlas | -metis  | -hermes     | -griffin     | -angelia |\n");
    t_print("                   -orion | -orion2 | -hermeslite | -hermeslite2 | -c25     |\n");
    t_print("                   -diversity | -P1 | -P2         | -fast        | -slow    |\n");
    t_print("                   -nb <num> <width>\n");
    exit(8);
  }

  //
  // put stdin into raw mode
  //
  tcgetattr(0, &tios_old);
  tios_new = tios_old;
  tios_new.c_lflag &= ~ICANON;
  tios_new.c_lflag &= ~ECHO;
  tcsetattr(0, TCSANOW, &tios_new);

  //
  // Establish a new signal handler for SIGINT and SIGTERM,
  // that takes care of the restoration of terminal attributes
  //
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = my_signal_handler;
  sigaction(SIGINT, &sa, &sigint_action);
  sigaction(SIGTERM, &sa, &sigterm_action);

  switch (NEWDEVICE) {
  case   NDEV_ATLAS:
    t_print("DEVICE is ATLAS/METIS\n");
    c1 = 3.3;
    c2 = 0.090;
    TXDAC = 1;
    maxpwr = 20.0;
    break;

  case   NDEV_HERMES:
    t_print("DEVICE is HERMES\n");
    c1 = 3.3;
    c2 = 0.095;
    maxpwr = 200.0;

    if (anan10e) {
      TXDAC = 1;
      t_print("Anan10E/Anan100B simulation\n");
      maxpwr = 20.0;
    } else {
      TXDAC = 3;
    }

    break;

  case   NDEV_HERMES2:
    t_print("DEVICE is HERMES2/GRIFFIN\n");
    c1 = 3.3;
    c2 = 0.095;
    TXDAC = 3;
    maxpwr = 200.0;
    break;

  case   NDEV_ANGELIA:
    t_print("DEVICE is ANGELIA\n");
    c1 = 3.3;
    c2 = 0.095;
    TXDAC = 4;
    maxpwr = 200.0;
    break;

  case   NDEV_HERMES_LITE:
    t_print("DEVICE is HermesLite V1\n");
    c1 = 3.3;
    c2 = 1.5;
    TXDAC = 1;
    maxpwr = 7.0;
    break;

  case   NDEV_HERMES_LITE2:
    t_print("DEVICE is HermesLite V2\n");
    c1 = 3.3;
    c2 = 1.5;
    TXDAC = 3;
    maxpwr = 7.0;
    break;

  case   NDEV_ORION:
    t_print("DEVICE is ORION\n");
    c1 = 5.0;
    c2 = 0.108;
    TXDAC = 4;
    maxpwr = 200.0;
    break;

  case   NDEV_ORION2:
    t_print("DEVICE is ORION MkII\n");
    c1 = 5.0;
    c2 = 0.12;
    TXDAC = 4;
    maxpwr = 500.0;
    break;

  case   NDEV_SATURN:
    t_print("DEVICE is SATURN/G2\n");
    c1 = 5.0;
    c2 = 0.12;
    TXDAC = 4;
    maxpwr = 200.0;
    break;

  case   NDEV_C25:
    t_print("DEVICE is STEMlab/C25\n");
    c1 = 3.3;
    c2 = 0.090;
    TXDAC = 3;
    maxpwr = 20.0;
    break;
  }

  if (speed == 1) {
    t_print("DEVICE is 1%% too fast\n");
  } else if (speed == -1) {
    t_print("DEVICE is 1%% too slow\n");
  }

  //
  //      Initialise the data in the sample tables
  //
  t_print(".... producing random noise\n");
  // Produce some noise
  j = RAND_MAX / 2;

  //
  // Note noise amplitude has to be multiplied with
  // sqrt(sample_rate/48k)
  //
  for (i = 0; i < LENNOISE; i++) {
    noiseItab[i] = ((double) rand_r(&seed) / j - 1.0) * 1.41421E-5;
    noiseQtab[i] = ((double) rand_r(&seed) / j - 1.0) * 1.41421E-5;
  }

  //
  // Use only one buffer, so diversity and
  // noise blanker testing are mutually exclusive
  // so diversity==0 means "no man-made noise",
  //    diversity==1 && noiseblank == 0 means "noise for testing diversity"
  //    diversity==1 && noiseblank == 1 means "noise for testing noise blanker"
  //
  if (noiseblank) { diversity = 1; }

  if (diversity && !noiseblank) {
    //
    // The diversity signal is a "comb" with a lot
    // of equally spaces cosines
    //
    t_print("DIVERSITY testing activated!\n");
    t_print(".... producing some man-made noise\n");
    memset(divtab, 0, LENDIV * sizeof(double));

    for (j = 1; j <= 200; j++) {
      run = 0.0;
      off = 0.25 * j * j;
      inc = j * 0.00039269908169872415480783042290994;

      for (i = 0; i < LENDIV; i++) {
        divtab[i] += cos(run + off);
        run += inc;
      }
    }

    // normalise
    off = 0.0;

    for (i = 0; i < LENDIV; i++) {
      if ( divtab[i] > off) { off = divtab[i]; }

      if (-divtab[i] > off) { off = -divtab[i]; }
    }

    off = 1.0 / off;
    t_print("(normalizing with %f)\n", off);

    for (i = 0; i < LENDIV; i++) {
      divtab[i] = divtab[i] * off;
    }
  }

  if (diversity && noiseblank) {
    //
    // Create impulse noise as a real-time signal
    // n impulses per second
    // m samples wide
    // about -80 dBm in 1000 Hz
    //
    off = sqrt(0.05 / (nb_pulse * nb_width));
    memset(divtab, 0, LENDIV * sizeof(double));
    t_print("NOISE BLANKER test activated: %d pulses of width %d within %d samples\n",
            nb_pulse, nb_width, LENDIV);

    for (i = 0; i < nb_pulse; i++) {
      for (j = (i * LENDIV) / nb_pulse; j < (i * LENDIV) / nb_pulse + nb_width; j++) { divtab[j] = off; }
    }
  }

  //
  //      clear TX fifo
  //
  txptr = -1;
  memset (isample, 0, OLDRTXLEN * sizeof(double));
  memset (qsample, 0, OLDRTXLEN * sizeof(double));

  if ((sock_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    t_perror("socket");
    restore_terminal_attributes();
    return EXIT_FAILURE;
  }

  setsockopt(sock_udp, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock_udp, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  tv.tv_sec = 0;
  tv.tv_usec = 1000;
  setsockopt(sock_udp, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
  memset(&addr_udp, 0, sizeof(addr_udp));
  addr_udp.sin_family = AF_INET;
  addr_udp.sin_addr.s_addr = htonl(INADDR_ANY);
  addr_udp.sin_port = htons(1024);

  if (bind(sock_udp, (struct sockaddr *)&addr_udp, sizeof(addr_udp)) < 0) {
    t_perror("bind");
    restore_terminal_attributes();
    return EXIT_FAILURE;
  }

  if ((sock_TCP_Server = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    t_perror("socket tcp");
    restore_terminal_attributes();
    return EXIT_FAILURE;
  }

  setsockopt(sock_TCP_Server, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  int tcpmaxseg = 1032;
  setsockopt(sock_TCP_Server, IPPROTO_TCP, TCP_MAXSEG, (const char *)&tcpmaxseg, sizeof(int));
  int sndbufsize = 65535;
  int rcvbufsize = 65535;
  setsockopt(sock_TCP_Server, SOL_SOCKET, SO_SNDBUF, (const char *)&sndbufsize, sizeof(int));
  setsockopt(sock_TCP_Server, SOL_SOCKET, SO_RCVBUF, (const char *)&rcvbufsize, sizeof(int));
  tv.tv_sec = 0;
  tv.tv_usec = 1000;
  setsockopt(sock_TCP_Server, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));

  if (bind(sock_TCP_Server, (struct sockaddr *)&addr_udp, sizeof(addr_udp)) < 0) {
    t_perror("bind tcp");
    restore_terminal_attributes();
    return EXIT_FAILURE;
  }

  listen(sock_TCP_Server, 1024);
  t_print( "Listening for TCP client connection request\n");
  int flags = fcntl(sock_TCP_Server, F_GETFL, 0);
  fcntl(sock_TCP_Server, F_SETFL, flags | O_NONBLOCK);

  while (1) {
    memcpy(buffer, id, 4);
    count++;
    //
    // If the keyboard has been hit, read character and consume it
    //
    FD_ZERO(&fds);
    FD_SET(0, &fds);   // 0 is stdin

    if (select(1, &fds, NULL, NULL, &tvzero) > 0) {
      unsigned char c;
      int rc = read(0, &c, sizeof(c));

      if (rc > 0) {
        radio_digi_changed = 1;

        switch (c) {
        case '1':
          radio_io1 = !radio_io1;
          break;

        case '2':
          radio_io2 = !radio_io2;
          break;

        case '3':
          radio_io3 = !radio_io3;
          break;

        case '4':
          radio_io4 = !radio_io4;
          break;

        case '5':
          radio_io5 = !radio_io5;
          break;

        case '6':
          radio_io6 = !radio_io6;
          break;

        case '8':
          radio_io8 = !radio_io8;
          break;

        case 'l':
          radio_dot = !radio_dot;
          break;

        case 'r':
          radio_dash = !radio_dash;
          break;

        case 'p':
          radio_ptt = !radio_ptt;
          break;
        }
      }
    }

    if (sock_TCP_Client > -1) {
      // Using recvmmsg with a time-out should be used for a byte-stream protocol like TCP
      // (Each "packet" in the datagram may be incomplete). This is especially true if the
      // socket has a receive time-out, but this problem also occurs if the is no such
      // receive time-out.
      // Therefore we read a complete packet here (1032 bytes). Our TCP-extension to the
      // HPSDR protocol ensures that only 1032-byte packets may arrive here.
      bytes_read = 0;
      bytes_left = 1032;

      while (bytes_left > 0) {
        size = recvfrom(sock_TCP_Client, buffer + bytes_read, (size_t)bytes_left, 0, NULL, 0);

        if (size < 0 && errno == EAGAIN) { continue; }

        if (size < 0) { break; }

        bytes_read += size;
        bytes_left -= size;
      }

#ifdef PACKETLIST
      t_print("TCP P1\n");
#endif
      bytes_read = size;

      if (size >= 0) {
        // 1032 bytes have successfully been read by TCP.
        // Let the downstream code know that there is a single packet, and its size
        bytes_read = 1032;

        // In the case of a METIS-discovery packet, change the size to 63
        if (*code0 == 0x0002feef) {
          bytes_read = 63;
        }

        // In principle, we should check on (*code0 & 0x00ffffff) == 0x0004feef,
        // then we cover all kinds of start and stop packets.
        // In the case of a METIS-stop packet, change the size to 64
        if (*code0 == 0x0004feef) {
          bytes_read = 64;
        }

        // In the case of a METIS-start TCP packet, change the size to 64
        // The special start code 0x11 has no function any longer, but we shall still support it.
        if (*code0 == 0x1104feef || *code0 == 0x0104feef) {
          bytes_read = 64;
        }
      }
    } else {
      lenaddr = sizeof(addr_from);
      bytes_read = recvfrom(sock_udp, buffer, 1032, 0, (struct sockaddr *)&addr_from, &lenaddr);

      if (bytes_read > 0) {
        udp_retries = 0;
#ifdef PACKETLIST
        t_print("UDP P1\n");
#endif
      } else {
        udp_retries++;
      }
    }

    if (bytes_read < 0 && errno != EAGAIN) {
      t_perror("recvfrom");
      restore_terminal_attributes();
      return EXIT_FAILURE;
    }

    // If nothing has arrived via UDP for some time, try to open TCP connection.
    // "for some time" means 10 subsequent un-successful UDP rcvmmsg() calls
    if (sock_TCP_Client < 0 && udp_retries > 10 && oldnew != 2) {
      if ((sock_TCP_Client = accept(sock_TCP_Server, (struct sockaddr *)&addr_from, &lenaddr)) > -1) {
        t_print("sock_TCP_Client: Connected from %s\n", inet_ntoa(addr_from.sin_addr));
      }

      // This avoids firing accept() too often if it constantly fails
      udp_retries = 0;
    }

    if (count >= 5000 && active_thread) {
      t_print( "WATCHDOG STOP the transmission via handler_ep6\n");
      enable_thread = 0;

      while (active_thread) { usleep(1000); }

      txptr = -1;

      if (sock_TCP_Client > -1) {
        close(sock_TCP_Client);
        sock_TCP_Client = -1;
      }

      continue;
    }

    if (bytes_read < 0) {
      continue;
    }

    count = 0;
    code = *code0;

    switch (code) {
    // PC to SDR transmission via process_ep2
    case 0x0201feef:

      // processing an invalid packet is too dangerous -- skip it!
      if (bytes_read != 1032) {
        t_print("InvalidLength: RvcMsg Code=0x%08x Len=%d\n", code, (int)bytes_read);
        break;
      }

      // sequence number check
      seqnum = ((buffer[4] & 0xFF) << 24) + ((buffer[5] & 0xFF) << 16) + ((buffer[6] & 0xFF) << 8) + (buffer[7] & 0xFF);

      if (seqnum != last_seqnum + 1) {
        t_print("SEQ ERROR: last %ld, recvd %ld\n", (long)last_seqnum, (long)seqnum);
      }

      last_seqnum = seqnum;
      process_ep2(buffer + 11);
      process_ep2(buffer + 523);

      if (labs(7100000L - rx_freq[0]) < (24000 << rate)) {
        //
        // weak single-tone signal at 7100 kHz
        //
        off = (double)(7100000 - rx_freq[0]);
        tonedelta = -6.283185307179586476925286766559 * off / ((double) (48000 << rate));
        do_tone = 3;
        t3l = 9600 << rate;
      } else if (labs(14100000L - rx_freq[0]) < (24000 << rate)) {
        //
        // -73 dBm single-tone signal at 14100 kHz
        //
        off = (double)(14100000 - rx_freq[0]);
        tonedelta = -6.283185307179586476925286766559 * off / ((double) (48000 << rate));
        do_tone = 1;
      } else if (labs(21100000L - rx_freq[0]) < (24000 << rate)) {
        //
        // two -73 dBm signals at 21100.0 and 21000.9 kHz
        //
        off = (double)(21100000 - rx_freq[0]);
        tonedelta = -6.283185307179586476925286766559 * off / ((double) (48000 << rate));
        off2 = (double)(21100900 - rx_freq[0]);
        tonedelta2 = -6.283185307179586476925286766559 * off2 / ((double) (48000 << rate));
        do_tone = 2;
      } else {
        do_tone = 0;
      }

      if (active_thread) {
        if (txptr < 0) {
          txptr = OLDRTXLEN / 2;
        }

        // Put TX IQ samples into the ring buffer
        // In the old protocol, samples come in groups of 8 bytes L1 L0 R1 R0 I1 I0 Q1 Q0
        // Here, L1/L0 and R1/R0 are audio samples, and I1/I0 and Q1/Q0 are the TX iq samples
        // I1 contains bits 8-15 and I0 bits 0-7 of a signed 16-bit integer. We convert this
        // here to double. If the RX sample rate is larger than the TX on, we perform a
        // simple linear interpolation between the last and current sample.
        // Note that this interpolation causes weak "sidebands" at 48/96/... kHz distance (the
        // strongest ones at 48 kHz).
        double disample, dqsample, idelta, qdelta;
        double sum;
        bp = buffer + 16; // skip 8 header and 8 SYNC/C&C bytes
        sum = 0.0;

        for (j = 0; j < 126; j++) {
          bp += 4; // skip audio samples
          sample  = (int)((signed char) * bp++) << 8;
          sample |= (int) ((signed char) * bp++ & 0xFF);
          disample = (double) sample * 0.000030517578125; // division by 32768
          sample  = (int)((signed char) * bp++) << 8;
          sample |= (int) ((signed char) * bp++ & 0xFF);
          dqsample = (double) sample * 0.000030517578125;
          sum += (disample * disample + dqsample * dqsample);

          switch (rate) {
          case 0:  // RX sample rate = TX sample rate = 48000
            isample[txptr  ] = disample;
            qsample[txptr++] = dqsample;
            break;

          case 1: // RX sample rate = 96000; TX sample rate = 48000
            idelta = 0.5 * (disample - last_i_sample);
            qdelta = 0.5 * (dqsample - last_q_sample);
            isample[txptr  ] = last_i_sample + idelta;
            qsample[txptr++] = last_q_sample + qdelta;
            isample[txptr  ] = disample;
            qsample[txptr++] = dqsample;
            break;

          case 2: // RX sample rate = 192000; TX sample rate = 48000
            idelta = 0.25 * (disample - last_i_sample);
            qdelta = 0.25 * (dqsample - last_q_sample);
            isample[txptr  ] = last_i_sample + idelta;
            qsample[txptr++] = last_q_sample + qdelta;
            isample[txptr  ] = last_i_sample + 2.0 * idelta;
            qsample[txptr++] = last_q_sample + 2.0 * qdelta;
            isample[txptr  ] = last_i_sample + 3.0 * idelta;
            qsample[txptr++] = last_q_sample + 3.0 * qdelta;
            isample[txptr  ] = disample;
            qsample[txptr++] = dqsample;
            break;

          case 3: // RX sample rate = 384000; TX sample rate = 48000
            idelta = 0.125 * (disample - last_i_sample);
            qdelta = 0.125 * (dqsample - last_q_sample);
            isample[txptr  ] = last_i_sample + idelta;
            qsample[txptr++] = last_q_sample + qdelta;
            isample[txptr  ] = last_i_sample + 2.0 * idelta;
            qsample[txptr++] = last_q_sample + 2.0 * qdelta;
            isample[txptr  ] = last_i_sample + 3.0 * idelta;
            qsample[txptr++] = last_q_sample + 3.0 * qdelta;
            isample[txptr  ] = last_i_sample + 4.0 * idelta;
            qsample[txptr++] = last_q_sample + 4.0 * qdelta;
            isample[txptr  ] = last_i_sample + 5.0 * idelta;
            qsample[txptr++] = last_q_sample + 5.0 * qdelta;
            isample[txptr  ] = last_i_sample + 6.0 * idelta;
            qsample[txptr++] = last_q_sample + 6.0 * qdelta;
            isample[txptr  ] = last_i_sample + 7.0 * idelta;
            qsample[txptr++] = last_q_sample + 7.0 * qdelta;
            isample[txptr  ] = disample;
            qsample[txptr++] = dqsample;
            break;
          }

          last_i_sample = disample;
          last_q_sample = dqsample;

          if (j == 62) { bp += 8; } // skip 8 SYNC/C&C bytes of second block
        }

        txlevel = txdrv_dbl * txdrv_dbl * sum * 0.0079365;

        // wrap-around of ring buffer
        if (txptr >= OLDRTXLEN) { txptr = 0; }
      }

      break;

    // respond to an incoming Metis detection request
    case 0x0002feef:

      // processing an invalid packet is too dangerous -- skip it!
      if (bytes_read != 63) {
        t_print("InvalidLength: RvcMsg Code=0x%08x Len=%d\n", code, (int)bytes_read);
        break;
      }

      if (oldnew == 2) {
        t_print("OldProtocol detection request from %s IGNORED.\n", inet_ntoa(addr_from.sin_addr));
        break;  // Swallow P1 detection requests
      }

      t_print( "Respond to an incoming Metis detection request from %s / code: 0x%08x\n", inet_ntoa(addr_from.sin_addr),
               code);
      memset(buffer, 0, 60);
      buffer[0] = 0xEF;
      buffer[1] = 0xFE;
      buffer[2] = 0x02;
      buffer[3] = MAC1; // buffer[3:8] is MAC address
      buffer[4] = MAC2;
      buffer[5] = MAC3;
      buffer[6] = MAC4;
      buffer[7] = MAC5; // specifies type of radio
      buffer[8] = MAC6; // encodes old protocol
      buffer[ 2] = 2;

      if (active_thread || new_protocol_running()) {
        buffer[2] = 3;
      }

      buffer[9] = 31; // software version
      buffer[10] = OLDDEVICE;

      if (OLDDEVICE == ODEV_HERMES_LITE2) {
        // use HL1 device ID and new software version
        buffer[9] = 73;
        buffer[10] = ODEV_HERMES_LITE;
        buffer[19] = 4; // number of receivers
        buffer[21] = 2;   // Version 73.2
      }

      if (sock_TCP_Client > -1) {
        // We will get into trouble if we respond via TCP while the radio is
        // running with TCP.
        // We simply suppress the response in this (very unlikely) case.
        if (!active_thread) {
          if (send(sock_TCP_Client, buffer, 60, 0) < 0) {
            t_print( "TCP send error occurred when responding to an incoming Metis detection request!\n");
          }

          // close the TCP socket which was only used for the detection
          close(sock_TCP_Client);
          sock_TCP_Client = -1;
        }
      } else {
        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
      }

      break;

    // stop the SDR to PC transmission via handler_ep6
    case 0x0004feef:

      // processing an invalid packet is too dangerous -- skip it!
      if (bytes_read != 64) {
        t_print("InvalidLength: RvcMsg Code=0x%08x Len=%d\n", code, bytes_read);
        break;
      }

      t_print( "STOP the transmission via handler_ep6 / code: 0x%08x\n", code);
      enable_thread = 0;

      while (active_thread) { usleep(1000); }

      txptr = -1;

      if (sock_TCP_Client > -1) {
        close(sock_TCP_Client);
        sock_TCP_Client = -1;
      }

      break;

    case 0x0104feef:
    case 0x0204feef:
    case 0x0304feef:

      // processing an invalid packet is too dangerous -- skip it!
      if (bytes_read != 64) {
        t_print("InvalidLength: RvcMsg Code=0x%08x Len=%d\n", code, bytes_read);
        break;
      }

      if (new_protocol_running()) {
        t_print("OldProtocol START command received but NewProtocol radio already running!\n");
        break;
      }

      t_print( "START the PC-to-SDR handler thread / code: 0x%08x\n", code);
      enable_thread = 0;

      while (active_thread) { usleep(1000); }

      memset(&addr_old, 0, sizeof(addr_old));
      addr_old.sin_family = AF_INET;
      addr_old.sin_addr.s_addr = addr_from.sin_addr.s_addr;
      addr_old.sin_port = addr_from.sin_port;
      memset(isample, 0, OLDRTXLEN * sizeof(double));
      memset(qsample, 0, OLDRTXLEN * sizeof(double));
      enable_thread = 1;
      active_thread = 1;

      if (pthread_create(&thread, NULL, handler_ep6, NULL) < 0) {
        t_perror("create old protocol thread");
        restore_terminal_attributes();
        return EXIT_FAILURE;
      }

      pthread_detach(thread);
      break;

    default:

      /*
       * Here we have to handle the following "non standard" cases:
       * OldProtocol "program"   packet  264 bytes starting with EF FE 03 01
       * OldProtocol "erase"     packet   64 bytes starting with EF FE 03 02
       * OldProtocol "Set IP"    packet   63 bytes starting with EF FE 03
       * NewProtocol "Discovery" packet   60 bytes starting with 00 00 00 00 02
       * NewProtocol "program"   packet  265 bytes starting with xx xx xx xx 05  (XXXXXXXX = Seq. Number)
       * NewProtocol "erase"     packet   60 bytes starting with 00 00 00 00 04
       * NewProtocol "Set IP"    packet   60 bytes starting with 00 00 00 00 03
       * NewProtocol "General"   packet   60 bytes starting with xx xx xx xx 00 (XXXXXXXX = Seq. Number)
       *                                  ==> this starts NewProtocol radio
       */
      if (bytes_read == 264 && buffer[0] == 0xEF && buffer[1] == 0xFE && buffer[2] == 0x03 && buffer[3] == 0x01) {
        static unsigned long cnt = 0;
        unsigned long blks = (buffer[4] << 24) + (buffer[5] << 16) + (buffer[6] << 8) + buffer[7];
        t_print("OldProtocol Program blks=%lu count=%ld\r", blks, ++cnt);
        usleep(1000);
        memset(buffer, 0, 60);
        buffer[0] = 0xEF;
        buffer[1] = 0xFE;
        buffer[2] = 0x04;
        buffer[3] = MAC1;
        buffer[4] = MAC2;
        buffer[5] = MAC3;
        buffer[6] = MAC4;
        buffer[7] = MAC5; // specifies type of radio
        buffer[8] = MAC6; // encodes old protocol
        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));

        if (blks == cnt) { t_print("\n\n Programming Done!\n"); }

        break;
      }

      if (bytes_read == 64 && buffer[0] == 0xEF && buffer[1] == 0xFE && buffer[2] == 0x03 && buffer[3] == 0x02) {
        t_print("OldProtocol Erase packet received:\n");
        sleep(1);
        memset(buffer, 0, 60);
        buffer[0] = 0xEF;
        buffer[1] = 0xFE;
        buffer[2] = 0x03;
        buffer[3] = MAC1;
        buffer[4] = MAC2;
        buffer[5] = MAC3;
        buffer[6] = MAC4;
        buffer[7] = MAC5; // specifies type of radio
        buffer[8] = MAC6; // encodes old protocol
        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        break;
      }

      if (bytes_read == 63 && buffer[0] == 0xEF && buffer[1] == 0xFE && buffer[2] == 0x03) {
        t_print("OldProtocol SetIP packet received:\n");
        t_print("MAC address is %02x:%02x:%02x:%02x:%02x:%02x\n", buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
                buffer[8]);
        t_print("IP  address is %03d:%03d:%03d:%03d\n", buffer[9], buffer[10], buffer[11], buffer[12]);
        buffer[2] = 0x02;
        memset(buffer + 9, 0, 54);
        sendto(sock_udp, buffer, 63, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        break;
      }

      //
      // P2 discovery packet: 60 bytes starting with 00 00 00 00 02
      //
      if (bytes_read == 60 && code == 0 && buffer[4] == 0x02) {
        if (oldnew == 1) {
          t_print("NewProtocol discovery packet from %s IGNORED.\n", inet_ntoa(addr_from.sin_addr));
          break;
        }

        t_print("NewProtocol discovery packet received from %s\n", inet_ntoa(addr_from.sin_addr));
        // prepeare response
        memset(buffer, 0, 60);
        buffer [4] = 0x02 + new_protocol_running();
        buffer [5] = MAC1;
        buffer[ 6] = MAC2;
        buffer[ 7] = MAC3;
        buffer[ 8] = MAC4;
        buffer[ 9] = MAC5; // specifies type of radio
        buffer[10] = MAC6N; // encodes new protocol
        buffer[11] = NEWDEVICE;
        buffer[12] = 38;
        buffer[13] = 19;
        buffer[20] = 2;
        buffer[21] = 1;
        buffer[22] = 3;

        // HERMES_LITE2 is a HermesLite with a new software version
        if (NEWDEVICE == NDEV_HERMES_LITE2) {
          buffer[11] = NDEV_HERMES_LITE;
        }

        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        break;
      }

      if (bytes_read == 60 && code == 0 && buffer[4] == 0x04) {
        if (oldnew == 1) {
          t_print("NewProtocol erase packet IGNORED.\n");
          break;
        }

        t_print("NewProtocol erase packet received\n");
        memset(buffer, 0, 60);
        buffer [4] = 0x02 + active_thread;
        buffer [5] = MAC1;
        buffer[ 6] = MAC2;
        buffer[ 7] = MAC3;
        buffer[ 8] = MAC4;
        buffer[ 9] = MAC5; // specifies type of radio
        buffer[10] = MAC6N; // encodes new protocol
        buffer[11] = NEWDEVICE;
        buffer[12] = 38;
        buffer[13] = 103;
        buffer[20] = 2;
        buffer[21] = 1;
        buffer[22] = 3;
        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        sleep(5); // pretend erase takes 5 seconds
        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        break;
      }

      //
      // P2 program packet: 265 bytes starting with xx xx xx xx 05
      //
      if (bytes_read == 265 && buffer[4] == 0x05) {
        if (oldnew == 1) {
          t_print("NewProtocol program packet IGNORED.\n");
          break;
        }

        unsigned long seq, blk;
        seq = (buffer[0] << 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];
        blk = (buffer[5] << 24) + (buffer[6] << 16) + (buffer[7] << 8) + buffer[8];
        t_print("NewProtocol Program packet received: seq=%lu blk=%lu\r", seq, blk);

        if (seq == 0) { checksum = 0; }

        for (j = 9; j <= 264; j++) { checksum += buffer[j]; }

        memset(buffer + 4, 0, 56); // keep seq. no
        buffer[ 4] = 0x04;
        buffer [5] = MAC1;
        buffer[ 6] = MAC2;
        buffer[ 7] = MAC3;
        buffer[ 8] = MAC4;
        buffer[ 9] = MAC5; // specifies type of radio
        buffer[10] = MAC6N; // encodes new protocol
        buffer[11] = 103;
        buffer[12] = NEWDEVICE;
        buffer[13] = (checksum >> 8) & 0xFF;
        buffer[14] = (checksum     ) & 0xFF;
        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));

        if (seq + 1 == blk) { t_print("\n\nProgramming Done!\n"); }

        break;
      }

      //
      // P2 SetIP packet: 60 bytes starting with 00 00 00 00 06
      //
      if (bytes_read == 60 && code == 0 && buffer[4] == 0x06) {
        if (oldnew == 1) {
          t_print("NewProtocol SetIP packet IGNORED.\n");
          break;
        }

        t_print("NewProtocol SetIP packet received for MAC %2x:%2x:%2x:%2x%2x:%2x IP=%d:%d:%d:%d\n",
                buffer[5], buffer[6], buffer[7], buffer[8], buffer[9], buffer[10],
                buffer[11], buffer[12], buffer[13], buffer[14]);

        // only respond if this is for OUR device
        if (buffer[ 5] != MAC1) { break; }

        if (buffer[ 6] != MAC2) { break; }

        if (buffer[ 7] != MAC3) { break; }

        if (buffer[ 8] != MAC4) { break; }

        if (buffer[ 9] != MAC5) { break; } // specifies type of radio

        if (buffer[10] != MAC6N) { break; } // encodes new protocol

        memset(buffer, 0, 60);
        buffer [4] = 0x02 + active_thread;
        buffer [5] = MAC1;
        buffer[ 6] = MAC2;
        buffer[ 7] = MAC3;
        buffer[ 8] = MAC4;
        buffer[ 9] = MAC5; // specifies type of radio
        buffer[10] = MAC6N; // encodes new protocol
        buffer[11] = NEWDEVICE;
        buffer[12] = 38;
        buffer[13] = 103;
        buffer[20] = 2;
        buffer[21] = 1;
        buffer[22] = 3;
        sendto(sock_udp, buffer, 60, 0, (struct sockaddr *)&addr_from, sizeof(addr_from));
        break;
      }

      //
      // P2 General packet: 60 bytes starting with xx xx xx xx 00
      //
      if (bytes_read == 60 && buffer[4] == 0x00) {
        if (oldnew == 1) {
          t_print("NewProtocol General packet IGNORED.\n");
          break;
        }

        // handle "general packet" of the new protocol
        memset(&addr_new, 0, sizeof(addr_new));
        addr_new.sin_family = AF_INET;
        addr_new.sin_addr.s_addr = addr_from.sin_addr.s_addr;
        addr_new.sin_port = addr_from.sin_port;
        new_protocol_general_packet(buffer);
        break;
      }

      //
      // Packet form not known
      //
      t_print("Invalid packet (len=%d) detected: ", bytes_read);

      if (bytes_read > 16) { bytes_read = 16; }

      for (i = 0; i < bytes_read; i++) {
        printf("%02x ", buffer[i]);
      }

      printf("\n");
      break;
    }
  }

  close(sock_udp);

  if (sock_TCP_Client > -1) {
    close(sock_TCP_Client);
  }

  if (sock_TCP_Server > -1) {
    close(sock_TCP_Server);
  }

  restore_terminal_attributes();
  return EXIT_SUCCESS;
}

#define chk_data(a,b,c) if ((a) != b) { b = (a); t_print("%20s= %08lx (%10ld)\n", c, (long) b, (long) b ); }

void process_ep2(uint8_t *frame) {
  int rc;
  int mod;

  if (!(frame[0] & 1) && ptt) {
    // TX/RX transition: reset TX fifo
    txptr = -1;
    memset (isample, 0, OLDRTXLEN * sizeof(double));
    memset (qsample, 0, OLDRTXLEN * sizeof(double));
  }

  chk_data(frame[0] & 1, ptt, "PTT");

  switch (frame[0]) {
  case 0:
  case 1:
    chk_data((frame[1] & 0x03) >> 0, rate, "SampleRate");
    chk_data((frame[1] & 0x0C) >> 2, ref10, "Ref10MHz");
    chk_data((frame[1] & 0x10) >> 4, src122, "Source122MHz");
    chk_data((frame[1] & 0x60) >> 5, PMconfig, "Penelope/Mercury config");
    chk_data((frame[1] & 0x80) >> 7, MicSrc, "MicSource");
    chk_data(frame[2] & 1, TX_class_E, "TX CLASS-E");
    chk_data((frame[2] & 0xfe) >> 1, OpenCollectorOutputs, "OpenCollector");
    chk_data(((frame[4] >> 3) & 7) + 1, receivers, "RECEIVERS");
    chk_data(((frame[4] >> 6) & 1), MicTS, "TimeStampMic");
    chk_data(((frame[4] >> 7) & 1), CommonMercuryFreq, "Common Mercury Freq");
    mod = 0;
    p1noisefac = sqrt((double)(48 << rate));
    rc = frame[3] & 0x03;

    if (rc != AlexAtt) {
      mod = 1;
      AlexAtt = rc;
    }

    rc = (frame[3] & 0x04) >> 2;

    if (rc != preamp) {
      mod = 1;
      preamp = rc;
    }

    rc = (frame[3] & 0x08) >> 3;

    if (rc != LTdither) {
      mod = 1;
      LTdither = rc;
    }

    rc = (frame[3] & 0x10) >> 4;

    if (rc != LTrandom) {
      mod = 1;
      LTrandom = rc;
    }

    if (mod) { t_print("AlexAtt=%d Preamp=%d Dither=%d Random=%d\n", AlexAtt, preamp, LTdither, LTrandom); }

    mod = 0;
    rc = (frame[3] & 0x60) >> 5;

    if (rc != alexRXant) {
      mod = 1;
      alexRXant = rc;
    }

    rc = (frame[3] & 0x80) >> 7;

    if (rc != alexRXout) {
      mod = 1;
      alexRXout = rc;
    }

    rc = (frame[4] >> 0) & 3;

    if (rc != AlexTXrel) {
      mod = 1;
      AlexTXrel = rc;
    }

    rc = (frame[4] >> 2) & 1;

    if (rc != duplex) {
      mod = 1;
      duplex = rc;
    }

    if (mod) { t_print("RXout=%d RXant=%d TXrel=%d Duplex=%d\n", alexRXout, alexRXant, AlexTXrel, duplex); }

    if (OLDDEVICE == ODEV_C25) {
      // Charly25: has two 18-dB preamps that are switched with "preamp" and "dither"
      //           and two attenuators encoded in Alex-ATT
      //           Both only applies to RX1!
      rxatt_dbl[0] = pow(10.0, -0.05 * (12 * AlexAtt - 18 * LTdither - 18 * preamp));
      rxatt_dbl[1] = 1.0;
    } else {
      // Assume that it has ALEX attenuators in addition to the Step Attenuators
      rxatt_dbl[0] = pow(10.0, -0.05 * (10 * AlexAtt + rx_att[0]));
      rxatt_dbl[1] = 1.0;
    }

    break;

  case 2:
  case 3:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), tx_freq, "TX FREQ");
    break;

  case 4:
  case 5:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), rx_freq[0], "RX FREQ1");
    break;

  case 6:
  case 7:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), rx_freq[1], "RX FREQ2");
    break;

  case 8:
  case 9:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), rx_freq[2], "RX FREQ3");
    break;

  case 10:
  case 11:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), rx_freq[3], "RX FREQ4");
    break;

  case 12:
  case 13:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), rx_freq[4], "RX FREQ5");
    break;

  case 14:
  case 15:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), rx_freq[5], "RX FREQ6");
    break;

  case 16:
  case 17:
    chk_data(frame[4] | (frame[3] << 8) | (frame[2] << 16) | (frame[1] << 24), rx_freq[6], "RX FREQ7");
    break;

  case 18:
  case 19:
    chk_data(frame[1], txdrive, "TX DRIVE");

    if (OLDDEVICE == ODEV_HERMES_LITE2) {
      chk_data((frame[2] >> 2) & 0x01, hl2_q5, "HermesLite2 Q5 switch");
      chk_data((frame[2] >> 3) & 0x01, hl2_pa, "HermesLite2 PA enable");
      chk_data((frame[2] >> 4) & 0x01, hl2_tune, "HermesLite2 Tune");
    } else {
      chk_data(frame[2] & 0x01, mic_boost, "MIC BOOST");
      chk_data((frame[2] >> 1)  & 0x01, line_in, "LINE IN");
      chk_data((frame[2] >> 2)  & 0x01, apollo_filter, "ApolloFilter");
      chk_data((frame[2] >> 3)  & 0x01, apollo_tuner, "ApolloTuner");
      chk_data((frame[2] >> 4)  & 0x01, apollo_auto_tune, "ApolloAutoTune");
      chk_data((frame[2] >> 5)  & 0x01, alex_apollo, "SelectAlexApollo");
    }

    chk_data((frame[2] >> 6) & 0x01, alex_manual, "ALEX manual HPF/LPF");
    chk_data((frame[2] >> 7) & 0x01, vna, "VNA mode");
    chk_data(frame[3] & 0x1F, alex_hpf, "ALEX HPF");
    chk_data((frame[3] >> 5) & 0x01, alex_bypass, "ALEX Bypass HPFs");
    chk_data((frame[3] >> 6) & 0x01, lna6m, "ALEX 6m LNA");
    chk_data((frame[3] >> 7) & 0x01, alexTRdisable, "ALEX T/R disable");
    chk_data(frame[4], alex_lpf, "ALEX LPF");

    // reset TX level. Leve a little head-room for noise
    if (OLDDEVICE == ODEV_HERMES_LITE2) {
      txdrv_dbl = hl2drv[txdrive / 16];
    } else {
      // reset TX level. Leve a little head-room for noise
      txdrv_dbl = (double) txdrive * 0.003921; // div. by. 255
    }

    break;

  case 20:
  case 21:
    chk_data((frame[1] & 0x01) >> 0, rx_preamp[0], "ADC1 preamp");
    chk_data((frame[1] & 0x02) >> 1, rx_preamp[1], "ADC2 preamp");
    chk_data((frame[1] & 0x04) >> 2, rx_preamp[2], "ADC3 preamp");
    chk_data((frame[1] & 0x08) >> 3, rx_preamp[3], "ADC4 preamp");
    chk_data((frame[1] & 0x10) >> 4, tip_ring, "TIP/Ring");
    chk_data((frame[1] & 0x20) >> 5, MicBias, "MicBias");
    chk_data((frame[1] & 0x40) >> 6, MicPTT, "MicPTT");
    chk_data((frame[2] & 0x1F) >> 0, LineGain, "LineGain");
    chk_data((frame[2] & 0x20) >> 5, MerTxATT0, "Mercury Att on TX/0");
    chk_data((frame[2] & 0x40) >> 6, PureSignal, "PureSignal");
    chk_data((frame[2] & 0x80) >> 7, PeneSel, "PenelopeSelect");
    chk_data((frame[3] & 0x0F) >> 0, MetisDB9, "MetisDB9");
    chk_data((frame[3] & 0x10) >> 4, MerTxATT1, "Mercury Att on TX/1");

    if (frame[4] & 0x40)   {
      // Some firmware/emulators use bit6 to indicate a 6-bit format
      // for a combined attenuator/preamplifier with the AD9866 chip.
      // The value is between 0 and 60 and formally correspondes to
      // to an RX gain of -12 to +48 dB. However, we set here that
      // a value of +14 (that is, 26 on the 0-60 scale) corresponds to
      // "zero attenuation"
      // This means that the nominal value of "RX gain calibration" is 14.
      chk_data(26 - (frame[4] & 0x3F), rx_att[0], "RX1 HL ATT/GAIN");
    } else {
      chk_data((frame[4] & 0x1F) >> 0, rx_att[0], "RX1 ATT");
      chk_data((frame[4] & 0x20) >> 5, rx1_attE, "RX1 ATT enable");
      //
      // Some hardware emulates "switching off ATT and preamp" by setting ATT
      // to 20 dB, because the preamp cannot be switched.
      // if (!rx1_attE) rx_att[0]=20;
    }

    if (OLDDEVICE != ODEV_C25) {
      // Set RX amplification factors. No switchable preamps available normally.
      rxatt_dbl[0] = pow(10.0, -0.05 * (10 * AlexAtt + rx_att[0]));
      rxatt_dbl[1] = pow(10.0, -0.05 * (rx_att[1]));
      rxatt_dbl[2] = 1.0;
      rxatt_dbl[3] = 1.0;
    }

    break;

  case 22:
  case 23:
    chk_data(frame[1] & 0x1f, rx_att[1], "RX2 ATT");
    chk_data((frame[2] >> 6) & 1, cw_reversed, "CW REV");
    chk_data(frame[3] & 63, cw_speed, "CW SPEED");
    chk_data((frame[3] >> 6) & 3, cw_mode, "CW MODE");
    chk_data(frame[4] & 127, cw_weight, "CW WEIGHT");
    chk_data((frame[4] >> 7) & 1, cw_spacing, "CW SPACING");
    // Set RX amplification factors.
    rxatt_dbl[1] = pow(10.0, -0.05 * (rx_att[1]));
    break;

  case 24:
  case 25:
    chk_data((frame[2] << 8) | frame[1], c25_ext_board_i2c_data, "C25 EXT BOARD DATA");
    break;

  case 28:
  case 29:
    if (OLDDEVICE == ODEV_C25) {
      // RedPitaya: Hard-wired ADC settings.
      rx_adc[0] = 0;
      rx_adc[1] = 1;
      rx_adc[2] = 1;
    } else {
      chk_data((frame[1] & 0x03) >> 0, rx_adc[0], "RX1 ADC");
      chk_data((frame[1] & 0x0C) >> 2, rx_adc[1], "RX2 ADC");
      chk_data((frame[1] & 0x30) >> 4, rx_adc[2], "RX3 ADC");
    }

    chk_data((frame[1] & 0xC0) >> 6, rx_adc[3], "RX4 ADC");
    chk_data((frame[2] & 0x03) >> 0, rx_adc[4], "RX5 ADC");
    chk_data((frame[2] & 0x0C) >> 2, rx_adc[5], "RX6 ADC");
    chk_data((frame[2] & 0x30) >> 4, rx_adc[6], "RX7 ADC");

    //
    // The HL2 enables/disables TXATT with bit7, and with bit6 it is
    // indicated that a "full-range" value is used, where values
    // from 0 to 60 correspond to TXATT values from 31 ... -29
    //
    if (frame[3] & 0x40) {
      chk_data((frame[3] & 0x3f), txatt, "HL2 TX ATT");
      txatt_dbl = pow(10.0, -0.05 * (double) (31 - txatt));
    } else {
      chk_data((frame[3] & 0x1f), txatt, "TX ATT");
      txatt_dbl = pow(10.0, -0.05 * (double) txatt);
    }

    break;

  case 30:
  case 31:
    chk_data(frame[1] & 1, cw_internal, "CW INT");
    chk_data(frame[2], sidetone_volume, "SIDE TONE VOLUME");
    chk_data(frame[3], cw_delay, "CW DELAY");
    cw_delay = frame[3];
    break;

  case 32:
  case 33:
    chk_data((frame[1] << 2) | (frame[2] & 3), cw_hang, "CW HANG");
    chk_data((frame[3] << 4) | (frame[4] & 255), freq, "SIDE TONE FREQ");
    break;

  case 34:
  case 35:
    chk_data(frame[1] << 2 | (frame[2] & 3), pwmmin, "PWM MIN");
    chk_data(frame[3] << 2 | (frame[4] & 3), pwmmax, "PWM MAX");
    break;

  case 36:
  case 37:
    chk_data(frame[1] & 0x7f, adc2bpf, "ADC2 BPF settings");
    chk_data((frame[1] >> 7) & 0x01, rx2gnd, "Ground-RX2-Input");
    chk_data(frame[2] & 0x02, anan7kxvtr, "Anan7k/8k XVTR enable");
    chk_data(frame[2] & 0x40, anan7kps,  "Anan7k PureSignal flag");
    chk_data(frame[3] << 8 | frame[4], envgain, "Firmware EnvGain");
    break;

  case 46:
  case 47:
    chk_data(frame[3] & 0x1f, hl2_ptt_hang, "HL2 PTT HANG");
    chk_data(frame[4] & 0x7f, hl2_tx_latency, "HL2 TX LATENCY");
    break;

  default:
    //
    // The HermesLite2 has an extended address range so we just
    // report if anything has changed. So if one address has not
    // been handled before explicitly, its changes will be reported
    // here in a generic form.
    //
    rc = frame[0] >> 1;

    if (hl2addr[rc].c1 != frame[1] || hl2addr[rc].c2 != frame[2] ||
        hl2addr[rc].c3 != frame[3] || hl2addr[rc].c4 != frame[4]) {
      t_print("        HL2 ADDR=0x%2x C0=0x%02x C1=0x%02x C2=0x%02x C3=0x%02x C4=0x%02x\n",
              rc, frame[0], frame[1], frame[2], frame[3], frame[4]);
      hl2addr[rc].c1 = frame[1];
      hl2addr[rc].c2 = frame[2];
      hl2addr[rc].c3 = frame[3];
      hl2addr[rc].c4 = frame[4];
    }
  }
}

void *handler_ep6(void *arg) {
  int i, j, k, n, size;
  int header_offset;
  uint32_t counter;
  uint8_t buffer[1032];
  uint8_t *pointer;
  uint8_t id[4] = { 0xef, 0xfe, 1, 6 };
  uint8_t header[40] = {
    //                             C0  C1  C2  C3  C4
    127, 127, 127,  0,  0, 33, 18, 21,
    127, 127, 127,  8,  0,  0,  0,  0,
    127, 127, 127, 16,  0,  0,  0,  0,
    127, 127, 127, 24,  0,  0,  0,  0,
    127, 127, 127, 32, 66, 66, 66, 66
  };
  int32_t adc1isample, adc1qsample;
  int32_t adc2isample, adc2qsample;
  int32_t dacisample, dacqsample;
  int32_t myisample, myqsample;
  struct timespec delay;
  long wait;
  int noiseIQpt, divpt, rxptr;
  double i1, q1, fac1, fac1a, fac2, fac3, fac4;
  unsigned int seed;
  int decimation;
  seed = ((uintptr_t) &seed) & 0xffffff;
  memcpy(buffer, id, 4);
  header_offset = 0;
  counter = 0;
  noiseIQpt = 0;
  divpt = 0;
  rxptr = OLDRTXLEN / 2 - 4096;
  clock_gettime(CLOCK_MONOTONIC, &delay);

  while (1) {
    if (!enable_thread) { break; }

    if (receivers > 0) {
      size = receivers * 6 + 2;
      n = 504 / size;  // number of samples per 512-byte-block
      // Time (in nanosecs) to "collect" the samples sent in one sendmsg
      wait = (2 * n * 1000000L) / (48 << rate);

      if (speed == 1) {
        wait = (wait * 99) / 100;
      } else if (speed == -1) {
        wait = (wait * 101) / 100;
      }
    } else {
      // this happens until the first packets are received from the radio.
      // however, we must keep things flowing.
      n = 0;
      wait = 1000000L;
    }

    // plug in sequence numbers
    buffer[4] = (counter >> 24) & 0xFF;
    buffer[5] = (counter >> 16) & 0xFF;
    buffer[6] = (counter >>  8) & 0xFF;
    buffer[7] = (counter      ) & 0xFF;
    ++counter;
    //
    //              This defines the distortion as well as the amplification
    //              Use PA settings such that there is full drive at full
    //              power (39 dB)
    //
    decimation = 32 >> rate;

    for (i = 0; i < 2; ++i) {
      static uint8_t old_radio_ptt = 0;
      static uint8_t old_radio_dash = 0;
      static uint8_t old_radio_dot = 0;
      static uint8_t old_radio_io1 = 0;
      static uint8_t old_radio_io2 = 0;
      static uint8_t old_radio_io3 = 0;
      static uint8_t old_radio_io4 = 0;
      pointer = buffer + i * 516 - i % 2 * 4 + 8;
      memcpy(pointer, header + header_offset, 8);
      // C0, C1, C2, C3, C4 are *(pointer+3) ... *(pointer+7)
      uint8_t C0 = header_offset;
      uint8_t C1;

      if (radio_ptt != old_radio_ptt) {
        t_print("Radio PTT=%d\n", radio_ptt);
        old_radio_ptt = radio_ptt;
      }

      if (radio_dash != old_radio_dash) {
        t_print("Radio DASH=%d\n", radio_dash);
        old_radio_dash = radio_dash;
      }

      if (radio_dot != old_radio_dot) {
        t_print("Radio DOT=%d\n", radio_dot);
        old_radio_dot = radio_dot;
      }

      if (radio_ptt)  { C0 |= 1; }

      if (radio_dash) { C0 |= 2; }

      if (radio_dot ) { C0 |= 4; }

      *(pointer + 3) = C0;

      switch (header_offset) {
      case 0:
        if (radio_io1 != old_radio_io1) {
          t_print("Radio IO1=%d\n", radio_io1);
          old_radio_io1 = radio_io1;
        }

        if (radio_io2 != old_radio_io2) {
          t_print("Radio IO2=%d\n", radio_io2);
          old_radio_io2 = radio_io2;
        }

        if (radio_io3 != old_radio_io3) {
          t_print("Radio IO3=%d\n", radio_io3);
          old_radio_io3 = radio_io3;
        }

        if (radio_io4 != old_radio_io4) {
          t_print("Radio IO4=%d\n", radio_io4);
          old_radio_io4 = radio_io4;
        }

        C1 = 0;

        if (radio_io1) { C1 |=  2; }

        if (radio_io2) { C1 |=  4; }

        if (radio_io3) { C1 |=  8; }

        if (radio_io4) { C1 |= 16; }

        *(pointer + 4) = C1;

        if (OLDDEVICE == ODEV_HERMES_LITE2) {
          *(pointer + 4) = 0;
          // C2/C3 is TX FIFO count
          *(pointer + 5) = 0;
          *(pointer + 6) = 0;
        }

        header_offset = 8;
        break;

      case 8:
        if (OLDDEVICE == ODEV_HERMES_LITE2) {
          // HL2: temperature
          *(pointer + 4) =  4;
          *(pointer + 5) =  0; // value = 1024, 31.5 degrees
        } else {
          // AIN5: Exciter power
          *(pointer + 4) = 0;       // about 500 mW
          *(pointer + 5) = txdrive;
        }

        // AIN1: Forward Power
        j = (int) ((4095.0 / c1) * sqrt(maxpwr * txlevel * c2));
        *(pointer + 6) = (j >> 8) & 0xFF;
        *(pointer + 7) = (j     ) & 0xFF;
        header_offset = 16;
        break;

      case 16:
        // AIN2: Reverse power, depends on TX drive to get a handle to vary the SWR
        j = (int) (txdrv_dbl * (4095.0 / c1) * sqrt(maxpwr * txlevel * c2));
        *(pointer + 4) = (j >> 8) & 0xFF;
        *(pointer + 5) = (j     ) & 0xFF;
        // AIN3: ADC0 (PA current for HL2, PA voltage for others)
        *(pointer + 6) =  2;
        *(pointer + 7) =  0; // value = 1024,
        header_offset = 24;
        break;

      case 24:
        // AIN4:
        *(pointer + 4) =  4;
        *(pointer + 5) =  0; // value = 1024,
        // AIN6: supply_voltage
        *(pointer + 6) = 4;
        *(pointer + 7) = 0;  // value = 1024
        header_offset = 32;
        break;

      case 32:
        // ADC overflow anhd Mercury software version
        // for up  to four Mercury cards
        header_offset = 0;
        break;
      }

      pointer += 8;
      memset(pointer, 0, 504);
      fac1 = rxatt_dbl[0] * 0.0002239;       //  -73 dBm signal
      fac1a = rxatt_dbl[0] * 0.000003162278; // -110 dBm signal
      fac2  = 0.0;                           // Amplitude of broad "man-made" noise to ADC1
      fac4  = 0.0;                           // Amplitude of broad "man-made" noise to ADC2

      if (diversity && !noiseblank) {
        fac2 = 0.0001 * rxatt_dbl[0];
        fac4 = 0.0002 * rxatt_dbl[1];        // (phase shifted 90 deg., 6 dB stronger)
      }

      if (diversity && noiseblank) {
        fac2 = 0.1;
        fac4 = 0.0;
      }

      //
      // Let rxptr start running only if the TX has begun
      //
      if (txptr < 0) {
        rxptr = OLDRTXLEN / 2 - 4096;
      }

      for (j = 0; j < n; j++) {
        // ADC1: noise + weak tone on RX, feedback sig. on TX (except STEMlab)
        if (ptt && (OLDDEVICE != ODEV_C25)) {
          i1 = isample[rxptr] * txdrv_dbl;
          q1 = qsample[rxptr] * txdrv_dbl;
          fac3 = IM3a + IM3b * (i1 * i1 + q1 * q1);
          adc1isample = (txatt_dbl * i1 * fac3 + noiseItab[noiseIQpt] * p1noisefac) * 8388607.0;
          adc1qsample = (txatt_dbl * q1 * fac3 + noiseItab[noiseIQpt] * p1noisefac) * 8388607.0;
        } else if (diversity && do_tone == 1) {
          // man made noise to ADC1 samples
          adc1isample = (noiseItab[noiseIQpt] * p1noisefac + cos(tonearg) * fac1 + divtab[divpt] * fac2) * 8388607.0;
          adc1qsample = (noiseQtab[noiseIQpt] * p1noisefac + sin(tonearg) * fac1                   ) * 8388607.0;
        } else if (do_tone == 1) {
          adc1isample = (noiseItab[noiseIQpt] * p1noisefac + cos(tonearg) * fac1) * 8388607.0;
          adc1qsample = (noiseQtab[noiseIQpt] * p1noisefac + sin(tonearg) * fac1) * 8388607.0;
        } else if (do_tone == 2) {
          adc1isample = (noiseItab[noiseIQpt] * p1noisefac + (cos(tonearg) + cos(tonearg2)) * fac1) * 8388607.0;
          adc1qsample = (noiseQtab[noiseIQpt] * p1noisefac + (sin(tonearg) + sin(tonearg2)) * fac1) * 8388607.0;
        } else if (do_tone == 3 && t3p >= 0) {
          adc1isample = (noiseItab[noiseIQpt] * p1noisefac + cos(tonearg) * fac1a) * 8388607.0;
          adc1qsample = (noiseQtab[noiseIQpt] * p1noisefac + sin(tonearg) * fac1a) * 8388607.0;
        } else {
          adc1isample = (noiseItab[noiseIQpt] * p1noisefac ) * 8388607.0;
          adc1qsample = (noiseQtab[noiseIQpt] * p1noisefac ) * 8388607.0;
        }

        // ADC2: noise RX, feedback sig. on TX (only STEMlab)
        if (ptt && (OLDDEVICE == ODEV_C25)) {
          i1 = isample[rxptr] * txdrv_dbl;
          q1 = qsample[rxptr] * txdrv_dbl;
          fac3 = IM3a + IM3b * (i1 * i1 + q1 * q1);
          adc2isample = (txatt_dbl * i1 * fac3 + noiseItab[noiseIQpt] * p1noisefac) * 8388607.0;
          adc2qsample = (txatt_dbl * q1 * fac3 + noiseItab[noiseIQpt] * p1noisefac) * 8388607.0;
        } else if (diversity) {
          // man made noise to Q channel only
          adc2isample = noiseItab[noiseIQpt] * p1noisefac               * 8388607.0;        // Noise
          adc2qsample = (noiseQtab[noiseIQpt] * p1noisefac + divtab[divpt] * fac4) * 8388607.0;
        } else {
          adc2isample = noiseItab[noiseIQpt] * p1noisefac  * 8388607.0;                     // Noise
          adc2qsample = noiseQtab[noiseIQpt] * p1noisefac  * 8388607.0;
        }

        //
        // TX signal with peak=0.407
        //
        if (OLDDEVICE == ODEV_HERMES_LITE2) {
          dacisample = isample[rxptr] * 0.230 * 8388607.0;
          dacqsample = qsample[rxptr] * 0.230 * 8388607.0;
        } else {
          dacisample = isample[rxptr] * 0.407 * 8388607.0;
          dacqsample = qsample[rxptr] * 0.407 * 8388607.0;
        }

        for (k = 0; k < receivers; k++) {
          myisample = 0;
          myqsample = 0;

          switch (rx_adc[k]) {
          case 0: // ADC1
            myisample = adc1isample;
            myqsample = adc1qsample;
            break;

          case 1: // ADC2
            myisample = adc2isample;
            myqsample = adc2qsample;
            break;

          default:
            myisample = 0;
            myqsample = 0;
            break;
          }

          if (ptt && (k == TXDAC)) {
            myisample = dacisample;
            myqsample = dacqsample;
          }

          *pointer++ = (myisample >> 16) & 0xFF;
          *pointer++ = (myisample >>  8) & 0xFF;
          *pointer++ = (myisample >>  0) & 0xFF;
          *pointer++ = (myqsample >> 16) & 0xFF;
          *pointer++ = (myqsample >>  8) & 0xFF;
          *pointer++ = (myqsample >>  0) & 0xFF;
        }

        // Microphone samples: silence
        pointer += 2;
        rxptr++;

        if (rxptr >= OLDRTXLEN) { rxptr = 0; }

        noiseIQpt++;

        if (noiseIQpt >= LENNOISE) { noiseIQpt = rand_r(&seed) / NOISEDIV; }

        t3p++;

        if (t3p >= t3l) { t3p = -t3l; }

        tonearg  += tonedelta;
        tonearg2 += tonedelta2;

        if (tonearg > 6.3) { tonearg -= 6.283185307179586476925286766559; }

        if (tonearg2 > 6.3) { tonearg2 -= 6.283185307179586476925286766559; }

        if (tonearg < -6.3) { tonearg += 6.283185307179586476925286766559; }

        if (tonearg2 < -6.3) { tonearg2  += 6.283185307179586476925286766559; }

        divpt += decimation;

        if (divpt >= LENDIV) { divpt = 0; }
      }
    }

    //
    // Wait until the time has passed for all these samples
    //
    delay.tv_nsec += wait;

    while (delay.tv_nsec >= 1000000000) {
      delay.tv_nsec -= 1000000000;
      delay.tv_sec++;
    }

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &delay, NULL);

    if (sock_TCP_Client > -1) {
      if (sendto(sock_TCP_Client, buffer, 1032, 0, (struct sockaddr *)&addr_old, sizeof(addr_old)) < 0) {
        t_print( "TCP sendmsg error occurred at sequence number: %u !\n", counter);
      }
    } else {
      sendto(sock_udp, buffer, 1032, 0, (struct sockaddr *)&addr_old, sizeof(addr_old));
    }
  }

  active_thread = 0;
  return NULL;
}

void t_print(const char *format, ...) {
  va_list(args);
  va_start(args, format);
  struct timespec ts;
  double now;
  static double starttime;
  static int first = 1;
  char line[1024];
  clock_gettime(CLOCK_MONOTONIC, &ts);
  now = ts.tv_sec + 1E-9 * ts.tv_nsec;

  if (first) {
    first = 0;
    starttime = now;
  }

  //
  // After 11 days, the time reaches 999999.999 so we simply wrap around
  //
  if (now - starttime >= 999999.995) { starttime += 1000000.0; }

  //
  // We have to use vsnt_print to handle the varargs stuff
  // g_print() seems to be thread-safe but call it only ONCE.
  //
  vsnprintf(line, sizeof(line), format, args);
  printf("%10.6f %s", now - starttime, line);
}

void t_perror(const char *string) {
  t_print("%s: %s\n", string, strerror(errno));
}
