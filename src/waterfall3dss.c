/* waterfall3dss.c - 3D OpenGL waterfall display (Yaesu 3DSS style)
 * Adapted from piHPSDR for deskHPSDR
 * Copyright (C) 2025-2026 Daniel Pechmann, PU2ODM
 * Copyright (C) 2015-2026 John Melton, G0ORX/N6LYT
 * Copyright (C) 2024-2025 Heiko Amft, DL1BZ (Project deskHPSDR)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtk/gtk.h>
#include <cairo.h>
#include <epoxy/gl.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "radio.h"
#include "receiver.h"
#include "vfo.h"
#include "band.h"
#include "waterfall3dss.h"

#define WATERFALL_DEPTH 120
#define WATERFALL_Z_SPAN 1.60f
#define WATERFALL_TILT_Y 0.22f

// =================== Math helpers ===================
static void mat4_identity(float *m) {
  memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul(float *out, const float *a, const float *b) {
  float tmp[16];
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++) {
        sum += a[i + k*4] * b[k + j*4];
      }
      tmp[i + j*4] = sum;
    }
  }
  memcpy(out, tmp, 16 * sizeof(float));
}

static void mat4_translate(float *m, float x, float y, float z) {
  mat4_identity(m);
  m[12] = x; m[13] = y; m[14] = z;
}

static void mat4_scale(float *m, float sx, float sy, float sz) {
  mat4_identity(m);
  m[0] = sx; m[5] = sy; m[10] = sz;
}

static void mat4_perspective(float *m, float fovy, float aspect, float near, float far) {
  memset(m, 0, 16 * sizeof(float));
  float f = 1.0f / tanf(fovy / 2.0f);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (far + near) / (near - far);
  m[11] = -1.0f;
  m[14] = (2.0f * far * near) / (near - far);
}

static void mat4_look_at(float *m, float ex, float ey, float ez,
                         float cx, float cy, float cz,
                         float ux, float uy, float uz) {
  float fx = cx - ex, fy = cy - ey, fz = cz - ez;
  float len = sqrtf(fx*fx + fy*fy + fz*fz);
  if (len > 0.0f) { fx /= len; fy /= len; fz /= len; }
  
  float sx = fy*uz - fz*uy;
  float sy = fz*ux - fx*uz;
  float sz = fx*uy - fy*ux;
  len = sqrtf(sx*sx + sy*sy + sz*sz);
  if (len > 0.0f) { sx /= len; sy /= len; sz /= len; }
  
  float ux2 = sy*fz - sz*fy;
  float uy2 = sz*fx - sx*fz;
  float uz2 = sx*fy - sy*fx;
  
  m[0] = sx;  m[4] = ux2; m[8]  = -fx; m[12] = 0.0f;
  m[1] = sy;  m[5] = uy2; m[9]  = -fy; m[13] = 0.0f;
  m[2] = sz;  m[6] = uz2; m[10] = -fz; m[14] = 0.0f;
  m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
  
  float tx = -(sx*ex + sy*ey + sz*ez);
  float ty = -(ux2*ex + uy2*ey + uz2*ez);
  float tz = (fx*ex + fy*ey + fz*ez);
  m[12] = tx; m[13] = ty; m[14] = tz;
}

static float clampf(float x, float a, float b) {
  return x < a ? a : (x > b ? b : x);
}

// =================== OpenGL State ===================
typedef struct {
  GLuint prog, vao, vbo, grid_vao, grid_vbo;
  GLint u_mvp;
  
  int bins, depth, head;
  float *history;
  
  float *vtx;
  size_t vtx_cap_floats;
  
  float *row_tmp0, *row_tmp1;
  int row_tmp_cap;
  
  int grid_vertices;
  float cam_angle;
  
  // Interactive controls
  float tilt_angle;
  float zoom_level;
  gboolean dragging;
  double drag_start_y;
  float drag_start_tilt;
  
  // Track zoom/pan state for sync with panadapter
  int waterfall_zoom;
  int waterfall_pan;
  
  // Stabilization counter
  int update_count;
  int render_count;
} WaterfallGLState;

// Global state storage (indexed by rx->id)
static GHashTable *wf_states = NULL;

static WaterfallGLState* wf_get(RECEIVER *rx) {
  if (!wf_states) {
    wf_states = g_hash_table_new(g_direct_hash, g_direct_equal);
  }
  return (WaterfallGLState*)g_hash_table_lookup(wf_states, GINT_TO_POINTER(rx->id));
}

static void wf_set(RECEIVER *rx, WaterfallGLState *st) {
  if (!wf_states) {
    wf_states = g_hash_table_new(g_direct_hash, g_direct_equal);
  }
  g_hash_table_insert(wf_states, GINT_TO_POINTER(rx->id), st);
}

static void wf_reset_history(WaterfallGLState *st) {
  if (!st || !st->history) return;
  for (int i = 0; i < st->depth * st->bins; i++) {
    st->history[i] = -140.0f;
  }
  st->head = 0;
}

// =================== Shaders ===================
static const char *vertex_shader_src =
  "#version 330 core\n"
  "layout(location = 0) in vec3 a_pos;\n"
  "layout(location = 1) in vec4 a_col;\n"
  "out vec4 v_col;\n"
  "uniform mat4 u_mvp;\n"
  "void main() {\n"
  "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
  "  v_col = a_col;\n"
  "}\n";

static const char *fragment_shader_src =
  "#version 330 core\n"
  "in vec4 v_col;\n"
  "out vec4 FragColor;\n"
  "void main() {\n"
  "  FragColor = v_col;\n"
  "}\n";

static GLuint compile_shader(GLenum type, const char *src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);
  
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[512];
    glGetShaderInfoLog(shader, sizeof(buf), NULL, buf);
    g_print("Shader compile error: %s\n", buf);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint link_program(GLuint vs, GLuint fs) {
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buf[512];
    glGetProgramInfoLog(prog, sizeof(buf), NULL, buf);
    g_print("Program link error: %s\n", buf);
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

// =================== Color palettes ===================
static void color_from_palette(int palette, float p, float *r, float *g, float *b) {
  p = clampf(p, 0.0f, 1.0f);
  
  switch (palette) {
    case 0: // Rainbow (default)
      if (p < 0.25f) {
        float t = p / 0.25f;
        *r = 0.0f; *g = t; *b = 1.0f;
      } else if (p < 0.5f) {
        float t = (p - 0.25f) / 0.25f;
        *r = 0.0f; *g = 1.0f; *b = 1.0f - t;
      } else if (p < 0.75f) {
        float t = (p - 0.5f) / 0.25f;
        *r = t; *g = 1.0f; *b = 0.0f;
      } else {
        float t = (p - 0.75f) / 0.25f;
        *r = 1.0f; *g = 1.0f - t * 0.5f; *b = 0.0f;
      }
      break;
      
    case 1: // Ocean
      *r = p * 0.3f;
      *g = 0.5f + p * 0.5f;
      *b = 0.7f + p * 0.3f;
      break;
      
    case 2: // Green
      *r = p * 0.2f;
      *g = 0.3f + p * 0.7f;
      *b = p * 0.1f;
      break;
      
    case 3: // Gray
      *r = *g = *b = p;
      break;
      
    case 4: // Hot (black->red->yellow->white)
      if (p < 0.33f) {
        float t = p / 0.33f;
        *r = t; *g = 0.0f; *b = 0.0f;
      } else if (p < 0.66f) {
        float t = (p - 0.33f) / 0.33f;
        *r = 1.0f; *g = t; *b = 0.0f;
      } else {
        float t = (p - 0.66f) / 0.34f;
        *r = 1.0f; *g = 1.0f; *b = t;
      }
      break;
      
    case 5: // Cool
      *r = 0.0f + p;
      *g = 1.0f - p * 0.5f;
      *b = 1.0f - p * 0.5f;
      break;
      
    case 6: // Plasma (white -> blue -> lilac -> red)
      if (p < 0.33f) {
        // White -> Blue
        float t = p / 0.33f;
        *r = 1.0f - t * 0.8f;
        *g = 1.0f - t * 0.6f;
        *b = 1.0f;
      } else if (p < 0.66f) {
        // Blue -> Lilac
        float t = (p - 0.33f) / 0.33f;
        *r = 0.2f + t * 0.5f;
        *g = 0.4f + t * 0.1f;
        *b = 1.0f;
      } else {
        // Lilac -> Red
        float t = (p - 0.66f) / 0.34f;
        *r = 0.7f + t * 0.3f;
        *g = 0.5f - t * 0.5f;
        *b = 1.0f - t * 1.0f;
      }
      break;
      
    default:
      *r = *g = *b = p;
  }
}

static void sample_to_color(RECEIVER *rx, float sample_db, float dist01,
                            float *r, float *g, float *b, float *a, float *h01) {
  float db_min = (float)rx->waterfall_low;
  float db_max = (float)rx->waterfall_high;
  float p = (sample_db - db_min) / (db_max - db_min);
  p = clampf(p, 0.0f, 1.0f);
  
  int palette = rx->waterfall3dss_palette; // 0=Rainbow, 1=Ocean, 2=Green, 3=Gray, 4=Hot, 5=Cool, 6=Plasma
  
  // Aggressive threshold: below 35% is completely black (noise floor)
  // This creates the Yaesu-style "black where no signal" look
  const float noise_threshold = 0.35f;
  
  if (p < noise_threshold) {
    // Noise floor: completely black and flat
    *r = 0.0f;
    *g = 0.0f;
    *b = 0.0f;
    *h01 = 0.0f;  // Keep flat (no height)
  } else {
    // Valid signal: remap to 0-1 range and apply power curve for emphasis
    float signal = (p - noise_threshold) / (1.0f - noise_threshold);
    
    // Apply power curve to make peaks more pronounced (square makes it more aggressive)
    signal = signal * signal; // Quadratic curve emphasizes strong signals
    
    // Increase height for valid signals (multiply by 1.8 for taller peaks)
    *h01 = signal * 1.8f;
    *h01 = clampf(*h01, 0.0f, 1.0f);
    
    // Plasma (6): White → Blue → Lilac → Red
    if (palette == 6) {
      if (dist01 < 0.33f) {
        float t = dist01 / 0.33f;
        *r = 1.0f - t * 0.8f;
        *g = 1.0f - t * 0.6f;
        *b = 1.0f;
      } else if (dist01 < 0.66f) {
        float t = (dist01 - 0.33f) / 0.33f;
        *r = 0.2f + t * 0.5f;
        *g = 0.4f + t * 0.1f;
        *b = 1.0f;
      } else {
        float t = (dist01 - 0.66f) / 0.34f;
        *r = 0.7f + t * 0.3f;
        *g = 0.5f - t * 0.5f;
        *b = 1.0f - t * 1.0f;
      }
    } else {
      // Other palettes: white (near) → target color (far)
      float target_r, target_g, target_b;
      color_from_palette(palette, 1.0f, &target_r, &target_g, &target_b);
      
      *r = 1.0f + dist01 * (target_r - 1.0f);
      *g = 1.0f + dist01 * (target_g - 1.0f);
      *b = 1.0f + dist01 * (target_b - 1.0f);
    }
    
    // Modulate brightness by signal intensity (using remapped signal value)
    *r *= signal;
    *g *= signal;
    *b *= signal;
  }
  
  *a = 1.0f;
}

// =================== Grid ===================
static void build_grid(WaterfallGLState *st) {
  const int N_VLINES = 20;
  const int N_HLINES = 15;
  
  int max_verts = (N_VLINES * 2 + N_HLINES * 2 + 8);
  float *grid_data = (float*)g_malloc(max_verts * 7 * sizeof(float));
  int idx = 0;
  
  float r = 0.15f, g = 0.60f, b = 0.70f, a = 0.50f;
  
  // Vertical lines (frequency)
  for (int i = 0; i < N_VLINES; i++) {
    float x01 = (float)i / (float)(N_VLINES - 1);
    float x = (x01 - 0.5f) * 2.0f;
    float x_front = x * 0.80f;
    float x_back  = x * 0.80f;
    
    grid_data[idx++] = x_front; grid_data[idx++] = 0.0f; grid_data[idx++] = 0.0f;
    grid_data[idx++] = r; grid_data[idx++] = g; grid_data[idx++] = b; grid_data[idx++] = a;
    
    float dist = 1.0f;
    float fog = clampf(1.0f - dist * 0.7f, 0.35f, 1.0f);
    grid_data[idx++] = x_back; grid_data[idx++] = st->tilt_angle; grid_data[idx++] = -WATERFALL_Z_SPAN;
    grid_data[idx++] = r*fog; grid_data[idx++] = g*fog; grid_data[idx++] = b*fog; grid_data[idx++] = a * 0.4f;
  }
  
  // Horizontal lines (time)
  for (int i = 0; i < N_HLINES; i++) {
    float z = -(float)i / (float)(N_HLINES - 1) * WATERFALL_Z_SPAN;
    float dist01 = (float)i / (float)(N_HLINES - 1);
    float taper = 0.80f;
    float fog = clampf(1.0f - dist01 * 0.7f, 0.35f, 1.0f);
    float y_tilt = st->tilt_angle * dist01;
    
    float x0 = -1.0f * taper;
    float x1 =  1.0f * taper;
    
    grid_data[idx++] = x0; grid_data[idx++] = y_tilt; grid_data[idx++] = z;
    grid_data[idx++] = r*fog; grid_data[idx++] = g*fog; grid_data[idx++] = b*fog; grid_data[idx++] = a * 0.8f;
    
    grid_data[idx++] = x1; grid_data[idx++] = y_tilt; grid_data[idx++] = z;
    grid_data[idx++] = r*fog; grid_data[idx++] = g*fog; grid_data[idx++] = b*fog; grid_data[idx++] = a * 0.8f;
  }
  
  // Border lines
  grid_data[idx++] = -0.80f; grid_data[idx++] = 0.0f; grid_data[idx++] = 0.0f;
  grid_data[idx++] = r; grid_data[idx++] = g; grid_data[idx++] = b; grid_data[idx++] = a;
  grid_data[idx++] =  0.80f; grid_data[idx++] = 0.0f; grid_data[idx++] = 0.0f;
  grid_data[idx++] = r; grid_data[idx++] = g; grid_data[idx++] = b; grid_data[idx++] = a;
  
  grid_data[idx++] = -0.80f; grid_data[idx++] = st->tilt_angle; grid_data[idx++] = -WATERFALL_Z_SPAN;
  grid_data[idx++] = r*0.7f; grid_data[idx++] = g*0.7f; grid_data[idx++] = b*0.7f; grid_data[idx++] = a * 0.5f;
  grid_data[idx++] =  0.80f; grid_data[idx++] = st->tilt_angle; grid_data[idx++] = -WATERFALL_Z_SPAN;
  grid_data[idx++] = r*0.7f; grid_data[idx++] = g*0.7f; grid_data[idx++] = b*0.7f; grid_data[idx++] = a * 0.5f;
  
  st->grid_vertices = idx / 7;
  
  glBindVertexArray(st->grid_vao);
  glBindBuffer(GL_ARRAY_BUFFER, st->grid_vbo);
  glBufferData(GL_ARRAY_BUFFER, idx * sizeof(float), grid_data, GL_STATIC_DRAW);
  
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  
  glBindVertexArray(0);
  g_free(grid_data);
}

// =================== OpenGL callbacks ===================
void waterfall3dss_gl_realize(GtkGLArea *area, gpointer data) {
  RECEIVER *rx = (RECEIVER*)data;
  g_print("[WF3DSS RX%d] realize called\n", rx->id);
  
  WaterfallGLState *st = wf_get(rx);
  if (!st) {
    g_print("[WF3DSS RX%d] ERROR: st is NULL in realize!\n", rx->id);
    return;
  }
  
  gtk_gl_area_make_current(area);
  GError *error = gtk_gl_area_get_error(area);
  if (error) {
    g_print("[WF3DSS RX%d] GL context error: %s\n", rx->id, error->message);
    return;
  }
  
  GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
  if (!vs || !fs) {
    g_print("[WF3DSS] Shader compilation failed!\n");
    return;
  }
  
  st->prog = link_program(vs, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  
  if (!st->prog) return;
  
  st->u_mvp = glGetUniformLocation(st->prog, "u_mvp");
  
  // Initialize interactive controls
  st->tilt_angle = 2.8f;
  st->zoom_level = 2.0f;
  st->dragging = FALSE;
  st->waterfall_zoom = 0;
  st->waterfall_pan = 0;
  
  // Main mesh VAO
  glGenVertexArrays(1, &st->vao);
  glGenBuffers(1, &st->vbo);
  glBindVertexArray(st->vao);
  glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
  
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  
  glBindVertexArray(0);
  
  // Grid VAO
  glGenVertexArrays(1, &st->grid_vao);
  glGenBuffers(1, &st->grid_vbo);
  build_grid(st);
  
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  
  gtk_gl_area_queue_render(area);
}

void waterfall3dss_gl_unrealize(GtkGLArea *area, gpointer data) {
  RECEIVER *rx = (RECEIVER*)data;
  WaterfallGLState *st = wf_get(rx);
  if (st) {
    gtk_gl_area_make_current(area);
    if (!gtk_gl_area_get_error(area)) {
      if (st->prog) glDeleteProgram(st->prog);
      if (st->vao) glDeleteVertexArrays(1, &st->vao);
      if (st->vbo) glDeleteBuffers(1, &st->vbo);
      if (st->grid_vao) glDeleteVertexArrays(1, &st->grid_vao);
      if (st->grid_vbo) glDeleteBuffers(1, &st->grid_vbo);
    }
  }
}

gboolean waterfall3dss_gl_render(GtkGLArea *area, GdkGLContext *context, gpointer data) {
  (void)context;
  RECEIVER *rx = (RECEIVER*)data;
  WaterfallGLState *st = wf_get(rx);
  
  GError *error = gtk_gl_area_get_error(area);
  if (error) {
    return FALSE;
  }
  
  st->render_count++;
  
  int screen_w = gtk_widget_get_allocated_width(GTK_WIDGET(area));
  int screen_h = gtk_widget_get_allocated_height(GTK_WIDGET(area));
  
  glViewport(0, 0, screen_w, screen_h);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  
  if (!st || !st->prog || !st->history) {
    return TRUE;
  }
  if (st->bins <= 2 || st->depth <= 2) {
    return TRUE;
  }
  if (screen_w <= 2 || screen_h <= 2) {
    return TRUE;
  }
  
  if (st->row_tmp_cap < screen_w) {
    st->row_tmp0 = (float*)g_realloc(st->row_tmp0, screen_w * sizeof(float));
    st->row_tmp1 = (float*)g_realloc(st->row_tmp1, screen_w * sizeof(float));
    st->row_tmp_cap = screen_w;
  }
  
  g_mutex_lock(&rx->display_mutex);
  
  // Camera setup
  float P[16], V[16], M[16], tmp[16], MVP[16];
  float aspect = (screen_h > 0) ? ((float)screen_w / (float)screen_h) : 1.0f;
  
  mat4_perspective(P, 50.0f * (float)M_PI / 180.0f, aspect, 0.1f, 10.0f);
  
  mat4_look_at(V,
    0.0f, 0.85f, st->zoom_level,
    0.0f, 0.20f, -0.8f,
    0.0f, 1.0f, 0.0f);
  
  float T[16], S[16];
  mat4_translate(T, 0.0f, -0.45f, 0.0f);
  const float scale_x = 10.0f;
  mat4_scale(S, scale_x, 1.0f, 1.0f);
  mat4_mul(M, T, S);
  mat4_mul(tmp, V, M);
  mat4_mul(MVP, P, tmp);
  
  glUseProgram(st->prog);
  glUniformMatrix4fv(st->u_mvp, 1, GL_FALSE, MVP);
  
  const int W = screen_w;
  const int D = st->depth;
  const int B = st->bins;
  
  const float z_span = WATERFALL_Z_SPAN;
  const float dz = z_span / (float)(D - 1);
  const float y_base = 0.0f;
  const float y_scale = 0.60f;
  
  size_t needed_floats = (size_t)D * (size_t)W * 2 * 7;
  if (st->vtx_cap_floats < needed_floats) {
    st->vtx = (float*)g_realloc(st->vtx, needed_floats * sizeof(float));
    st->vtx_cap_floats = needed_floats;
  }
  
  float *out = st->vtx;
  
  for (int d = 0; d < D - 1; d++) {
    int row0 = (st->head - d - 1 + D) % D;
    int row1 = (st->head - d - 2 + D) % D;
    
    float z0 = -(float)d * dz;
    float z1 = -(float)(d + 1) * dz;
    
    float dist0 = (float)d / (float)(D - 1);
    float dist1 = (float)(d + 1) / (float)(D - 1);
    
    // Resample from bins to screen width
    for (int x = 0; x < W; x++) {
      float bin_f = (float)x / (float)(W - 1) * (float)(B - 1);
      int bin_i = (int)bin_f;
      if (bin_i >= B - 1) bin_i = B - 2;
      
      st->row_tmp0[x] = st->history[row0 * B + bin_i];
      st->row_tmp1[x] = st->history[row1 * B + bin_i];
    }
    
    // Generate triangle strip
    for (int x = 0; x < W; x++) {
      float px_norm = (float)x / (float)(W - 1);
      float px_centered = (px_norm - 0.5f) * 2.0f;
      
      const float width0 = 0.80f;
      const float width1 = 0.80f;
      
      float px0 = px_centered * width0;
      float px1 = px_centered * width1;
      
      float r, g, b, a, h01;
      
      sample_to_color(rx, st->row_tmp0[x], dist0, &r, &g, &b, &a, &h01);
      float y0 = y_base + h01 * y_scale + st->tilt_angle * dist0;
      
      *out++ = px0; *out++ = y0; *out++ = z0;
      *out++ = r; *out++ = g; *out++ = b; *out++ = a;
      
      sample_to_color(rx, st->row_tmp1[x], dist1, &r, &g, &b, &a, &h01);
      float y1 = y_base + h01 * y_scale + st->tilt_angle * dist1;
      
      *out++ = px1; *out++ = y1; *out++ = z1;
      *out++ = r; *out++ = g; *out++ = b; *out++ = a;
    }
  }
  
  glBindVertexArray(st->vao);
  glBindBuffer(GL_ARRAY_BUFFER, st->vbo);
  glBufferData(GL_ARRAY_BUFFER, needed_floats * sizeof(float), st->vtx, GL_STREAM_DRAW);
  
  size_t base = 0;
  for (int s = 0; s < (D - 1); s++) {
    glDrawArrays(GL_TRIANGLE_STRIP, (GLint)base, (GLint)(W * 2));
    base += (size_t)W * 2;
  }
  glBindVertexArray(0);
  
  if (st->grid_vertices > 0) {
    glBindVertexArray(st->grid_vao);
    glDrawArrays(GL_LINES, 0, (GLsizei)st->grid_vertices);
    glBindVertexArray(0);
  }
  
  glUseProgram(0);
  g_mutex_unlock(&rx->display_mutex);
  
  return TRUE;
}

// =================== Mouse Event Handlers ===================
static gboolean waterfall3dss_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  RECEIVER *rx = (RECEIVER*)data;
  WaterfallGLState *st = wf_get(rx);
  if (!st) return FALSE;
  
  if (event->button == 1) {
    st->dragging = TRUE;
    st->drag_start_y = event->y;
    st->drag_start_tilt = st->tilt_angle;
    return TRUE;
  }
  return FALSE;
}

static gboolean waterfall3dss_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
  RECEIVER *rx = (RECEIVER*)data;
  WaterfallGLState *st = wf_get(rx);
  if (!st || !st->dragging) return FALSE;
  
  double delta_y = event->y - st->drag_start_y;
  float sensitivity = 0.002f;
  st->tilt_angle = st->drag_start_tilt + (float)delta_y * sensitivity;
  st->tilt_angle = clampf(st->tilt_angle, 0.0f, 5.0f);
  
  gtk_gl_area_make_current(GTK_GL_AREA(widget));
  if (!gtk_gl_area_get_error(GTK_GL_AREA(widget))) {
    build_grid(st);
  }
  
  gtk_gl_area_queue_render(GTK_GL_AREA(widget));
  return TRUE;
}

static gboolean waterfall3dss_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  RECEIVER *rx = (RECEIVER*)data;
  WaterfallGLState *st = wf_get(rx);
  if (!st) return FALSE;
  
  if (event->button == 1) {
    st->dragging = FALSE;
    return TRUE;
  }
  return FALSE;
}

static gboolean waterfall3dss_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
  RECEIVER *rx = (RECEIVER*)data;
  WaterfallGLState *st = wf_get(rx);
  if (!st) return FALSE;
  
  float zoom_step = 0.15f;
  
  if (event->direction == GDK_SCROLL_UP) {
    st->zoom_level -= zoom_step;
  } else if (event->direction == GDK_SCROLL_DOWN) {
    st->zoom_level += zoom_step;
  } else if (event->direction == GDK_SCROLL_SMOOTH) {
    gdouble delta_x, delta_y;
    gdk_event_get_scroll_deltas((GdkEvent*)event, &delta_x, &delta_y);
    st->zoom_level += (float)delta_y * zoom_step * 0.5f;
  }
  
  st->zoom_level = clampf(st->zoom_level, 1.0f, 4.0f);
  
  gtk_gl_area_queue_render(GTK_GL_AREA(widget));
  return TRUE;
}

// =================== Public API ===================
void waterfall3dss_init(RECEIVER *rx, int width, int height) {
  g_print("[WF3DSS] waterfall3dss_init: rx->id=%d width=%d height=%d\n", rx->id, width, height);
  
  WaterfallGLState *st = wf_get(rx);
  
  if (!st) {
    st = g_malloc0(sizeof(WaterfallGLState));
    wf_set(rx, st);
    st->depth = WATERFALL_DEPTH;
    st->bins = rx->pixels;
    st->history = (float*)g_malloc0(st->depth * st->bins * sizeof(float));
    st->update_count = 0;
    wf_reset_history(st);
  }
  
  if (!rx->waterfall) {
    rx->waterfall = gtk_gl_area_new();
    if (!rx->waterfall) {
      g_print("[WF3DSS RX%d] ERROR: gtk_gl_area_new() returned NULL!\n", rx->id);
      return;
    }
    
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(rx->waterfall), TRUE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(rx->waterfall), FALSE);
    gtk_gl_area_set_required_version(GTK_GL_AREA(rx->waterfall), 3, 3);
    
    g_signal_connect(rx->waterfall, "realize", G_CALLBACK(waterfall3dss_gl_realize), rx);
    g_signal_connect(rx->waterfall, "unrealize", G_CALLBACK(waterfall3dss_gl_unrealize), rx);
    g_signal_connect(rx->waterfall, "render", G_CALLBACK(waterfall3dss_gl_render), rx);
    
    gtk_widget_add_events(rx->waterfall,
      GDK_BUTTON_PRESS_MASK |
      GDK_BUTTON_RELEASE_MASK |
      GDK_POINTER_MOTION_MASK |
      GDK_SCROLL_MASK |
      GDK_SMOOTH_SCROLL_MASK);
    
    g_signal_connect(rx->waterfall, "button-press-event", G_CALLBACK(waterfall3dss_button_press), rx);
    g_signal_connect(rx->waterfall, "motion-notify-event", G_CALLBACK(waterfall3dss_motion), rx);
    g_signal_connect(rx->waterfall, "button-release-event", G_CALLBACK(waterfall3dss_button_release), rx);
    g_signal_connect(rx->waterfall, "scroll-event", G_CALLBACK(waterfall3dss_scroll), rx);
  }
  
  gtk_widget_set_size_request(rx->waterfall, width, height);
  g_print("[WF3DSS RX%d] waterfall3dss_init COMPLETE\n", rx->id);
}

void waterfall3dss_update(RECEIVER *rx) {
  WaterfallGLState *st = wf_get(rx);
  if (!st || !st->history) return;
  
  // Use screen width as bin count (like 2D waterfall)
  // This represents the visible spectrum width considering zoom
  int screen_w = gtk_widget_get_allocated_width(rx->waterfall);
  if (screen_w <= 0) screen_w = 800; // fallback
  
  int bins = screen_w;
  int zoom = rx->zoom;
  int pan = rx->pan;
  int pixels_total = rx->pixels;
  
  if (bins <= 2) return;
  
  // Detect zoom changes and reset history
  if (st->waterfall_zoom != zoom) {
    st->waterfall_zoom = zoom;
    wf_reset_history(st);
  }
  
  // Track pan changes
  if (st->waterfall_pan != pan) {
    st->waterfall_pan = pan;
  }
  
  if (st->bins != bins || !st->history) {
    st->bins = bins;
    if (st->history) g_free(st->history);
    st->history = (float*)g_malloc0(st->depth * st->bins * sizeof(float));
    wf_reset_history(st);
  }

  // Handle frequency changes
  if (st->bins > 0) {
    double hz_per_bin = (double)rx->sample_rate / (double)st->bins;
    if (rx->waterfall_frequency != 0 && (rx->sample_rate == rx->waterfall_sample_rate)) {
      long long current_freq = vfo[rx->id].frequency;
      if (rx->waterfall_frequency != current_freq) {
        long long half = (long long)(rx->sample_rate / 2);
        if (rx->waterfall_frequency < (current_freq - half) || rx->waterfall_frequency > (current_freq + half)) {
          wf_reset_history(st);
        } else {
          int rotate_bins = (int)(((double)(rx->waterfall_frequency - current_freq)) / hz_per_bin);
          if (rotate_bins != 0) {
            for (int r = 0; r < st->depth; r++) {
              float *rowp = st->history + r * st->bins;
              if (rotate_bins < 0) {
                int nmove = (st->bins + rotate_bins);
                if (nmove < 0) nmove = 0;
                if (nmove > st->bins) nmove = st->bins;
                if (nmove > 0) memmove(rowp, rowp - rotate_bins, (size_t)nmove * sizeof(float));
                int nclear = -rotate_bins;
                if (nclear > st->bins) nclear = st->bins;
                if (nclear > 0) {
                  for (int i = st->bins - nclear; i < st->bins; i++) rowp[i] = -140.0f;
                }
              } else {
                int nmove = (st->bins - rotate_bins);
                if (nmove < 0) nmove = 0;
                if (nmove > st->bins) nmove = st->bins;
                if (nmove > 0) memmove(rowp + rotate_bins, rowp, (size_t)nmove * sizeof(float));
                int nclear = rotate_bins;
                if (nclear > st->bins) nclear = st->bins;
                if (nclear > 0) {
                  for (int i = 0; i < nclear; i++) rowp[i] = -140.0f;
                }
              }
            }
          }
        }
      }
    } else {
      wf_reset_history(st);
    }

    rx->waterfall_frequency = vfo[rx->id].frequency;
    rx->waterfall_sample_rate = rx->sample_rate;
  }
  
  st->update_count++;
  
  // Skip first 5 updates for AGC stabilization
  if (st->update_count < 5) {
    if (rx->waterfall) {
      gtk_gl_area_queue_render(GTK_GL_AREA(rx->waterfall));
    }
    return;
  }
  
  // Copy new spectrum data
  // Sample from pixel_samples array considering pan offset
  float *row = st->history + st->head * st->bins;
  int rx_pan = rx->pan;
  
  for (int i = 0; i < st->bins; i++) {
    // Map screen position to source pixel index
    int src_idx = i + rx_pan;
    float sample_db;
    
    if (src_idx < 0 || src_idx >= pixels_total) {
      sample_db = -140.0f;
    } else {
      sample_db = rx->pixel_samples[src_idx];
    }
    
    row[i] = sample_db;
  }
  
  st->head = (st->head + 1) % st->depth;
  
  if (rx->waterfall) {
    gtk_gl_area_queue_render(GTK_GL_AREA(rx->waterfall));
  }
}
