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
#include <gdk/gdk.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdint.h>

#include "appearance.h"
#include "agc.h"
#include "band.h"
#include "discovered.h"
#include "radio.h"
#include "receiver.h"
#include "transmitter.h"
#include "rx_panadapter.h"
#include "vfo.h"
#include "mode.h"
#include "actions.h"
#include "message.h"
#include "toolset.h"
#include "old_protocol.h"
#ifdef GPIO
  #include "gpio.h"
#endif
#ifdef USBOZY
  #include "ozyio.h"
#endif
#include "audio.h"
#include "map_d.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif

char zeitString[20];
static time_t last_noisefloor_calc_time = 0;  // Zeit der letzten Berechnung
int g_noise_level = 0;
int val_agcsetpoint = 0;
int val_hwagc = 0;
int val_rfgr = 0;
int val_ifgr = 0;
int val_currGain = 0;
char txt_ifgr[16];
char txt_rfgr[16];
char txt_currGain[16];
gboolean val_biast = FALSE;

//----------------------------------------------------------------------------------------------
// Peak-and-Hold (spectrum trace) with configurable hold time
// Peak mode from INI (used only when pan_peak_hold_enabled==1): 1=Peak Hold (max per bin), 2=Peak Decay (hold+decay)

#define PAN_PEAK_HOLD_MAX_RX 8
static int pan_peak_hold_mode_last[PAN_PEAK_HOLD_MAX_RX] = { 0 };
static long long pan_peak_min_display_last[PAN_PEAK_HOLD_MAX_RX] = { 0 };
static int pan_peak_min_display_valid[PAN_PEAK_HOLD_MAX_RX] = { 0 };

typedef struct {
  float    *buf;   // peak values (samples domain)
  uint16_t *age;   // frames since last peak
  int size;
} PAN_PEAK_HOLD;

static PAN_PEAK_HOLD pan_peak_hold[PAN_PEAK_HOLD_MAX_RX];

void rx_panadapter_peak_hold_clear(RECEIVER *rx) {
  if (!rx) { return; }

  if (rx->id < 0 || rx->id >= PAN_PEAK_HOLD_MAX_RX) { return; }

  // reset shift baseline for this RX
  pan_peak_min_display_valid[rx->id] = 0;
  PAN_PEAK_HOLD *ph = &pan_peak_hold[rx->id];

  if (ph->buf && ph->age) {
    for (int i = 0; i < ph->size; i++) {
      ph->buf[i] = -200.0f;
      ph->age[i] = UINT16_MAX;
    }
  }
}

static void rx_panadapter_peak_hold_shift(PAN_PEAK_HOLD *ph, int width, int dp) {
  if (!ph || !ph->buf || !ph->age) { return; }

  if (dp == 0) { return; }

  if (dp >= width || dp <= -width) {
    for (int i = 0; i < width; i++) {
      ph->buf[i] = -200.0f;
      ph->age[i] = UINT16_MAX;
    }

    return;
  }

  if (dp > 0) {
    // shift left by dp: new[x] = old[x+dp]
    memmove(&ph->buf[0], &ph->buf[dp], (size_t)(width - dp) * sizeof(float));
    memmove(&ph->age[0], &ph->age[dp], (size_t)(width - dp) * sizeof(uint16_t));

    for (int i = width - dp; i < width; i++) {
      ph->buf[i] = -200.0f;
      ph->age[i] = UINT16_MAX;
    }
  } else {
    // shift right by -dp
    int sh = -dp;
    memmove(&ph->buf[sh], &ph->buf[0], (size_t)(width - sh) * sizeof(float));
    memmove(&ph->age[sh], &ph->age[0], (size_t)(width - sh) * sizeof(uint16_t));

    for (int i = 0; i < sh; i++) {
      ph->buf[i] = -200.0f;
      ph->age[i] = UINT16_MAX;
    }
  }
}

typedef struct {
  long long freq;      // absolute RF-Frequenz in Hz
  gboolean enabled;
  char label[32];
  gint64 expire_time;  // 0 = nie automatisch entfernen, sonst Monotonic-Time (us)
} PAN_LABEL;

typedef struct {
  int index;
  double x;
  int row;
} PAN_LABEL_POS;

#define PAN_LABEL_MIN_DX 40.0   // Mindestabstand in Pixeln in einer Zeile
#define MAX_PAN_LABELS 64 // max. saved DX spots

static PAN_LABEL pan_labels[MAX_PAN_LABELS];
static int pan_label_count = 0;

void panadapter_set_max_label_rows(int r) {
  if (r < 1) { r = 1; }

  if (r > 32) { r = 32; }   /* arbitrary upper limit */

  max_pan_label_rows = r;
}

/* Prüft, ob ein DX-Spot-Label mit gleicher Frequenz und gleichem Text schon existiert.
 * Falls ja: expire_time aktualisieren und TRUE zurückgeben.
 * Falls nein: FALSE (neues Label muss angelegt werden).
*/
static gboolean pan_dxspot_update_if_exists(long long freq_hz, const char *text, int lifetime_ms) {
  gint64 now;
  int i;

  if (text == NULL) {
    return FALSE;
  }

  for (i = 0; i < pan_label_count; i++) {
    PAN_LABEL *pl = &pan_labels[i];

    if (!pl->enabled) {
      continue;
    }

    if (pl->freq != freq_hz) {
      continue;
    }

    if (g_strcmp0(pl->label, text) != 0) {
      continue;
    }

    if (lifetime_ms > 0) {
      now = g_get_monotonic_time();      /* us */
      pl->expire_time = now + (gint64)lifetime_ms * 1000;
    } else {
      pl->expire_time = 0;
    }

    return TRUE;
  }

  return FALSE;
}

/* interner Helfer: freien Slot für ein neues Label ermitteln
* - bevorzugt deaktivierte Einträge wiederverwenden
* - wenn alle Slots belegt/aktiv sind: FIFO -> ältestes Label (Index 0) raus
*/
static PAN_LABEL *pan_label_get_slot(void) {
  int i;

  /* 1) deaktivierte Einträge wiederverwenden */
  for (i = 0; i < pan_label_count; i++) {
    if (!pan_labels[i].enabled) {
      return &pan_labels[i];
    }
  }

  /* 2) noch Platz im Array: anhängen */
  if (pan_label_count < MAX_PAN_LABELS) {
    return &pan_labels[pan_label_count++];
  }

  /* 3) FIFO: ältestes Label (Index 0) verwerfen, Rest nach vorne schieben */
  memmove(&pan_labels[0], &pan_labels[1],
          (MAX_PAN_LABELS - 1) * sizeof(PAN_LABEL));
  pan_label_count = MAX_PAN_LABELS - 1;
  return &pan_labels[pan_label_count++];
}

static int pan_label_cmp(const void *a, const void *b) {
  const PAN_LABEL_POS *pa = (const PAN_LABEL_POS *)a;
  const PAN_LABEL_POS *pb = (const PAN_LABEL_POS *)b;

  if (pa->x < pb->x) { return -1; }

  if (pa->x > pb->x) { return 1; }

  return 0;
}

// Example:
// pan_add_label(7100000LL, "Beacon");
// pan_add_label(7074000LL, "Relais");

void pan_add_label(long long freq, const char *text) {
  PAN_LABEL *pl;

  if (text == NULL) {
    return;
  }

  pl = pan_label_get_slot();
  pl->freq = freq;
  pl->enabled = TRUE;
  g_strlcpy(pl->label, text, sizeof(pl->label));
  pl->expire_time = 0;  /* 0 => kein automatisches Entfernen */
}

// Example:
// pan_add_label_timeout(7100000LL, "Spot", 5000);  // 5 Sekunden sichtbar

void pan_add_label_timeout(long long freq, const char *text, int lifetime_ms) {
  PAN_LABEL *pl;

  if (text == NULL) {
    return;
  }

  pl = pan_label_get_slot();
  pl->freq = freq;
  pl->enabled = TRUE;
  g_strlcpy(pl->label, text, sizeof(pl->label));

  if (lifetime_ms > 0) {
    gint64 now = g_get_monotonic_time();  /* us */
    pl->expire_time = now + (gint64)lifetime_ms * 1000;
  } else {
    pl->expire_time = 0;  /* 0 => kein Timeout */
  }
}

void pan_clear_labels(void) {
  pan_label_count = 0;
}

void pan_add_dx_spot(double freq_khz, const char *dxcall) {
  long long freq_hz;
  char label[32];

  if (pan_spot_lifetime_min < 1) { pan_spot_lifetime_min = 1; } // 1min minimum

  if (pan_spot_lifetime_min > 720) { pan_spot_lifetime_min = 720; } // 720min = 12h = maximum

  int lifetime_ms = pan_spot_lifetime_min * 60000;

  if (dxcall == NULL || freq_khz <= 0.0) {
    return;
  }

  /* Cluster-Frequenz kHz → Hz, sauber gerundet */
  freq_hz = (long long)(freq_khz * 1000.0 + 0.5);
  /* Label-Text – hier nur das Call, ggf. später erweitern */
  g_strlcpy(label, dxcall, sizeof(label));

  /* Doublet-Check: gleicher Call auf gleicher Frequenz? -> nur Timeout erneuern */
  if (pan_dxspot_update_if_exists(freq_hz, label, lifetime_ms)) {
    return;
  }

  /* Kein bestehender Eintrag -> neues Label anlegen */
  pan_add_label_timeout(freq_hz, label, lifetime_ms);
}

//------------------------------------------------------------------------------
static GdkPixbuf *worldmap_scaled = NULL;

/*
  1. Wir laden einmal das Map-Bild und berechnen es
  2. Nur wenn sich die Auflösung ändert, wird komplett neu gerendert

  Wir berechnen und zeichnen also nur, wenn notwendig und nicht mehr
  synchron zur Framerate wie bisher -> senkt CPU Last !
*/
static void init_worldmap_pixbuf(int w, int h) {
  if (worldmap_scaled &&
      gdk_pixbuf_get_width(worldmap_scaled) == w &&
      gdk_pixbuf_get_height(worldmap_scaled) == h) {
    return;  // schon vorhanden in richtiger Größe
  }

  if (worldmap_scaled) {
    g_object_unref(worldmap_scaled);  // wichtig: alten freigeben
    worldmap_scaled = NULL;
  }

  GError *error = NULL;
  GInputStream *mem_stream = g_memory_input_stream_new_from_data(worldmap_png, worldmap_png_len, NULL);
  GdkPixbuf *raw_pixbuf = gdk_pixbuf_new_from_stream(mem_stream, NULL, &error);
  g_object_unref(mem_stream);

  if (!raw_pixbuf) {
    t_print("%s: ERROR loading map pic: %s\n", __func__, error->message);
    g_error_free(error);
    return;
  }

  worldmap_scaled = gdk_pixbuf_scale_simple(raw_pixbuf, w, h, GDK_INTERP_BILINEAR);
  g_object_unref(raw_pixbuf);
}

static void draw_image(cairo_t *cr, GdkPixbuf *pixbuf, int x_offset, int y_offset) {
  // Bild auf dem Cairo-Zeichenkontext setzen
  gdk_cairo_set_source_pixbuf(cr, pixbuf, x_offset, y_offset);
  cairo_paint(cr);  // Bild zeichnen
}
//------------------------------------------------------------------------------

/* Create a new surface of the appropriate size to store our scribbles */
static gboolean panadapter_configure_event_cb (GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;
  int mywidth = gtk_widget_get_allocated_width (widget);
  int myheight = gtk_widget_get_allocated_height (widget);

  if (rx->panadapter_surface) {
    cairo_surface_destroy (rx->panadapter_surface);
  }

  rx->panadapter_surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                           CAIRO_CONTENT_COLOR,
                           mywidth, myheight);
  cairo_t *cr = cairo_create(rx->panadapter_surface);

  if (display_wmap) {
    cairo_set_source_rgba(cr, COLOUR_PAN_BG_MAP, 0.15); // 0.00..1.00 Transparenz abnehmend
  } else {
    cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
  }

  cairo_paint(cr);
  cairo_destroy(cr);
  return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean panadapter_draw_cb(GtkWidget *widget, cairo_t *cr, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;

  if (rx->panadapter_surface) {
    cairo_set_source_surface (cr, rx->panadapter_surface, 0.0, 0.0);
    cairo_paint (cr);
  }

  return FALSE;
}

static gboolean panadapter_button_press_event_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  return rx_button_press_event(widget, event, data);
}

static gboolean panadapter_button_release_event_cb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  return rx_button_release_event(widget, event, data);
}

static gboolean panadapter_motion_notify_event_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  return rx_motion_notify_event(widget, event, data);
}

// cppcheck-suppress constParameterCallback
static gboolean panadapter_scroll_event_cb(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  return rx_scroll_event(widget, event, data);
}

//----------------------------------------------------------------------------------------------
// Reference of calculate S-Meter values: https://de.wikipedia.org/wiki/S-Meter
// <= 30 MHz: S9 = -73dbm
//  > 30 MHz: S9 = -93dbm

#define NUM_SWERTE 19   /* Number of S-Werte */

// lower limits <= 30 MHz
static short int lowlimitsHF[NUM_SWERTE] = {
  -200, -121, -115, -109, -103, -97, -91, -85, -79, -73, -68, -63, -58, -53, -48, -43, -33, -23, -13
  //      S1    S2    S3    S4    S5   S6   S7   S8   S9   +5   +10  +15  +20  +25  +30  +40  +50  +60
};
// upper limits <= 30 MHz
static short int uplimitsHF[NUM_SWERTE] = {
  -122, -116, -110, -104, -98, -92, -86, -80, -74, -69, -64, -59, -54, -49, -44, -34, -24, -14, 0
};

// lower limits > 30 MHz
static short int lowlimitsUKW[NUM_SWERTE] = {
  -200, -141, -135, -129, -123, -117, -111, -105, -99, -93, -88, -83, -78, -73, -68, -63, -53, -43, -33
  //      S1    S2    S3    S4    S5    S6    S7    S8   S9   +5   +10  +15  +20  +25  +30  +40  +50  +60
};
// upper limits > 30 MHz
static short int uplimitsUKW[NUM_SWERTE] = {
  -142, -136, -130, -124, -118, -112, -106, -100, -94, -89, -84, -79, -74, -69, -64, -54, -44, -34, 0
};

static const char* (dbm2smeter[NUM_SWERTE + 1]) = {
  "no signal", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9", "S9+5db", "S9+10db", "S9+15db", "S9+20db", "S9+25db", "S9+30db", "S9+40db", "S9+50db", "S9+60db", "out of range"
};


static unsigned char get_SWert(short int dbm) {
  int i;

  for (i = 0; i < NUM_SWERTE; i++) {
    // if VFO > 30 MHz reference S9 = -93dbm
    if (vfo[active_receiver->id].frequency > 30000000LL) {
      if ((dbm >= lowlimitsUKW[i]) && (dbm <= uplimitsUKW[i])) {
        return i;
      }

      // if VFO <= 30 MHz reference S9 = -73dbm
    } else {
      if ((dbm >= lowlimitsHF[i]) && (dbm <= uplimitsHF[i])) {
        return i;
      }
    }
  }

  return NUM_SWERTE; // no valid S-Werte -> return not defined
}

//----------------------------------------------------------------------------------------------

static void get_local_time(char *zeitString, size_t groesse) {
  // Aktuelle Zeit abrufen
  time_t aktuelleZeit;
  time(&aktuelleZeit);
  // Zeit in lokales Format konvertieren
  struct tm Zeit;
  // Zeit in UTC konvertieren (Thread-sicher)
  gmtime_r(&aktuelleZeit, &Zeit); // thread-sicher

  // Zeit in lokales Format konvertieren
  // localtime_r(&aktuelleZeit, &Zeit); // thread-sicher
  // Formatierter Zeit-String erstellen
  if (region == REGION_UK) {
    snprintf(zeitString, groesse, "%02d/%02d/%04d %02d:%02d:%02d",
             Zeit.tm_mday,
             Zeit.tm_mon + 1,
             Zeit.tm_year + 1900,
             Zeit.tm_hour,
             Zeit.tm_min,
             Zeit.tm_sec);
  } else if (region == REGION_US) {
    snprintf(zeitString, groesse, "%02d/%02d/%04d %02d:%02d:%02d",
             Zeit.tm_mon + 1,
             Zeit.tm_mday,
             Zeit.tm_year + 1900,
             Zeit.tm_hour,
             Zeit.tm_min,
             Zeit.tm_sec);
  } else {
    snprintf(zeitString, groesse, "%02d.%02d.%04d %02d:%02d:%02d",
             Zeit.tm_mday,
             Zeit.tm_mon + 1, // Monate beginnen bei 0
             Zeit.tm_year + 1900, // Jahre ab 1900
             Zeit.tm_hour,
             Zeit.tm_min,
             Zeit.tm_sec);
  }
}

static int autoscale_panadapter_with_offset(double noise_value, int offset_db) {
  int value = (((int)noise_value / 10) - ((int)noise_value % 10 != 0 ? 1 : 0)) * 10 + offset_db;
  value = (value > -95) ? -95 : (value < -220) ? -220 : value;
  return value;
}

/*
int compare_doubles(const void *a, const void *b) {
  double arg1 = *(const double *)a;
  double arg2 = *(const double *)b;

  if (arg1 < arg2) { return -1; }

  if (arg1 > arg2) { return 1; }

  return 0;
}
*/

void rx_panadapter_update(RECEIVER *rx) {
  if (!rx || !rx->panadapter_surface) {
    return;
  }

  int i;
  float *samples;
  cairo_text_extents_t extents;
  long long f;
  long long divisor;
  double soffset;
  gboolean active = active_receiver == rx;
  int mywidth = gtk_widget_get_allocated_width (rx->panadapter);
  int myheight = gtk_widget_get_allocated_height (rx->panadapter);
  samples = rx->pixel_samples;
  cairo_t *cr;
  cr = cairo_create (rx->panadapter_surface);

  if (display_wmap) {
    //------------------------------------------------------------------------------
    init_worldmap_pixbuf(mywidth, myheight);  // nur wenn nötig

    if (worldmap_scaled) {
      draw_image(cr, worldmap_scaled, 0, 0);
    }

    //------------------------------------------------------------------------------
    cairo_set_source_rgba(cr, COLOUR_PAN_BG_MAP, 0.15); // 0.00..1.00 Transparenz abnehmend
  } else {
    cairo_set_source_rgba(cr, COLOUR_PAN_BACKGND);
  }

  cairo_rectangle(cr, 0, 0, mywidth, myheight);
  cairo_fill(cr);
  double HzPerPixel = rx->hz_per_pixel;  // need this many times
  int mode = vfo[rx->id].mode;
  long long frequency = vfo[rx->id].frequency;
  int vfoband = vfo[rx->id].band;
  long long offset;
  //
  // soffset contains all corrections for attenuation and preamps
  // Perhaps some adjustment is necessary for those old radios which have
  // switchable preamps.
  //
  const BAND *band = band_get_band(vfoband);
  int calib = rx_gain_calibration - band->gain;
#ifdef SOAPYSDR

  if (device == SOAPYSDR_USB_DEVICE && strcmp(radio->name, "sdrplay") == 0) {
    int v_Gain = (int)soapy_protocol_get_gain_element(active_receiver, "CURRENT");
    adc[rx->adc].gain = 0;
    adc[rx->adc].attenuation = 0;
    adc[rx->adc].gain = v_Gain;
    // t_print("%s: adc[rx->adc].gain = %f adc[rx->adc].attenuation = %f calib = %f\n", __func__, adc[rx->adc].gain,adc[rx->adc].attenuation, calib);
  }

#endif
  soffset = (double) calib + (double)adc[rx->adc].attenuation - adc[rx->adc].gain;

  //
  // offset is used to calculate the filter edges. They move  with the RIT value
  //
  if (vfo[rx->id].ctun) {
    offset = vfo[rx->id].offset;
  } else {
    offset = vfo[rx->id].rit_enabled ? vfo[rx->id].rit : 0;
  }

  if (filter_board == ALEX && rx->adc == 0) {
    soffset += (double)(10 * rx->alex_attenuation - 20 * rx->preamp);
  }

  if (filter_board == CHARLY25 && rx->adc == 0) {
    soffset += (double)(12 * rx->alex_attenuation - 18 * rx->preamp - 18 * rx->dither);
  }

  // In diversity mode, the RX2 frequency tracks the RX1 frequency
  if (diversity_enabled && rx->id == 1) {
    frequency = vfo[0].frequency;
    vfoband = vfo[0].band;
    mode = vfo[0].mode;
  }

  long long half = (long long)rx->sample_rate / 2LL;
  double vfofreq = ((double) rx->pixels * 0.5) - (double)rx->pan;

  //
  //
  // The CW frequency is the VFO frequency and the center of the spectrum
  // then is at the VFO frequency plus or minus the sidetone frequency. However we
  // will keep the center of the PANADAPTER at the VFO frequency and shift the
  // pixels of the spectrum.
  //
  if (mode == modeCWU) {
    frequency -= cw_keyer_sidetone_frequency;
    vfofreq += (double) cw_keyer_sidetone_frequency / HzPerPixel;
  } else if (mode == modeCWL) {
    frequency += cw_keyer_sidetone_frequency;
    vfofreq -= (double) cw_keyer_sidetone_frequency / HzPerPixel;
  }

  long long min_display = frequency - half + (long long)((double)rx->pan * HzPerPixel);
  long long max_display = min_display + (long long)((double)rx->width * HzPerPixel);

  if (vfoband == band60 && band_channels_60m != NULL && region > 0) {
    for (i = 0; i < channel_entries; i++) {
      long long low_freq = band_channels_60m[i].frequency - (band_channels_60m[i].width / (long long)2);
      long long hi_freq = band_channels_60m[i].frequency + (band_channels_60m[i].width / (long long)2);
      double x1 = (double) (low_freq - min_display) / HzPerPixel;
      double x2 = (double) (hi_freq - min_display) / HzPerPixel;
      cairo_set_source_rgba(cr, COLOUR_PAN_60M_OPQ);
      cairo_rectangle(cr, x1, 0.0, x2 - x1, myheight);
      cairo_fill(cr);
    }
  }

  //
  // Filter edges.
  //
  cairo_set_source_rgba (cr, COLOUR_PAN_FILTER);
  double filter_left = ((double)rx->pixels * 0.5) - (double)rx->pan + (((double)rx->filter_low + offset) / HzPerPixel);
  double filter_right = ((double)rx->pixels * 0.5) - (double)rx->pan + (((double)rx->filter_high + offset) / HzPerPixel);
  cairo_rectangle(cr, filter_left, 0.0, filter_right - filter_left, myheight);
  cairo_fill(cr);

  // plot the levels
  if (active) {
    cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
  } else {
    cairo_set_source_rgba(cr, COLOUR_PAN_LINE_WEAK);
  }

  double dbm_per_line = (double)myheight / ((double)rx->panadapter_high - (double)rx->panadapter_low);
  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
  char v[32];

  for (i = rx->panadapter_high; i >= rx->panadapter_low; i--) {
    int mod = abs(i) % rx->panadapter_step;

    if (mod == 0) {
      double y = (double)(rx->panadapter_high - i) * dbm_per_line;
      cairo_move_to(cr, 0.0, y);
      cairo_line_to(cr, mywidth, y);
      snprintf(v, 32, "%d dBm", i);
      cairo_move_to(cr, 1, y);
      cairo_show_text(cr, v);
    }
  }

  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);
  //
  // plot frequency markers
  // calculate a divisor such that we have about 65
  // pixels distance between frequency markers,
  // and then round upwards to the  next 1/2/5 seris
  //
  divisor = (rx->sample_rate * 65) / rx->pixels;

  if (divisor > 500000LL) { divisor = 1000000LL; }
  else if (divisor > 200000LL) { divisor = 500000LL; }
  else if (divisor > 100000LL) { divisor = 200000LL; }
  else if (divisor >  50000LL) { divisor = 100000LL; }
  else if (divisor >  20000LL) { divisor =  50000LL; }
  else if (divisor >  10000LL) { divisor =  20000LL; }
  else if (divisor >   5000LL) { divisor =  10000LL; }
  else if (divisor >   2000LL) { divisor =   5000LL; }
  else if (divisor >   1000LL) { divisor =   2000LL; }
  else { divisor =   1000LL; }

  //
  // Calculate the actual distance of frequency markers
  // (in pixels)
  //
  int marker_distance = (rx->pixels * divisor) / rx->sample_rate;
  f = ((min_display / divisor) * divisor) + divisor;
  cairo_select_font_face(cr, DISPLAY_FONT_BOLD, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  //
  // If space is available, increase font size of freq. labels a bit
  //
  int marker_extra = (marker_distance > 100) ? 2 : 0;
  cairo_set_font_size(cr, DISPLAY_FONT_SIZE2 + marker_extra);

  while (f < max_display) {
    double x = (double)(f - min_display) / HzPerPixel;
    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, myheight);

    //
    // For frequency marker lines very close to the left or right
    // edge, do not print a frequency since this probably won't fit
    // on the screen
    //
    if ((f >= min_display + divisor / 2) && (f <= max_display - divisor / 2)) {
      //
      // For frequencies larger than 10 GHz, we cannot
      // display all digits here so we give three dots
      // and three "MHz" digits
      //
      if (f > 10000000000LL && marker_distance < 80) {
        snprintf(v, 32, "...%03lld.%03lld", (f / 1000000) % 1000, (f % 1000000) / 1000);
      } else {
        snprintf(v, 32, "%0lld.%03lld", f / 1000000, (f % 1000000) / 1000);
      }

      // center text at "x" position
      cairo_text_extents(cr, v, &extents);
      cairo_move_to(cr, x - (extents.width / 2.0), 10 + marker_extra);
      cairo_show_text(cr, v);
    }

    f += divisor;
  }

  cairo_set_line_width(cr, PAN_LINE_THIN);
  cairo_stroke(cr);

  //--------------------------------------------------------------------------------------------
  /* Custom Labels auf exakten Frequenzen (nur Text, mit Timeout + Y-Staffelung) */
  if (pan_label_count > 0) {
    PAN_LABEL_POS pos[MAX_PAN_LABELS];
    int pos_count = 0;
    gint64 now = g_get_monotonic_time();  /* us */

    /* Sichtbare Labels einsammeln, abgelaufene deaktivieren */
    for (int m = 0; m < pan_label_count; m++) {
      PAN_LABEL *pl = &pan_labels[m];

      if (!pl->enabled) {
        continue;
      }

      /* Timeout-Check */
      if (pl->expire_time != 0 && now >= pl->expire_time) {
        pl->enabled = FALSE;
        continue;
      }

      /* Außerhalb des sichtbaren Frequenzbereichs */
      if (pl->freq < min_display || pl->freq > max_display) {
        continue;
      }

      double x = (double)(pl->freq - min_display) / HzPerPixel;
      pos[pos_count].index = m;
      pos[pos_count].x = x;
      pos[pos_count].row = 0;
      pos_count++;

      if (pos_count >= MAX_PAN_LABELS) {
        break;
      }
    }

    if (pos_count > 0) {
      double last_x_in_row[max_pan_label_rows];

      /* Reiheninitialisierung */
      for (int r = 0; r < max_pan_label_rows; r++) {
        last_x_in_row[r] = -1e9;
      }

      /* Links-nach-rechts sortieren */
      qsort(pos, pos_count, sizeof(PAN_LABEL_POS), pan_label_cmp);

      /* Reihen (Y-Level) zuweisen, um Überlappung zu minimieren */
      for (int i = 0; i < pos_count; i++) {
        double x = pos[i].x;
        int assigned_row = 0;
        gboolean placed = FALSE;

        for (int r = 0; r < max_pan_label_rows; r++) {
          if (fabs(x - last_x_in_row[r]) >= PAN_LABEL_MIN_DX) {
            assigned_row = r;
            last_x_in_row[r] = x;
            placed = TRUE;
            break;
          }
        }

        if (!placed) {
          /* Fallback: erste Reihe */
          assigned_row = 0;
        }

        pos[i].row = assigned_row;
      }

      /* Labels zeichnen */
      for (int i = 0; i < pos_count; i++) {
        PAN_LABEL *pl = &pan_labels[pos[i].index];
        double x = pos[i].x;
        int row = pos[i].row;
        cairo_set_source_rgba(cr, COLOUR_WHITE);
        cairo_select_font_face(cr,
                               DISPLAY_FONT_BOLD,
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, DISPLAY_FONT_SIZE2 + marker_extra);
        cairo_text_extents_t te;
        cairo_text_extents(cr, pl->label, &te);
        /* Basis-Y unter der Skala; Zeilen vertikal staffeln */
        double base_y = 10.0 + marker_extra + te.height + 2.0;
        double row_height = te.height + 4.0;
        double y = base_y + (double)row * row_height;
        cairo_move_to(cr, x - te.width / 2.0, y);
        cairo_show_text(cr, pl->label);
      }
    }
  }

  //--------------------------------------------------------------------------------------------

  // band edges
  if (band->frequencyMin != 0LL) {
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_line_width(cr, PAN_LINE_THICK);

    if ((min_display < band->frequencyMin) && (max_display > band->frequencyMin)) {
      double x = (double)(band->frequencyMin - min_display) / HzPerPixel;
      cairo_move_to(cr, x, 0);
      cairo_line_to(cr, x, myheight);
      cairo_set_line_width(cr, PAN_LINE_EXTRA);
      cairo_stroke(cr);
    }

    if ((min_display < band->frequencyMax) && (max_display > band->frequencyMax)) {
      double x = (double) (band->frequencyMax - min_display) / HzPerPixel;
      cairo_move_to(cr, x, 0);
      cairo_line_to(cr, x, myheight);
      cairo_set_line_width(cr, PAN_LINE_EXTRA);
      cairo_stroke(cr);
    }
  }

  // cursor
  if (active) {
    cairo_set_source_rgba(cr, COLOUR_WHITE);
  } else {
    cairo_set_source_rgba(cr, COLOUR_WHITE);
  }

  double x_coord = vfofreq + (offset / HzPerPixel);

  if (x_coord < 0) { x_coord = 0; }

  if (x_coord > mywidth - 1) { x_coord = mywidth - 1; }

  cairo_move_to(cr, x_coord, 0.0);
  cairo_line_to(cr, x_coord, myheight);
  cairo_set_line_width(cr, PAN_LINE_EXTRA);
  cairo_stroke(cr);
  // Marker oben zeichnen
  double cursor_w = 12.0;
  double cursor_h = 9.0;
  /*
  if (mode == modeDIGL || mode == modeLSB) {
    // Dreieck nach links
    cairo_move_to(cr, x_coord, 0.0);
    cairo_line_to(cr, x_coord, cursor_w);
    cairo_line_to(cr, x_coord - cursor_h, cursor_w / 2);
  } else if (mode == modeDIGU || mode == modeUSB) {
    // Dreieck nach rechts
    cairo_move_to(cr, x_coord, 0.0);
    cairo_line_to(cr, x_coord, cursor_w);
    cairo_line_to(cr, x_coord + cursor_h, cursor_w / 2);
  } else { }
  */
  // Dreieck nach unten
  cairo_move_to(cr, x_coord - (cursor_w / 2), 0.0);
  cairo_line_to(cr, x_coord + (cursor_w / 2), 0.0);
  cairo_line_to(cr, x_coord, 0.0 + cursor_h);
  cairo_close_path(cr);
  cairo_fill(cr);
  // signal
  double s1;
  int pan = rx->pan;
  samples[pan] = -200.0;
  samples[mywidth - 1 + pan] = -200.0;

  //---------------------------------------------------------------------------------------
  // Peak-and-Hold update
  if (pan_peak_hold_enabled && rx->id >= 0 && rx->id < PAN_PEAK_HOLD_MAX_RX) {
    PAN_PEAK_HOLD *ph = &pan_peak_hold[rx->id];

    // Auto-clear on mode change (prevents immediate decay after Peak Hold mode)
    if (pan_peak_hold_mode_last[rx->id] != pan_peak_hold_mode) {
      rx_panadapter_peak_hold_clear(rx);
      pan_peak_hold_mode_last[rx->id] = pan_peak_hold_mode;
    }

    if (ph->size != mywidth) {
      float *nbuf = realloc(ph->buf, mywidth * sizeof(float));
      uint16_t *nage = realloc(ph->age, mywidth * sizeof(uint16_t));

      if (nbuf && nage) {
        ph->buf = nbuf;
        ph->age = nage;
        ph->size = mywidth;

        for (int i = 0; i < mywidth; i++) {
          ph->buf[i] = -200.0f;
          ph->age[i] = UINT16_MAX;
        }

        pan_peak_min_display_valid[rx->id] = 0;
      }
    }

    if (ph->buf && ph->age && rx->fps > 0) {
      // Shift peak buffer to follow the same frequency->pixel mapping as the spectrum.
      // min_display = vfo[rx->id].frequency - (sample_rate/2) + pan*hz_per_pixel
      {
        long long half = (long long)rx->sample_rate / 2LL;
        long long min_display = vfo[rx->id].frequency - half
                                + (long long)llround((double)rx->pan * rx->hz_per_pixel);

        if (!pan_peak_min_display_valid[rx->id]) {
          pan_peak_min_display_last[rx->id] = min_display;
          pan_peak_min_display_valid[rx->id] = 1;
        } else {
          long long df = min_display - pan_peak_min_display_last[rx->id];

          if (df != 0 && rx->hz_per_pixel > 0.0) {
            int dp = (int)llround((double)df / rx->hz_per_pixel);

            if (dp != 0) {
              // If min_display increases, spectrum content shifts left => shift peak buffer left (dp>0).
              rx_panadapter_peak_hold_shift(ph, mywidth, dp);
              pan_peak_min_display_last[rx->id] = min_display;
            }
          }
        }
      }

      if (pan_peak_hold_mode == 1) {
        // Peak Hold (Thetis): display the largest signal per bin while enabled
        for (int i = 0; i < mywidth; i++) {
          float cur = (float)samples[i + pan];
          float peak = ph->buf[i];

          if (cur > peak) {
            peak = cur;
          }

          ph->buf[i] = peak;
          ph->age[i] = UINT16_MAX;
        }
      } else {
        // Default: Peak Decay (hold for pan_peak_hold_hold_sec seconds, then decay)
        const int hold_frames =
          (int)(pan_peak_hold_hold_sec * (float)rx->fps + 0.5f);
        const float decay_per_frame =
          pan_peak_hold_decay_db_per_sec / (float)rx->fps;

        for (int i = 0; i < mywidth; i++) {
          float cur = (float)samples[i + pan];
          float peak = ph->buf[i];
          uint16_t age = ph->age[i];

          if (cur > peak) {
            peak = cur;
            age = 0;
          } else {
            if (age < UINT16_MAX) { age++; }

            if (age > hold_frames) {
              peak -= decay_per_frame;
            }
          }

          ph->buf[i] = peak;
          ph->age[i] = age;
        }
      }
    }
  }

  //
  // most HPSDR only have attenuation (no gain), while HermesLite-II and SOAPY use gain (no attenuation)
  //
  s1 = (double)samples[pan] + soffset;
  s1 = floor((rx->panadapter_high - s1)
             * (double) myheight
             / (rx->panadapter_high - rx->panadapter_low));
  cairo_move_to(cr, 0.0, s1);

  for (i = 1; i < mywidth; i++) {
    double s2;
    s2 = (double)samples[i + pan] + soffset;
    s2 = floor((rx->panadapter_high - s2)
               * (double) myheight
               / (rx->panadapter_high - rx->panadapter_low));
    cairo_line_to(cr, i, s2);
  }

  cairo_pattern_t *gradient;
  gradient = NULL;

  if (rx->display_gradient) {
    gradient = cairo_pattern_create_linear(0.0, myheight, 0.0, 0.0);
    // calculate where S9 is as gradient offset (0.0 = bottom, 1.0 = top)
    double denom = (double)rx->panadapter_high - (double)rx->panadapter_low;

    if (denom <= 0.0) { denom = 1.0; } // Fallback, falls high<=low

    double S9 = ( (vfo[rx->id].frequency > 30000000LL) ? -93.0 : -73.0 );
    S9 += 10; // 10db nach oben schieben
    S9 = (S9 - (double)rx->panadapter_low) / denom;
    S9 = (S9 < 0.0) ? 0.0 : (S9 > 1.0) ? 1.0 : S9;
    // t_print("S9(off)=%.6f low=%d high=%d h=%d\n", S9, rx->panadapter_low, rx->panadapter_high, myheight);

    if (active) {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,       GRAD_GREEN);
      cairo_pattern_add_color_stop_rgba(gradient, S9 * 0.20, GRAD_YELLOW);
      cairo_pattern_add_color_stop_rgba(gradient, S9 * 0.55, GRAD_ORANGE);
      cairo_pattern_add_color_stop_rgba(gradient, S9 * 0.80, GRAD_RED);
      cairo_pattern_add_color_stop_rgba(gradient, S9,        GRAD_PURPLE);
    } else {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,       GRAD_GREEN_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9 * 0.20, GRAD_YELLOW_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9 * 0.55, GRAD_ORANGE_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9 * 0.80, GRAD_RED_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9,        GRAD_PURPLE_WEAK);
    }

    /*
        // calculate where S9 is
        double S9 = -73;

        if (vfo[rx->id].frequency > 30000000LL) {
          S9 = -93;
        }

        S9 = floor((rx->panadapter_high - S9)
                   * (double) myheight
                   / (rx->panadapter_high - rx->panadapter_low));
        S9 = 1.0 - (S9 / (double)myheight);

    if (active) {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,              GRAD_GREEN);
      cairo_pattern_add_color_stop_rgba(gradient, S9 / 3.0,         GRAD_YELLOW);
      cairo_pattern_add_color_stop_rgba(gradient, (S9 / 3.0) * 2.0, GRAD_ORANGE);
      cairo_pattern_add_color_stop_rgba(gradient, S9,               GRAD_RED);
    } else {
      cairo_pattern_add_color_stop_rgba(gradient, 0.0,              GRAD_GREEN_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9 / 3.0,         GRAD_YELLOW_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, (S9 / 3.0) * 2.0, GRAD_ORANGE_WEAK);
      cairo_pattern_add_color_stop_rgba(gradient, S9,               GRAD_RED_WEAK);
    }
    */
    cairo_set_source(cr, gradient);
  } else {
    //
    // Different shades of white
    //
    if (active) {
      if (!rx->display_filled) {
        cairo_set_source_rgba(cr, COLOUR_PAN_FILL3);
      } else {
        cairo_set_source_rgba(cr, COLOUR_PAN_FILL2);
      }
    } else {
      cairo_set_source_rgba(cr, COLOUR_PAN_FILL1);
    }
  }

  if (rx->display_filled) {
    cairo_close_path (cr);
    cairo_fill_preserve (cr);
    cairo_set_line_width(cr, PAN_LINE_THIN);
  } else {
    //
    // if not filling, use thicker line
    //
    cairo_set_line_width(cr, PAN_LINE_THICK);
  }

  cairo_stroke(cr);

  //---------------------------------------------------------------------------------------
  // Peak-and-Hold trace rendering
  if (pan_peak_hold_enabled && rx->id >= 0 && rx->id < PAN_PEAK_HOLD_MAX_RX) {
    PAN_PEAK_HOLD *ph = &pan_peak_hold[rx->id];

    if (ph->buf && ph->size == mywidth) {
      // cairo_set_source_rgba(cr, COLOUR_SHADE);
      cairo_set_source_rgba(cr,
                            peak_line_col.r,
                            peak_line_col.g,
                            peak_line_col.b,
                            peak_line_col.a);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      double y = (double)ph->buf[0] + soffset;
      y = floor((rx->panadapter_high - y) * myheight /
                (rx->panadapter_high - rx->panadapter_low));
      cairo_move_to(cr, 0.0, y);

      for (int i = 1; i < mywidth; i++) {
        y = (double)ph->buf[i] + soffset;
        y = floor((rx->panadapter_high - y) * myheight /
                  (rx->panadapter_high - rx->panadapter_low));
        cairo_line_to(cr, (double)i, y);
      }

      cairo_stroke(cr);
    }
  }

  if (gradient) {
    cairo_pattern_destroy(gradient);
  }

  //---------------------------------------------------------------------------------------
  // move downward to show the line, otherwise the spectrum overlay this line
  // AGC line
  if (rx->agc != AGC_OFF) {
    cairo_set_line_width(cr, PAN_LINE_THICK);
    double knee_y = rx->agc_thresh + soffset;
    knee_y = floor((rx->panadapter_high - knee_y)
                   * (double) myheight
                   / (rx->panadapter_high - rx->panadapter_low));
    double hang_y = rx->agc_hang + soffset;
    hang_y = floor((rx->panadapter_high - hang_y)
                   * (double) myheight
                   / (rx->panadapter_high - rx->panadapter_low));

    if (rx->agc != AGC_MEDIUM && rx->agc != AGC_FAST) {
      if (active) {
        cairo_set_source_rgba(cr, GRAD_CORAL);
      } else {
        cairo_set_source_rgba(cr, COLOUR_ATTN_WEAK);
      }

      cairo_move_to(cr, 40.0, hang_y - 8.0);
      cairo_rectangle(cr, 40, hang_y - 8.0, 8.0, 8.0);
      cairo_fill(cr);
      cairo_move_to(cr, 40.0, hang_y);
      cairo_line_to(cr, (double)mywidth - 40.0, hang_y);
      cairo_set_line_width(cr, PAN_LINE_THICK);
      cairo_stroke(cr);
      cairo_move_to(cr, 48.0, hang_y);
      cairo_show_text(cr, "-H");
    }

    if (active) {
      cairo_set_source_rgba(cr, GRAD_CORAL);
    } else {
      cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
    }

    cairo_move_to(cr, 40.0, knee_y - 8.0);
    cairo_rectangle(cr, 40, knee_y - 8.0, 8.0, 8.0);
    cairo_fill(cr);
    cairo_move_to(cr, 40.0, knee_y);
    cairo_line_to(cr, (double)mywidth - 40.0, knee_y);
    cairo_set_line_width(cr, PAN_LINE_THICK);
    cairo_stroke(cr);
    cairo_move_to(cr, 48.0, knee_y);

    if (active) {
      cairo_set_source_rgba(cr, GRAD_CORAL);
    } else {
      cairo_set_source_rgba(cr, COLOUR_OK_WEAK);
    }

    if (device == DEVICE_HERMES_LITE2) {
      cairo_move_to(cr, 58.0, knee_y - 2.0);
      cairo_show_text(cr, "[AGC]");
      char AGCgain[64];
      snprintf(AGCgain, 64, "%+d", (int)active_receiver->agc_gain);
      cairo_move_to(cr, 62.0, knee_y + 12.0);
      cairo_show_text(cr, AGCgain);
    } else {
      cairo_show_text(cr, "-Gain");
    }
  }

  //---------------------------------------------------------------------------------------
  if (rx->panadapter_autoscale_enabled) {
    double noise_floor_level = -175.0; // inital value
    double ignore_noise_percentile = 60.0; // means 80%
    double *qsorted_samples = malloc(mywidth * sizeof(double));
    static double noise_floor_level_sum = 0.0; // inital value
    static int anz_messungen = 0; // initial value
    static int noisefloor_first_run_flag = 1;
    static int noisefloor_update_interval = 5; // in sec
    static int panadapter_scale_corr_f = 5;
    // Berechne die aktuelle Zeit
    time_t current_time;
    time(&current_time);

    // calculate the noise level from samples
    if (qsorted_samples != NULL) {
      for (int i = 0; i < mywidth; i++) {
        qsorted_samples[i] = (double)samples[i + rx->pan] + soffset;
      }

      qsort(qsorted_samples, mywidth, sizeof(double), compare_doubles);
      int index = (int)((ignore_noise_percentile / 100.0) * mywidth);
      noise_floor_level = qsorted_samples[index] + 3.0;
      // t_print("noise_floor = %f\n", noise_floor_level);
      free(qsorted_samples); // Free memory after use
    }

    noise_floor_level_sum += noise_floor_level;
    anz_messungen++;

    if (anz_messungen >= rx->fps) { // number of runs = rx->fps
      noise_floor_level = noise_floor_level_sum / rx->fps;  // flatten the noise_floor_level
      g_noise_level = (int)noise_floor_level - 3;
      /*
      rx->panadapter_low = autoscale_panadapter_with_offset(noise_floor_level, -5);
        if (rx->panadapter_high <= -50) {
          rx->panadapter_high = -50;
      }
      */
      noise_floor_level_sum = 0.0;
      anz_messungen = 0;
      noisefloor_first_run_flag = 0;
    }

    // Überprüfe, ob 5 Minuten vergangen sind, bevor rx->panadapter_low angepasst wird
    if (noisefloor_first_run_flag
        || difftime(current_time, last_noisefloor_calc_time) >= noisefloor_update_interval) {
      if (abs(autoscale_panadapter_with_offset(noise_floor_level, -5) - rx->panadapter_low) > 10
          || rx->panadapter_low < autoscale_panadapter_with_offset(noise_floor_level, -5)) {
        t_print("%s: rx->panadapter_low: %d noise_floor: %d\n", __func__, rx->panadapter_low,
                autoscale_panadapter_with_offset(noise_floor_level, -5));
        rx->panadapter_low = autoscale_panadapter_with_offset(noise_floor_level, -5) - panadapter_scale_corr_f;
      }

      if (rx->panadapter_high <= -50) {
        rx->panadapter_high = -50;
      }

      // update time of the last calculation
      last_noisefloor_calc_time = current_time;
    }
  }

  if (rx->panadapter_peaks_on != 0) {
    int num_peaks = rx->panadapter_num_peaks;
    /*
    gboolean peaks_in_passband = TRUE;

    if (rx->panadapter_peaks_in_passband_filled != 1) {
      peaks_in_passband = FALSE;
    }

    gboolean hide_noise = TRUE;

    if (rx->panadapter_hide_noise_filled != 1) {
      hide_noise = FALSE;
    }
    */
    gboolean peaks_in_passband = SET(rx->panadapter_peaks_in_passband_filled);
    gboolean hide_noise = SET(rx->panadapter_hide_noise_filled);
    double noise_percentile = (double)rx->panadapter_ignore_noise_percentile;
    int ignore_range_divider = rx->panadapter_ignore_range_divider;
    int ignore_range = (mywidth + ignore_range_divider - 1) / ignore_range_divider; // Round up
    double peaks[num_peaks];
    int peak_positions[num_peaks];

    for (int a = 0; a < num_peaks; a++) {
      peaks[a] = -200;
      peak_positions[a] = 0;
    }

    /*
    // Dynamically allocate a copy of samples for sorting
    double *sorted_samples = malloc(mywidth * sizeof(double));

    if (sorted_samples == NULL) {
      fprintf(stderr, "Memory allocation failed.\n");
      return; // Handle memory allocation failure
    }

    for (int i = 0; i < mywidth; i++) {
      sorted_samples[i] = (double)samples[i + rx->pan] + soffset;
    }
    */
    // Calculate the noise level if needed
    double noise_level = 0.0;

    if (hide_noise) {
      // Dynamically allocate a copy of samples for sorting
      double *sorted_samples = malloc(mywidth * sizeof(double));

      if (sorted_samples != NULL) {
        for (int i = 0; i < mywidth; i++) {
          sorted_samples[i] = (double)samples[i + rx->pan] + soffset;
        }

        qsort(sorted_samples, mywidth, sizeof(double), compare_doubles);
        int index = (int)((noise_percentile / 100.0) * mywidth);
        // noise_level = sorted_samples[index];
        noise_level = sorted_samples[index] + 3.0;
        free(sorted_samples); // Free memory after use
      }
    }

    // free(sorted_samples); // Free memory after use
    // Detect peaks
    double filter_left_bound = peaks_in_passband ? filter_left : 0;
    double filter_right_bound = peaks_in_passband ? filter_right : mywidth;

    for (int i = 1; i < mywidth - 1; i++) {
      if (i >= filter_left_bound && i <= filter_right_bound) {
        double s = (double)samples[i + rx->pan] + soffset;

        // Check if the point is a peak
        if ((!hide_noise || s >= noise_level) && s > samples[i - 1 + rx->pan] && s > samples[i + 1 + rx->pan]) {
          int replace_index = -1;
          int start_range = i - ignore_range;
          int end_range = i + ignore_range;

          // Check if the peak is within the ignore range of any existing peak
          for (int j = 0; j < num_peaks; j++) {
            if (peak_positions[j] >= start_range && peak_positions[j] <= end_range) {
              if (s > peaks[j]) {
                replace_index = j;
                break;
              } else {
                replace_index = -2;
                break;
              }
            }
          }

          // Replace the existing peak if a higher peak is found within the ignore range
          if (replace_index >= 0) {
            peaks[replace_index] = s;
            peak_positions[replace_index] = i;
          }
          // Add the peak if no peaks are found within the ignore range
          else if (replace_index == -1) {
            // Find the index of the lowest peak
            int lowest_peak_index = 0;

            for (int j = 1; j < num_peaks; j++) {
              if (peaks[j] < peaks[lowest_peak_index]) {
                lowest_peak_index = j;
              }
            }

            // Replace the lowest peak if the current peak is higher
            if (s > peaks[lowest_peak_index]) {
              peaks[lowest_peak_index] = s;
              peak_positions[lowest_peak_index] = i;
            }
          }
        }
      }
    }

    // Sort peaks in descending order
    for (int i = 0; i < num_peaks - 1; i++) {
      for (int j = i + 1; j < num_peaks; j++) {
        if (peaks[i] < peaks[j]) {
          double temp_peak = peaks[i];
          peaks[i] = peaks[j];
          peaks[j] = temp_peak;
          int temp_pos = peak_positions[i];
          peak_positions[i] = peak_positions[j];
          peak_positions[j] = temp_pos;
        }
      }
    }

    // Draw peak values on the chart
    // #define COLOUR_PAN_TEXT 1.0, 1.0, 1.0, 1.0 // Define white color with full opacity
    cairo_set_source_rgba(cr, COLOUR_WHITE);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    double previous_text_positions[num_peaks][2]; // Store previous text positions (x, y)

    for (int j = 0; j < num_peaks; j++) {
      previous_text_positions[j][0] = -1; // Initialize x positions
      previous_text_positions[j][1] = -1; // Initialize y positions
    }

    for (int j = 0; j < num_peaks; j++) {
      if (peak_positions[j] > 0) {
        char peak_label[32];

        if (active_receiver->panadapter_peaks_as_smeter) {
          snprintf(peak_label, sizeof(peak_label), "%s", dbm2smeter[get_SWert((int)(peaks[j]))]);
        } else {
          snprintf(peak_label, sizeof(peak_label), "%d dBm", (int)peaks[j]);
        }

        cairo_text_extents_t extents;
        cairo_text_extents(cr, peak_label, &extents);
        // Calculate initial text position: slightly above the peak
        double text_x = peak_positions[j];
        double text_y = floor((rx->panadapter_high - peaks[j])
                              * (double)myheight
                              / (rx->panadapter_high - rx->panadapter_low)) - 5;

        // Ensure text stays within the drawing area
        if (text_y < extents.height) {
          text_y = extents.height; // Push text down to fit inside the top boundary
        }

        // Adjust position to avoid overlap with previous labels
        for (int k = 0; k < j; k++) {
          double prev_x = previous_text_positions[k][0];
          double prev_y = previous_text_positions[k][1];

          if (prev_x >= 0 && prev_y >= 0) {
            double distance_x = fabs(text_x - prev_x);
            double distance_y = fabs(text_y - prev_y);

            if (distance_y < extents.height && distance_x < extents.width) {
              // Try moving vertically first
              if (text_y + extents.height < myheight) {
                text_y += extents.height + 5; // Move below
              } else if (text_y - extents.height > 0) {
                text_y -= extents.height + 5; // Move above
              } else {
                // Move horizontally if no vertical space is available
                if (text_x + extents.width < mywidth) {
                  text_x += extents.width + 5; // Move right
                } else if (text_x - extents.width > 0) {
                  text_x -= extents.width + 5; // Move left
                }
              }
            }
          }
        }

        // Draw text
        cairo_move_to(cr, text_x - (extents.width / 2.0), text_y);
        cairo_show_text(cr, peak_label);
        // Store current text position for overlap checks
        previous_text_positions[j][0] = text_x;
        previous_text_positions[j][1] = text_y;
      }
    }
  }

  if (rx->id == 0) {
    display_panadapter_messages(cr, mywidth, rx->fps);
  }

  //
  // For horizontal stacking, draw a vertical separator,
  // at the right edge of RX1, and at the left
  // edge of RX2.
  //
  if (rx_stack_horizontal && receivers > 1) {
    if (rx->id == 0) {
      cairo_move_to(cr, mywidth - 1, 0);
      cairo_line_to(cr, mywidth - 1, myheight);
    } else {
      cairo_move_to(cr, 0, 0);
      cairo_line_to(cr, 0, myheight);
    }

    cairo_set_source_rgba(cr, COLOUR_PAN_LINE);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
  }

  if (display_info_bar && active_receiver->display_panadapter && !active_receiver->display_waterfall && rx->id == 0
      && !rx_stack_horizontal) {
    // cairo_rectangle(cr, x, y, width, height) -> all as double()
    // X coordinate of the top left corner of the rectangle
    // Y coordinate to the top left corner of the rectangle
    // width of the rectangle
    // height of the rectangle
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.70);
    cairo_rectangle(cr, 0.0, myheight - 30, mywidth, 30.0);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, COLOUR_WHITE);
    // cairo_set_source_rgba(cr, COLOUR_ORANGE);
    // cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    cairo_move_to(cr, (mywidth / 2) + 100, myheight - 10);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
    cairo_move_to(cr, mywidth / 2, myheight - 10);
#endif

    if (can_transmit) {
      cairo_show_text(cr, "[T]une  [b]and  [M]ode  [v]fo  [f]ilter  [n]oise  [a]nf  n[r]  [w]binaural  [e]SNB");
    } else {
      cairo_show_text(cr, "[b]and  [M]ode  [v]fo  [f]ilter  [n]oise  [a]nf  n[r]  [w]binaural  [e]SNB");
    }

    char _text[128];
    cairo_set_source_rgba(cr, COLOUR_ORANGE);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
#endif

    if (can_transmit) {
#if defined (__APPLE__)
      snprintf(_text, sizeof(_text), "[%d] %s", active_receiver->id, truncate_text_3p(transmitter->microphone_name, 36));
#else
      int _audioindex = 0;

      if (n_input_devices > 0) {
        for (int i = 0; i < n_input_devices; i++) {
          if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
            _audioindex = i;
          }
        }

        snprintf(_text, sizeof(_text), "[%d] %s", active_receiver->id, truncate_text_3p(input_devices[_audioindex].description,
                 28));
      } else {
        snprintf(_text, sizeof(_text), "NO AUDIO INPUT DETECTED");
      }

#endif
      cairo_move_to(cr, 10.0, myheight - 10);
      cairo_show_text(cr, _text);
    }

    if (display_solardata) {
      check_and_run(1); // 0=no_log_output, 1=print_to_log
      // g_idle_add(check_and_run_idle_cb, GINT_TO_POINTER(1));
#if defined (__APPLE__)
      cairo_move_to(cr, (mywidth / 4) + 20, myheight - 10);
#else
      cairo_move_to(cr, (mywidth / 4) - 50, myheight - 10);
#endif

      if (sunspots != -1) {
        snprintf(_text, sizeof(_text), "SN:%d SFI:%d A:%d K:%d X:%s GmF:%s", sunspots, solar_flux, a_index, k_index, xray,
                 geomagfield);
      } else {
        snprintf(_text, sizeof(_text), " ");
      }

      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, _text);
    }
  }

  cairo_destroy (cr);
  gtk_widget_queue_draw (rx->panadapter);
}

void rx_panadapter_init(RECEIVER * rx, int width, int height) {
  rx->panadapter_surface = NULL;
  rx->panadapter = gtk_drawing_area_new ();
  gtk_widget_set_size_request (rx->panadapter, width, height);
  /* Signals used to handle the backing surface */
  g_signal_connect (rx->panadapter, "draw",
                    G_CALLBACK (panadapter_draw_cb), rx);
  g_signal_connect (rx->panadapter, "configure-event",
                    G_CALLBACK (panadapter_configure_event_cb), rx);
  /* Event signals */
  g_signal_connect (rx->panadapter, "motion-notify-event",
                    G_CALLBACK (panadapter_motion_notify_event_cb), rx);
  g_signal_connect (rx->panadapter, "button-press-event",
                    G_CALLBACK (panadapter_button_press_event_cb), rx);
  g_signal_connect (rx->panadapter, "button-release-event",
                    G_CALLBACK (panadapter_button_release_event_cb), rx);
  g_signal_connect(rx->panadapter, "scroll_event",
                   G_CALLBACK(panadapter_scroll_event_cb), rx);
  /* Ask to receive events the drawing area doesn't normally
   * subscribe to. In particular, we need to ask for the
   * button press and motion notify events that want to handle.
   */
  gtk_widget_set_events (rx->panadapter, gtk_widget_get_events (rx->panadapter)
                         | GDK_BUTTON_PRESS_MASK
                         | GDK_BUTTON_RELEASE_MASK
                         | GDK_BUTTON1_MOTION_MASK
                         | GDK_SCROLL_MASK
                         | GDK_POINTER_MOTION_MASK
                         | GDK_POINTER_MOTION_HINT_MASK);
}
void display_panadapter_messages(cairo_t *cr, int width, unsigned int fps) {
  char text[64];
  static unsigned int msg_cycle = 0;

  if (display_warnings) {
    //
    // Sequence errors
    // ADC overloads
    // TX FIFO under- and overruns
    // high SWR warning
    //
    // Are shown on display for 2 seconds
    //
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE4);

    if (sequence_errors != 0) {
      static unsigned int sequence_error_count = 0;
      cairo_move_to(cr, 100.0, 50.0);
      cairo_set_source_rgba(cr, COLOUR_ORANGE);
      cairo_show_text(cr, "UDP Stream Sequence Error");
      cairo_set_source_rgba(cr, COLOUR_ALARM);
      sequence_error_count++;

      if (sequence_error_count >= 2 * fps) {
        sequence_errors = 0;
        sequence_error_count = 0;
      }
    }

    if (adc0_overload || adc1_overload) {
      static unsigned int adc_error_count = 0;
      cairo_move_to(cr, 100.0, 70.0);

      if (adc0_overload && !adc1_overload) {
#if defined (__AUTOG__)

        if (!autogain_enabled && (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2)) {
          cairo_set_source_rgba(cr, COLOUR_ALARM);
          cairo_show_text(cr, "ADC0 OVF » Decrease RxPGA Gain !");
        } else if (active_receiver->panadapter_ovf_on && autogain_enabled && (device == DEVICE_HERMES_LITE2
                   || device == NEW_DEVICE_HERMES_LITE2)) {
          cairo_set_source_rgba(cr, COLOUR_ALARM);
          cairo_show_text(cr, "ADC0 OVF");
        }

#else
        cairo_show_text(cr, "ADC0 overload");
#endif
      }

      cairo_set_source_rgba(cr, COLOUR_ALARM);

      if (adc1_overload && !adc0_overload) {
        cairo_show_text(cr, "ADC1 overload");
      }

      if (adc0_overload && adc1_overload) {
        cairo_show_text(cr, "ADC0+1 overload");
      }

      adc_error_count++;
#if defined (__AUTOG__)

      if (!autogain_enabled && adc_error_count > 2 * fps) {
        adc_error_count = 0;
        adc0_overload = 0;
        adc1_overload = 0;
#ifdef USBOZY
        mercury_overload[0] = 0;
        mercury_overload[1] = 0;
#endif
      } else if (adc_error_count > 1 * fps) {
        adc_error_count = 0;
        adc0_overload = 0;
        adc1_overload = 0;
#ifdef USBOZY
        mercury_overload[0] = 0;
        mercury_overload[1] = 0;
#endif
      }

#else

      if (adc_error_count > 2 * fps) {
        adc_error_count = 0;
        adc0_overload = 0;
        adc1_overload = 0;
#ifdef USBOZY
        mercury_overload[0] = 0;
        mercury_overload[1] = 0;
#endif
      }

#endif
    }

    if (high_swr_seen) {
      static unsigned int swr_protection_count = 0;
      cairo_move_to(cr, 100.0, 90.0);
      snprintf(text, sizeof(text), "! High SWR");
      cairo_show_text(cr, text);
      swr_protection_count++;

      if (swr_protection_count >= 3 * fps) {
        high_swr_seen = 0;
        swr_protection_count = 0;
      }
    }

    static unsigned int tx_fifo_count = 0;

    if (tx_fifo_underrun) {
      cairo_move_to(cr, 100.0, 110.0);
      cairo_show_text(cr, "TX Underrun");
      tx_fifo_count++;
    }

    if (tx_fifo_overrun) {
      cairo_move_to(cr, 100.0, 130.0);
      cairo_show_text(cr, "TX Overrun");
      tx_fifo_count++;
    }

    if (tx_fifo_count >= 2 * fps) {
      tx_fifo_underrun = 0;
      tx_fifo_overrun = 0;
      tx_fifo_count = 0;
    }
  }

  char _text[128];

  if (can_transmit && !display_info_bar && active_receiver->display_panadapter && !rx_stack_horizontal) {
    cairo_set_source_rgba(cr, COLOUR_ORANGE);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
#endif
    cairo_move_to(cr, 375.0, 30.0);
#if defined (__APPLE__)
    snprintf(_text, sizeof(_text), "%s", transmitter->microphone_name);
#else
    int _audioindex = 0;

    if (n_input_devices > 0) {
      for (int i = 0; i < n_input_devices; i++) {
        if (strcmp(transmitter->microphone_name, input_devices[i].name) == 0) {
          _audioindex = i;
        }
      }

      snprintf(_text, sizeof(_text), "%s", input_devices[_audioindex].description);
    } else {
      snprintf(_text, sizeof(_text), "NO AUDIO INPUT DETECTED");
    }

#endif
    cairo_show_text(cr, _text); // show onscreen if status bar switched off
  }

  if (strcmp(own_callsign, "YOUR_CALLSIGN") != 0) {
    cairo_move_to(cr, 60, 30);
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, 18);

    if (strcmp(own_locator, "JO01AA") != 0) {
      snprintf(_text, sizeof(_text), "%s - %s", own_callsign, own_locator);
    } else {
      snprintf(_text, sizeof(_text), "%s", own_callsign);
    }

    cairo_show_text(cr, _text);
  }

  // show RX200 data
  cairo_select_font_face(cr, DISPLAY_FONT_UDP_B, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
  cairo_set_source_rgba(cr, COLOUR_WHITE);

  if (can_transmit && display_clock) {
    if (rx200_udp_valid) {
      double rx200_x = 0.0;
      double rt_rx200_y = 15.0;
      double rt_rx200_w = 305.0;
      double rt_rx200_h = 60.0;

      if (display_wmap) {
        if (can_transmit && radio_is_transmitting()) {
          cairo_set_source_rgba(cr, 38.0 / 255, 38.0 / 255, 38.0 / 255, 0.80 ); // Hintergrund
        } else {
          cairo_set_source_rgba(cr, 9.0 / 255, 57.0 / 255, 88.0 / 255, 0.80); // Hintergrund
        }
      } else {
        cairo_set_source_rgba(cr, 38.0 / 255, 38.0 / 255, 38.0 / 255, 0.80); // Hintergrund
      }

      cairo_rectangle(cr, width - rt_rx200_w, rt_rx200_y, rt_rx200_w, rt_rx200_h); // x, y, Breite, Höhe
      cairo_fill(cr);
      cairo_set_source_rgba(cr, COLOUR_WHITE);
      snprintf(_text, sizeof(_text), "Fwd:");
      cairo_move_to(cr, width - 300, 30.0);
      cairo_show_text(cr, _text);
      snprintf(_text, sizeof(_text), "Ref:");
      cairo_move_to(cr, width - 300, 50.0);
      cairo_show_text(cr, _text);
      cairo_text_extents_t rx200_extents;
      snprintf(_text, sizeof(_text), "%s W", g_rx200_data[0]);
      cairo_text_extents(cr, _text, &rx200_extents);
      rx200_x = width - 200.0 - (rx200_extents.width + rx200_extents.x_bearing);
      cairo_move_to(cr, rx200_x, 30.0);
      cairo_show_text(cr, _text);
      snprintf(_text, sizeof(_text), "%s W", g_rx200_data[1]);
      cairo_text_extents(cr, _text, &rx200_extents);
      rx200_x = width - 200.0 - (rx200_extents.width + rx200_extents.x_bearing);
      cairo_move_to(cr, rx200_x, 50.0);
      cairo_show_text(cr, _text);
      snprintf(_text, sizeof(_text), "%s", g_rx200_data[3]);
      cairo_move_to(cr, width - 190.0, 30.0);
      cairo_show_text(cr, _text);

      if (!(strcmp(g_rx200_data[2], "0.0") == 0)) {
        snprintf(_text, sizeof(_text), "SWR:");
      } else {
        snprintf(_text, sizeof(_text), " ");
      }

      cairo_move_to(cr, width - 190.0, 50.0);
      cairo_show_text(cr, _text);

      if (!(strcmp(g_rx200_data[2], "0.0") == 0)) {
        snprintf(_text, sizeof(_text), "%s:1", g_rx200_data[2]);
      } else {
        snprintf(_text, sizeof(_text), " ");
      }

      cairo_text_extents(cr, _text, &rx200_extents);
      rx200_x = width - 90.0 - (rx200_extents.width + rx200_extents.x_bearing);
      cairo_move_to(cr, rx200_x, 50.0);
      cairo_show_text(cr, _text);
    } else {
      snprintf(_text, sizeof(_text), " ");
      cairo_move_to(cr, width - 300.0, 30.0);
      cairo_show_text(cr, _text);
      cairo_move_to(cr, width - 300.0, 50.0);
      cairo_show_text(cr, _text);
      cairo_move_to(cr, width - 190.0, 50.0);
      cairo_show_text(cr, _text);
      cairo_move_to(cr, width - 190.0, 30.0);
      get_local_time(zeitString, sizeof(zeitString));
      snprintf(_text, sizeof(_text), "%s", zeitString);
      cairo_show_text(cr, _text);
    }
  }

#ifdef SOAPYSDR

  if (!can_transmit && display_clock) {
    double rt_rx_y = 15.0;
    double rt_rx_w = 255.0;
    double rt_rx_h = 60.0;

    if (display_wmap) {
      cairo_set_source_rgba(cr, 9.0 / 255, 57.0 / 255, 88.0 / 255, 0.80); // Hintergrund
    } else {
      cairo_set_source_rgba(cr, 38.0 / 255, 38.0 / 255, 38.0 / 255, 0.80); // Hintergrund
    }

    cairo_rectangle(cr, width - rt_rx_w, rt_rx_y, rt_rx_w, rt_rx_h); // x, y, Breite, Höhe
    cairo_fill(cr);
    cairo_set_source_rgba(cr, COLOUR_WHITE);
    cairo_move_to(cr, width - 250.0, 30.0);
    get_local_time(zeitString, sizeof(zeitString));
    snprintf(_text, sizeof(_text), "%s", zeitString);
    cairo_show_text(cr, _text);

    if (device == SOAPYSDR_USB_DEVICE && radio->info.soapy.rx_gains > 0 && strcmp(radio->name, "sdrplay") == 0) {
      if (msg_cycle == 0) {
        val_agcsetpoint = soapy_protocol_get_agc_setpoint(active_receiver);
        snprintf(txt_ifgr, sizeof(txt_ifgr), "%s", radio->info.soapy.rx_gain[index_if_gain()]);
        snprintf(txt_rfgr, sizeof(txt_rfgr), "%s", radio->info.soapy.rx_gain[index_rf_gain()]);
        snprintf(txt_currGain, sizeof(txt_currGain), "CURRENT");
        val_ifgr = (int)soapy_protocol_get_gain_element(active_receiver, txt_ifgr);
        val_rfgr = (int)soapy_protocol_get_gain_element(active_receiver, txt_rfgr);
        val_currGain = (int)soapy_protocol_get_gain_element(active_receiver, txt_currGain);
        val_biast = soapy_protocol_get_bias_t(active_receiver);
        t_print("%s: current Gain = %d\n", __func__, (int)soapy_protocol_get_gain_element(active_receiver, txt_currGain));
      }

      if (adc[active_receiver->adc].agc) {
        snprintf(_text, sizeof(_text), "HW-AGC: ON");
        cairo_move_to(cr, width - 250.0, 50.0);
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        cairo_show_text(cr, _text);
        //---------------------------------------------------
        snprintf(_text, sizeof(_text), "(%ddbFS)", val_agcsetpoint);
        cairo_move_to(cr, width - 110.0, 50.0);
        cairo_show_text(cr, _text);
        //---------------------------------------------------
        cairo_set_source_rgba(cr, COLOUR_SHADE);
        snprintf(_text, sizeof(_text), "%s:%ddb", txt_ifgr, val_ifgr);
      } else {
        snprintf(_text, sizeof(_text), "HW-AGC: OFF");
        cairo_move_to(cr, width - 250.0, 50.0);
        cairo_set_source_rgba(cr, COLOUR_SHADE);
        cairo_show_text(cr, _text);
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        snprintf(_text, sizeof(_text), "%s:%ddb", txt_ifgr, val_ifgr);
      }

      cairo_move_to(cr, width - 110.0, 70.0);
      cairo_show_text(cr, _text);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      snprintf(_text, sizeof(_text), "%s:%d", txt_rfgr, val_rfgr);
      cairo_move_to(cr, width - 180.0, 70.0);
      cairo_show_text(cr, _text);
      cairo_set_source_rgba(cr, COLOUR_WHITE);
      snprintf(_text, sizeof(_text), "G:%ddb", val_currGain);
      cairo_move_to(cr, width - 250.0, 70.0);
      cairo_show_text(cr, _text);

      if (val_biast) {
        cairo_set_source_rgba(cr, COLOUR_ATTN);
        snprintf(_text, sizeof(_text), "BIAS");
      } else {
        cairo_set_source_rgba(cr, COLOUR_SHADE);
        snprintf(_text, sizeof(_text), "BIAS");
      }

      cairo_move_to(cr, width - 45.0, 30.0);
      cairo_show_text(cr, _text);
      //----------------------------------------------------
    }
  }

#endif

  if (can_transmit && display_clock) {
    if (lpf_udp_valid) {
      if (strcasecmp(g_lpf_data[5], "true") == 0) {
        cairo_set_source_rgba(cr, COLOUR_ORANGE);
      } else {
        cairo_set_source_rgba(cr, COLOUR_WHITE);
      }

      cairo_move_to(cr, width - 300.0, 70.0);
      snprintf(_text, sizeof(_text), "LPF %s", g_lpf_data[0]);
      cairo_show_text(cr, _text);
    } else {
      snprintf(_text, sizeof(_text), " ");
      cairo_move_to(cr, width - 300.0, 70.0);
      cairo_show_text(cr, _text);
    }
  }

#ifdef __AH4IOB__

  if (can_transmit && device == DEVICE_HERMES_LITE2 && display_ah4 && !rx_stack_horizontal
      && active_receiver->display_panadapter) {
    cairo_set_source_rgb(cr, 38.0 / 255, 38.0 / 255, 38.0 / 255); // Hintergrund
    cairo_rectangle(cr, width - 445.0, 15.0, 135.0, 20.0); // x, y, Breite, Höhe
    cairo_fill_preserve(cr);   // füllt, Pfad bleibt erhalten
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);  // nur Rand, keine Füllung
    cairo_move_to(cr, width - 440.0, 30.0);
    cairo_set_font_size(cr, 14);
    unsigned char ah4s = hl2_iob_get_antenna_tuner_status();
    // unsigned char ah4s = 0xEE; // for testing only
    char ah4_state[16];

    if (ah4s == 0x00) {
      snprintf(ah4_state, sizeof(ah4_state), "READY");
    } else if (ah4s == 0xEE) {
      snprintf(ah4_state, sizeof(ah4_state), "RF needed");
    } else if (ah4s >= 0xF0) {
      cairo_set_source_rgba(cr, GRAD_CORAL);
      snprintf(ah4_state, sizeof(ah4_state), "ERROR 0x%02X", ah4s);
    } else {
      snprintf(ah4_state, sizeof(ah4_state), "STATE 0x%02X", ah4s);
    }

    snprintf(_text, sizeof(_text), "AH4: %s", ah4_state);
    cairo_show_text(cr, _text);
  }

#endif

  if (TxInhibit) {
    cairo_set_source_rgba(cr, COLOUR_ALARM);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    cairo_move_to(cr, 100.0, 30.0);
    cairo_show_text(cr, "TX Inhibit");
  }

  if (display_pacurr && radio_is_transmitting() && !TxInhibit) {
    double v;  // value
    int flag;  // 0: dont, 1: do
    static unsigned int count = 0;
    //
    // Display a maximum value twice per second
    // to avoid flicker
    //
    static double max1 = 0.0;
    static double max2 = 0.0;
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);

    //
    // Supply voltage or PA temperature
    //
    switch (device) {
    case DEVICE_HERMES_LITE2:
      // (3.26*(ExPwr/4096.0) - 0.5) /0.01
      v = 0.0795898 * exciter_power - 50.0;

      if (v < 0) { v = 0; }

      if (count == 0) { max1 = v; }

      snprintf(text, sizeof(text), "%0.0f°C", max1);
      flag = 1;
      break;

    case DEVICE_ORION2:
    case NEW_DEVICE_ORION2:
    case NEW_DEVICE_SATURN:
      // 5 (ADC0_avg / 4095 )* VDiv, VDiv = (22.0 + 1.0) / 1.1
      v = 0.02553 * ADC0;

      if (v < 0) { v = 0; }

      if (count == 0) { max1 = v; }

      snprintf(text, sizeof(text), "%0.1fV", max1);
      flag = 1;
      break;

    default:
      flag = 0;
      break;
    }

    if (flag) {
      cairo_move_to(cr, 250.0, 30.0);
      cairo_show_text(cr, text);
    }

    //
    // PA current
    //
    switch (device) {
    case DEVICE_HERMES_LITE2:
      // 1270 ((3.26f * (ADC0 / 4096)) / 50) / 0.04
      v = 0.505396 * ADC0;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, sizeof(text), "%0.0fmA", max2);
      flag = 1;
      break;

    case DEVICE_ORION2:
    case NEW_DEVICE_ORION2:
      // ((ADC1*5000)/4095 - Voff)/Sens, Voff = 360, Sens = 120
      v = 0.0101750 * ADC1 - 3.0;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, sizeof(text), "%0.1fA", max2);
      flag = 1;
      break;

    case NEW_DEVICE_SATURN:
      // ((ADC1*5000)/4095 - Voff)/Sens, Voff = 0, Sens = 66.23
      v = 0.0184358 * ADC1;

      if (v < 0) { v = 0; }

      if (count == 0) { max2 = v; }

      snprintf(text, sizeof(text), "%0.1fA", max2);
      flag = 1;
      break;

    default:
      flag = 0;
      break;
    }

    if (flag) {
      cairo_move_to(cr, 300.0, 30.0);
      cairo_show_text(cr, text);
    }

    if (++count >= fps / 2) { count = 0; }
  }

  if (capture_state == CAP_RECORDING || capture_state == CAP_XMIT || capture_state == CAP_REPLAY
      || capture_state == CAP_AVAIL) {
    static unsigned int cap_count = 0;
    double cx = (double) width - 100.0;
    double cy = 60.0;
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, cx, cy +  5.0);
    cairo_line_to(cr, cx + 90.0, cy +  5.0);
    cairo_line_to(cr, cx + 90.0, cy + 20.0);
    cairo_line_to(cr, cx, cy + 20.0);
    cairo_line_to(cr, cx, cy +  5.0);

    if (capture_state == CAP_XMIT || capture_state == CAP_REPLAY) {
      cairo_move_to(cr, cx + (90.0 * capture_record_pointer) / capture_max, cy +  5.0);
      cairo_line_to(cr, cx + (90.0 * capture_record_pointer) / capture_max, cy + 20.0);
    }

    cairo_stroke(cr);
    cairo_move_to(cr, cx, cy);

    switch (capture_state) {
    case CAP_RECORDING:
      cairo_show_text(cr, "RECORD");
      cairo_rectangle(cr, cx, cy + 5.0, (90.0 * capture_record_pointer) / capture_max, 15.0);
      cairo_fill(cr);
      break;

    case CAP_REPLAY:
    case CAP_XMIT:
      cairo_set_source_rgba(cr, COLOUR_ALARM);

      if (capture_state == CAP_REPLAY) {
        cairo_show_text(cr, "REPLAY");
      } else {
        cairo_show_text(cr, "TRANSMIT");
      }

      cairo_rectangle(cr, cx + 1.0, cy + 6.0, (90.0 * capture_replay_pointer) / capture_max - 1.0, 13.0);
      cairo_fill(cr);
      break;

    case CAP_AVAIL:
      cairo_show_text(cr, "REC STBY");
      cairo_rectangle(cr, cx, cy + 5.0, (90.0 * capture_record_pointer) / capture_max, 15.0);
      cairo_fill(cr);
      cap_count++;

      if (cap_count > 30 * fps) {
        capture_state = CAP_GOTOSLEEP;
        schedule_action(capture_trigger_action, PRESSED, 0);
        cap_count = 0;
      }

      break;
    }
  }

  msg_cycle++;

  if (msg_cycle >= fps) {
    msg_cycle = 0;
  }
}