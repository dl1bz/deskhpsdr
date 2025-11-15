/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
*   This source code has been forked and was adapted from piHPSDR by DL1YCF to deskHPSDR in October 2024
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

#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <wdsp.h>

#include "appearance.h"
#include "adc.h"
#include "dac.h"
#include "audio.h"
#include "discovered.h"
#include "filter.h"
#include "main.h"
#include "mode.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "agc.h"
#include "band.h"
#include "channel.h"
#include "property.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "store.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "actions.h"
#include "gpio.h"
#include "vfo.h"
#include "vox.h"
#include "meter.h"
#include "rx_panadapter.h"
#include "tx_panadapter.h"
#include "waterfall.h"
#include "zoompan.h"
#include "sliders.h"
#include "toolbar.h"
#include "rigctl.h"
#include "tci.h"
#include "ext.h"
#include "radio_menu.h"
#include "iambic.h"
#include "rigctl_menu.h"
#include "screen_menu.h"
#ifdef MIDI
  #include "midi.h"
  #include "alsa_midi.h"
  #include "midi_menu.h"
#endif
#include "message.h"
#ifdef SATURN
  #include "saturnmain.h"
  #include "saturnserver.h"
#endif
#include "version.h"
#include "exit_menu.h"
#include "message.h"

#if defined(__APPLE__)
  static int dock_guard_pixels = 0;  // wird zur Laufzeit bestimmt
#endif

#ifdef __linux__
  #include <webkit2/webkit2.h>
  static GHashTable *linux_dock_windows = NULL;
#endif

#include "macos_webview.h"

#define min(x,y) (x<y?x:y)
#define max(x,y) (x<y?y:x)

int MENU_HEIGHT = 30;             // always set to VFO_HEIGHT/2
int MENU_WIDTH = 65;              // nowhere changed
int VFO_HEIGHT = 60;              // taken from the current VFO bar layout
int VFO_WIDTH = 530;              // taken from the current VFO bar layout
const int MIN_METER_WIDTH = 200;  // nowhere changed
int METER_HEIGHT = 60;            // always set to  VFO_HEIGHT
int METER_WIDTH = 200;            // dynamically set in choose_vfo_layout
int ZOOMPAN_HEIGHT = 50;
int SLIDERS_HEIGHT = 100;
int TOOLBAR_HEIGHT = 30;

int rx_stack_horizontal = 0;

int suppress_popup_sliders = 0;

int controller = NO_CONTROLLER;

int rxgain_index_0 = 0;
int rxgain_index_1 = 0;

GtkWidget *fixed;
static GtkWidget *hide_b;
static GtkWidget *menu_b;
static GtkWidget *exit_b;
static GtkWidget *vfo_panel;
static GtkWidget *meter;
static GtkWidget *zoompan;
static GtkWidget *sliders;
static GtkWidget *toolbar;

// RX and TX calibration
long long frequency_calibration = 0LL;
double ppm_factor = 0.0;

int sat_mode;

int region = REGION_VFO;
int radio_sample_rate;   // alias for radio->info.soapy.sample_rate
gboolean iqswap;

DISCOVERED *radio = NULL;
gboolean radio_is_remote = FALSE;     // only used with CLIENT_SERVER

static char property_path[128];
static GMutex property_mutex;

RECEIVER *receiver[8];
RECEIVER *active_receiver;
TRANSMITTER *transmitter;

int RECEIVERS;
int PS_TX_FEEDBACK;
int PS_RX_FEEDBACK;

int atlas_penelope = 0; // 0: no TX, 1: Penelope TX, 2: PennyLane TX
int atlas_clock_source_10mhz = 0;
int atlas_clock_source_128mhz = 0;
int atlas_config = 0;
int atlas_mic_source = 0;
int atlas_janus = 0;

//
// if hl2_audio_codec is set,  audio data is included in the HPSDR
// data stream and the "dither" bit is set. This is used by a
// "compagnion board" and  a variant of the HL2 firmware
// This bit can be set in the "RADIO" menu.
//
// if hl2_cl1_input is set, CL1 is used as a master clock input
// for a 10 MHz reference clock.
//
int hl2_audio_codec = 0;
int hl2_cl1_input = 0;

//
// if anan10E is set, we have a limited-capacity HERMES board
// with 2 RX channels max, and the PureSignal TX DAC feedback
// is hard-coded to RX2, while for the PureSignal RX feedback
// one must use RX1. This is the case for Anan-10E and Anan-100B
// radios.
//
int anan10E = 0;

int adc0_filter_bypass = 0; // Bypass ADC0 filters on receive
int adc1_filter_bypass = 0; // Bypass ADC1 filters on receiver  (ANAN-7000/8000/G2)
int mute_spkr_amp = 0;      // Mute audio amplifier in radio    (ANAN-7000, G2)

int classE = 0;

int tx_out_of_band_allowed = 0;

int filter_board = ALEX;
int pa_enabled = 1;
int pa_power = PA_1W;
const int pa_power_list[] = {1, 5, 10, 30, 50, 100, 200, 500, 1000};
double pa_trim[11];

int rx200_udp_port = 5573;  // Portnummer für den RX200 UDP Listener
char g_rx200_data[4][64];
int rx200_udp_valid = 0;

int lpf_udp_port = 7355;    // Portnummer für den LPF UDP Listener
char g_lpf_data[6][64];
int lpf_udp_valid = 0;

int display_zoompan = 0;
int display_sliders = 0;
int display_extra_sliders = 1;
int display_toolbar = 0;
double percent_pan_wf = 50.0;
int display_info_bar = 0;
int display_clock = 0;
int display_solardata = 0;
char radio_bgcolor_rgb_hex[8] = "#E6E6FA";

int mic_linein = 0;        // Use microphone rather than linein in radio's audio codec
double linein_gain = 0.0;  // -34.0 ... +12.5 in steps of 1.5 dB
int mic_boost = 0;
int mic_bias_enabled = 0;
int mic_ptt_enabled = 0;
int mic_ptt_tip_bias_ring = 0;
int mic_input_xlr = 0;

struct audio_profile mic_prof = {0, {"NOMIC", "NOMIC", "NOMIC"}};
int autogain_enabled = 0;
int autogain_time_enabled = 0;
gchar own_callsign[32] = "YOUR_CALLSIGN";
int receivers;

ADC adc[2];
DAC dac[2];                            // only first entry used

int locked = 0;

int cw_keys_reversed = 0;              // 0=disabled 1=enabled
int cw_keyer_speed = 16;               // 1-60 WPM
int cw_keyer_mode = KEYER_MODE_A;      // Modes A/B and STRAIGHT
int cw_keyer_weight = 50;              // 0-100
int cw_keyer_spacing = 0;              // 0=on 1=off
int cw_keyer_internal = 1;             // 0=external 1=internal
int cw_keyer_sidetone_volume = 50;     // 0-127
int cw_keyer_ptt_delay = 30;           // 0-255ms
int cw_keyer_hang_time = 500;          // ms
int cw_keyer_sidetone_frequency = 800; // Hz
int cw_breakin = 1;                    // 0=disabled 1=enabled
int cw_ramp_width = 9;                 // default value (in ms)

int enable_auto_tune = 0;
int auto_tune_flag = 0;
int auto_tune_end = 0;

int enable_tx_inhibit = 0;
int TxInhibit = 0;

int vfo_encoder_divisor = 1;

int protocol;
int device;
int new_pa_board = 0; // Indicates Rev.24 PA board for HERMES/ANGELIA/ORION
int ozy_software_version;
int mercury_software_version[2] = {0, 0};
int penelope_software_version;

int adc0_overload = 0;
int adc1_overload = 0;
int tx_fifo_underrun = 0;
int tx_fifo_overrun = 0;
int sequence_errors = 0;
int high_swr_seen = 0;

unsigned int exciter_power = 0;
unsigned int alex_forward_power = 0;
unsigned int alex_reverse_power = 0;
unsigned int ADC1 = 0;
unsigned int ADC0 = 0;

//
// At the moment we have "late mox update", this means:
// in a RX/TX or TX/RX transition, mox is updated after
// rxtx has completed (which may take a while during
// down- and up-slew).
// Sometimes one wants to know before, that a RX/TX
// change is being initiated. So rxtx() sets the pre_mox
// variable immediatedly after it has been called to the new
// state.
// This variable is used to suppress audio samples being
// sent to the radio while shutting down the receivers,
// so it shall not be used in DUPLEX mode.
//
int pre_mox = 0;

int ptt = 0;
int mox = 0;
int tune = 0;
int memory_tune = 0;
int full_tune = 0;
int have_rx_gain = 0;
int have_rx_att = 0;
int have_alex_att = 0;
int have_preamp = 0;
int have_dither = 1;
int have_saturn_xdma = 0;
int have_radioberry1 = 0;
int have_radioberry2 = 0;
int have_radioberry3 = 0;
int rx_gain_calibration = 0;

int split = 0;

int n2adr_hpf_enable = 1;
unsigned char OCtune = 0;
int OCfull_tune_time = 2800; // ms
int OCmemory_tune_time = 550; // ms
long long tune_timeout;

int analog_meter = 0;

int eer_pwm_min = 100;
int eer_pwm_max = 800;

int tx_filter_low = 100;
int tx_filter_high = 2900;

static int pre_tune_mode;
static int pre_tune_cw_internal;

int vox_enabled = 0;
double vox_threshold = 0.001;
double vox_hang = 250.0;
int vox = 0;
int CAT_cw_is_active = 0;
int MIDI_cw_is_active = 0;
int radio_ptt = 0;
int cw_key_hit = 0;
int n_adc = 1;

int diversity_enabled = 0;
double div_cos = 1.0;      // I factor for diversity
double div_sin = 1.0;      // Q factor for diversity
double div_gain = 0.0;     // gain for diversity (in dB)
double div_phase = 0.0;    // phase for diversity (in degrees, 0 ... 360)

//
// Audio capture and replay
// (Equalizers are switched off during capture and replay)
//
int capture_state = CAP_INIT;
int capture_max = 480000;  // 10 seconds
int capture_record_pointer;
int capture_replay_pointer;
double *capture_data = NULL;

int can_transmit = 0;
int optimize_for_touchscreen = 0;

gboolean duplex = FALSE;
gboolean mute_rx_while_transmitting = TRUE;

double drive_max = 100.0;
double drive_digi_max = 100.0; // maximum drive in DIGU/DIGL

gboolean display_warnings = TRUE;
gboolean display_pacurr = TRUE;

gint window_x_pos = 0;
gint window_y_pos = 0;

int rx_height;

const int tx_dialog_width = 240;
const int tx_dialog_height = 400;

typedef struct {
  char *port;
  int baud;
} SaturnSerialPort;

static SaturnSerialPort SaturnSerialPortsList[] = {
  {"/dev/serial/by-id/g2-front-9600", B9600},
  {"/dev/serial/by-id/g2-front-115200", B115200},
  {"/dev/ttyAMA1", B9600},
  {"/dev/ttyS3", B9600},
  {"/dev/ttyS7", B115200},
  {NULL, 0}
};

#if defined (__AUTOG__)
static gboolean launch_autogain_hl2_wrapper(gpointer data) {
  launch_autogain_hl2();
  return FALSE;
}
#endif

static void radio_restore_state();

void radio_stop() {
  if (can_transmit) {
    t_print("radio_stop: TX: stop display update\n");
    transmitter->displaying = 0;
    tx_set_displaying(transmitter);
    t_print("radio_stop: TX id=%d: close\n", transmitter->id);
    tx_close(transmitter);
  }

  for (int i = 0; i < RECEIVERS; i++) {
    t_print("radio_stop: RX id=%d: stop display update\n", receiver[i]->id);
    receiver[i]->displaying = 0;
    rx_set_displaying(receiver[i]);
    t_print("radio_stop: RX id=%d: close\n", receiver[i]->id);
    rx_close(receiver[i]);
  }
}

/*
static void choose_vfo_layout() {
  //
  // a) secure that vfo_layout is a valid pointer
  // b) secure that the VFO layout width fits
  //
  int rc;
  const VFO_BAR_LAYOUT *vfl;
  rc = 1;
  vfl = vfo_layout_list;

  // make sure vfo_layout points to a valid entry in vfo_layout_list
  for (;;) {
    if (vfl->width < 0) { break; }

    if ((vfl - vfo_layout_list) == vfo_layout) { rc = 0; }

    vfl++;
  }

  if (rc) {
    vfo_layout = 0;
  }

  METER_WIDTH = MIN_METER_WIDTH;
  VFO_WIDTH = full_screen ? screen_width : display_width;
  VFO_WIDTH -= (MENU_WIDTH + METER_WIDTH);

  //
  // If chosen layout does not fit:
  // Choose the first largest layout that fits
  // with a minimum-width meter
  //
  if (vfo_layout_list[vfo_layout].width > VFO_WIDTH) {
    vfl = vfo_layout_list;

    for (;;) {
      if (vfl->width < 0) {
        vfl--;
        break;
      }

      if (vfl->width <= VFO_WIDTH) { break; }

      vfl++;
    }

    vfo_layout = vfl - vfo_layout_list;
    t_print("%s: vfo_layout changed (width=%d)\n", __FUNCTION__, vfl->width);
  }

  //
  // If chosen layout leaves at least 50 pixels unused:
  // give 50 extra pixels to the meter
  //
  if (vfo_layout_list[vfo_layout].width < VFO_WIDTH - 50) {
    VFO_WIDTH -= 50;
    METER_WIDTH += 50;
  }
}
*/

static void choose_vfo_layout() {
  g_return_if_fail(vfo_layout_list[0].width > 0);
  vfo_layout = 0;
  METER_WIDTH = MIN_METER_WIDTH;
  VFO_WIDTH = full_screen ? screen_width : display_width;
  VFO_WIDTH -= (MENU_WIDTH + METER_WIDTH);

  if (vfo_layout_list[0].width < VFO_WIDTH - 50) {
    VFO_WIDTH -= 50;
    METER_WIDTH += 50;
  }
}

static guint full_screen_timeout = 0;

static void measure_decoration_height(void) __attribute__((unused));
static void measure_decoration_height(void) {
#if defined(__APPLE__)

  if (dock_guard_pixels != 0) { return; }  // schon bestimmt

  if (!gtk_widget_get_realized(top_window)) { return; }

  GdkWindow *gw = gtk_widget_get_window(top_window);

  if (!gw) { return; }

  GtkAllocation alloc;
  gtk_widget_get_allocation(top_window, &alloc);   // Client-Innenfläche
  GdkRectangle frame;
  gdk_window_get_frame_extents(gw, &frame);        // inkl. Titelbar/Border
  int deco = frame.height - alloc.height;

  if (deco > 0 && deco < 200) {   // sanity check
    dock_guard_pixels = deco;
  }

#endif
}

static int set_full_screen(gpointer data) {
  full_screen_timeout = 0;
  int flag = GPOINTER_TO_INT(data);
#if defined(__APPLE__)

  if (flag) {
    // Einmalig Dekohöhe messen im normalen Fensterzustand
    measure_decoration_height();
    GdkScreen *gdk_screen = gdk_screen_get_default();
    GdkRectangle wa;
    gdk_screen_get_monitor_workarea(gdk_screen, this_monitor, &wa);
    // Zielhöhe = Workarea minus Deko-Höhe, damit der Client-Bereich
    // nicht in den Dock-Bereich hineinragt.
    int fs_height = wa.height;

    if (dock_guard_pixels > 0 && dock_guard_pixels < fs_height) {
      fs_height -= dock_guard_pixels;
    }

    gtk_window_move(GTK_WINDOW(top_window), wa.x, wa.y);
    gtk_window_resize(GTK_WINDOW(top_window), wa.width, fs_height);
    // Maximieren kannst du dir dann sparen, wenn du selbst resizest,
    // oder du lässt es weg, um kein weiteres WM-Magic zu triggern.
    // gtk_window_maximize(GTK_WINDOW(top_window));
    screen_height = fs_height;
    full_screen = 1;
  } else {
    gtk_window_unmaximize(GTK_WINDOW(top_window));
    gtk_window_resize(GTK_WINDOW(top_window), display_width, display_height);
    gtk_window_move(GTK_WINDOW(top_window),
                    (screen_width  - display_width)  / 2,
                    (screen_height - display_height) / 2);
    full_screen = 0;
  }

#else

  //
  // Put the top window in full-screen mode, if full_screen is set
  //
  if (flag) {
    if (use_wayland) {
      gtk_window_fullscreen(GTK_WINDOW(top_window));
    } else {
      //
      // Window-to-fullscreen-transition
      //
      gtk_window_fullscreen_on_monitor(GTK_WINDOW(top_window), screen, this_monitor);
    }
  } else {
    if (!use_wayland) {
      //
      // FullScreen to window transition. Place window in the center of the screen
      //
      gtk_window_move(GTK_WINDOW(top_window),
                      (screen_width - display_width) / 2,
                      (screen_height - display_height) / 2);
    }
  }

#endif
#if __APPLE__
  radio_reconfigure();
#endif
  return G_SOURCE_REMOVE;
}

/*
static int set_full_screen(gpointer data) {
  full_screen_timeout = 0;
  int flag = GPOINTER_TO_INT(data);

  //
  // Put the top window in full-screen mode, if full_screen is set
  //
  if (flag) {
    if (use_wayland) {
      gtk_window_fullscreen(GTK_WINDOW(top_window));
    } else {
      //
      // Window-to-fullscreen-transition
      //
      gtk_window_fullscreen_on_monitor(GTK_WINDOW(top_window), screen, this_monitor);
    }
  } else {
    if (!use_wayland) {
      //
      // FullScreen to window transition. Place window in the center of the screen
      //
      gtk_window_move(GTK_WINDOW(top_window),
                      (screen_width - display_width) / 2,
                      (screen_height - display_height) / 2);
    }
  }

  return G_SOURCE_REMOVE;
}
*/

static gboolean destroy_cb(gpointer data) {
  GtkWidget *w = GTK_WIDGET(data);

  if (GTK_IS_WIDGET(w)) {
    gtk_widget_destroy(w);
  }

  return G_SOURCE_REMOVE;
}

void destroy_widget_safe(GtkWidget **pwidget) {
  if (!pwidget) {
    return;
  }

  GtkWidget *w = *pwidget;
  *pwidget = NULL;  // sofort invalidieren

  if (!w) {
    return;
  }

  // Bin ich im GTK-/Main-Thread?
  if (g_main_context_is_owner(g_main_context_default())) {
    // Hier ist GTK-Zugriff erlaubt
    if (GTK_IS_WIDGET(w)) {
      gtk_widget_destroy(w);
    }
  } else {
    // Worker-Thread: nur schedulen, GTK nur im Callback anfassen
    g_idle_add_full(
      G_PRIORITY_HIGH_IDLE,
      destroy_cb,
      g_object_ref(w),   // Schutz bis zum Callback
      (GDestroyNotify)g_object_unref
    );
  }
}

void radio_reconfigure_screen() {
  GdkWindow *gw = gtk_widget_get_window(top_window);
  GdkWindowState ws = gdk_window_get_state(GDK_WINDOW(gw));
  int last_fullscreen = SET(ws & GDK_WINDOW_STATE_FULLSCREEN);
  int my_fullscreen = SET(full_screen);  // this will not change during this procedure

  if (last_fullscreen != my_fullscreen) {
    if (full_screen_timeout > 0) {
      g_source_remove(full_screen_timeout);
      full_screen_timeout = 0;
    }
  }

  //
  // Re-configure the deskHPSDR screen after dimensions have changed
  // Start with removing the toolbar, the slider area and the zoom/pan area
  // (these will be re-constructed in due course)
  //
  int my_width  = my_fullscreen ? screen_width  : display_width;
  int my_height = my_fullscreen ? screen_height : display_height;

  if (toolbar) {
    // gtk_container_remove(GTK_CONTAINER(fixed), toolbar);
    // toolbar = NULL;
    destroy_widget_safe(&toolbar);
  }

  if (sliders) {
    // gtk_container_remove(GTK_CONTAINER(fixed), sliders);
    // sliders = NULL;
    destroy_widget_safe(&sliders);
  }

  if (zoompan) {
    // gtk_container_remove(GTK_CONTAINER(fixed), zoompan);
    // zoompan = NULL;
    destroy_widget_safe(&zoompan);
  }

  choose_vfo_layout();
  VFO_HEIGHT = vfo_layout_list[vfo_layout].height;
  MENU_HEIGHT = VFO_HEIGHT / 2;
  METER_HEIGHT = VFO_HEIGHT;

  //
  // If there is enough space, increase the meter width
  //

  //
  // Change sizes of main window, Hide and Menu buttons, meter, and vfo
  //
  if (last_fullscreen != my_fullscreen && !my_fullscreen) {
    //
    // A full-screen to window transition
    //
    gtk_window_unfullscreen(GTK_WINDOW(top_window));
    //
    // For some reason, moving the window immediately does not work
    // on MacOS, therefore do this after waiting a second
    //
    full_screen_timeout = g_timeout_add(1000, set_full_screen, GINT_TO_POINTER(0));
  }

  if (last_fullscreen != my_fullscreen && my_fullscreen) {
    if (!use_wayland) {
      //
      // A window-to-fullscreen transition
      // here we move the window, the transition is then
      // scheduled at the end of this function
      //
      gtk_window_move(GTK_WINDOW(top_window), 0, 0);
    }
  }

  /* Unter Wayland im Fullscreen kein explizites Resize erzwingen */
  if (!(use_wayland && my_fullscreen)) {
    gtk_window_resize(GTK_WINDOW(top_window), my_width, my_height);
  }

  //
  // Move Hide and Menu buttons, meter to new position
  //
  gtk_widget_set_size_request(menu_b, MENU_WIDTH, MENU_HEIGHT * 2 / 3);
  gtk_widget_set_size_request(hide_b, MENU_WIDTH, MENU_HEIGHT * 2 / 3);
  gtk_widget_set_size_request(exit_b, MENU_WIDTH, MENU_HEIGHT * 2 / 3);
  gtk_fixed_move(GTK_FIXED(fixed), menu_b, VFO_WIDTH + METER_WIDTH, 1);
  gtk_fixed_move(GTK_FIXED(fixed), hide_b, VFO_WIDTH + METER_WIDTH, MENU_HEIGHT / 2 + 9);
  gtk_fixed_move(GTK_FIXED(fixed), exit_b, VFO_WIDTH + METER_WIDTH, MENU_HEIGHT + 16);
  gtk_widget_set_size_request(meter,  METER_WIDTH, METER_HEIGHT);
  gtk_fixed_move(GTK_FIXED(fixed), meter, VFO_WIDTH, 0);
  gtk_widget_set_size_request(vfo_panel, VFO_WIDTH, VFO_HEIGHT);

  // Adjust position of the TX panel.
  // This must even be done in duplex mode, if we switch back
  // to non-duplex in the future.
  //
  if (can_transmit) {
    transmitter->x = 0;
    transmitter->y = VFO_HEIGHT;
  }

  //
  // This re-creates all the panels and the Toolbar/Slider/Zoom area
  //
  radio_reconfigure();

  if (last_fullscreen != my_fullscreen && my_fullscreen) {
    if (use_wayland) {
      /* Wayland: sofort Fullscreen, kein Timeout */
      set_full_screen(GINT_TO_POINTER(1));
    } else {
      //
      // For some reason, going to full-screen immediately does not
      // work on MacOS, so do this after 1 second
      //
      full_screen_timeout = g_timeout_add(1000, set_full_screen, GINT_TO_POINTER(1));
    }
  }

  g_idle_add(ext_vfo_update, NULL);
}

void radio_reconfigure() {
  int i;
  int y;
  t_print("%s: receivers=%d\n", __FUNCTION__, receivers);
  int my_height = full_screen ? screen_height : display_height;
  int my_width  = full_screen ? screen_width  : display_width;
  rx_height = my_height - VFO_HEIGHT;

  //
  // Many "large" displays have many pixels, but also a higher
  // pixel density. Therefore, increase the toolbar height such
  // that those buttons have at least one finger's height on
  // a touch screen
  //
  if (my_height < 560) {
    TOOLBAR_HEIGHT = 30;
    ZOOMPAN_HEIGHT = 50;
    SLIDERS_HEIGHT = 100;
  } else if (my_height < 720) {
    TOOLBAR_HEIGHT = 40;
    ZOOMPAN_HEIGHT = 55;
    SLIDERS_HEIGHT = 110;

    if (can_transmit) {
      SLIDERS_HEIGHT += 50;
    }
  } else {
    TOOLBAR_HEIGHT = 50;
    ZOOMPAN_HEIGHT = 60;
    SLIDERS_HEIGHT = 120;

    if (can_transmit) {
      SLIDERS_HEIGHT += 50;
    }
  }

  if (display_zoompan) {
    rx_height -= ZOOMPAN_HEIGHT;
  }

  if (display_sliders) {
    rx_height -= SLIDERS_HEIGHT;
  }

  if (display_toolbar) {
    rx_height -= TOOLBAR_HEIGHT;
  }

  y = VFO_HEIGHT;

  // if there is only one receiver, both cases here do the same.
  if (rx_stack_horizontal) {
    int x = 0;

    for (i = 0; i < receivers; i++) {
      RECEIVER *rx = receiver[i];
      rx->width = my_width / receivers;
      rx_update_zoom(rx);
      rx_reconfigure(rx, rx_height);

      if (!radio_is_transmitting() || duplex) {
        gtk_fixed_move(GTK_FIXED(fixed), rx->panel, x, y);
      }

      rx->x = x;
      rx->y = y;
      x = x + my_width / receivers;
    }

    y += rx_height;
  } else {
    for (i = 0; i < receivers; i++) {
      RECEIVER *rx = receiver[i];
      rx->width = my_width;
      rx_update_zoom(rx);
      rx_reconfigure(rx, rx_height / receivers);

      if (!radio_is_transmitting() || duplex) {
        gtk_fixed_move(GTK_FIXED(fixed), rx->panel, 0, y);
      }

      rx->x = 0;
      rx->y = y;
      y += rx_height / receivers;
    }
  }

  if (display_zoompan) {
    if (zoompan == NULL) {
      zoompan = zoompan_init(my_width, ZOOMPAN_HEIGHT);
      gtk_fixed_put(GTK_FIXED(fixed), zoompan, 0, y);
    } else {
      gtk_fixed_move(GTK_FIXED(fixed), zoompan, 0, y);
    }

    gtk_widget_show_all(zoompan);
    y += ZOOMPAN_HEIGHT;
  } else {
    if (zoompan != NULL) {
      // gtk_container_remove(GTK_CONTAINER(fixed), zoompan);
      // zoompan = NULL;
      destroy_widget_safe(&zoompan);
    }
  }

  if (display_sliders) {
    if (sliders == NULL) {
      sliders = sliders_init(my_width, SLIDERS_HEIGHT);
      gtk_fixed_put(GTK_FIXED(fixed), sliders, 0, y);
    } else {
      gtk_fixed_move(GTK_FIXED(fixed), sliders, 0, y);
    }

    gtk_widget_show_all(sliders);  // ... this shows both C25 and Alex ATT/Preamp, and both Mic/Linein sliders
    att_type_changed();            // ... and this hides the „wrong“ ones.
    y += SLIDERS_HEIGHT;

    if (can_transmit && display_extra_sliders) {
      sliders_show_row(2);
    } else {
      sliders_hide_row(2);
    }
  } else {
    if (sliders != NULL) {
      // gtk_container_remove(GTK_CONTAINER(fixed), sliders);
      // sliders = NULL;
      destroy_widget_safe(&sliders);
    }
  }

  /*
  if (display_toolbar) {
    if (toolbar == NULL) {
      toolbar = toolbar_init(my_width, TOOLBAR_HEIGHT);
      gtk_fixed_put(GTK_FIXED(fixed), toolbar, 0, y);
    } else {
      gtk_fixed_move(GTK_FIXED(fixed), toolbar, 0, y);
    }

    gtk_widget_show_all(toolbar);
  } else {
    if (toolbar != NULL) {
      // gtk_container_remove(GTK_CONTAINER(fixed), toolbar);
      // toolbar = NULL;
      destroy_widget_safe(&toolbar);
    }
  }
  */

  if (display_toolbar) {
    if (toolbar == NULL) {
      // Einmalig erzeugen und in das Fixed einhängen
      toolbar = toolbar_init(my_width, TOOLBAR_HEIGHT);
      gtk_fixed_put(GTK_FIXED(fixed), toolbar, 0, y);
    } else {
      // Bestehende Toolbar nur verschieben und ggf. Größe anpassen
      gtk_widget_set_size_request(toolbar, my_width, TOOLBAR_HEIGHT);
      // Bereits existente Toolbar nur verschieben
      gtk_fixed_move(GTK_FIXED(fixed), toolbar, 0, y);
    }

    gtk_widget_show_all(toolbar);
  } else {
    if (toolbar != NULL) {
      // NICHT mehr zerstören – nur verstecken
      gtk_widget_hide(toolbar);
    }
  }

  if (can_transmit && !duplex) {
    tx_reconfigure(transmitter, my_width, my_width, rx_height);
  }
}

//
// These variables are set in hideall_cb and read
// in radio_save_state.
// If the props file is written while "Hide"-ing,
// these values are written instead of the current
// hide/show status of the Zoom/Sliders/Toolbar area.
//
static int hide_status = 0;
static int old_zoom = 0;
static int old_tool = 0;
static int old_slid = 0;

static gboolean hideall_cb  (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  //
  // radio_reconfigure must not be called during TX
  //
  if (radio_is_transmitting()) {
    if (!duplex) { return TRUE; }
  }

  if (hide_status == 0) {
    //
    // Hide everything but store old status
    //
    hide_status = 1;
    gtk_button_set_label(GTK_BUTTON(hide_b), "Show");
    old_zoom = display_zoompan;
    old_slid = display_sliders;
    old_tool = display_toolbar;
    display_toolbar = display_sliders = display_zoompan = 0;
    radio_reconfigure();
  } else {
    //
    // Re-display everything
    //
    hide_status = 0;
    gtk_button_set_label(GTK_BUTTON(hide_b), "Hide");
    gtk_widget_set_tooltip_text(hide_b, "Hide all buttons and slider");
    display_zoompan = old_zoom;
    display_sliders = old_slid;
    display_toolbar = old_tool;
    radio_reconfigure();
  }

  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean menu_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  new_menu();
  return TRUE;
}

// cppcheck-suppress constParameterCallback
static gboolean exit_cb (GtkWidget *widget, GdkEventButton *event, gpointer data) {
  stop_program();
  _exit(0);
}

gboolean radio_set_bgcolor(GtkWidget *widget, gpointer data) {
  GdkRGBA bgcolor;  // Deklaration der GdkRGBA-Struktur

  if (strlen(radio_bgcolor_rgb_hex) != 7) {
    g_strlcpy(radio_bgcolor_rgb_hex, "#E6E6FA", sizeof(radio_bgcolor_rgb_hex));
  }

  // Definiere und prüfe die Hintergrundfarbe
  if (is_valid_hex(radio_bgcolor_rgb_hex) && is_valid_rgb(radio_bgcolor_rgb_hex)) {
    gdk_rgba_parse(&bgcolor, radio_bgcolor_rgb_hex);
    gtk_widget_override_background_color(widget, GTK_STATE_FLAG_NORMAL, &bgcolor);
    gtk_widget_queue_draw(widget);
  }

  return FALSE;
}

/* Wrapper, damit wir 'clicked' nutzen können */
static void hide_clicked(GtkButton *btn, gpointer data) {
  (void)btn;
  hideall_cb(NULL, NULL, data);
}

/*
// bezogen auf Bildschirm
static void open_atu_window(void) {
  GdkDisplay *display = gdk_display_get_default();
  GdkMonitor *monitor = gdk_display_get_primary_monitor(display);

  int width  = 0;
  int height = 0;

  if (monitor) {
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);

    width  = geometry.width;
    height = geometry.height;
    // Jetzt hast du die Bildschirmauflösung
  }

  // Fenstergröße
  const int win_w = 430;
  const int win_h = 430;

  // Rechts oben platzieren
  int pos_x = (width  > win_w) ? (width  - win_w) : 0;
  int pos_y = (height > win_h) ? (height - win_h) : 0;

#ifdef __APPLE__
  macos_open_webview_window(
    "http://192.168.253.95:8801",
    "ATU Control by DL1BZ",
    pos_x, pos_y, // X/Y
    430, 430    // W/H
  );
#else
    // unter Linux/Windows ggf. ignorieren oder Log ausgeben
#endif
}
*/

#ifdef __linux__
static void linux_dock_window_destroyed_cb(GtkWidget *widget, gpointer user_data) {
  char *id = (char *)user_data;

  if (linux_dock_windows) {
    g_hash_table_remove(linux_dock_windows, id);

    if (g_hash_table_size(linux_dock_windows) == 0) {
      g_hash_table_destroy(linux_dock_windows);
      linux_dock_windows = NULL;
    }
  }

  g_free(id);
}

static void linux_open_webview_window_with_id(
  const char *id,
  const char *url,
  const char *title,
  int x,
  int y,
  int w,
  int h
) {
  if (!linux_dock_windows) {
    linux_dock_windows = g_hash_table_new_full(g_str_hash, g_str_equal,
                         g_free, NULL);
  }

  // g_setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", TRUE);
  GtkWidget *window = g_hash_table_lookup(linux_dock_windows, id);

  if (window) {
    // Optional: Position nachziehen
    // gtk_window_move(GTK_WINDOW(window), x, y);
    gtk_window_present(GTK_WINDOW(window));
    return;
  }

  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(window), w, h);
  gtk_window_set_title(GTK_WINDOW(window), title);
  WebKitWebView *web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));
  webkit_web_view_load_uri(web_view, url);
  gtk_window_move(GTK_WINDOW(window), x, y);
  // ID duplizieren, als Key im Hash und als user_data im Callback verwenden
  char *id_copy = g_strdup(id);
  g_hash_table_insert(linux_dock_windows, id_copy, window);
  g_signal_connect(window, "destroy",
                   G_CALLBACK(linux_dock_window_destroyed_cb),
                   g_strdup(id));   // eigener Duplicate für Callback
  gtk_widget_show_all(window);
}
#endif

// bezogen auf top_window
void open_atu_window(GtkWindow *top_window) {
  // 1. Monitor-Geometrie holen (primärer Monitor)
  GdkDisplay  *display = gdk_display_get_default();
  GdkMonitor  *monitor = gdk_display_get_primary_monitor(display);

  if (!monitor) {
    return; // defensiv
  }

  GdkRectangle mgeo;
  gdk_monitor_get_geometry(monitor, &mgeo);
  // mgeo.x/mgeo.y = Ursprung (oben-links) dieses Monitors
  // mgeo.width/height = Größe
  // 2. GTK-Top-Window-Position/-Größe (oben-links)
  const char *win_id    = "atu";
  const char *win_title = "ATU Control by DL1BZ";
  const char *win_url   = "http://192.168.253.95:8801";
  int win_x = 0, win_y = 0;
  int win_w = 0, win_h = 0;
  gtk_window_get_position(top_window, &win_x, &win_y);
  gtk_window_get_size(top_window, &win_w, &win_h);
  t_print("%s: win_x=%d win_y=%d win_w=%d win_h=%d\n",
          __FUNCTION__, win_x, win_y, win_w, win_h);
  // 3. WebView-Fenstergröße
  const int wv_w = 430;
  const int wv_h = 430;
  // 4. Position des GTK-Fensters relativ zum Monitor bestimmen
  int rel_x = win_x - mgeo.x; // Abstand von linker Monitor-Kante
  int rel_y = win_y - mgeo.y; // Abstand von oberer Monitor-Kante
  // Rechts neben dem GTK-Fenster (im GDK-System, oben-links)
  int right_x_rel = rel_x + win_w;
  int top_y_rel   = rel_y;
  // 5. Auf Cocoa-System (unten-links) mappen
  int screen_height = mgeo.height;
  int cocoa_x = mgeo.x + right_x_rel;
  int cocoa_y = mgeo.y + (screen_height - top_y_rel - wv_h);

  // 6. Clamping, damit wir nicht außerhalb landen
  if (cocoa_x + wv_w > mgeo.x + mgeo.width) {
    cocoa_x = mgeo.x + mgeo.width - wv_w;
  }

  if (cocoa_y < mgeo.y) {
    cocoa_y = mgeo.y;
  }

#ifdef __APPLE__
  macos_open_webview_window_with_id(
    win_id,
    win_url,
    win_title,
    cocoa_x,
    cocoa_y,
    wv_w,
    wv_h
  );
#endif
#ifdef __linux__
  int linux_x = win_x + win_w;   // direkt rechts vom GTK-Fenster
  int linux_y = win_y;           // gleiche Oberkante
  linux_open_webview_window_with_id(
    win_id,
    win_url,
    win_title,
    linux_x,
    linux_y,
    wv_w,
    wv_h
  );
#endif
}

static void radio_create_visual() {
  int y = 0;
  fixed = gtk_fixed_new();
  g_object_ref(topgrid);  // so it does not get deleted
  gtk_container_remove(GTK_CONTAINER(top_window), topgrid);
  gtk_container_add(GTK_CONTAINER(top_window), fixed);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  radio_set_bgcolor(top_window, NULL);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //t_print("radio: vfo_init\n");
  int my_height = full_screen ? screen_height : display_height;
  int my_width  = full_screen ? screen_width  : display_width;
  VFO_WIDTH = my_width - MENU_WIDTH - METER_WIDTH;
  vfo_panel = vfo_init(VFO_WIDTH, VFO_HEIGHT);
  gtk_fixed_put(GTK_FIXED(fixed), vfo_panel, 0, y);
  //t_print("radio: meter_init\n");
  meter = meter_init(METER_WIDTH, METER_HEIGHT);
  gtk_fixed_put(GTK_FIXED(fixed), meter, VFO_WIDTH, y);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  t_print("%s: hide_b MENU_WIDTH=%d MENU_HEIGHT=%d VFO_WIDTH=%d y=%d\n", __FUNCTION__, MENU_WIDTH, MENU_HEIGHT, VFO_WIDTH,
          y);
  hide_b = gtk_button_new_with_label("Hide");
  gtk_widget_set_name(hide_b, "boldlabel_vfo_sf");
  gtk_widget_set_tooltip_text(hide_b, "Hide all buttons and slider");
  gtk_widget_set_size_request (hide_b, MENU_WIDTH, MENU_HEIGHT * 2 / 3);
  // g_signal_connect(hide_b, "button-press-event", G_CALLBACK(hideall_cb), NULL);
  g_signal_connect(hide_b, "clicked", G_CALLBACK(hide_clicked), NULL);
  gtk_fixed_put(GTK_FIXED(fixed), hide_b, VFO_WIDTH + METER_WIDTH, y + 1);
  y += MENU_HEIGHT - 10;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  t_print("%s: menu_b MENU_WIDTH=%d MENU_HEIGHT=%d VFO_WIDTH=%d y=%d\n", __FUNCTION__, MENU_WIDTH, MENU_HEIGHT, VFO_WIDTH,
          y);
  menu_b = gtk_button_new_with_label("Menu");
  gtk_widget_set_tooltip_text(menu_b, "Main Menu and Settings");
  gtk_widget_set_name(menu_b, "boldlabel_vfo_sf");
  gtk_widget_set_size_request (menu_b, MENU_WIDTH, MENU_HEIGHT * 2 / 3);
  g_signal_connect (menu_b, "button-press-event", G_CALLBACK(menu_cb), NULL) ;
  gtk_fixed_put(GTK_FIXED(fixed), menu_b, VFO_WIDTH + METER_WIDTH, y + 1);
  y += MENU_HEIGHT - 10;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  t_print("%s: exit_b MENU_WIDTH=%d MENU_HEIGHT=%d VFO_WIDTH=%d y=%d\n", __FUNCTION__, MENU_WIDTH, MENU_HEIGHT, VFO_WIDTH,
          y);
  exit_b = gtk_button_new_with_label("Exit");
  gtk_widget_set_tooltip_text(exit_b, "Close and Exit this App");
  gtk_widget_set_name(exit_b, "boldlabel_vfo_sf");
  gtk_widget_set_size_request (exit_b, MENU_WIDTH, MENU_HEIGHT * 2 / 3);
  g_signal_connect (exit_b, "button-press-event", G_CALLBACK(exit_cb), NULL) ;
  gtk_fixed_put(GTK_FIXED(fixed), exit_b, VFO_WIDTH + METER_WIDTH, y + 2);
  y += MENU_HEIGHT - 10;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  rx_height = my_height - VFO_HEIGHT;

  if (display_zoompan) {
    rx_height -= ZOOMPAN_HEIGHT;
  }

  if (display_sliders) {
    rx_height -= SLIDERS_HEIGHT;
  }

  if (display_toolbar) {
    rx_height -= TOOLBAR_HEIGHT;
  }

  //
  // To be on the safe side, we create ALL receiver panels here
  // If upon startup, we only should display one panel, we do the switch below
  //
  for (int i = 0; i < RECEIVERS; i++) {
    receiver[i] = rx_create_receiver(CHANNEL_RX0 + i, my_width, my_width, rx_height / RECEIVERS);
    rx_set_squelch(receiver[i]);
    receiver[i]->x = 0;
    receiver[i]->y = y;
    // Upon startup, if RIT or CTUN is active, tell WDSP.
    receiver[i]->displaying = 1;
    rx_set_displaying(receiver[i]);
    rx_set_offset(receiver[i], vfo[i].offset);
    gtk_fixed_put(GTK_FIXED(fixed), receiver[i]->panel, 0, y);
    g_object_ref((gpointer)receiver[i]->panel);
    y += rx_height / RECEIVERS;
  }

  active_receiver = receiver[0];
  //
  // This is to detect illegal accesses to the PS receivers
  //
  receiver[PS_RX_FEEDBACK] = NULL;
  receiver[PS_TX_FEEDBACK] = NULL;
  //t_print("Create transmitter\n");
  transmitter = NULL;
  can_transmit = 0;
  //
  //  do not set can_transmit before transmitter exists, because we assume
  //  if (can_transmit) is equivalent to if (transmitter)
  //
  int radio_has_transmitter = 0;

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    radio_has_transmitter = 1;
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    radio_has_transmitter = (radio->info.soapy.tx_channels != 0);
    break;
#endif
  }

  if (radio_has_transmitter) {
    if (duplex) {
      transmitter = tx_create_transmitter(CHANNEL_TX, 4 * tx_dialog_width, tx_dialog_width, tx_dialog_height);
    } else {
      transmitter = tx_create_transmitter(CHANNEL_TX, my_width, my_width, rx_height);
    }

    can_transmit = 1;
    transmitter->x = 0;
    transmitter->y = VFO_HEIGHT;
    radio_calc_drive_level();

    if (protocol == NEW_PROTOCOL || protocol == ORIGINAL_PROTOCOL) {
      tx_ps_set_sample_rate(transmitter, protocol == NEW_PROTOCOL ? 192000 : active_receiver->sample_rate);
      receiver[PS_TX_FEEDBACK] = rx_create_pure_signal_receiver(PS_TX_FEEDBACK,
                                 protocol == ORIGINAL_PROTOCOL ? active_receiver->sample_rate : 192000, my_width, transmitter->fps);
      receiver[PS_RX_FEEDBACK] = rx_create_pure_signal_receiver(PS_RX_FEEDBACK,
                                 protocol == ORIGINAL_PROTOCOL ? active_receiver->sample_rate : 192000, my_width, transmitter->fps);

      //
      // If the pk value is slightly too large, this does no harm, but
      // if it is slightly too small, very strange things can happen.
      // Therefore it is good to "measure" this value and then slightly
      // increase it.
      //
      switch (protocol) {
      case NEW_PROTOCOL:
        switch (device) {
        case NEW_DEVICE_SATURN:
          tx_ps_setpk(transmitter, 0.6121);
          break;

        default:
          // recommended "new protocol value"
          tx_ps_setpk(transmitter, 0.2899);
          break;
        }

        break;

      case ORIGINAL_PROTOCOL:
        switch (device) {
        case DEVICE_HERMES_LITE2:
          // measured value: 0.2386
          tx_ps_setpk(transmitter, 0.2400);
          break;

        case DEVICE_STEMLAB:
          // measured value: 0.4155
          tx_ps_setpk(transmitter, 0.4160);
          break;

        default:
          // recommended "old protocol" value
          tx_ps_setpk(transmitter, 0.4067);
          break;
        }

        break;

      default:
        // NOTREACHED
        tx_ps_setpk(transmitter, 1.0000);
        break;
      }
    }
  }

  // init local keyer if enabled
  if (cw_keyer_internal == 0) {
    t_print("Initialize keyer.....\n");
    keyer_update();
  }

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    old_protocol_init(receiver[0]->sample_rate);
    break;

  case NEW_PROTOCOL:
    new_protocol_init();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_init(FALSE);
    break;
#endif
  }

  if (display_zoompan) {
    zoompan = zoompan_init(my_width, ZOOMPAN_HEIGHT);
    gtk_fixed_put(GTK_FIXED(fixed), zoompan, 0, y);
    y += ZOOMPAN_HEIGHT;
  }

  if (display_sliders) {
    //t_print("create sliders\n");
    sliders = sliders_init(my_width, SLIDERS_HEIGHT);
    gtk_fixed_put(GTK_FIXED(fixed), sliders, 0, y);
    y += SLIDERS_HEIGHT;

    if (can_transmit && display_extra_sliders) {
      sliders_show_row(2);
    } else {
      sliders_hide_row(2);
    }
  }

  if (display_toolbar) {
    toolbar = toolbar_init(my_width, TOOLBAR_HEIGHT);
    gtk_fixed_put(GTK_FIXED(fixed), toolbar, 0, y);
  }

  //
  // Now, if there should only one receiver be displayed
  // at startup, do the change. We must momentarily fake
  // the number of receivers otherwise radio_change_receivers
  // will do nothing.
  //
  t_print("radio_create_visual: receivers=%d RECEIVERS=%d\n", receivers, RECEIVERS);

  if (receivers != RECEIVERS) {
    int r = receivers;
    receivers = RECEIVERS;
    t_print("radio_create_visual: calling radio_change_receivers: receivers=%d r=%d\n", receivers, r);
    radio_change_receivers(r);
  }

  gtk_widget_show_all (top_window);  // ... this shows both the HPSDR and C25 preamp/att sliders
  att_type_changed();                // ... and this hides the „wrong“ ones.
}

int index_rf_gain() {
  int rxgain_index = 0;
#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE && radio->info.soapy.rx_gains > 0 && strcmp(radio->name, "sdrplay") == 0) {
    for (int gain_index = 0; gain_index < (int)radio->info.soapy.rx_gains; gain_index++) {
      if (strcmp(radio->info.soapy.rx_gain[gain_index], "RFGR") == 0) {
        rxgain_index = gain_index;
      }
    }
  }

#endif
  return rxgain_index;
  t_print("%s: index = %d\n", __FUNCTION__, rxgain_index);
}

int index_if_gain() {
  int ifgain_index = -1;
#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE && radio->info.soapy.rx_gains > 0 && strcmp(radio->name, "sdrplay") == 0) {
    for (int gain_index = 0; gain_index < (int)radio->info.soapy.rx_gains; gain_index++) {
      if (strcmp(radio->info.soapy.rx_gain[gain_index], "IFGR") == 0) {
        ifgain_index = gain_index;
      }
    }
  }

#endif
  return ifgain_index;
  t_print("%s: index = %d\n", __FUNCTION__, ifgain_index);
}

static void on_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
  // GtkWindow *parent = gtk_window_get_transient_for(GTK_WINDOW(dialog));
  gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void show_message(GtkWindow *parent, const char *text) __attribute__((unused));
static void show_message(GtkWindow *parent, const char *text) {
  GtkWidget *dialog = gtk_message_dialog_new(parent,
                      GTK_DIALOG_MODAL,
                      GTK_MESSAGE_INFO,
                      GTK_BUTTONS_OK,
                      NULL);
  gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), text);
  gtk_window_set_title(GTK_WINDOW(dialog), "deskHPSDR Error Message");
  gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);
  g_signal_connect(dialog, "response", G_CALLBACK(on_response), NULL);
  gtk_widget_show(dialog);
}

void radio_start_radio() {
  //
  // Debug code. Placed here at the start of the program. deskHPSDR  implicitly assumes
  //             that the entires in the action table (actions.c) are sorted by their
  //             action enum values (actions.h).
  //             This will produce no output if the ActionTable is sorted correctly.
  //             If the warning appears, correct the order of actions in actions.h
  //             and re-compile.
  //
  for (enum ACTION i = 0; i < ACTIONS; i++) {
    if (i != ActionTable[i].action) {
      t_print("WARNING: action table messed up\n");
      t_print("WARNING: Position %d Action=%d str=%s\n", i, ActionTable[i].action, ActionTable[i].button_str);
    }
  }

  gdk_window_set_cursor(gtk_widget_get_window(top_window), gdk_cursor_new(GDK_WATCH));
  //
  // The behaviour of pop-up menus (Combo-Boxes) can be set to
  // "mouse friendly" (standard case) and "touchscreen friendly"
  // menu pops up upon press, and stays upon release, and the selection can
  // be made with a second press).
  //
  // Here we set it to "touch-screen friendly" by default, since it does
  // not harm MUCH if it set to touch-screen for a mouse, but it can be
  // it VERY DIFFICULT if "mouse friendly" settings are encountered with
  // a touch screen.
  //
  // The setting can be changed in the RADIO menu and is stored in the
  // props file, so will be restored therefrom as well.
  //
  optimize_for_touchscreen = 0;
  protocol = radio->protocol;
  device = radio->device;

  if (device == DEVICE_HERMES_LITE2) {
    if (realpath("/dev/radioberry", NULL) != NULL) {
      //
      // This is a RadioBerry.
      //
      if (radio->software_version < 732) {
        have_radioberry1 = 1;
      } else if (radio->software_version < 750) {
        have_radioberry2 = 1;
      } else {
        have_radioberry3 = 1;
      }
    }
  }

#ifdef GPIO

  //
  // Post-pone GPIO initialization until here since
  // we must first set the RadioBerry flags
  //
  if (gpio_init() < 0) {
    t_print("GPIO failed to initialize\n");
  }

#endif

  if (device == NEW_DEVICE_SATURN && (strcmp(radio->info.network.interface_name, "XDMA") == 0)) {
    have_saturn_xdma = 1;
  }

  for (int id = 0; id <= MAX_SERIAL + 1; id++) {
    //
    // Apply some default values. The name ttyACMx is suitable for
    // USB-serial adapters on Linux
    //
    SerialPorts[id].enable = 0;
    SerialPorts[id].andromeda = 0;
    SerialPorts[id].baud = 0;
    SerialPorts[id].autoreporting = 0;
    SerialPorts[id].g2 = 0;
    SerialPorts[id].swapRtsDtr = 0;
    snprintf(SerialPorts[id].port, sizeof(SerialPorts[id].port), "/dev/ttyACM%d", id);
  }

  // On G2-Ultra systems, we need to know the serial port used for the
  // connection to the uC of the panel. This could be a uart or a
  // USB connection. We go through a list of "bona fide" device names
  // and take the first "match".

  //
  // Note any serial setting set by this mechanism now is read-only
  //
  if (have_saturn_xdma) {
    for (SaturnSerialPort *ChkSerial = SaturnSerialPortsList; ChkSerial->port != NULL; ChkSerial++) {
      char *cp = realpath(ChkSerial->port, NULL);

      if (cp != NULL) {
        SerialPorts[MAX_SERIAL - 1].enable = 1;
        SerialPorts[MAX_SERIAL - 1].andromeda = 1;
        SerialPorts[MAX_SERIAL - 1].baud = ChkSerial->baud;
        SerialPorts[MAX_SERIAL - 1].autoreporting = 0;
        SerialPorts[MAX_SERIAL - 1].g2 = 1;
        snprintf(SerialPorts[MAX_SERIAL - 1].port, sizeof(SerialPorts[MAX_SERIAL - 1].port), "%s", cp);
        t_print("Serial port %s used for G2 panel with %d baud\n", cp, ChkSerial->baud);
        break;
      } else {
        t_print("Serial port %s not found.\n", ChkSerial->port);
      }
    }
  }

  if (device == DEVICE_METIS || device == DEVICE_OZY || device == NEW_DEVICE_ATLAS) {
    //
    // by default, assume there is a penelope board (no PennyLane)
    // when using an ATLAS bus system, to avoid TX overdrive due to
    // missing IQ scaling. Furthermore, deskHPSDR assumes the presence
    // of a Mercury board, so use that as the default clock source
    // (until changed in the RADIO menu)
    //
    atlas_penelope = 1;                 // TX present, do IQ scaling
    atlas_clock_source_10mhz = 2;       // default: Mercury
    atlas_clock_source_128mhz = 1;      // default: Mercury
    atlas_mic_source = 1;               // default: Mic source = Penelope
  }

  // set the default power output and max drive value
  drive_max = 100.0;

  switch (device) {
  case DEVICE_METIS:
  case DEVICE_OZY:
  case NEW_DEVICE_ATLAS:
    pa_power = PA_1W;
    break;

  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_HERMES_LITE2:
    pa_power = PA_5W;
    break;

  case DEVICE_STEMLAB:
    pa_power = PA_10W;
    break;

  case DEVICE_HERMES:
  case DEVICE_GRIFFIN:
  case DEVICE_ANGELIA:
  case DEVICE_ORION:
  case DEVICE_STEMLAB_Z20:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_HERMES2:
  case NEW_DEVICE_ANGELIA:
  case NEW_DEVICE_ORION:
  case NEW_DEVICE_SATURN:  // make 100W the default for G2
    pa_power = PA_100W;
    break;

  case DEVICE_ORION2:
  case NEW_DEVICE_ORION2:
    pa_power = PA_200W; // So ANAN-8000 is the default, not ANAN-7000
    break;

  case SOAPYSDR_USB_DEVICE:
    if (strcmp(radio->name, "lime") == 0) {
      drive_max = 64.0;
    } else if (strcmp(radio->name, "plutosdr") == 0) {
      drive_max = 89.0;
    }

    pa_power = PA_1W;
    break;

  default:
    pa_power = PA_1W;
    break;
  }

  drive_digi_max = drive_max; // To be updated when reading props file

  for (int i = 0; i < 11; i++) {
    pa_trim[i] = i * pa_power_list[pa_power] * 0.1;
  }

  //
  // Set various capabilities, depending in the radio model
  //
  switch (device) {
  case DEVICE_METIS:
  case DEVICE_OZY:
  case NEW_DEVICE_ATLAS:
    have_rx_att = 1; // Sure?
    have_alex_att = 1;
    have_preamp = 1;
    break;

  case DEVICE_HERMES:
  case DEVICE_GRIFFIN:
  case DEVICE_ANGELIA:
  case DEVICE_ORION:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_HERMES2:
  case NEW_DEVICE_ANGELIA:
  case NEW_DEVICE_ORION:
    have_rx_att = 1;
    have_alex_att = 1;
    break;

  case DEVICE_ORION2:
  case NEW_DEVICE_ORION2:
  case NEW_DEVICE_SATURN:
    // ANAN7000/8000/G2 boards have no ALEX attenuator
    have_rx_att = 1;
    break;

  case DEVICE_HERMES_LITE:
  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_HERMES_LITE:
  case NEW_DEVICE_HERMES_LITE2:
    //
    // Note: HL2 does not have Dither and Random.
    //       BUT: the Dither bit is hi-jacked without documentation (!)
    //       for a "band voltage" output, see:
    //       https://github.com/softerhardware/Hermes-Lite2/wiki/Band-Volts
    //
    have_dither = 1;
    have_rx_gain = 1;
    rx_gain_calibration = 14;
    break;

  case SOAPYSDR_USB_DEVICE:
    have_dither = 0;
    have_rx_gain = 1;
    rx_gain_calibration = 10;
    break;

  case DEVICE_STEMLAB:
    have_dither = 0;
    break;

  default:
    //
    // DEFAULT: we have a step attenuator nothing else
    //
    have_dither = 0;
    have_rx_att = 1;
    break;
  }

  //
  // The GUI expects that we either have a gain or an attenuation slider,
  // but not both.
  //
  if (have_rx_gain) {
    have_rx_att = 0;
  }

  char p[32];
  char version[32];
  char ip[32];
  char iface[64];
  char text[2048];

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    g_strlcpy(p, "Protocol 1", 32);
    snprintf(version, 32, "v%d.%d",
             radio->software_version / 10,
             radio->software_version % 10);
    snprintf(ip, 32, "%s", inet_ntoa(radio->info.network.address.sin_addr));
    snprintf(iface, 64, "%s", radio->info.network.interface_name);
    break;

  case NEW_PROTOCOL:
    g_strlcpy(p, "Protocol 2", 32);
    snprintf(version, 32, "v%d.%d",
             radio->software_version / 10,
             radio->software_version % 10);
    snprintf(ip, 32, "%s", inet_ntoa(radio->info.network.address.sin_addr));
    snprintf(iface, 64, "%s", radio->info.network.interface_name);
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    g_strlcpy(p, "SoapySDR", 32);
    snprintf(version, 32, "%4.20s v%d.%d.%d",
             radio->info.soapy.driver_key,
             (radio->software_version % 10000) / 100,
             (radio->software_version % 100) / 10,
             radio->software_version % 10);
    break;
#endif
  }

  //
  // "Starting" message in status text
  // Note for OZY devices, the name is "Ozy USB"
  //
  snprintf(text, 1024, "Starting %s (%s %s)",
           radio->name,
           p,
           version);
  status_text(text);

  //
  // text for top bar of deskHPSDR Window
  //
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    if (have_saturn_xdma) {
      // radio has no ip and MAC
      snprintf(text, 1024, "%s by DL1BZ %s[%s] WDSP Version %d.%02d SDR Device: %s (%s v%d) on %s",
               PGNAME,
               build_version,
               unameData.machine,
               GetWDSPVersion() / 100,
               GetWDSPVersion() % 100,
               radio->name,
               p,
               radio->software_version,
               iface);
    } else if (device == DEVICE_OZY) {
      // radio has no ip, and name is "Ozy USB"
      snprintf(text, 1024, "%s by DL1BZ %s[%s] WDSP Version %d.%02d SDR Device: %s (%s %s)",
               PGNAME,
               build_version,
               unameData.machine,
               GetWDSPVersion() / 100,
               GetWDSPVersion() % 100,
               radio->name,
               p,
               version);
    } else {
      // radio MAC address removed from the top bar otherwise
      // it does not fit  in windows 640 pixels wide.
      // if needed, the MAC address of the radio can be
      // found in the ABOUT menu.
      snprintf(text, 1024, "%s by DL1BZ %s[%s] WDSP Version %d.%02d SDR Device: %s (%s %s) %s on %s",
               PGNAME,
               build_version,
               unameData.machine,
               GetWDSPVersion() / 100,
               GetWDSPVersion() % 100,
               radio->name,
               p,
               version,
               ip,
               iface);
    }

    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    snprintf(text, 2048, "%s by DL1BZ %s[%s] WDSP Version %d.%02d SDR Device: %s (%s %s %s %s)",
             PGNAME,
             build_version,
             unameData.machine,
             GetWDSPVersion() / 100,
             GetWDSPVersion() % 100,
             radio->name,
             p,
             radio->info.soapy.driver_key,
             radio->info.soapy.hardware_key,
             radio->info.soapy.version);
    break;
#endif
  }

  gtk_window_set_title (GTK_WINDOW (top_window), text);

  //
  // determine name of the props file
  //
  switch (device) {
  case DEVICE_OZY:
    snprintf(property_path, sizeof(property_path), "ozy.props");
    break;

  case SOAPYSDR_USB_DEVICE:
    snprintf(property_path, sizeof(property_path), "%s.props", radio->name);
    break;

  default:
    if (have_saturn_xdma) {
      snprintf(property_path, sizeof(property_path), "saturn.xdma.props");
    } else {
      snprintf(property_path, sizeof(property_path), "%02X-%02X-%02X-%02X-%02X-%02X.props",
               radio->info.network.mac_address[0],
               radio->info.network.mac_address[1],
               radio->info.network.mac_address[2],
               radio->info.network.mac_address[3],
               radio->info.network.mac_address[4],
               radio->info.network.mac_address[5]);
    }

    break;
  }

  for (unsigned int i = 0; i < strlen(property_path); i++) {
    if (property_path[i] == '/') { property_path[i] = '.'; }
  }

  //
  // Determine number of ADCs in the device
  //
  switch (device) {
  case DEVICE_METIS:
  case DEVICE_OZY:
  case DEVICE_HERMES:
  case DEVICE_HERMES_LITE:
  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_ATLAS:
  case NEW_DEVICE_HERMES:
  case NEW_DEVICE_HERMES2:
  case NEW_DEVICE_HERMES_LITE:
  case NEW_DEVICE_HERMES_LITE2:
    //
    // If there are two MERCURY cards on the ATLAS bus, this is detected
    // in old_protocol.c, But, n_adc can keep the value of 1 since the
    // ADC assignment is fixed in that case (RX1: first mercury card,
    // RX2: second mercury card).
    //
    n_adc = 1;
    break;

  case SOAPYSDR_USB_DEVICE:
    if (strcmp(radio->name, "lime") == 0) {
      n_adc = 2;
    } else {
      n_adc = 1;
    }

    break;

  default:
    n_adc = 2;
    break;
  }

  iqswap = 0;

  //
  // In most cases, ALEX is the best default choice for the filter board.
  // here we set filter_board to a different default value for some
  // "special" hardware. The choice made here will possibly overwritten
  // with data from the props file.
  //
  if (device == SOAPYSDR_USB_DEVICE) {
    iqswap = 1;
    receivers = 1;
    filter_board = NO_FILTER_BOARD;
  }

  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2)  {
    filter_board = N2ADR;
    n2adr_oc_settings(); // Apply default OC settings for N2ADR board
  }

  if (device == DEVICE_STEMLAB || device == DEVICE_STEMLAB_Z20) {
    filter_board = CHARLY25;
  }

  /* Set defaults */
  adc[0].antenna = ANTENNA_1;
  adc[0].filters = AUTOMATIC;
  adc[0].hpf = HPF_13;
  adc[0].lpf = LPF_30_20;
  adc[0].dither = FALSE;
  adc[0].random = FALSE;
  adc[0].preamp = FALSE;
  adc[0].attenuation = 0;
  adc[0].enable_step_attenuation = 0;
  adc[0].gain = rx_gain_calibration;
  adc[0].min_gain = 0.0;
  adc[0].max_gain = 100.0;
  dac[0].antenna = 1;
  dac[0].gain = 0;

  if (have_rx_gain && (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL)) {
    //
    // The "magic values" here are for the AD98656 chip that is used in radios
    // such as the HermesLite and the RadioBerry. This is a best estimate and
    // will be overwritten with data from the props file.
    //
    adc[0].min_gain = -12.0;
    adc[0].max_gain = +48.0;
  }

  adc[0].agc = FALSE;
#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE) {
    if (radio->info.soapy.rx_gains > 0) {
      rxgain_index_0 = index_rf_gain();
      adc[0].min_gain = radio->info.soapy.rx_range[rxgain_index_0].minimum;
      adc[0].max_gain = radio->info.soapy.rx_range[rxgain_index_0].maximum;;
      adc[0].gain = adc[0].min_gain;
      t_print("%s: adc[0].min_gain = %f, adc[0].max_gain = %f, adc[0].gain = %f\n", __FUNCTION__, adc[0].min_gain,
              adc[0].max_gain, adc[0].gain);
    }
  }

#endif
  adc[1].antenna = ANTENNA_1;
  adc[1].filters = AUTOMATIC;
  adc[1].hpf = HPF_9_5;
  adc[1].lpf = LPF_60_40;
  adc[1].dither = FALSE;
  adc[1].random = FALSE;
  adc[1].preamp = FALSE;
  adc[1].attenuation = 0;
  adc[1].enable_step_attenuation = 0;
  adc[1].gain = rx_gain_calibration;
  adc[1].min_gain = 0.0;
  adc[1].max_gain = 100.0;
  dac[1].antenna = 1;
  dac[1].gain = 0;

  if (have_rx_gain && (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL)) {
    adc[1].min_gain = -12.0;
    adc[1].max_gain = +48.0;
  }

  adc[1].agc = FALSE;
#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE) {
    if (radio->info.soapy.rx_gains > 0) {
      rxgain_index_0 = index_rf_gain();
      adc[1].min_gain = radio->info.soapy.rx_range[rxgain_index_0].minimum;
      adc[1].max_gain = radio->info.soapy.rx_range[rxgain_index_0].maximum;;
      adc[1].gain = adc[1].min_gain;
      t_print("%s: adc[1].min_gain = %f, adc[1].max_gain = %f, adc[1].gain = %f\n", __FUNCTION__, adc[1].min_gain,
              adc[1].max_gain, adc[1].gain);
    }

    radio_sample_rate = radio->info.soapy.sample_rate;
  }

#endif
#ifdef GPIO

  switch (controller) {
  case NO_CONTROLLER:
    display_zoompan = 1;
    display_sliders = 1;
    display_toolbar = 1;
    break;

  case CONTROLLER2_V1:
  case CONTROLLER2_V2:
  case G2_FRONTPANEL:
    display_zoompan = 1;
    display_sliders = 0;
    display_toolbar = 0;
    break;
  }

#else
  display_zoompan = 1;
  display_sliders = 1;
  display_toolbar = 1;
#endif
  t_print("%s: setup RECEIVERS protocol=%d\n", __FUNCTION__, protocol);

  switch (protocol) {
  case SOAPYSDR_PROTOCOL:
    t_print("%s: setup RECEIVERS SOAPYSDR\n", __FUNCTION__);
    RECEIVERS = 1;
    PS_TX_FEEDBACK = 1;
    PS_RX_FEEDBACK = 2;
    break;

  default:
    t_print("%s: setup RECEIVERS default\n", __FUNCTION__);
    RECEIVERS = 2;
    PS_TX_FEEDBACK = (RECEIVERS);
    PS_RX_FEEDBACK = (RECEIVERS + 1);
    break;
  }

  receivers = RECEIVERS;
  radio_restore_state();
  radio_change_region(region);
  radio_create_visual();
  radio_reconfigure_screen();
#ifdef TCI

  if (tci_enable) {
    launch_tci();
  }

#endif

  if (rigctl_tcp_enable) {
    launch_tcp_rigctl();
    rigctld_enabled = 1;

    if (use_rigctld) {
      launch_rigctld_monitor();
    }
  }

  if (SerialPorts[MAX_SERIAL].enable) {
    launch_sertune();
  }

  if (SerialPorts[MAX_SERIAL + 1].enable) {
    launch_serptt();
  }

#if defined (__AUTOG__)

  if ((device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) && autogain_enabled) {
    if (pthread_equal(pthread_self(), deskhpsdr_main_thread)) {
      launch_autogain_hl2();
    } else {
      g_idle_add((GSourceFunc)launch_autogain_hl2_wrapper, NULL);
    }
  }

#endif

  // first call to start RX200 & LPF UDP Listener if SDR can transmit
  if (can_transmit) {
    launch_rx200_monitor();
    launch_lpf_monitor();
  }

  for (int id = 0; id < MAX_SERIAL; id++) {
    //
    // If serial port is enabled but no success, clear "enable" flag
    //
    if (SerialPorts[id].enable) {
      SerialPorts[id].enable = launch_serial_rigctl(id);
    }
  }

  if (can_transmit) {
    radio_calc_drive_level();

    if (transmitter->puresignal) {
      tx_ps_onoff(transmitter, 1);
    }
  }

  schedule_high_priority();
#ifdef SOAPYSDR

  if (protocol == SOAPYSDR_PROTOCOL) {
    RECEIVER *rx = receiver[0];
    soapy_protocol_create_receiver(rx);

    if (can_transmit) {
      soapy_protocol_create_transmitter(transmitter);
      soapy_protocol_set_tx_antenna(transmitter, dac[0].antenna);
      soapy_protocol_set_tx_gain(transmitter, transmitter->drive);
      soapy_protocol_set_tx_frequency(transmitter);
      soapy_protocol_start_transmitter(transmitter);
    }

    soapy_protocol_set_rx_antenna(rx, adc[0].antenna);
    soapy_protocol_set_rx_frequency(rx, VFO_A);
    soapy_protocol_set_automatic_gain(rx, adc[0].agc);

    if (!adc[0].agc) { soapy_protocol_set_gain(rx); }

    if (vfo[0].ctun) {
      rx_set_frequency(rx, vfo[0].ctun_frequency);
    }

    soapy_protocol_start_receiver(rx);

    //t_print("radio: set rf_gain=%f\n",rx->rf_gain);
    if (strcmp(radio->name, "sdrplay") != 0) {
      soapy_protocol_set_gain(rx);
      t_print("%s: Is not SDRPlay\n", __FUNCTION__);
    } else {
      t_print("%s: Is %s\n", __FUNCTION__, radio->info.soapy.hardware_key);
    }

    if (strcmp(radio->name, "sdrplay") == 0 && radio->info.soapy.rx_gains > 1) {
      for (int gain_id = 0; gain_id < (int)radio->info.soapy.rx_gains; gain_id++) {
        if (strcmp(radio->info.soapy.rx_gain[gain_id], "RFGR") == 0) {
          soapy_protocol_set_gain_element(rx, radio->info.soapy.rx_gain[gain_id], 1.0);
        }
      }
    }

    if (radio->info.soapy.rx_has_automatic_gain) {
      adc[0].agc = TRUE;
      soapy_protocol_set_automatic_gain(rx, adc[0].agc);
    }

    if (strcmp(radio->name, "sdrplay") == 0) {
      t_print("%s: Bias-T: %d agc_setpoint = %d\n", __FUNCTION__, soapy_protocol_get_bias_t(rx),
              soapy_protocol_get_agc_setpoint(rx));

      if (rx->panadapter_autoscale_enabled) {
        rx->panadapter_autoscale_enabled = 0;
        radio_reconfigure();
        g_idle_add(ext_vfo_update, NULL);
      }
    }

    update_rf_gain_scale_soapy(index_rf_gain());
    update_slider_hwagc_btn();

    if (radio && strcmp(radio->name, "sdrplay") == 0) {
      soapy_protocol_get_settings_info(active_receiver);
      t_print("%s: has correct driver: %d\n", __FUNCTION__, soapy_protocol_check_sdrplay_mod(active_receiver));

      if (soapy_protocol_check_sdrplay_mod(active_receiver) == FALSE) {
        char msg_txt_win[2048];
        snprintf(msg_txt_win, sizeof(msg_txt_win),
                 "<span color='red' size='x-large' weight='bold'>deskHPSDR critical error:</span>"
                 "<span size='large' weight='bold'>\n\nBroken SoapySDRPlay3 plugin detected:\n"
                 "RX Gain \"CURRENT\" doesn't exist.\n"
                 "\nThis is a known issue, because the official SoapySDRPlay3 plugin code is broken.\n"
                 "</span>"
                 "<span color='red' size='large' weight='bold'>\nYour SDRplay can be used, but calculation of the RX panadapter, S-meter and waterfall is not possible.</span>"
                 "<span size='large' weight='bold'>\n\nSolution:\nRemove the current SoapySDRPlay3 plugin and replace it with the SoapySDRPlay3 plugin using branch \"get-current-gain\".\n"
                 "DO NOT USE the \"master\" branch.\n\n"
                 "Get the required source of SoapySDRPlay3 plugin from\nhttps://github.com/pothosware/SoapySDRPlay3\nand rebuild &amp; install the plugin new."
                 "</span>\n\n");
        show_message(GTK_WINDOW(top_window), msg_txt_win);
      }
    }

    soapy_protocol_get_driver(rx);
  }

#endif
  g_idle_add(ext_vfo_update, NULL);
  {
    GdkWindow  *w = gtk_widget_get_window(top_window);
    GdkDisplay *d = gdk_window_get_display(w);
    GdkCursor  *c = use_wayland ? gdk_cursor_new_from_name(d, "default")
                    : gdk_cursor_new(GDK_ARROW);
    gdk_window_set_cursor(w, c);
    g_object_unref(c);
  }
#ifdef MIDI

  for (int i = 0; i < n_midi_devices; i++) {
    if (midi_devices[i].active) {
      //
      // Normally the "active" flags marks a MIDI device that is up and running.
      // It is hi-jacked by the props file to indicate the device should be
      // opened, so we set it to zero. Upon successfull opening of the MIDI device,
      // it will be set again.
      //
      midi_devices[i].active = 0;
      register_midi_device(i);
    }
  }

#endif
#ifdef SATURN

  if (have_saturn_xdma && saturn_server_en) {
    start_saturn_server();
  }

#endif
}

void reassign_pa_trim() {
  for (int j = 0; j < 11; j++) {
    pa_trim[j] = j * pa_power_list[pa_power] * 0.1;
  }
}

void radio_change_receivers(int r) {
  t_print("radio_change_receivers: from %d to %d\n", receivers, r);

  // The button in the radio menu will call this function even if the
  // number of receivers has not changed.
  if (receivers == r) { return; }  // This is always the case if RECEIVERS==1

  //
  // When changing the number of receivers, restart the
  // old protocol
  //
  if (protocol == ORIGINAL_PROTOCOL) {
    old_protocol_stop();
  }

  switch (r) {
  case 1:
    receiver[1]->displaying = 0;
    rx_set_displaying(receiver[1]);
    gtk_container_remove(GTK_CONTAINER(fixed), receiver[1]->panel);
    receivers = 1;
    break;

  case 2:
    gtk_fixed_put(GTK_FIXED(fixed), receiver[1]->panel, 0, 0);
    receiver[1]->displaying = 1;
    rx_set_displaying(receiver[1]);
    receivers = 2;

    //
    // Make sure RX2 shares the sample rate  with RX1 when running P1.
    //
    if (protocol == ORIGINAL_PROTOCOL && receiver[1]->sample_rate != receiver[0]->sample_rate) {
      rx_change_sample_rate(receiver[1], receiver[0]->sample_rate);
    }

    break;
  }

  radio_reconfigure_screen();
  rx_set_active(receiver[0]);
  schedule_high_priority();

  if (protocol == ORIGINAL_PROTOCOL) {
    old_protocol_run();
  }
}

void radio_change_sample_rate(int rate) {
  int i;

  //
  // The radio menu calls this function even if the sample rate
  // has not changed. Do nothing in this case.
  //
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    if (receiver[0]->sample_rate != rate) {
      radio_protocol_stop();

      for (i = 0; i < receivers; i++) {
        rx_change_sample_rate(receiver[i], rate);
      }

      rx_change_sample_rate(receiver[PS_RX_FEEDBACK], rate);
      old_protocol_set_mic_sample_rate(rate);
      radio_protocol_run();

      if (can_transmit) {
        tx_ps_set_sample_rate(transmitter, rate);
      }
    }

    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    if (receiver[0]->sample_rate != rate) {
      radio_protocol_stop();
      rx_change_sample_rate(receiver[0], rate);
      radio_protocol_run();
    }

    break;
#endif
  }
}

static void rxtx(int state) {
  int i;

  if (!can_transmit) {
    t_print("WARNING: rxtx called but no transmitter!");
    return;
  }

  pre_mox = state && !duplex;

  if (state) {
    // switch to tx
    RECEIVER *rx_feedback = receiver[PS_RX_FEEDBACK];
    RECEIVER *tx_feedback = receiver[PS_TX_FEEDBACK];

    if (rx_feedback) { rx_feedback->samples = 0; }

    if (tx_feedback) { tx_feedback->samples = 0; }

    if (!duplex) {
      for (i = 0; i < receivers; i++) {
        // Delivery of RX samples
        // to WDSP via fexchange0() may come to an abrupt stop
        // (especially with PureSignal or DIVERSITY).
        // Therefore, wait for *all* receivers to complete
        // their slew-down before going TX.
        rx_off(receiver[i]);
        receiver[i]->displaying = 0;
        rx_set_displaying(receiver[i]);
        g_object_ref((gpointer)receiver[i]->panel);

        if (receiver[i]->panadapter != NULL) {
          g_object_ref((gpointer)receiver[i]->panadapter);
        }

        if (receiver[i]->waterfall != NULL) {
          g_object_ref((gpointer)receiver[i]->waterfall);
        }

        gtk_container_remove(GTK_CONTAINER(fixed), receiver[i]->panel);
      }
    }

    if (transmitter->dialog) {
      gtk_widget_show_all(transmitter->dialog);

      if (transmitter->dialog_x != -1 && transmitter->dialog_y != -1) {
        gtk_window_move(GTK_WINDOW(transmitter->dialog), transmitter->dialog_x, transmitter->dialog_y);
      }
    } else {
      gtk_fixed_put(GTK_FIXED(fixed), transmitter->panel, transmitter->x, transmitter->y);
    }

    if (transmitter->puresignal) {
      tx_ps_mox(transmitter, 1);
    }

    tx_on(transmitter);
    transmitter->displaying = 1;
    tx_set_displaying(transmitter);

    switch (protocol) {
#ifdef SOAPYSDR

    case SOAPYSDR_PROTOCOL:
      soapy_protocol_set_tx_frequency(transmitter);
      //soapy_protocol_start_transmitter(transmitter);
      break;
#endif
    }

#ifdef DUMP_TX_DATA
    rxiq_count = 0;
#endif
  } else {
    // switch to rx
#ifdef DUMP_TX_DATA
    static int snapshot = 0;
    snapshot++;
    char fname[32];
    snprintf(fname, 32, "TXDUMP%d.iqdata", snapshot);
    FILE *fp = fopen(fname, "w");

    if (fp) {
      for (int i = 0; i < rxiq_count; i++) {
        fprintf(fp, "%d  %ld  %ld\n", i, rxiqi[i], rxiqq[i]);
      }

      fclose(fp);
    }

#endif

    switch (protocol) {
#ifdef SOAPYSDR

    case SOAPYSDR_PROTOCOL:
      //soapy_protocol_stop_transmitter(transmitter);
      break;
#endif
    }

    if (transmitter->puresignal) {
      tx_ps_mox(transmitter, 0);
    }

    tx_off(transmitter);
    transmitter->displaying = 0;
    tx_set_displaying(transmitter);

    if (transmitter->dialog) {
      gtk_window_get_position(GTK_WINDOW(transmitter->dialog), &transmitter->dialog_x, &transmitter->dialog_y);
      gtk_widget_hide(transmitter->dialog);
    } else {
      gtk_container_remove(GTK_CONTAINER(fixed), transmitter->panel);
    }

    if (!duplex) {
      //
      // Set parameters for the "silence first RXIQ samples after TX/RX transition" feature
      // the default is "no silence", that is, fastest turnaround.
      // Seeing "tails" of the own TX signal (from crosstalk at the T/R relay) has been observed
      // for RedPitayas (the identify themself as STEMlab or HERMES) and HermesLite2 devices,
      // we also include the original HermesLite in this list (which can be enlarged if necessary).
      //
      int do_silence = 0;

      if (device == DEVICE_HERMES_LITE2 || device == DEVICE_HERMES_LITE ||
          device == DEVICE_HERMES || device == DEVICE_STEMLAB || device == DEVICE_STEMLAB_Z20) {
        //
        // These systems get a significant "tail" of the RX feedback signal into the RX after TX/RX,
        // leading to AGC pumping. The problem is most severe if there is a carrier until the end of
        // the TX phase (TUNE, AM, FM), the problem is virtually non-existent for CW, and of medium
        // importance in SSB. On the other hand, one wants a very fast turnaround in CW.
        // So there is no "muting" for CW, 31 msec "muting" for TUNE/AM/FM, and 16 msec for other modes.
        //
        // Note that for doing "TwoTone" the silence is built into tx_set_twotone().
        //
        switch (vfo_get_tx_mode()) {
        case modeCWU:
        case modeCWL:
          do_silence = 0; // no "silence"
          break;

        case modeAM:
        case modeFMN:
          do_silence = 5; // leads to 31 ms "silence"
          break;

        default:
          do_silence = 6; // leads to 16 ms "silence"
          break;
        }

        if (tune) { do_silence = 5; } // 31 ms "silence" for TUNEing in any mode
      }

      for (i = 0; i < receivers; i++) {
        gtk_fixed_put(GTK_FIXED(fixed), receiver[i]->panel, receiver[i]->x, receiver[i]->y);
        rx_on(receiver[i]);
        receiver[i]->displaying = 1;
        rx_set_displaying(receiver[i]);
        //
        // There might be some left-over samples in the RX buffer that were filled in
        // *before* going TX, delete them
        //
        receiver[i]->samples = 0;

        if (do_silence) {
          receiver[i]->txrxmax = receiver[i]->sample_rate >> do_silence;
        } else {
          receiver[i]->txrxmax = 0;
        }

        receiver[i]->txrxcount = 0;
      }
    }
  }

#ifdef GPIO
  gpio_set_ptt(state);
#endif
}

void radio_mox_update(int state) {
  if (!can_transmit) { return; }

  if (state && !TransmitAllowed()) {
    state = 0;
    tx_set_out_of_band(transmitter);
  }

  radio_set_mox(state);
  g_idle_add(ext_vfo_update, NULL);
}

void radio_tune_update(int state) {
  if (!can_transmit) { return; }

  radio_set_mox(0);  // This will also cancel VOX and TUNE

  if (state && !TransmitAllowed()) {
    state = 0;
    tx_set_out_of_band(transmitter);
  }

  radio_set_tune(state);
  g_idle_add(ext_vfo_update, NULL);
}

void radio_set_mox(int state) {
  //t_print("%s: mox=%d vox=%d tune=%d NewState=%d\n", __FUNCTION__, mox,vox,tune,state);
  if (!can_transmit) { return; }

  if (state && TxInhibit) { return; }

  //
  // - setting MOX (no matter in which direction) stops TUNEing
  // - setting MOX (no matter in which direction) ends a pending VOX
  // - activating MOX while VOX is pending continues transmission
  // - deactivating MOX while VOX is pending makes a TX/RX transition
  //
  if (tune) {
    radio_set_tune(0);
  }

  vox_cancel();  // remove time-out

  //
  // If MOX is activated while VOX is already pending,
  // then switch from VOX to MOX mode but no RX/TX
  // transition is necessary.
  //
  if (state != radio_is_transmitting()) {
    rxtx(state);
  }

  mox  = state;
  tune = 0;
  vox  = 0;

  switch (protocol) {
  case NEW_PROTOCOL:
    schedule_high_priority();
    schedule_receive_specific();
    break;

  default:
    break;
  }
}

int radio_get_mox() {
  return mox;
}

void radio_set_vox(int state) {
  //t_print("%s: mox=%d vox=%d tune=%d NewState=%d\n", __FUNCTION__, mox,vox,tune,state);
  if (!can_transmit) { return; }

  if (mox || tune) { return; }

  if (state && TxInhibit) { return; }

  if (vox != state) {
    rxtx(state);
  }

  vox = state;
  schedule_high_priority();
  schedule_receive_specific();
}

void radio_set_tune(int state) {
  t_print("%s: mox=%d vox=%d tune=%d NewState=%d\n", __FUNCTION__, mox, vox, tune, state);

  if (!can_transmit) { return; }

  if (state && TxInhibit) { return; }

  // if state==tune, this function is a no-op
  if (tune != state) {
    vox_cancel();

    if (vox || mox) {
      rxtx(0);
      vox = 0;
      mox = 0;
    }

    if (state) {
      //
      // Ron has reported that TX underruns occur if TUNEing with
      // compressor or CFC engaged, and that this can be
      // suppressed by either turning off the phase rotator or
      // by *NOT* silencing the TX audio samples while TUNEing.
      //
      // Experimentally, this means the phase rotator may make
      // funny things when it sees only zero samples.
      //
      // A clean solution is to disable compressor/CFC temporarily
      // while TUNEing.
      //
      // DL1BZ: cannot confirm this, never see such effect here
      // but I was adopting DL1YCF's patch if anybody else has a similiar effect
      //
      int _save_lev = transmitter->lev_enable;
      int _save_phrot = transmitter->phrot_enable;
      int _save_cfc  = transmitter->cfc;
      int _save_cfc_eq = transmitter->cfc_eq;
      int _save_cmpr = transmitter->compressor;
      transmitter->lev_enable = 0;
      transmitter->phrot_enable = 0;
      transmitter->cfc = 0;
      transmitter->cfc_eq = 0;
      transmitter->compressor = 0;
      // set off if TUNEing, but restore later
      tx_set_compressor(transmitter);

      if (can_transmit && display_sliders) {
        update_slider_bbcompr_scale(FALSE);
        update_slider_bbcompr_button(FALSE);
        update_slider_lev_scale(FALSE);
        update_slider_lev_button(FALSE);
      }

      //
      // Keep previous state in transmitter data, so we just need
      // call tx_set_compressor when TUNEing ends.
      //
      transmitter->lev_enable = _save_lev;
      transmitter->phrot_enable = _save_phrot;
      transmitter->cfc = _save_cfc;
      transmitter->cfc_eq = _save_cfc_eq;
      transmitter->compressor = _save_cmpr;

      if (transmitter->puresignal && ! transmitter->ps_oneshot) {
        //
        // DL1YCF:
        // Some users have reported that especially when having
        // very long (10 hours) operating times with PS, hitting
        // the "TUNE" button makes the PS algorithm crazy, such that
        // it produces a very broad line spectrum. Experimentally, it
        // has been observed that this can be avoided by hitting
        // "Off" in the PS menu before hitting "TUNE", and hitting
        // "Restart" in the PS menu when tuning is complete.
        //
        // It is therefore suggested to to so implicitly when PS
        // is enabled.
        // Added April 2024: if in "OneShot" mode, this is probably
        //                   not necessary and the PS reset also
        //                   most likely not wanted here
        //
        // So before start tuning: Reset PS engine
        //
        tx_ps_reset(transmitter);
        usleep(50000);
      }

      if (full_tune) {
        if (OCfull_tune_time != 0) {
          struct timeval te;
          gettimeofday(&te, NULL);
          tune_timeout = (te.tv_sec * 1000LL + te.tv_usec / 1000) + (long long)OCfull_tune_time;
        }
      }

      if (memory_tune) {
        if (OCmemory_tune_time != 0) {
          struct timeval te;
          gettimeofday(&te, NULL);
          tune_timeout = (te.tv_sec * 1000LL + te.tv_usec / 1000) + (long long)OCmemory_tune_time;
        }
      }
    }

    schedule_high_priority();

    if (state) {
      if (!duplex) {
        for (int i = 0; i < receivers; i++) {
          // Delivery of RX samples
          // to WDSP via fexchange0() may come to an abrupt stop
          // (especially with PureSignal or DIVERSITY)
          // Therefore, wait for *all* receivers to complete
          // their slew-down before going TX.
          rx_off(receiver[i]);
          receiver[i]->displaying = 0;
          rx_set_displaying(receiver[i]);
          schedule_high_priority();
        }
      }

      int txmode = vfo_get_tx_mode();
      pre_tune_mode = txmode;
      pre_tune_cw_internal = cw_keyer_internal;
      double freq = 0.0;
#if 0

      // Code currently not active:
      // depending on the mode, do not necessarily tune on the dial frequency
      // if this frequency is not within the pass-band
      //
      // in USB/DIGU      tune 1000 Hz above dial freq
      // in LSB/DIGL,     tune 1000 Hz below dial freq
      //
      switch (txmode) {
      case modeLSB:
      case modeDIGL:
        freq = -1000.0;
        break;

      case modeUSB:
      case modeDIGU:
        freq = 1000.0;
        break;

      default:
        freq = 0.0;
        break;
      }

#endif
      tx_set_singletone(transmitter, 1, freq);

      switch (txmode) {
      case modeCWL:
        cw_keyer_internal = 0;
        tx_set_mode(transmitter, modeLSB);
        break;

      case modeCWU:
        cw_keyer_internal = 0;
        tx_set_mode(transmitter, modeUSB);
        break;
      }

      tune = state;
      radio_calc_drive_level();
      rxtx(state);
    } else {
      tx_set_singletone(transmitter, 0, 0.0);
      rxtx(state);

      switch (pre_tune_mode) {
      case modeCWL:
      case modeCWU:
        tx_set_mode(transmitter, pre_tune_mode);
        cw_keyer_internal = pre_tune_cw_internal;
        break;
      }

      if (transmitter->puresignal && !transmitter->ps_oneshot) {
        //
        // DL1YCF:
        // If we have done a "PS reset" when we started tuning,
        // resume PS engine now.
        //
        tx_ps_resume(transmitter);
      }

      // restore settings we switched off earlier
      tx_set_compressor(transmitter);
      int id = active_receiver->id;
      int m = vfo[id].mode;

      if (can_transmit && display_sliders) {
        if (m == modeDIGU || m == modeDIGL) {
          update_slider_bbcompr_scale(FALSE);
          update_slider_bbcompr_button(FALSE);
          update_slider_lev_scale(FALSE);
          update_slider_lev_button(FALSE);
        } else {
          update_slider_bbcompr_scale(TRUE);
          update_slider_bbcompr_button(TRUE);
          update_slider_lev_scale(TRUE);
          update_slider_lev_button(TRUE);
        }
      }

      tune = state;
      radio_calc_drive_level();
#if defined (__HAVEATU__)
      transmitter->is_tuned = 1;

      if (transmitter->stored_drive > 0) {
        set_drive(transmitter->stored_drive);
      }

      t_print("%s: stored drive level: %.1f\n", __FUNCTION__, transmitter->stored_drive);
      t_print("%s: current drive level: %.1f\n", __FUNCTION__, radio_get_drive());
#endif
#if defined (__AUTOG__)

      if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
        autogain_is_adjusted = 0;
      }

#endif
    }
  }

  schedule_high_priority();
  schedule_transmit_specific();
  schedule_receive_specific();
}

int radio_get_tune() {
  return tune;
}

int radio_is_transmitting() {
  return mox | vox | tune;
}

double radio_get_drive() {
  if (can_transmit) {
    return transmitter->drive;
  } else {
    return 0.0;
  }
}

static int calcLevel(double d) {
  int level = 0;
  int v = vfo_get_tx_vfo();
  const BAND *band = band_get_band(vfo[v].band);
  double target_dbm = 10.0 * log10(d * 1000.0);
  double gbb = band->pa_calibration;
  target_dbm -= gbb;
  double target_volts = sqrt(pow(10, target_dbm * 0.1) * 0.05);
  double volts = min((target_volts / 0.8), 1.0);
  double actual_volts = volts * (1.0 / 0.98);

  if (actual_volts < 0.0) {
    actual_volts = 0.0;
  } else if (actual_volts > 1.0) {
    actual_volts = 1.0;
  }

  level = (int)(actual_volts * 255.0);
  return level;
}

void radio_calc_drive_level() {
  int level;

  if (!can_transmit) { return; }

  if (tune && !transmitter->tune_use_drive) {
    level = calcLevel(transmitter->tune_drive);
  } else {
    level = calcLevel(transmitter->drive);
  }

  //
  // For most of the radios, just copy the "level" and switch off scaling
  //
  transmitter->do_scale = 0;
  transmitter->drive_level = level;

  //
  // For the original Penelope transmitter, the drive level has no effect. Instead, the TX IQ
  // samples must be scaled.
  // The HermesLite-II needs a combination of hardware attenuation and TX IQ scaling.
  // The inverse of the scale factor is needed to reverse the scaling for the TX DAC feedback
  // samples used in the PureSignal case.
  //
  // The constants have been rounded off so the drive_scale is slightly (0.01%) smaller then needed
  // so we have to reduce the inverse a little bit to avoid overflows.
  //
  if ((device == NEW_DEVICE_ATLAS || device == DEVICE_OZY || device == DEVICE_METIS) && atlas_penelope == 1) {
    transmitter->drive_scale = level * 0.0039215;
    transmitter->drive_level = 255;
    transmitter->drive_iscal = 0.9999 / transmitter->drive_scale;
    transmitter->do_scale = 1;
  }

  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
    //
    // Calculate a combination of TX attenuation (values from -7.5 to 0 dB are encoded as 0, 16, 32, ..., 240)
    // and a TX IQ scaling. If level is above 107, the scale factor will be between 0.94 and 1.00, but if
    // level is smaller than 107 it may adopt any value between 0.0 and 1.0
    //
    double d = level;

    if (level > 240) {
      transmitter->drive_level = 240;                     //  0.0 dB hardware ATT
      transmitter->drive_scale = d * 0.0039215;
    } else if (level > 227) {
      transmitter->drive_level = 224;                     // -0.5 dB hardware ATT
      transmitter->drive_scale = d * 0.0041539;
    } else if (level > 214) {
      transmitter->drive_level = 208;                     // -1.0 dB hardware ATT
      transmitter->drive_scale = d * 0.0044000;
    } else if (level > 202) {
      transmitter->drive_level = 192;
      transmitter->drive_scale = d * 0.0046607;
    } else if (level > 191) {
      transmitter->drive_level = 176;
      transmitter->drive_scale = d * 0.0049369;
    } else if (level > 180) {
      transmitter->drive_level = 160;
      transmitter->drive_scale = d * 0.0052295;
    } else if (level > 170) {
      transmitter->drive_level = 144;
      transmitter->drive_scale = d * 0.0055393;
    } else if (level > 160) {
      transmitter->drive_level = 128;
      transmitter->drive_scale = d * 0.0058675;
    } else if (level > 151) {
      transmitter->drive_level = 112;
      transmitter->drive_scale = d * 0.0062152;
    } else if (level > 143) {
      transmitter->drive_level = 96;
      transmitter->drive_scale = d * 0.0065835;
    } else if (level > 135) {
      transmitter->drive_level = 80;
      transmitter->drive_scale = d * 0.0069736;
    } else if (level > 127) {
      transmitter->drive_level = 64;
      transmitter->drive_scale = d * 0.0073868;
    } else if (level > 120) {
      transmitter->drive_level = 48;
      transmitter->drive_scale = d * 0.0078245;
    } else if (level > 113) {
      transmitter->drive_level = 32;
      transmitter->drive_scale = d * 0.0082881;
    } else if (level > 107) {
      transmitter->drive_level = 16;
      transmitter->drive_scale = d * 0.0087793;
    } else {
      transmitter->drive_level = 0;
      transmitter->drive_scale = d * 0.0092995;    // can be between 0.0 and 0.995
    }

    transmitter->drive_iscal = 0.9999 / transmitter->drive_scale;
    transmitter->do_scale = 1;
  }

  //if (transmitter->do_scale) {
  //  t_print("%s: Level=%d Fac=%f\n", __FUNCTION__, transmitter->drive_level, transmitter->drive_scale);
  //} else {
  //  t_print("%s: Level=%d\n", __FUNCTION__, transmitter->drive_level);
  //}
  schedule_high_priority();
}

void radio_set_drive(double value) {
  t_print("%s: drive=%f\n", __FUNCTION__, value);

  if (!can_transmit) { return; }

#if defined (__HAVEATU__)
  transmitter->drive = (double) value;
  t_print("%s: transmitter_drive: %f\n", __FUNCTION__, (double) transmitter->drive);
#else
  transmitter->drive = value;
#endif

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
  case NEW_PROTOCOL:
    radio_calc_drive_level();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_set_tx_gain(transmitter, transmitter->drive);
    break;
#endif
  }
}

void radio_set_satmode(int mode) {
  sat_mode = mode;
}

void radio_set_rf_gain(const RECEIVER *rx) {
#ifdef SOAPYSDR
  soapy_protocol_set_gain_element(rx, radio->info.soapy.rx_gain[rx->adc], (int)adc[rx->adc].gain);
#endif
}

void radio_set_alex_antennas() {
  //
  // Obtain band of VFO-A and transmitter, set ALEX RX/TX antennas
  // and the step attenuator
  // This function is a no-op when running SOAPY.
  // This function also takes care of updating the PA dis/enable
  // status for P2.
  //
  const BAND *band;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    band = band_get_band(vfo[VFO_A].band);
    receiver[0]->alex_antenna = band->alexRxAntenna;

    if (filter_board != CHARLY25) {
      receiver[0]->alex_attenuation = band->alexAttenuation;
    }

    if (can_transmit) {
      band = band_get_band(vfo[vfo_get_tx_vfo()].band);
      transmitter->alex_antenna = band->alexTxAntenna;
    }
  }

  schedule_high_priority();         // possibly update RX/TX antennas
  schedule_general();               // possibly update PA disable
}

void radio_tx_vfo_changed() {
  //
  // When changing the active receiver or changing the split status,
  // the VFO that controls the transmitter my flip between VFOA/VFOB.
  // In these cases, we have to update the TX mode,
  // and re-calculate the drive level from the band-specific PA calibration
  // values. For SOAPY, the only thing to do is the update the TX mode.
  //
  // Note each time radio_tx_vfo_changed() is called, calling radio_set_alex_antennas()
  // is also due.
  //
  if (can_transmit) {
    tx_set_mode(transmitter, vfo_get_tx_mode());
    radio_calc_drive_level();
  }

  schedule_high_priority();         // possibly update RX/TX antennas
  schedule_transmit_specific();     // possibly un-set "CW mode"
  schedule_general();               // possibly update PA disable
}

void radio_set_alex_attenuation(int v) {
  //
  // Change the value of the step attenuator. Store it
  // in the "band" data structure of the current band,
  // and in the receiver[0] data structure
  //
  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    //
    // Store new value of the step attenuator in band data structure
    // (v can be 0,1,2,3)
    //
    BAND *band = band_get_band(vfo[VFO_A].band);
    band->alexAttenuation = v;
    receiver[0]->alex_attenuation = v;
  }

  schedule_high_priority();
}

void radio_split_toggle() {
  radio_set_split(!split);
}

void radio_set_split(int val) {
  //
  // "split" *must only* be set through this interface,
  // since it may change the TX band and thus requires
  // radio_tx_vfo_changed() and radio_set_alex_antennas().
  //
  if (can_transmit) {
    split = val;
    radio_tx_vfo_changed();
    radio_set_alex_antennas();
    g_idle_add(ext_vfo_update, NULL);
    update_slider_split_btn();
  }
}

static void radio_restore_state() {
  t_print("%s: path=%s\n", __FUNCTION__, property_path);
  g_mutex_lock(&property_mutex);
  loadProperties(property_path);
  //
  // For consistency, all variables should get default values HERE,
  // but this is too much for the moment.
  //
  GetPropF0("percent_pan_wf",                                percent_pan_wf);
  GetPropI0("WindowPositionX",                               window_x_pos);
  GetPropI0("WindowPositionY",                               window_y_pos);
  GetPropI0("display_info_bar",                              display_info_bar);
  GetPropI0("display_clock",                                 display_clock);
  GetPropI0("display_solardata",                             display_solardata);
  GetPropI0("display_zoompan",                               display_zoompan);
  GetPropI0("display_sliders",                               display_sliders);
  GetPropI0("display_extra_sliders",                         display_extra_sliders);
  GetPropI0("display_toolbar",                               display_toolbar);
  GetPropI0("display_width",                                 display_width);
  GetPropI0("display_height",                                display_height);
  GetPropI0("full_screen",                                   full_screen);
  GetPropI0("vfo_layout",                                    vfo_layout);
  GetPropI0("optimize_touchscreen",                          optimize_for_touchscreen);
  GetPropI0("capture_max",                                   capture_max);

  //
  // TODO: I think some further options related to the GUI
  // have to be moved up here for Client-Server operation
  //
  // We want to do some internal consistency checking, most of which is done at
  // the very end of this function. However, if the radio is remote we will return
  // from this function in due course so have to check some things here.
  //
  if (display_width  > screen_width  ) { display_width  = screen_width; }

  if (display_height > screen_height ) { display_height = screen_height; }

  //
  // Re-position top window to the position in the props file, provided
  // there are at least 100 pixels left. This assumes the default setting
  // (GDK_GRAVITY_NORTH_WEST) where the "position" refers to the top left corner
  // of the window.
  //
  if (!use_wayland && (window_x_pos < screen_width - 100) && (window_y_pos < screen_height - 100)) {
    gtk_window_move(GTK_WINDOW(top_window), window_x_pos, window_y_pos);
  }

  GetPropS0("radio_bgcolor_rgb_hex",                         radio_bgcolor_rgb_hex);
  GetPropI0("enable_auto_tune",                              enable_auto_tune);
  GetPropI0("enable_tx_inhibit",                             enable_tx_inhibit);
  GetPropI0("radio_sample_rate",                             radio_sample_rate);
  GetPropI0("diversity_enabled",                             diversity_enabled);
  GetPropF0("diversity_gain",                                div_gain);
  GetPropF0("diversity_phase",                               div_phase);
  GetPropF0("diversity_cos",                                 div_cos);
  GetPropF0("diversity_sin",                                 div_sin);
  GetPropI0("new_pa_board",                                  new_pa_board);
  GetPropI0("region",                                        region);
  GetPropI0("atlas_penelope",                                atlas_penelope);
  GetPropI0("atlas_clock_source_10mhz",                      atlas_clock_source_10mhz);
  GetPropI0("atlas_clock_source_128mhz",                     atlas_clock_source_128mhz);
  GetPropI0("atlas_mic_source",                              atlas_mic_source);
  GetPropI0("atlas_janus",                                   atlas_janus);
  GetPropI0("hl2_audio_codec",                               hl2_audio_codec);
  GetPropI0("hl2_cl1_input",                                 hl2_cl1_input)
  GetPropI0("anan10E",                                       anan10E);
  GetPropI0("tx_out_of_band",                                tx_out_of_band_allowed);
  GetPropI0("filter_board",                                  filter_board);
  GetPropI0("pa_enabled",                                    pa_enabled);
  GetPropI0("rx200_udp_port",                                rx200_udp_port);
  GetPropI0("pa_power",                                      pa_power);
  GetPropI0("mic_boost",                                     mic_boost);
  GetPropI0("mic_linein",                                    mic_linein);
  GetPropF0("linein_gain",                                   linein_gain);
  GetPropI0("mic_ptt_enabled",                               mic_ptt_enabled);
  GetPropI0("mic_bias_enabled",                              mic_bias_enabled);
  GetPropI0("mic_ptt_tip_bias_ring",                         mic_ptt_tip_bias_ring);
  GetPropI0("mic_input_xlr",                                 mic_input_xlr);
  GetPropI0("mic_profile_nr",                                mic_prof.nr);

  for (int i = 0; i < 3; i++) {
    GetPropS1("mic_profile.%d.desc", i,                      mic_prof.desc[i])
  }

  GetPropI0("autogain_enabled",                              autogain_enabled);
  GetPropI0("autogain_time_enabled",                         autogain_time_enabled);
  GetPropI0("tx_filter_low",                                 tx_filter_low);
  GetPropI0("tx_filter_high",                                tx_filter_high);
  GetPropI0("cw_keys_reversed",                              cw_keys_reversed);
  GetPropI0("cw_keyer_speed",                                cw_keyer_speed);
  GetPropI0("cw_keyer_mode",                                 cw_keyer_mode);
  GetPropI0("cw_keyer_weight",                               cw_keyer_weight);
  GetPropI0("cw_keyer_spacing",                              cw_keyer_spacing);
  GetPropI0("cw_keyer_internal",                             cw_keyer_internal);
  GetPropI0("cw_keyer_sidetone_volume",                      cw_keyer_sidetone_volume);
  GetPropI0("cw_keyer_ptt_delay",                            cw_keyer_ptt_delay);
  GetPropI0("cw_keyer_hang_time",                            cw_keyer_hang_time);
  GetPropI0("cw_keyer_sidetone_frequency",                   cw_keyer_sidetone_frequency);
  GetPropI0("cw_breakin",                                    cw_breakin);
  //GetPropI0("cw_ramp_width",                                 cw_ramp_width);
  GetPropI0("vfo_encoder_divisor",                           vfo_encoder_divisor);
  GetPropI0("n2adr_hpf_enable",                              n2adr_hpf_enable);
  GetPropI0("OCtune",                                        OCtune);
  GetPropI0("OCfull_tune_time",                              OCfull_tune_time);
  GetPropI0("OCmemory_tune_time",                            OCmemory_tune_time);
  GetPropI0("analog_meter",                                  analog_meter);
  GetPropI0("vox_enabled",                                   vox_enabled);
  GetPropF0("vox_threshold",                                 vox_threshold);
  GetPropF0("vox_hang",                                      vox_hang);
  GetPropI0("calibration",                                   frequency_calibration);
  GetPropF0("ppm_factor",                                    ppm_factor);
  GetPropI0("receivers",                                     receivers);
  GetPropI0("iqswap",                                        iqswap);
  GetPropI0("rx_gain_calibration",                           rx_gain_calibration);
  GetPropF0("drive_digi_max",                                drive_digi_max);
  GetPropI0("split",                                         split);
  GetPropI0("duplex",                                        duplex);
  GetPropI0("sat_mode",                                      sat_mode);
  GetPropI0("mute_rx_while_transmitting",                    mute_rx_while_transmitting);
  GetPropI0("radio.display_warnings",                        display_warnings);
  GetPropI0("radio.display_pacurr",                          display_pacurr);
#ifdef TCI
  GetPropI0("tci_enable",                                  tci_enable);
  GetPropI0("tci_port",                                    tci_port);
  GetPropI0("tci_txonly",                                  tci_txonly);
#endif
  GetPropI0("rigctl_tcp_enable",                             rigctl_tcp_enable);
  GetPropI0("rigctl_tcp_andromeda",                          rigctl_tcp_andromeda);
  GetPropI0("rigctl_tcp_autoreporting",                      rigctl_tcp_autoreporting);
  GetPropI0("rigctl_port_base",                              rigctl_tcp_port);
  GetPropI0("rigctl_debug",                                  rigctl_debug);
  GetPropI0("use_rigctld",                                   use_rigctld);
  GetPropI0("mute_spkr_amp",                                 mute_spkr_amp);
  GetPropI0("adc0_filter_bypass",                            adc0_filter_bypass);
  GetPropI0("adc1_filter_bypass",                            adc1_filter_bypass);
#ifdef SATURN
  GetPropI0("client_enable_tx",                              client_enable_tx);
  GetPropI0("saturn_server_en",                              saturn_server_en);
#endif

  for (int i = 0; i < 11; i++) {
    GetPropF1("pa_trim[%d]", i,                              pa_trim[i]);
  }

  for (int id = 0; id < MAX_SERIAL; id++) {
    //
    // Do not overwrite a "detected" port
    //
    if (!SerialPorts[id].g2) {
      GetPropS1("rigctl_serial_port[%d]", id,                  SerialPorts[id].port);
      GetPropI1("rigctl_serial_enable[%d]", id,                SerialPorts[id].enable);
      GetPropI1("rigctl_serial_andromeda[%d]", id,             SerialPorts[id].andromeda);
      GetPropI1("rigctl_serial_baud_rate[%i]", id,             SerialPorts[id].baud);
      GetPropI1("rigctl_serial_autoreporting[%d]", id,         SerialPorts[id].autoreporting);

      if (SerialPorts[id].andromeda) {
        SerialPorts[id].baud = B9600;
      }
    }
  }

  GetPropS1("tune_serial_port[%d]", MAX_SERIAL,            SerialPorts[MAX_SERIAL].port);
  GetPropI1("tune_serial_baud_rate[%i]", MAX_SERIAL,       SerialPorts[MAX_SERIAL].baud);
  GetPropI1("tune_serial_enable[%d]", MAX_SERIAL,          SerialPorts[MAX_SERIAL].enable);
  GetPropI1("tune_serial_swapRtsDtr[%d]", MAX_SERIAL,      SerialPorts[MAX_SERIAL].swapRtsDtr);
  GetPropS1("ptt_serial_port[%d]", MAX_SERIAL + 1,         SerialPorts[MAX_SERIAL + 1].port);
  GetPropI1("ptt_serial_baud_rate[%i]", MAX_SERIAL + 1,    SerialPorts[MAX_SERIAL + 1].baud);
  GetPropI1("ptt_serial_enable[%d]", MAX_SERIAL + 1,       SerialPorts[MAX_SERIAL + 1].enable);
  GetPropI1("ptt_serial_swapRtsDtr[%d]", MAX_SERIAL + 1,   SerialPorts[MAX_SERIAL + 1].swapRtsDtr);
  GetPropS0("own_callsign",                                own_callsign);

  for (int i = 0; i < n_adc; i++) {
    GetPropI1("radio.adc[%d].filters", i,                    adc[i].filters);
    GetPropI1("radio.adc[%d].hpf", i,                        adc[i].hpf);
    GetPropI1("radio.adc[%d].lpf", i,                        adc[i].lpf);
    GetPropI1("radio.adc[%d].antenna", i,                    adc[i].antenna);
    GetPropI1("radio.adc[%d].dither", i,                     adc[i].dither);
    GetPropI1("radio.adc[%d].random", i,                     adc[i].random);
    GetPropI1("radio.adc[%d].preamp", i,                     adc[i].preamp);
    GetPropI1("radio.adc[%d].attenuation", i,                adc[i].attenuation);
    GetPropI1("radio.adc[%d].enable_step_attenuation", i,    adc[i].enable_step_attenuation);
    GetPropF1("radio.adc[%d].gain", i,                       adc[i].gain);

    if (radio && strcmp(radio->name, "sdrplay") != 0) {
      GetPropF1("radio.adc[%d].min_gain", i,                   adc[i].min_gain);
      GetPropF1("radio.adc[%d].max_gain", i,                   adc[i].max_gain);
    }

    GetPropI1("radio.adc[%d].agc", i,                        adc[i].agc);
    GetPropI1("radio.dac[%d].antenna", i,                    dac[i].antenna);
    GetPropF1("radio.dac[%d].gain", i,                       dac[i].gain);
  }

  filterRestoreState();
  bandRestoreState();
  memRestoreState();
  vfo_restore_state();
  gpioRestoreActions();
#ifdef MIDI
  midiRestoreState();
#endif
  t_print("%s: radio state (except receiver/transmitter) restored.\n", __FUNCTION__);

  if (pa_enabled && (device == DEVICE_HERMES_LITE || device == DEVICE_HERMES_LITE2 ||
                     device == NEW_DEVICE_HERMES_LITE || device == NEW_DEVICE_HERMES_LITE2)) {
    reassign_pa_trim();
    t_print("%s: using HL2: re-assign pa_trim[]\n", __FUNCTION__);
  }

  //
  // Sanity check part 2:
  //
  // 1.) If the radio does not have 2 ADCs, there is no DIVERSITY
  //
  if (RECEIVERS < 2 || n_adc < 2) {
    diversity_enabled = 0;
  }

  //
  // 2.) Selecting the N2ADR filter board overrides most OC settings
  //
  if (filter_board == N2ADR) {
    n2adr_oc_settings(); // Apply default OC settings for N2ADR board
  }

  if (filter_board == N2ADR_TX) {
    n2adr_oc_settings_tx(); // Apply default OC settings for N2ADR board (if RX the LPF is OFF)
  }

  g_mutex_unlock(&property_mutex);
}

void radio_save_state() {
  g_mutex_lock(&property_mutex);
  clearProperties();

  //
  // Save the receiver and transmitter data structures. These
  // are restored in create_receiver/create_transmitter
  //
  for (int i = 0; i < RECEIVERS; i++) {
    rx_save_state(receiver[i]);
  }

  if (can_transmit) {
    // The only variables of interest in this receiver are
    // the alex_antenna an the adc
    if (receiver[PS_RX_FEEDBACK]) {
      rx_save_state(receiver[PS_RX_FEEDBACK]);
    }

    tx_save_state(transmitter);
  }

  //
  // Obtain window position and save in props file
  //
  gtk_window_get_position(GTK_WINDOW(top_window), &window_x_pos, &window_y_pos);
  SetPropF0("percent_pan_wf",                                percent_pan_wf);
  SetPropI0("WindowPositionX",                               window_x_pos);
  SetPropI0("WindowPositionY",                               window_y_pos);
  //
  // What comes now is essentially copied from radio_restore_state,
  // with "GetProp" replaced by "SetProp".
  //
  //
  // Use the "saved" Zoompan/Slider/Toolbar display status
  // if they are currently hidden via the "Hide" button
  //
  SetPropI0("display_info_bar",                              display_info_bar);
  SetPropI0("display_clock",                                 display_clock);
  SetPropI0("display_solardata",                             display_solardata);
  SetPropI0("display_zoompan",                               hide_status ? old_zoom : display_zoompan);
  SetPropI0("display_sliders",                               hide_status ? old_slid : display_sliders);
  SetPropI0("display_extra_sliders",                         display_extra_sliders);
  SetPropI0("display_toolbar",                               hide_status ? old_tool : display_toolbar);
  SetPropI0("display_width",                                 display_width);
  SetPropI0("display_height",                                display_height);
  SetPropI0("full_screen",                                   full_screen);
  SetPropI0("vfo_layout",                                    vfo_layout);
  SetPropI0("optimize_touchscreen",                          optimize_for_touchscreen);
  SetPropI0("capture_max",                                   capture_max);
  SetPropS0("radio_bgcolor_rgb_hex",                         radio_bgcolor_rgb_hex);
  //
  // TODO: I think some further options related to the GUI
  // have to be moved up here for Client-Server operation
  //
  SetPropI0("enable_auto_tune",                              enable_auto_tune);
  SetPropI0("enable_tx_inhibit",                             enable_tx_inhibit);
  SetPropI0("radio_sample_rate",                             radio_sample_rate);
  SetPropI0("diversity_enabled",                             diversity_enabled);
  SetPropF0("diversity_gain",                                div_gain);
  SetPropF0("diversity_phase",                               div_phase);
  SetPropF0("diversity_cos",                                 div_cos);
  SetPropF0("diversity_sin",                                 div_sin);
  SetPropI0("new_pa_board",                                  new_pa_board);
  SetPropI0("region",                                        region);
  SetPropI0("atlas_penelope",                                atlas_penelope);
  SetPropI0("atlas_clock_source_10mhz",                      atlas_clock_source_10mhz);
  SetPropI0("atlas_clock_source_128mhz",                     atlas_clock_source_128mhz);
  SetPropI0("atlas_mic_source",                              atlas_mic_source);
  SetPropI0("atlas_janus",                                   atlas_janus);
  SetPropI0("hl2_audio_codec",                               hl2_audio_codec);
  SetPropI0("hl2_cl1_input",                                 hl2_cl1_input)
  SetPropI0("anan10E",                                       anan10E);
  SetPropI0("tx_out_of_band",                                tx_out_of_band_allowed);
  SetPropI0("filter_board",                                  filter_board);
  SetPropI0("pa_enabled",                                    pa_enabled);
  SetPropI0("rx200_udp_port",                                rx200_udp_port);
  SetPropI0("pa_power",                                      pa_power);
  SetPropI0("mic_boost",                                     mic_boost);
  SetPropI0("mic_linein",                                    mic_linein);
  SetPropF0("linein_gain",                                   linein_gain);
  SetPropI0("mic_ptt_enabled",                               mic_ptt_enabled);
  SetPropI0("mic_bias_enabled",                              mic_bias_enabled);
  SetPropI0("mic_ptt_tip_bias_ring",                         mic_ptt_tip_bias_ring);
  SetPropI0("mic_input_xlr",                                 mic_input_xlr);
  SetPropI0("mic_profile_nr",                                mic_prof.nr);

  for (int i = 0; i < 3; i++) {
    SetPropS1("mic_profile.%d.desc", i,                      mic_prof.desc[i])
  }

  SetPropI0("autogain_enabled",                              autogain_enabled);
  SetPropI0("autogain_time_enabled",                         autogain_time_enabled);
  SetPropI0("tx_filter_low",                                 tx_filter_low);
  SetPropI0("tx_filter_high",                                tx_filter_high);
  SetPropI0("cw_keys_reversed",                              cw_keys_reversed);
  SetPropI0("cw_keyer_speed",                                cw_keyer_speed);
  SetPropI0("cw_keyer_mode",                                 cw_keyer_mode);
  SetPropI0("cw_keyer_weight",                               cw_keyer_weight);
  SetPropI0("cw_keyer_spacing",                              cw_keyer_spacing);
  SetPropI0("cw_keyer_internal",                             cw_keyer_internal);
  SetPropI0("cw_keyer_sidetone_volume",                      cw_keyer_sidetone_volume);
  SetPropI0("cw_keyer_ptt_delay",                            cw_keyer_ptt_delay);
  SetPropI0("cw_keyer_hang_time",                            cw_keyer_hang_time);
  SetPropI0("cw_keyer_sidetone_frequency",                   cw_keyer_sidetone_frequency);
  SetPropI0("cw_breakin",                                    cw_breakin);
  //SetPropI0("cw_ramp_width",                                 cw_ramp_width);
  SetPropI0("vfo_encoder_divisor",                           vfo_encoder_divisor);
  SetPropI0("n2adr_hpf_enable",                              n2adr_hpf_enable);
  SetPropI0("OCtune",                                        OCtune);
  SetPropI0("OCfull_tune_time",                              OCfull_tune_time);
  SetPropI0("OCmemory_tune_time",                            OCmemory_tune_time);
  SetPropI0("analog_meter",                                  analog_meter);
  SetPropI0("vox_enabled",                                   vox_enabled);
  SetPropF0("vox_threshold",                                 vox_threshold);
  SetPropF0("vox_hang",                                      vox_hang);
  // SetPropI0("calibration",                                frequency_calibration);
  SetPropF0("ppm_factor",                                    ppm_factor);
  SetPropI0("receivers",                                     receivers);
  SetPropI0("iqswap",                                        iqswap);
  SetPropI0("rx_gain_calibration",                           rx_gain_calibration);
  SetPropF0("drive_digi_max",                                drive_digi_max);
  SetPropI0("split",                                         split);
  SetPropI0("duplex",                                        duplex);
  SetPropI0("sat_mode",                                      sat_mode);
  SetPropI0("mute_rx_while_transmitting",                    mute_rx_while_transmitting);
  SetPropI0("radio.display_warnings",                        display_warnings);
  SetPropI0("radio.display_pacurr",                          display_pacurr);
#ifdef TCI
  SetPropI0("tci_enable",                                  tci_enable);
  SetPropI0("tci_port",                                    tci_port);
  SetPropI0("tci_txonly",                                  tci_txonly);
#endif
  SetPropI0("rigctl_tcp_enable",                             rigctl_tcp_enable);
  SetPropI0("rigctl_tcp_andromeda",                          rigctl_tcp_andromeda);
  SetPropI0("rigctl_tcp_autoreporting",                      rigctl_tcp_autoreporting);
  SetPropI0("rigctl_port_base",                              rigctl_tcp_port);
  SetPropI0("rigctl_debug",                                  rigctl_debug);
  SetPropI0("use_rigctld",                                   use_rigctld);
  SetPropI0("mute_spkr_amp",                                 mute_spkr_amp);
  SetPropI0("adc0_filter_bypass",                            adc0_filter_bypass);
  SetPropI0("adc1_filter_bypass",                            adc1_filter_bypass);
#ifdef SATURN
  SetPropI0("client_enable_tx",                              client_enable_tx);
  SetPropI0("saturn_server_en",                              saturn_server_en);
#endif

  for (int i = 0; i < 11; i++) {
    SetPropF1("pa_trim[%d]", i,                              pa_trim[i]);
  }

  for (int id = 0; id < MAX_SERIAL; id++) {
    SetPropI1("rigctl_serial_enable[%d]", id,                SerialPorts[id].enable);
    SetPropI1("rigctl_serial_andromeda[%d]", id,             SerialPorts[id].andromeda);
    SetPropI1("rigctl_serial_baud_rate[%i]", id,             SerialPorts[id].baud);
    SetPropS1("rigctl_serial_port[%d]", id,                  SerialPorts[id].port);
    SetPropI1("rigctl_serial_autoreporting[%d]", id,         SerialPorts[id].autoreporting);
  }

  SetPropS1("tune_serial_port[%d]", MAX_SERIAL,            SerialPorts[MAX_SERIAL].port);
  SetPropI1("tune_serial_baud_rate[%i]", MAX_SERIAL,       SerialPorts[MAX_SERIAL].baud);
  SetPropI1("tune_serial_enable[%d]", MAX_SERIAL,          SerialPorts[MAX_SERIAL].enable);
  SetPropI1("tune_serial_swapRtsDtr[%d]", MAX_SERIAL,    SerialPorts[MAX_SERIAL].swapRtsDtr);
  SetPropS1("ptt_serial_port[%d]", MAX_SERIAL + 1,         SerialPorts[MAX_SERIAL + 1].port);
  SetPropI1("ptt_serial_baud_rate[%i]", MAX_SERIAL + 1,    SerialPorts[MAX_SERIAL + 1].baud);
  SetPropI1("ptt_serial_enable[%d]", MAX_SERIAL + 1,       SerialPorts[MAX_SERIAL + 1].enable);
  SetPropI1("ptt_serial_swapRtsDtr[%d]", MAX_SERIAL + 1, SerialPorts[MAX_SERIAL + 1].swapRtsDtr);
  SetPropS0("own_callsign",                                own_callsign);

  for (int i = 0; i < n_adc; i++) {
    SetPropI1("radio.adc[%d].filters", i,                    adc[i].filters);
    SetPropI1("radio.adc[%d].hpf", i,                        adc[i].hpf);
    SetPropI1("radio.adc[%d].lpf", i,                        adc[i].lpf);
    SetPropI1("radio.adc[%d].antenna", i,                    adc[i].antenna);
    SetPropI1("radio.adc[%d].dither", i,                     adc[i].dither);
    SetPropI1("radio.adc[%d].random", i,                     adc[i].random);
    SetPropI1("radio.adc[%d].preamp", i,                     adc[i].preamp);
    SetPropI1("radio.adc[%d].attenuation", i,                adc[i].attenuation);
    SetPropI1("radio.adc[%d].enable_step_attenuation", i,    adc[i].enable_step_attenuation);
    SetPropF1("radio.adc[%d].gain", i,                       adc[i].gain);

    if (radio && strcmp(radio->name, "sdrplay") != 0) {
      SetPropF1("radio.adc[%d].min_gain", i,                   adc[i].min_gain);
      SetPropF1("radio.adc[%d].max_gain", i,                   adc[i].max_gain);
    }

    SetPropI1("radio.adc[%d].agc", i,                        adc[i].agc);
    SetPropI1("radio.dac[%d].antenna", i,                    dac[i].antenna);
    SetPropF1("radio.dac[%d].gain", i,                       dac[i].gain);
  }

  filterSaveState();
  bandSaveState();
  memSaveState();
  vfo_save_state();
  gpioSaveActions();
#ifdef MIDI
  midiSaveState();
#endif
  saveProperties(property_path);
  sync();
  g_mutex_unlock(&property_mutex);
}


///////////////////////////////////////////////////////////////////////////////////////////
//
// A mechanism to make ComboBoxes "touchscreen-friendly".
// If the variable "optimize_for_touchscreen" is nonzero, their
// behaviour is modified such that they only react on "button release"
// events, the first release event pops up the menu, the second one makes
// the choice.
//
// This is necessary since a "slow click" (with some delay between press and release)
// leads you nowhere: the PRESS event lets the menu open, it grabs the focus, and
// the RELEASE event makes the choice. With a mouse this is no problem since you
// hold the button while making a choice, but with a touch-screen it may make the
// GUI un-usable.
//
// The variable "optimize_for_touchscreen" can be changed in the RADIO menu (or whereever
// it is decided to move this).
//
///////////////////////////////////////////////////////////////////////////////////////////

// cppcheck-suppress constParameterCallback
static gboolean eventbox_callback(GtkWidget *widget, GdkEvent *event, gpointer data) {
  //
  // data is the ComboBox that is contained in the EventBox
  //
  if (event->type == GDK_BUTTON_RELEASE) {
    gtk_combo_box_popup(GTK_COMBO_BOX(data));
  }

  return TRUE;
}

//
// This function has to be called instead of "gtk_grid_attach" for ComboBoxes.
// Basically, it creates an EventBox and puts the ComboBox therein,
// such that all events (mouse clicks) go to the EventBox. This ignores
// everything except "button release" events, in this case it lets the ComboBox
// pop-up the menu which then goes to the foreground.
// Then, the choice can be made from the menu in the usual way.
//
void my_combo_attach(GtkGrid *grid, GtkWidget *combo, int row, int col, int spanrow, int spancol) {
  if (optimize_for_touchscreen) {
    GtkWidget *eventbox = gtk_event_box_new();
    g_signal_connect( eventbox, "event",   G_CALLBACK(eventbox_callback),   combo);
    gtk_container_add(GTK_CONTAINER(eventbox), combo);
    gtk_event_box_set_above_child(GTK_EVENT_BOX(eventbox), TRUE);
    gtk_grid_attach(GTK_GRID(grid), eventbox, row, col, spanrow, spancol);
  } else {
    gtk_grid_attach(GTK_GRID(grid), combo, row, col, spanrow, spancol);
  }
}

//
// This is used in several places (ant_menu, oc_menu, pa_menu)
// and determines the highest band that the radio can use
// (xvtr bands are not counted here)
//

int radio_max_band() {
  int max = BANDS - 1;

  switch (device) {
  case DEVICE_HERMES_LITE:
  case DEVICE_HERMES_LITE2:
  case NEW_DEVICE_HERMES_LITE:
  case NEW_DEVICE_HERMES_LITE2:
    max = band10;
    break;

  case SOAPYSDR_USB_DEVICE:
    // This function will not be called for SOAPY
    max = BANDS - 1;
    break;

  default:
    max = band6;
    break;
  }

  return max;
}

void radio_protocol_stop() {
  //
  // paranoia ...
  //
  radio_mox_update(0);
  usleep(100000);

  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    old_protocol_stop();
    break;

  case NEW_PROTOCOL:
    new_protocol_menu_stop();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_stop_receiver(receiver[0]);
    break;
#endif
  }
}

void radio_protocol_run() {
  switch (protocol) {
  case ORIGINAL_PROTOCOL:
    old_protocol_run();
    break;

  case NEW_PROTOCOL:
    new_protocol_menu_start();
    break;
#ifdef SOAPYSDR

  case SOAPYSDR_PROTOCOL:
    soapy_protocol_start_receiver(receiver[0]);
    break;
#endif
  }
}

void radio_protocol_restart() {
  radio_protocol_stop();
  usleep(200000);
  radio_protocol_run();
}

static gpointer auto_tune_thread(gpointer data) {
  //
  // This routine is triggered when an "auto tune" event
  // occurs, which usually is triggered by an input.
  //
  // Start TUNEing and keep TUNEing until the auto_tune_flag
  // becomes zero. Abort TUNEing if it takes too long
  //
  // To avoid race conditions, there are two flags:
  // auto_tune_flag is set while this thread is running
  // auto_tune_end  signals that tune can stop
  //
  // The thread will not terminate until auto_tune_end is flagged,
  // but  it may stop tuning before.
  //
  int count = 0;
  g_idle_add(ext_tune_update, GINT_TO_POINTER(1));

  for (;;) {
    if (count >= 0) {
      count++;
    }

    usleep(50000);

    if (auto_tune_end) {
      g_idle_add(ext_tune_update, GINT_TO_POINTER(0));
      break;
    }

    if (count >= 200) {
      g_idle_add(ext_tune_update, GINT_TO_POINTER(0));
      count = -1;
    }
  }

  usleep(50000);       // debouncing
  auto_tune_flag = 0;
  return NULL;
}

void radio_start_auto_tune() {
  static GThread *tune_thread_id = NULL;

  if (tune_thread_id) {
    auto_tune_end  = 1;
    g_thread_join(tune_thread_id);
  }

  auto_tune_flag = 1;
  auto_tune_end  = 0;
  tune_thread_id = g_thread_new("TUNE", auto_tune_thread, NULL);
}

//
// The next four functions implement a temporary change
// of settings during capture/replay.
//
void radio_start_capture() {
  //
  // - turn off  equalizers for both RX but keep the state in rx
  //
  for (int i = 0; i < receivers; i++) {
    int eq = receiver[i]->eq_enable;
    receiver[i]->eq_enable = 0;
    rx_set_equalizer(receiver[i]);
    receiver[i]->eq_enable = eq;
  }
}

void radio_end_capture() {
  //
  // - normalize what has been captured
  // - restore  RX equalizer on/off flags
  //
  double max = 0.0;

  //
  // Note: when using AGC, this normalization should not
  //       be necessary except for the weakest signals on
  //       the quietest bands.
  //
  for (int i = 0; i < capture_record_pointer; i++) {
    double t = fabs(capture_data[i]);

    if (t > max) { max = t; }
  }

  //t_print("%s: max=%f\n", __FUNCTION__, max);

  if (max > 0.05) {
    //
    // If max. amplitude is below -25 dB, then assume this
    // is "noise only" and do not normalize
    //
    max = 1.0 / max;  // scale factor

    for (int i = 0; i < capture_record_pointer; i++) {
      capture_data[i] *= max;
    }
  }

  //
  // re-activate equalizers if they had been active before
  //
  for (int i = 0; i < receivers; i++) {
    rx_set_equalizer(receiver[i]);
  }
}

void radio_start_playback() {
  //
  // - turn off TX equalizer   but keep equalizer  info in transmitter->eq_enable
  // - turn off TX compression but keep compressor info in transmitter->compression
  // - set mic gain  to zero   but keep mic_gain   info in transmitter->mic_gain
  // - disable CFC             but keep            info in transmitter->mic_gain
  // - disable DEXP            but keep            info in transmitter->mic_gain
  //
  int  comp   = transmitter->compressor;
  int  cfc    = transmitter->cfc;
  int  cfc_eq = transmitter->cfc_eq;
  int  eq     = transmitter->eq_enable;
  int  dexp   = transmitter->dexp;
  double gain = transmitter->mic_gain;
  int leveler_enable = transmitter->lev_enable;
  int phrot_enable = transmitter->phrot_enable;
  transmitter->eq_enable = 0;
  transmitter->compressor = 0;
  transmitter->mic_gain = 0.0;
  transmitter->cfc = 0;
  transmitter->cfc_eq = 0;
  transmitter->dexp = 0;
  transmitter->lev_enable = 0;
  transmitter->phrot_enable = 0;
  tx_set_equalizer(transmitter);
  tx_set_mic_gain(transmitter);
  tx_set_compressor(transmitter);
  tx_set_dexp(transmitter);
  transmitter->compressor = comp;
  transmitter->cfc = cfc;
  transmitter->cfc_eq = cfc_eq;
  transmitter->dexp = dexp;
  transmitter->eq_enable  = eq;
  transmitter->mic_gain = gain;
  transmitter->lev_enable = leveler_enable;
  transmitter->phrot_enable = phrot_enable;
}

void radio_end_playback() {
  //
  // re-inforce settings stored in transmitter:
  // - TX equalizer on/off
  // - TX compressor on/off
  // - TX mic gain setting
  // - CFC and DEXP
  //
  tx_set_equalizer(transmitter);
  tx_set_mic_gain(transmitter);
  tx_set_compressor(transmitter);
  tx_set_dexp(transmitter);
}

//
// utility function needed e.g. for qsort
//
int compare_doubles(const void *a, const void *b) {
  double arg1 = *(const double *)a;
  double arg2 = *(const double *)b;

  if (arg1 < arg2) { return -1; }

  if (arg1 > arg2) { return 1; }

  return 0;
}
