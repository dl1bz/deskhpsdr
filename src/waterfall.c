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
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <sys/resource.h>
#ifdef __APPLE__
  #include <mach/mach.h>
  #include <mach/host_info.h>
  #include <mach/mach_host.h>
#endif
#include "radio.h"
#include "main.h"
#include "vfo.h"
#include "band.h"
#include "appearance.h"
#include "audio.h"
#include "toolset.h"
#include "waterfall.h"
#include "rx_panadapter.h"
#include "message.h"

static int colorLowR = 0; // black
static int colorLowG = 0;
static int colorLowB = 0;

static int colorHighR = 255; // yellow
static int colorHighG = 255;
static int colorHighB = 0;


static double process_cpu_load = 0.0;
static double system_cpu_load = 0.0;
static guint performance_timer_id = 0;


#define WATERFALL_3D_MAX_RX 8
#define WATERFALL_3D_DEPTH 80
#define WATERFALL_3D_INTERP 5
#define WATERFALL_3D_TARGET_HZ 8.0

typedef struct {
  float *frames;
  float *smoothed;
  int width;
  int head;
  int count;
  double capture_accumulator;
  long long frequency;
  int pan;
  int zoom;
  int sample_rate;
  gboolean mapping_valid;
} WATERFALL_3D_HISTORY;

static WATERFALL_3D_HISTORY waterfall_3d_history[WATERFALL_3D_MAX_RX];

static void waterfall_3d_reset_history(WATERFALL_3D_HISTORY *h) {
  if (h == NULL) {
    return;
  }
  h->head = 0;
  h->count = 0;
  /* Capture the first generated waterfall row after a reset.  Subsequent
   * captures are phase-accumulated from the current runtime waterfall FPS. */
  h->capture_accumulator = 1.0;
  h->mapping_valid = FALSE;
}

void waterfall_3d_clear(RECEIVER *rx) {
  if (rx == NULL || rx->id < 0 || rx->id >= WATERFALL_3D_MAX_RX) {
    return;
  }
  waterfall_3d_reset_history(&waterfall_3d_history[rx->id]);
  if (rx->pixbuf != NULL) {
    int height = gdk_pixbuf_get_height(rx->pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(rx->pixbuf);
    memset(gdk_pixbuf_get_pixels(rx->pixbuf), 0, (size_t) height * rowstride);
  }
}

static gboolean waterfall_3d_prepare(WATERFALL_3D_HISTORY *h, int width) {
  if (h->frames != NULL && h->smoothed != NULL && h->width == width) {
    return TRUE;
  }
  size_t frame_bytes = (size_t) width * WATERFALL_3D_DEPTH * sizeof(float);
  float *frames = malloc(frame_bytes);
  float *smoothed = malloc(frame_bytes);
  if (frames == NULL || smoothed == NULL) {
    free(frames);
    free(smoothed);
    return FALSE;
  }
  free(h->frames);
  free(h->smoothed);
  h->frames = frames;
  h->smoothed = smoothed;
  h->width = width;
  waterfall_3d_reset_history(h);
  return TRUE;
}

static void waterfall_3d_smooth_depth(WATERFALL_3D_HISTORY *h) {
  if (h == NULL || h->frames == NULL || h->smoothed == NULL ||
      h->width <= 0 || h->count <= 0) {
    return;
  }
  /* Smooth only along the time/depth axis; frequency detail is unchanged. */
  for (int age = 0; age < h->count; age++) {
    int idx = h->head - 1 - age;
    int newer_idx = h->head - 1 - (age > 0 ? age - 1 : age);
    int older_idx = h->head - 1 - (age + 1 < h->count ? age + 1 : age);
    while (idx < 0) { idx += WATERFALL_3D_DEPTH; }
    while (newer_idx < 0) { newer_idx += WATERFALL_3D_DEPTH; }
    while (older_idx < 0) { older_idx += WATERFALL_3D_DEPTH; }
    const float *newer = h->frames + (size_t)newer_idx * h->width;
    const float *current = h->frames + (size_t)idx * h->width;
    const float *older = h->frames + (size_t)older_idx * h->width;
    float *out = h->smoothed + (size_t)idx * h->width;
    for (int x = 0; x < h->width; x++) {
      out[x] = 0.25f * newer[x] + 0.50f * current[x] + 0.25f * older[x];
    }
  }
}

static inline void waterfall_3d_rgb(float sample, float low, float high,
                                    unsigned char *r, unsigned char *g, unsigned char *b) {
  float p = (sample - low) / (high - low);
  p = CLAMP(p, 0.0f, 1.0f);
  /* Give weak 3D signals more colour separation without moving the
   * upper part of the waterfall transfer function.  Keep 0.30 and above
   * bit-for-bit on the existing scale; only expand the lower 30 %. */
  if (p > 0.0f && p < 0.30f) {
    p = 0.30f * powf(p / 0.30f, 0.75f);
  }
  /* Use the same level-to-colour transfer function as the conventional
   * waterfall so equal signal levels have the same visual intensity. */
  if (p < 0.222222f) {
    float q = p * 4.5f;
    *r = (unsigned char)((1.0f - q) * colorLowR);
    *g = (unsigned char)((1.0f - q) * colorLowG);
    *b = (unsigned char)(colorLowB + q * (255 - colorLowB));
  } else if (p < 0.333333f) {
    float q = (p - 0.222222f) * 9.0f;
    *r = 0;
    *g = (unsigned char)(q * 255.0f);
    *b = 255;
  } else if (p < 0.444444f) {
    float q = (p - 0.333333f) * 9.0f;
    *r = 0;
    *g = 255;
    *b = (unsigned char)((1.0f - q) * 255.0f);
  } else if (p < 0.555555f) {
    float q = (p - 0.444444f) * 9.0f;
    *r = (unsigned char)(q * 255.0f);
    *g = 255;
    *b = 0;
  } else if (p < 0.777777f) {
    float q = (p - 0.555555f) * 4.5f;
    *r = 255;
    *g = (unsigned char)((1.0f - q) * 255.0f);
    *b = 0;
  } else if (p < 0.888888f) {
    float q = (p - 0.777777f) * 9.0f;
    *r = 255;
    *g = 0;
    *b = (unsigned char)(q * 255.0f);
  } else {
    float q = (p - 0.888888f) * 9.0f;
    *r = (unsigned char)((0.75f + 0.25f * (1.0f - q)) * 255.0f);
    *g = (unsigned char)(q * 255.0f * 0.5f);
    *b = 255;
  }
}

static inline void waterfall_3d_put_pixel(unsigned char *pixels, int rowstride,
    int width, int height, int x, int y,
    unsigned char r, unsigned char g, unsigned char b) {
  if (x < 0 || x >= width || y < 0 || y >= height) {
    return;
  }
  unsigned char *p = pixels + (size_t)y * rowstride + (size_t)x * 3;
  p[0] = r;
  p[1] = g;
  p[2] = b;
}

static inline float waterfall_3d_sample_at(const float *frame, int width, double x) {
  if (x <= 0.0) {
    return frame[0];
  }
  if (x >= (double)(width - 1)) {
    return frame[width - 1];
  }
  int i = (int)x;
  double f = x - (double)i;
  return (float)((1.0 - f) * frame[i] + f * frame[i + 1]);
}

static void waterfall_3d_render(RECEIVER *rx, unsigned char *pixels, int rowstride,
                                int width, int terrain_height, const float *samples,
                                int pan, float soffset, float low, float high,
                                long long frequency) {
  if (rx == NULL || pixels == NULL || samples == NULL || terrain_height < 24 ||
      rx->id < 0 || rx->id >= WATERFALL_3D_MAX_RX || high <= low) {
    return;
  }
  WATERFALL_3D_HISTORY *h = &waterfall_3d_history[rx->id];
  if (!waterfall_3d_prepare(h, width)) {
    return;
  }
  if (h->mapping_valid &&
      (h->frequency != frequency || h->pan != pan || h->zoom != rx->zoom ||
       h->sample_rate != rx->sample_rate)) {
    waterfall_3d_reset_history(h);
  }
  h->frequency = frequency;
  h->pan = pan;
  h->zoom = rx->zoom;
  h->sample_rate = rx->sample_rate;
  h->mapping_valid = TRUE;
  /* waterfall_3d_render() is called once for each waterfall row that is
   * actually generated.  Keep the 3D history at a constant 8 Hz target while
   * remaining locked to those real rows.  The phase accumulator follows
   * runtime FPS changes immediately and introduces no independent timer. */
  if (rx->fps <= 0) {
    return;
  }
  double capture_step = WATERFALL_3D_TARGET_HZ / (double)rx->fps;
  if (capture_step > 1.0) {
    capture_step = 1.0;
  }
  h->capture_accumulator += capture_step;
  if (h->capture_accumulator < 1.0) {
    return;
  }
  h->capture_accumulator -= 1.0;
  float *dst = h->frames + (size_t)h->head * width;
  memcpy(dst, samples + pan, (size_t)width * sizeof(float));
  h->head = (h->head + 1) % WATERFALL_3D_DEPTH;
  if (h->count < WATERFALL_3D_DEPTH) {
    h->count++;
  }
  waterfall_3d_smooth_depth(h);
  memset(pixels, 0, (size_t)terrain_height * rowstride);
  if (h->count < 2) {
    return;
  }
  /* Perspective model:
   *   - newest real history slice uses the full width at the front;
   *   - older slices recede upward and shrink symmetrically towards centre;
   *   - 80 captured FFT frames keep the Z history fine at the 8 Hz target;
   *   - four linearly interpolated virtual slices are inserted between every
   *     pair of captured frames, yielding up to 396 visible depth planes;
   *   - every display column is sampled with linear interpolation, so there
   *     is no x decimation and no coarse horizontal stair-stepping;
   *   - ribbons are rasterised directly into the RGB pixbuf. */
  const double depth_span = terrain_height * 0.62;
  const double amplitude_span = terrain_height * 0.82;
  const double perspective_shrink = 0.18;   /* oldest slice retains 82% width */
  const double signal_range = (double)high - (double)low;
  const double centre = 0.5 * (double)(width - 1);
  for (int age = h->count - 2; age >= 0; age--) {
    int newer_idx = h->head - 1 - age;
    int older_idx = newer_idx - 1;
    while (newer_idx < 0) {
      newer_idx += WATERFALL_3D_DEPTH;
    }
    while (older_idx < 0) {
      older_idx += WATERFALL_3D_DEPTH;
    }
    const float *newer = h->smoothed + (size_t)newer_idx * width;
    const float *older = h->smoothed + (size_t)older_idx * width;
    double newer_real_depth = WATERFALL_3D_DEPTH > 1 ?
                              (double)age / (double)(WATERFALL_3D_DEPTH - 1) : 0.0;
    double older_real_depth = WATERFALL_3D_DEPTH > 1 ?
                              (double)(age + 1) / (double)(WATERFALL_3D_DEPTH - 1) : 0.0;
    /* Render from the older sub-segment towards the newer one.  This preserves
     * back-to-front occlusion while inserting three virtual depth planes. */
    for (int sub = WATERFALL_3D_INTERP - 1; sub >= 0; sub--) {
      double t_near = (double)sub / (double)WATERFALL_3D_INTERP;
      double t_far = (double)(sub + 1) / (double)WATERFALL_3D_INTERP;
      double near_depth = newer_real_depth +
                          (older_real_depth - newer_real_depth) * t_near;
      double far_depth = newer_real_depth +
                         (older_real_depth - newer_real_depth) * t_far;
      double near_scale = 1.0 - perspective_shrink * near_depth;
      double far_scale = 1.0 - perspective_shrink * far_depth;
      double near_half = centre * near_scale;
      double far_half = centre * far_scale;
      double near_left = centre - near_half;
      double near_right = centre + near_half;
      double far_left = centre - far_half;
      double far_right = centre + far_half;
      int near_base = terrain_height - 2 - (int)lround(near_depth * depth_span);
      int far_base = terrain_height - 2 - (int)lround(far_depth * depth_span);
      double age_gain = 1.0 - 0.15 * near_depth;
      int x0 = MAX(0, (int)ceil(MAX(near_left, far_left)));
      int x1 = MIN(width - 1, (int)floor(MIN(near_right, far_right)));
      if (x1 < x0) {
        continue;
      }
      for (int x = x0; x <= x1; x++) {
        double source_near = ((double)x - near_left) / near_scale;
        double source_far = ((double)x - far_left) / far_scale;
        float sn0 = waterfall_3d_sample_at(newer, width, source_near);
        float so0 = waterfall_3d_sample_at(older, width, source_near);
        float sn1 = waterfall_3d_sample_at(newer, width, source_far);
        float so1 = waterfall_3d_sample_at(older, width, source_far);
        float s_near = (float)((1.0 - t_near) * sn0 + t_near * so0) + soffset;
        float s_far = (float)((1.0 - t_far) * sn1 + t_far * so1) + soffset;
        double nn = CLAMP(((double)s_near - low) / signal_range, 0.0, 1.0);
        double no = CLAMP(((double)s_far - low) / signal_range, 0.0, 1.0);
        int yn = near_base - (int)lround(nn * amplitude_span);
        int yo = far_base - (int)lround(no * amplitude_span);
        yn = CLAMP(yn, 0, terrain_height - 1);
        yo = CLAMP(yo, 0, terrain_height - 1);
        int y0 = MIN(yn, yo);
        int y1 = MAX(yn, yo);
        float colour_sample = MAX(s_near, s_far);
        unsigned char rr, gg, bb;
        waterfall_3d_rgb(colour_sample, low, high, &rr, &gg, &bb);
        rr = (unsigned char)((double)rr * age_gain);
        gg = (unsigned char)((double)gg * age_gain);
        bb = (unsigned char)((double)bb * age_gain);
        int span = y1 - y0;
        for (int py = y0; py <= y1; py++) {
          double t = span > 0 ? (double)(py - y0) / (double)span : 0.0;
          double shade = 0.82 + 0.18 * (1.0 - t);
          waterfall_3d_put_pixel(pixels, rowstride, width, terrain_height,
                                 x, py,
                                 (unsigned char)((double)rr * shade),
                                 (unsigned char)((double)gg * shade),
                                 (unsigned char)((double)bb * shade));
        }
        /* Keep ridges sparse: only real (non-interpolated) slices and only
         * every second one.  The surface stays readable without the previous
         * wire-grid appearance. */
        if (sub == 0 && (age & 1) == 0) {
          waterfall_3d_put_pixel(pixels, rowstride, width, terrain_height,
                                 x, yn, rr, gg, bb);
        }
      }
    }
  }
  /* Draw the newest FFT slice as a solid front face down to the terrain /
   * waterfall boundary.  This gives the foreground spectrum physical mass
   * while the older slices remain visible as the receding surface behind it. */
  {
    int newest_idx = h->head - 1;
    if (newest_idx < 0) {
      newest_idx += WATERFALL_3D_DEPTH;
    }
    const float *front = h->smoothed + (size_t)newest_idx * width;
    const int front_base = terrain_height - 1;
    for (int x = 0; x < width; x++) {
      float sample = front[x] + soffset;
      double norm = CLAMP(((double)sample - low) / signal_range, 0.0, 1.0);
      int ridge_y = front_base - (int)lround(norm * amplitude_span);
      ridge_y = CLAMP(ridge_y, 0, terrain_height - 1);
      unsigned char rr, gg, bb;
      waterfall_3d_rgb(sample, low, high, &rr, &gg, &bb);
      int span = MAX(1, front_base - ridge_y);
      for (int y = ridge_y; y <= front_base; y++) {
        double t = (double)(y - ridge_y) / (double)span;
        /* Keep the ridge at full waterfall intensity, but fade the front
         * face progressively towards the boundary.  This preserves the
         * foreground mass without leaving a bright vertical wall. */
        double shade = 1.0 - 0.60 * t;
        waterfall_3d_put_pixel(pixels, rowstride, width, terrain_height,
                               x, y,
                               (unsigned char)((double)rr * shade),
                               (unsigned char)((double)gg * shade),
                               (unsigned char)((double)bb * shade));
      }
      waterfall_3d_put_pixel(pixels, rowstride, width, terrain_height,
                             x, ridge_y, rr, gg, bb);
    }
  }
}

typedef struct {
  guint64 busy;
  guint64 total;
} CPU_TICKS;

static gboolean read_system_cpu_ticks(CPU_TICKS *ticks) {
#ifdef __APPLE__
  host_cpu_load_info_data_t info;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                      (host_info_t)&info, &count) != KERN_SUCCESS) {
    return FALSE;
  }
  guint64 user = info.cpu_ticks[CPU_STATE_USER];
  guint64 nice = info.cpu_ticks[CPU_STATE_NICE];
  guint64 system = info.cpu_ticks[CPU_STATE_SYSTEM];
  guint64 idle = info.cpu_ticks[CPU_STATE_IDLE];
  ticks->busy = user + nice + system;
  ticks->total = ticks->busy + idle;
  return TRUE;
#else
  FILE *fp = fopen("/proc/stat", "r");
  if (fp == NULL) {
    return FALSE;
  }
  guint64 user = 0, nice = 0, system = 0, idle = 0;
  guint64 iowait = 0, irq = 0, softirq = 0, steal = 0;
  int fields = fscanf(fp, "cpu %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT
                      " %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT
                      " %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT
                      " %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT,
                      &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
  fclose(fp);
  if (fields < 4) {
    return FALSE;
  }
  ticks->busy = user + nice + system + irq + softirq + steal;
  ticks->total = ticks->busy + idle + iowait;
  return TRUE;
#endif
}

static double process_cpu_seconds(void) {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0.0;
  }
  return (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / 1000000.0
         + (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / 1000000.0;
}

static gboolean waterfall_performance_update(gpointer data) {
  (void)data;
  static gboolean initialized = FALSE;
  static CPU_TICKS previous_ticks;
  static double previous_process_seconds = 0.0;
  static gint64 previous_time_us = 0;
  CPU_TICKS current_ticks;
  double current_process_seconds = process_cpu_seconds();
  gint64 current_time_us = g_get_monotonic_time();
  if (read_system_cpu_ticks(&current_ticks)) {
    if (initialized && current_ticks.total > previous_ticks.total) {
      guint64 total_delta = current_ticks.total - previous_ticks.total;
      guint64 busy_delta = current_ticks.busy - previous_ticks.busy;
      system_cpu_load = 100.0 * (double)busy_delta / (double)total_delta;
    }
    previous_ticks = current_ticks;
  }
  if (initialized && current_time_us > previous_time_us) {
    double elapsed = (double)(current_time_us - previous_time_us) / 1000000.0;
    double cpu_delta = current_process_seconds - previous_process_seconds;
    process_cpu_load = 100.0 * cpu_delta / elapsed;
    if (process_cpu_load < 0.0) {
      process_cpu_load = 0.0;
    }
    system_cpu_load = CLAMP(system_cpu_load, 0.0, 100.0);
  }
  previous_process_seconds = current_process_seconds;
  previous_time_us = current_time_us;
  initialized = TRUE;
  for (int i = receivers - 1; i >= 0; i--) {
    if (receiver[i] != NULL && receiver[i]->display_waterfall && receiver[i]->waterfall != NULL) {
      gtk_widget_queue_draw(receiver[i]->waterfall);
      break;
    }
  }
  return G_SOURCE_CONTINUE;
}

static gboolean
waterfall_is_last_visible(const RECEIVER *rx) {
  if (rx == NULL || !rx->display_waterfall) {
    return FALSE;
  }
  for (int i = rx->id + 1; i < receivers; i++) {
    if (receiver[i] != NULL && receiver[i]->display_waterfall) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean waterfall_local_audio_active(void) {
  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL && receiver[i]->local_audio) {
      return TRUE;
    }
  }
  return FALSE;
}

/* Create a new surface of the appropriate size to store our scribbles */
static gboolean
waterfall_configure_event_cb(GtkWidget         *widget,
                             GdkEventConfigure *event,
                             gpointer           data) {
  RECEIVER *rx = (RECEIVER *) data;
  int width = gtk_widget_get_allocated_width(widget);
  int height = gtk_widget_get_allocated_height(widget);
  if (rx->pixbuf != NULL) {
    g_object_unref(rx->pixbuf);
    rx->pixbuf = NULL;
  }
  rx->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
  if (rx->pixbuf == NULL) {
    return TRUE;
  }
  unsigned char *pixels = gdk_pixbuf_get_pixels(rx->pixbuf);
  int rowstride = gdk_pixbuf_get_rowstride(rx->pixbuf);
  memset(pixels, 0, (size_t)height * rowstride);
  return TRUE;
}

/* Redraw the screen from the surface. Note that the ::draw
 * signal receives a ready-to-be-used cairo_t that is already
 * clipped to only draw the exposed areas of the widget
 */
static gboolean
waterfall_draw_cb(GtkWidget *widget,
                  cairo_t   *cr,
                  gpointer   data) {
  const RECEIVER *rx = (RECEIVER *) data;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GtkAllocation allocation;
  gtk_widget_get_allocation(rx->waterfall, &allocation);
  int b_width = allocation.width;
  int b_height = allocation.height;
  int box_height = 30;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  gdk_cairo_set_source_pixbuf(cr, rx->pixbuf, 0, 0);
  cairo_paint(cr);  // vor dem Zeichnen der Box aufrufen, sinst wird der pixbuf überschrieben !
  /* Keep the RX frequency reference visible in the conventional 2D
   * waterfall.  Match the panadapter cursor geometry (including CTUN/RIT and
   * the CW sidetone displacement), but deliberately do not draw through the
   * 3D terrain area. */
  if (b_width > 0 && b_height > 0 && rx->sample_rate > 0 && rx->zoom > 0) {
    int vfo_id = (diversity_enabled && !radio_is_transmitting() && !radio_ptt && rx->id == 1) ? 0 : rx->id;
    double marker_hz_per_pixel = (double)rx->sample_rate / ((double)b_width * rx->zoom);
    double marker_x = ((double)rx->sample_rate * 0.5 / marker_hz_per_pixel) - (double)rx->pan;
    long long marker_offset = vfo[vfo_id].ctun ? vfo[vfo_id].offset
                              : (vfo[vfo_id].rit_enabled ? vfo[vfo_id].rit : 0);
    int marker_mode = vfo[vfo_id].mode;
    if (marker_mode == modeCWU) {
      marker_x += (double)cw_keyer_sidetone_frequency / marker_hz_per_pixel;
    } else if (marker_mode == modeCWL) {
      marker_x -= (double)cw_keyer_sidetone_frequency / marker_hz_per_pixel;
    }
    marker_x += (double)marker_offset / marker_hz_per_pixel;
    marker_x = CLAMP(marker_x, 0.0, (double)b_width - 1.0);
    int marker_y = rx->display_3d ? (b_height * 40) / 100 : 0;
    if (b_height - marker_y < 24) {
      marker_y = 0;
    }
    cairo_set_source_rgba(cr, COLOUR_WHITE);
    cairo_set_line_width(cr, PAN_LINE_EXTRA);
    /* In 3D mode use a separate marker at the front of the terrain.  Its
     * triangle points into the 3D history and the perspective line starts at
     * that triangle's apex.  The conventional 2D marker below is independent
     * and remains unchanged. */
    if (rx->display_3d && marker_y > 0) {
      const double marker_centre = 0.5 * (double)(b_width - 1);
      const double marker_perspective_shrink = 0.18;
      const int marker_segments = 32;
      const double marker_3d_w = 12.0;
      const double marker_3d_h = 9.0;
      const double marker_3d_apex_y = (double)marker_y - marker_3d_h;
      double marker_far_y = marker_3d_apex_y -
                            ((double)marker_y * 0.62 + 2.0 - marker_3d_h);
      /* The visible rear edge is signal-height dependent.  Use the already
       * rendered terrain itself as the authority and terminate the marker at
       * the first non-black pixel in its fully-receded column.  This keeps the
       * marker exactly on the visible blue/black transition instead of on the
       * geometric depth baseline. */
      if (rx->pixbuf != NULL) {
        double marker_far_x = marker_centre +
                              (marker_x - marker_centre) *
                              (1.0 - marker_perspective_shrink);
        int pixbuf_width = gdk_pixbuf_get_width(rx->pixbuf);
        int pixbuf_height = gdk_pixbuf_get_height(rx->pixbuf);
        int far_x = CLAMP((int)lround(marker_far_x), 0, pixbuf_width - 1);
        int scan_height = MIN(marker_y, pixbuf_height);
        int rowstride = gdk_pixbuf_get_rowstride(rx->pixbuf);
        int channels = gdk_pixbuf_get_n_channels(rx->pixbuf);
        guchar *pixels = gdk_pixbuf_get_pixels(rx->pixbuf);
        for (int y = 0; y < scan_height; y++) {
          guchar *pixel = pixels + (size_t)y * rowstride +
                          (size_t)far_x * channels;
          if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) {
            marker_far_y = (double)y;
            break;
          }
        }
      }
      /* Separate upward-pointing triangle at the front edge of the 3D area. */
      cairo_move_to(cr, marker_x - (marker_3d_w / 2.0), marker_y);
      cairo_line_to(cr, marker_x + (marker_3d_w / 2.0), marker_y);
      cairo_line_to(cr, marker_x, marker_3d_apex_y);
      cairo_close_path(cr);
      cairo_fill(cr);
      /* Continue from the triangle apex backwards through the perspective. */
      cairo_move_to(cr, marker_x, marker_3d_apex_y);
      for (int i = 1; i <= marker_segments; i++) {
        double depth = (double)i / (double)marker_segments;
        double scale = 1.0 - marker_perspective_shrink * depth;
        double x = marker_centre + (marker_x - marker_centre) * scale;
        double y = marker_3d_apex_y + depth * (marker_far_y - marker_3d_apex_y);
        cairo_line_to(cr, x, y);
      }
      cairo_stroke(cr);
    }
    /* Keep the conventional 2D-waterfall reference vertical. */
    cairo_move_to(cr, marker_x, marker_y);
    cairo_line_to(cr, marker_x, b_height);
    cairo_stroke(cr);
    /* Match the RX panadapter cursor marker: a 12 x 9 px downward triangle
     * anchored at the top edge of the conventional 2D waterfall. */
    const double cursor_w = 12.0;
    const double cursor_h = 9.0;
    cairo_move_to(cr, marker_x - (cursor_w / 2.0), marker_y);
    cairo_line_to(cr, marker_x + (cursor_w / 2.0), marker_y);
    cairo_line_to(cr, marker_x, marker_y + cursor_h);
    cairo_close_path(cr);
    cairo_fill(cr);
  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if (display_info_bar && waterfall_is_last_visible(rx) && (rx->display_panadapter == 0
      || rx->display_panadapter == 1)) {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.70);
    cairo_rectangle(cr, 0.0, b_height - box_height, b_width, box_height);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, COLOUR_WHITE);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
    cairo_move_to(cr, b_width - 390, b_height - 13);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
    cairo_move_to(cr, b_width - 510, b_height - 10);
#endif
    if (can_transmit) {
      cairo_show_text(cr, "[T]une  [b]and  [M]ode  [v]fo  [f]ilter  [n]oise  [a]nf  n[r]  [w]binaural  [e]SNB");
    } else {
      cairo_show_text(cr, "[b]and  [M]ode  [v]fo  [f]ilter  [n]oise  [a]nf  n[r]  [w]binaural  [e]SNB");
    }
    char _text[128];
    if (can_transmit) {
      cairo_set_source_rgba(cr, COLOUR_ORANGE);
      cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
#else
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
#endif
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
        snprintf(_text, 128, "[%d] %s", active_receiver->id, truncate_text_3p(input_devices[_audioindex].description, 28));
      } else {
        snprintf(_text, 128, "NO AUDIO INPUT DETECTED");
      }
#endif
      cairo_move_to(cr, 10.0, b_height - 10);
      cairo_show_text(cr, _text);
    }
    if (display_solardata) {
      check_and_run(1);  // 0=no_log_output, 1=print_to_log
      // g_idle_add(check_and_run_idle_cb, GINT_TO_POINTER(1));
#if defined (__APPLE__)
      cairo_move_to(cr, b_width / 4, b_height - 10);
#else
      cairo_move_to(cr, (b_width / 4) - 10, b_height - 10);
#endif
      if (sunspots != -1) {
        if (iaru_region == 1) {
          snprintf(_text, 128, "SN:%d SFI:%d A:%d K:%d X:%s GmF:%s MUF3k:%.1f Es6:%s", sunspots, solar_flux, a_index,
                   k_index, xray, geomagfield, muf, es6_status > 0 ? "ON" : es6_status == 0 ? "---" : "N/A");
        } else {
          snprintf(_text, 128, "SN:%d SFI:%d A:%d K:%d X:%s GmF:%s MUF3k:%.1f", sunspots, solar_flux, a_index, k_index, xray,
                   geomagfield, muf);
        }
      } else {
        snprintf(_text, 128, " ");
      }
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_show_text(cr, _text);
    }
  }
  if (rx->display_waterfall) {
    /*
     * RX-local waterfall status.  Noise floor and sample rate belong to the
     * individual receiver, so draw them in every visible waterfall.  When the
     * global info bar is drawn in the last waterfall, keep the RX-local lines
     * above that bar instead of letting them overlap the microphone/status text.
     */
    gboolean show_global_info = display_info_bar && waterfall_is_last_visible(rx)
                                && (rx->display_panadapter == 0 || rx->display_panadapter == 1);
    char _text[64];
    cairo_set_source_rgba(cr, COLOUR_ATTN);
    cairo_select_font_face(cr, DISPLAY_FONT_METER, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
#if defined (__APPLE__)
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE3);
#else
    cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
#endif
    cairo_font_extents_t font_extents;
    cairo_font_extents(cr, &font_extents);
    int line_height = (int)ceil(font_extents.height);
    if (line_height < 18) {
      line_height = 18;
    }
    int noise_y;
    int sr_y;
    int local_bottom = b_height - 5;
    if (show_global_info) {
      /*
       * The last visible waterfall also owns the global info bar at the
       * bottom.  Keep RX-local noise floor/SR immediately above that bar,
       * so all waterfalls use the same visual convention while avoiding
       * overlap with the global microphone/status line.
       */
      local_bottom = b_height - box_height - 5;
    }
    sr_y = local_bottom;
    noise_y = sr_y - line_height;
    if (noise_y < line_height) {
      noise_y = line_height;
      sr_y = noise_y + line_height;
    }
    if (rx->display_panadapter == 0 || rx->display_panadapter == 1) {
      snprintf(_text, sizeof(_text), "%d db", rx->panadapter_noise_level);
      cairo_text_extents_t nf_extents;
      cairo_text_extents(cr, _text, &nf_extents);
      double _x = 65 - nf_extents.width;
      cairo_move_to(cr, _x, noise_y);
      cairo_show_text(cr, _text);
    }
    const char *diversity_role = "";
    gboolean show_diversity_role = diversity_enabled &&
                                   !radio_is_transmitting() &&
                                   !radio_ptt;
    if (show_diversity_role) {
      if (rx->id == 0) {
        diversity_role = " DIV";
      } else if (rx->id == 1) {
        diversity_role = " ADC1";
      }
    }
    if (rx->sample_rate >= 1000000) {
      if (diversity_role[0] != '\0') {
        snprintf(_text, sizeof(_text), "%dM%s", rx->sample_rate / 1000000, diversity_role);
      } else {
        snprintf(_text, sizeof(_text), "SR %dM", rx->sample_rate / 1000000);
      }
    } else {
      if (diversity_role[0] != '\0') {
        snprintf(_text, sizeof(_text), "%dk%s", rx->sample_rate / 1000, diversity_role);
      } else {
        snprintf(_text, sizeof(_text), "SR %dk", rx->sample_rate / 1000);
      }
    }
    cairo_text_extents_t sr_extents;
    cairo_text_extents(cr, _text, &sr_extents);
    double sr_right_edge = diversity_role[0] != '\0' ? 95.0 : 65.0;
    double _x = sr_right_edge - sr_extents.width;
    if (_x < 2.0) {
      _x = 2.0;
    }
    cairo_move_to(cr, _x, sr_y);
    cairo_show_text(cr, _text);
    if (display_sysinfo && waterfall_is_last_visible(rx)) {
      char perf_text[64];
      cairo_text_extents_t perf_extents;
      cairo_font_extents_t perf_font_extents;
      cairo_save(cr);
      cairo_set_font_size(cr, DISPLAY_FONT_SIZE2);
      cairo_font_extents(cr, &perf_font_extents);
      double perf_line_height = ceil(perf_font_extents.height);
      double perf_y = sr_y - (2.0 * perf_line_height);
#ifdef __APPLE__
      perf_y += 2.0;
#else
      perf_y += 5.0;
#endif
      cairo_set_line_width(cr, 2.0);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
      snprintf(perf_text, sizeof(perf_text), "APP %.0f%%", process_cpu_load);
      cairo_text_extents(cr, perf_text, &perf_extents);
      cairo_move_to(cr, b_width - perf_extents.width - 5.0, perf_y);
      cairo_text_path(cr, perf_text);
      cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
      cairo_stroke_preserve(cr);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_fill(cr);
      snprintf(perf_text, sizeof(perf_text), "SYS %.0f%%", system_cpu_load);
      cairo_text_extents(cr, perf_text, &perf_extents);
      cairo_move_to(cr, b_width - perf_extents.width - 5.0, perf_y + perf_line_height);
      cairo_text_path(cr, perf_text);
      cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
      cairo_stroke_preserve(cr);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_fill(cr);
      if (waterfall_local_audio_active()) {
        snprintf(perf_text, sizeof(perf_text), "XRUN %" G_GUINT64_FORMAT, audio_get_xrun_count());
      } else {
        g_strlcpy(perf_text, "XRUN -", sizeof(perf_text));
      }
      cairo_text_extents(cr, perf_text, &perf_extents);
      cairo_move_to(cr, b_width - perf_extents.width - 5.0, perf_y + (2.0 * perf_line_height));
      cairo_text_path(cr, perf_text);
      cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
      cairo_stroke_preserve(cr);
      cairo_set_source_rgba(cr, COLOUR_ATTN);
      cairo_fill(cr);
      cairo_restore(cr);
    }
  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  return FALSE;
}

static gboolean
waterfall_button_press_event_cb(GtkWidget      *widget,
                                GdkEventButton *event,
                                gpointer        data) {
  return rx_button_press_event(widget, event, data);
}

static gboolean
waterfall_button_release_event_cb(GtkWidget      *widget,
                                  GdkEventButton *event,
                                  gpointer        data) {
  return rx_button_release_event(widget, event, data);
}

static gboolean waterfall_motion_notify_event_cb(GtkWidget      *widget,
    GdkEventMotion *event,
    gpointer        data) {
  return rx_motion_notify_event(widget, event, data);
}

// cppcheck-suppress constParameterCallback
static gboolean waterfall_scroll_event_cb(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  return rx_scroll_event(widget, event, data);
}

void waterfall_update(RECEIVER *rx) {
  if (rx->pixbuf) {
    const float *samples;
    long long vfofreq = vfo[rx->id].frequency; // access only once to be thread-safe
    int  freq_changed = 0;                    // flag whether we have just "rotated"
    int pan = rx->pan;
    int zoom = rx->zoom;
    unsigned char *pixels = gdk_pixbuf_get_pixels(rx->pixbuf);
    int width = gdk_pixbuf_get_width(rx->pixbuf);
    int height = gdk_pixbuf_get_height(rx->pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(rx->pixbuf);
    int terrain_height = rx->display_3d ? (height * 40) / 100 : 0;
    if (height - terrain_height < 24) {
      terrain_height = 0;
    }
    double hz_per_pixel = (double) rx->sample_rate / ((double) width * rx->zoom);
    //
    // The existing waterfall corresponds to a VFO frequency rx->waterfall_frequency, a zoom value rx->waterfall_zoom and
    // a pan value rx->waterfall_pan. If the zoom value changes, or if the waterfill needs horizontal shifting larger
    // than the width of the waterfall (band change or big frequency jump), re-init the waterfall.
    // Otherwise, shift the waterfall by an appropriate number of pixels.
    //
    // Note that VFO frequency changes can occur in very many very small steps, such that in each step, the horizontal
    // shifting is only a fraction of one pixel. In this case, there will be every now and then a horizontal shift that
    // corrects for a number of VFO update steps.
    //
    if (rx->waterfall_frequency != 0 && (rx->sample_rate == rx->waterfall_sample_rate)
        && (rx->zoom == rx->waterfall_zoom)) {
      if (rx->waterfall_frequency != vfofreq || rx->waterfall_pan != pan) {
        //
        // Frequency and/or PAN value changed: possibly shift waterfall
        //
        int rotfreq = (int)((double)(rx->waterfall_frequency - vfofreq) / hz_per_pixel);    // shift due to freq. change
        int rotpan  = rx->waterfall_pan - pan;                                        // shift due to pan   change
        int rotate_pixels = rotfreq + rotpan;
        if (rotate_pixels >= width || rotate_pixels <= -width) {
          //
          // If horizontal shift is too large, re-init waterfall
          //
          memset(pixels, 0, (size_t) height * rowstride);
          rx->waterfall_frequency = vfofreq;
          rx->waterfall_pan = pan;
        } else {
          //
          // If rotate_pixels != 0, shift waterfall horizontally and set "freq changed" flag
          // calculated which VFO/pan value combination the shifted waterfall corresponds to
          //
          //
          if (rotate_pixels < 0) {
            // shift left, and clear the right-most part
            int shift_pixels = -rotate_pixels;
            size_t shift_bytes = (size_t) shift_pixels * 3;
            size_t keep_bytes = (size_t)(width - shift_pixels) * 3;
            if (rowstride == width * 3) {
              // Fast path for tightly packed RGB pixbufs.
              memmove(pixels, pixels + shift_bytes,
                      (size_t) height * rowstride - shift_bytes);
              for (int i = 0; i < height; i++) {
                memset(pixels + (size_t) i * rowstride + keep_bytes, 0, shift_bytes);
              }
            } else {
              // GdkPixbuf may pad rows; keep each row independent.
              for (int i = 0; i < height; i++) {
                unsigned char *row = pixels + (size_t) i * rowstride;
                memmove(row, row + shift_bytes, keep_bytes);
                memset(row + keep_bytes, 0, shift_bytes);
              }
            }
          } else if (rotate_pixels > 0) {
            // shift right, and clear left-most part
            int shift_pixels = rotate_pixels;
            size_t shift_bytes = (size_t) shift_pixels * 3;
            size_t keep_bytes = (size_t)(width - shift_pixels) * 3;
            if (rowstride == width * 3) {
              // Fast path for tightly packed RGB pixbufs.
              memmove(pixels + shift_bytes, pixels,
                      (size_t) height * rowstride - shift_bytes);
              for (int i = 0; i < height; i++) {
                memset(pixels + (size_t) i * rowstride, 0, shift_bytes);
              }
            } else {
              // GdkPixbuf may pad rows; keep each row independent.
              for (int i = 0; i < height; i++) {
                unsigned char *row = pixels + (size_t) i * rowstride;
                memmove(row + shift_bytes, row, keep_bytes);
                memset(row, 0, shift_bytes);
              }
            }
          }
          if (rotfreq != 0) {
            freq_changed = 1;
            rx->waterfall_frequency -= lround(rotfreq * hz_per_pixel);  // this is not necessarily vfofreq!
          }
          rx->waterfall_pan = pan;
        }
      }
    } else {
      //
      // waterfall frequency not (yet) set, sample rate changed, or zoom value changed:
      // (re-) init waterfall
      //
      memset(pixels, 0, (size_t) height * rowstride);
      rx->waterfall_frequency = vfofreq;
      rx->waterfall_pan = pan;
      rx->waterfall_zoom = zoom;
      rx->waterfall_sample_rate = rx->sample_rate;
    }
    //
    // If we have just shifted the waterfall befause the VFO frequency has changed,
    // there are  still IQ samples in the input queue corresponding to the "old"
    // VFO frequency, and this produces artifacts both on the panadaper and on the
    // waterfall. However, for the panadapter these are overwritten in due course,
    // while artifacts "stay" on the waterfall. We therefore refrain from updating
    // the waterfall *now* and continue updating when the VFO frequency has
    // stabilized. This will not remove the artifacts in any case but is a big
    // improvement.
    //
    if (!freq_changed) {
      float soffset;
      unsigned char *p;
      samples = rx->pixel_samples;
      float wf_low, wf_high, rangei;
      int id = rx->id;
      int b = vfo[id].band;
      const BAND *band = band_get_band(b);
      int calib = rx_gain_calibration - band->gain;
      //
      // soffset contains all corrections due to attenuation, preamps, etc.
      //
      soffset = (float)(calib + adc[rx->adc].attenuation - adc[rx->adc].gain);
      if (filter_board == ALEX && rx->adc == 0) {
        soffset += (float)(10 * rx->alex_attenuation - 20 * rx->preamp);
      }
      if (filter_board == CHARLY25 && rx->adc == 0) {
        soffset += (float)(12 * rx->alex_attenuation - 18 * rx->preamp - 18 * rx->dither);
      }
      if (rx->waterfall_automatic) {
        float average = 0.0F;
        for (int i = 0; i < width; i++) {
          average += samples[i];
        }
        wf_low = (average / (float) width) + soffset - 5.0F;
        wf_high = wf_low + 55.0F;
      } else {
        wf_low  = (float) rx->waterfall_low;
        wf_high = (float) rx->waterfall_high;
      }
      rangei = 1.0F / (wf_high - wf_low);
      /* Keep the conventional waterfall in the lower part.  In 3D mode
       * the upper part is reserved for the backward-running spectrum
       * history; the normal panadapter is not touched. */
      int waterfall_rows = height - terrain_height;
      if (waterfall_rows > 1) {
        memmove(pixels + (size_t)(terrain_height + 1) * rowstride,
                pixels + (size_t)terrain_height * rowstride,
                (size_t)(waterfall_rows - 1) * rowstride);
      }
      if (terrain_height > 0) {
        waterfall_3d_render(rx, pixels, rowstride, width, terrain_height,
                            samples, pan, soffset, wf_low, wf_high, vfofreq);
      }
      p = pixels + (size_t)terrain_height * rowstride;
      for (int i = 0; i < width; i++) {
        float sample = samples[i + pan] + soffset;
        if (sample < wf_low) {
          *p++ = colorLowR;
          *p++ = colorLowG;
          *p++ = colorLowB;
        } else if (sample > wf_high) {
          *p++ = colorHighR;
          *p++ = colorHighG;
          *p++ = colorHighB;
        } else {
          float percent = (sample - wf_low) * rangei;
          if (percent < 0.222222f) {
            float local_percent = percent * 4.5f;
            *p++ = (int)((1.0f - local_percent) * colorLowR);
            *p++ = (int)((1.0f - local_percent) * colorLowG);
            *p++ = (int)(colorLowB + local_percent * (255 - colorLowB));
          } else if (percent < 0.333333f) {
            float local_percent = (percent - 0.222222f) * 9.0f;
            *p++ = 0;
            *p++ = (int)(local_percent * 255);
            *p++ = 255;
          } else if (percent < 0.444444f) {
            float local_percent = (percent - 0.333333) * 9.0f;
            *p++ = 0;
            *p++ = 255;
            *p++ = (int)((1.0f - local_percent) * 255);
          } else if (percent < 0.555555f) {
            float local_percent = (percent - 0.444444f) * 9.0f;
            *p++ = (int)(local_percent * 255);
            *p++ = 255;
            *p++ = 0;
          } else if (percent < 0.777777f) {
            float local_percent = (percent - 0.555555f) * 4.5f;
            *p++ = 255;
            *p++ = (int)((1.0f - local_percent) * 255);
            *p++ = 0;
          } else if (percent < 0.888888f) {
            float local_percent = (percent - 0.777777f) * 9.0f;
            *p++ = 255;
            *p++ = 0;
            *p++ = (int)(local_percent * 255);
          } else {
            float local_percent = (percent - 0.888888f) * 9.0f;
            *p++ = (int)((0.75f + 0.25f * (1.0f - local_percent)) * 255.0f);
            *p++ = (int)(local_percent * 255.0f * 0.5f);
            *p++ = 255;
          }
        }
      }
    }
    gtk_widget_queue_draw(rx->waterfall);
  }
}

void waterfall_init(RECEIVER *rx, int width, int height) {
  if (performance_timer_id == 0) {
    performance_timer_id = g_timeout_add_seconds(1, waterfall_performance_update, NULL);
  }
  rx->pixbuf = NULL;
  rx->waterfall_frequency = 0;
  rx->waterfall_sample_rate = 0;
  rx->waterfall = gtk_drawing_area_new();
  gtk_widget_set_size_request(rx->waterfall, width, height);
  /* Signals used to handle the backing surface */
  g_signal_connect(rx->waterfall, "draw",
                   G_CALLBACK(waterfall_draw_cb), rx);
  g_signal_connect(rx->waterfall, "configure-event",
                   G_CALLBACK(waterfall_configure_event_cb), rx);
  /* Event signals */
  g_signal_connect(rx->waterfall, "motion-notify-event",
                   G_CALLBACK(waterfall_motion_notify_event_cb), rx);
  g_signal_connect(rx->waterfall, "button-press-event",
                   G_CALLBACK(waterfall_button_press_event_cb), rx);
  g_signal_connect(rx->waterfall, "button-release-event",
                   G_CALLBACK(waterfall_button_release_event_cb), rx);
  g_signal_connect(rx->waterfall, "scroll_event",
                   G_CALLBACK(waterfall_scroll_event_cb), rx);
  /* Ask to receive events the drawing area doesn't normally
   * subscribe to. In particular, we need to ask for the
   * button press and motion notify events that want to handle.
   */
  gtk_widget_set_events(rx->waterfall, gtk_widget_get_events(rx->waterfall)
                        | GDK_BUTTON_PRESS_MASK
                        | GDK_BUTTON_RELEASE_MASK
                        | GDK_BUTTON1_MOTION_MASK
                        | GDK_SCROLL_MASK
                        | GDK_POINTER_MOTION_MASK
                        | GDK_POINTER_MOTION_HINT_MASK);
}
