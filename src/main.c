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
#include <locale.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include <pthread.h>

#include <wdsp.h>    // only needed for WDSPwisdom() and wisdom_get_status()

#include "appearance.h"
#include "audio.h"
#include "band.h"
#include "bandstack.h"
#include "main.h"
#include "discovered.h"
#include "configure.h"
#include "actions.h"
#ifdef GPIO
  #include "gpio.h"
#endif
#include "new_menu.h"
#include "radio.h"
#include "version.h"
#include "discovery.h"
#include "new_protocol.h"
#include "old_protocol.h"
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#ifdef SATURN
  #include "saturnmain.h"
#endif
#include "ext.h"
#include "vfo.h"
#include "css.h"
#include "exit_menu.h"
#include "message.h"
#include "startup.h"
#ifdef TTS
  #include "tts.h"
#endif
#include "sliders.h"
#include "noise_menu.h"
#include "rigctl.h"
#ifdef MIDI
  #include "midi_layer.h"
#endif
#include "trx_logo.h"
#include "toolset.h"

struct utsname unameData;

GdkScreen *screen;
int display_width;
int display_height;
int screen_height;
int screen_width;
int full_screen;
int this_monitor;

int use_wayland;

static GdkCursor *cursor_arrow;
static GdkCursor *cursor_watch;

GtkWidget *top_window = NULL;
GtkWidget *topgrid;

static GtkWidget *status_label;

pthread_t deskhpsdr_main_thread;  // global

#if !defined(__WAYLAND__)
static gboolean block_delete(GtkWidget *w, GdkEvent *e, gpointer data) {
  return TRUE; // verhindert Schließen über Fenstermanager
}

static void show_error_dialog(const char *msg) __attribute__((unused));
static void show_error_dialog(const char *msg) {
  GtkWidget *dlg = gtk_message_dialog_new(
                     NULL,
                     GTK_DIALOG_MODAL,
                     GTK_MESSAGE_ERROR,
                     GTK_BUTTONS_NONE,
                     "%s", msg
                   );
  gtk_window_set_title(GTK_WINDOW(dlg), "deskHPSDR - Important notice");
  gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER_ALWAYS);
  gtk_window_set_keep_above(GTK_WINDOW(dlg), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_NONE);
  gtk_dialog_add_button(GTK_DIALOG(dlg), "_Continue", GTK_RESPONSE_CLOSE);
  // Nur der Close-Button darf beenden
  g_signal_connect(dlg, "delete-event", G_CALLBACK(block_delete), NULL);
  gint r;

  do {
    r = gtk_dialog_run(GTK_DIALOG(dlg));
  } while (r != GTK_RESPONSE_CLOSE);

  gtk_widget_destroy(dlg);
  return;
}

static void enforce_x11_backend_policy(void) {
#if defined(__APPLE__)
  g_setenv("GDK_BACKEND", "quartz", TRUE);
  gdk_set_allowed_backends("quartz");
  use_wayland = 0;
  return;
#else
  const char *xdg = g_getenv("XDG_SESSION_TYPE");
  const char *w   = g_getenv("WAYLAND_DISPLAY");

  if ((xdg && g_ascii_strcasecmp(xdg, "wayland") == 0) || (w && *w)) {
    g_setenv("GDK_BACKEND", "wayland", TRUE);
    gdk_set_allowed_backends("wayland");
    g_setenv("DESKHPSDR_WAYLAND_NOTICE", "1", TRUE);
    use_wayland = 1;
  } else {
    g_setenv("GDK_BACKEND", "x11", TRUE);
    gdk_set_allowed_backends("x11");
    g_setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", TRUE);
    use_wayland = 0;
  }

  // g_setenv("GDK_BACKEND", "x11", TRUE);
  // gdk_set_allowed_backends("x11");
#endif
  const char *result_x11_be = g_getenv("GDK_BACKEND");
  const char *result_x11_sess = g_getenv("XDG_SESSION_TYPE");
  const char *result_x11_wayland = g_getenv("WAYLAND_DISPLAY");
  t_print("%s: GDK_BACKEND = %s XDG_SESSION_TYPE = %s WAYLAND_DISPLAY = %s\n", __func__, result_x11_be,
          result_x11_sess, result_x11_wayland);
}
#endif

void status_text(const char *text) {
  gtk_label_set_text(GTK_LABEL(status_label), text);
  usleep(100000);

  while (gtk_events_pending ()) {
    gtk_main_iteration ();
  }
}

static pthread_t wisdom_thread_id;
static int wisdom_running = 0;

static void* wisdom_thread(void *arg) {
  int wdsp_subversion = GetWDSPVersion() % 100;
  t_print("%s: WDSP Subversion: %d\n", __func__, wdsp_subversion);

  if (WDSPwisdom ((char *)arg)) {
    t_print("%s: WDSP wisdom file has been rebuilt.\n", __func__);
  } else {
    t_print("%s: Re-using existing WDSP wisdom file.\n", __func__);
  }

  wisdom_running = 0;
  return NULL;
}

const char* get_current_gtk_theme(void) {
  GtkSettings *settings = gtk_settings_get_default();
  gchar *theme_name = NULL;

  if (!settings) {
    return NULL;
  }

  g_object_get(settings, "gtk-theme-name", &theme_name, NULL);

  // Achtung: Übergib eine Kopie, da theme_name freigegeben wird
  if (theme_name) {
    gchar *copy = g_strdup(theme_name);
    g_free(theme_name);
    return copy;
  }

  return NULL;
}

gboolean is_theme_adwaita(void) {
  GtkSettings *settings = gtk_settings_get_default();
  gchar *theme_name = NULL;
  gboolean result = FALSE;

  if (!settings) {
    return FALSE;
  }

  g_object_get(settings, "gtk-theme-name", &theme_name, NULL);

  if (theme_name) {
    if (g_strcmp0(theme_name, "Adwaita") == 0) {
      result = TRUE;
    } else {
      t_print("Active GTK theme is '%s', but not 'Adwaita'\n", theme_name);
    }

    g_free(theme_name);
  }

  return result;
}

// cppcheck-suppress constParameterCallback
gboolean keypress_cb(GtkWidget *widget, GdkEventKey *event, gpointer data) {
  gboolean ret = TRUE;

  // Ignore key-strokes until radio is ready
  if (radio == NULL) { return FALSE; }

  //
  // Intercept key-strokes. The "keypad" stuff
  // has been contributed by Ron.
  // Everything that is not intercepted is handled downstream.
  //
  // space             ==>  MOX
  // u                 ==>  active receiver VFO up
  // d                 ==>  active receiver VFO down
  // Keypad 0..9       ==>  NUMPAD 0 ... 9
  // Keypad Decimal    ==>  NUMPAD DEC
  // Keypad Subtract   ==>  NUMPAD BS
  // Keypad Divide     ==>  NUMPAD CL
  // Keypad Multiply   ==>  NUMPAD Hz
  // Keypad Add        ==>  NUMPAD kHz
  // Keypad Enter      ==>  NUMPAD MHz
  //
  // Function keys invoke Text-to-Speech machine
  // (see tts.c)
  // F1                ==>  Frequency
  // F2                ==>  Mode
  // F3                ==>  Filter width
  // F4                ==>  RX S-meter level
  // F5                ==>  TX drive
  // F6                ==>  Attenuation/Preamp
  //
  switch (event->keyval) {
#ifdef TTS

  case GDK_KEY_F1:
    tts_freq();
    break;

  case GDK_KEY_F2:
    tts_mode();
    break;

  case GDK_KEY_F3:
    tts_filter();
    break;

  case GDK_KEY_F4:
    tts_smeter();
    break;

  case GDK_KEY_F5:
    tts_txdrive();
    break;

  case GDK_KEY_F6:
    tts_atten();
    break;
#endif

  case GDK_KEY_F10:
    if (main_menu == NULL) {
      new_menu();
    }

    break;

  // DH0DM: add additional keyboard shortcuts b,m,v,n,a,w,e,r,T
  case GDK_KEY_b:
    start_band();
    break;

  case GDK_KEY_M:
    start_mode();
    break;

  case GDK_KEY_m:

    // toggle anf
    if (active_receiver->mute_radio > 0) {
      active_receiver->mute_radio = 0;
    } else {
      active_receiver->mute_radio = 1;
    }

    break;

  case GDK_KEY_v:
    start_vfo(active_receiver->id);
    break;

  case GDK_KEY_f:
    start_filter();
    break;

  case GDK_KEY_n:
    start_noise();
    break;

  case GDK_KEY_a:

    // toggle anf
    if (active_receiver->anf > 0) {
      active_receiver->anf = 0;
    } else {
      active_receiver->anf = 1;
    }

    update_noise();
    break;

  case GDK_KEY_w:

    // toggle binaural audio (w)ide
    if (!radio_is_transmitting() && active_receiver->binaural > 0) {
      active_receiver->binaural = 0;
    } else {
      active_receiver->binaural = 1;
    }

    rx_set_af_binaural(active_receiver);
    break;

  case GDK_KEY_e:

    // toggle SNB
    if (active_receiver->snb > 0) {
      active_receiver->snb = 0;
    } else {
      active_receiver->snb = 1;
    }

    update_noise();
    break;

  case GDK_KEY_r:

    // toggle NR
    if (active_receiver->nr == 0) {
      active_receiver->nr = 1;
    } else if (active_receiver->nr == 1) {
      active_receiver->nr = 2;
    } else if (active_receiver->nr == 2) {
      active_receiver->nr = 3;
    } else if (active_receiver->nr == 3) {
      active_receiver->nr = 4;
    } else {
      active_receiver->nr = 0;
    }

    update_noise();
    break;

  case GDK_KEY_T:

    // Tune - Uppercase to avoid unwanted tuning by hitting the t key
    if (can_transmit) {
      if (radio_get_mox() == 1) {
        radio_set_mox(0);
      }

      if (radio_get_tune() == 0) {
        radio_set_tune(1);
      } else {
        radio_set_tune(0);
      }
    }

    break;

  case GDK_KEY_space:

    // DH0DM: changed the logic here for combination with the tune key
    // if tuning is active space will stop tuning and not switching to mox with
    // the same keypress. for me more logical and space remains "emergency tx off"
    // if tx is actice
    if (can_transmit) {
      if (radio_get_tune() == 1) {
        radio_set_tune(0);
      } else {
        if (radio_get_mox() == 1) {
          radio_set_mox(0);
        } else if (TransmitAllowed()) {
          radio_set_mox(1);
        } else {
          tx_set_out_of_band(transmitter);
        }
      }
    }

    break;

  case GDK_KEY_Page_Down: {
    double _vol = active_receiver->volume;
    _vol -= 1;

    if (_vol < -40.0) {
      _vol = -40.0;
    }

    set_af_gain(active_receiver->id, _vol);
  }
  break;

  case GDK_KEY_Page_Up: {
    double _vol = active_receiver->volume;
    _vol += 1;

    if (_vol > 0) {
      _vol = 0;
    }

    set_af_gain(active_receiver->id, _vol);
  }
  break;

  case  GDK_KEY_d:
  case  GDK_KEY_Left:
#ifdef __APPLE__

    // Shift + Option
    if ( (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK)) == (GDK_SHIFT_MASK | GDK_MOD1_MASK) ) {
      vfo_id_step(1 - active_receiver->id, -10);
    }
    // nur Shift
    else if ( (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK ) {
      vfo_step(-10);
    }
    // nur Option
    else if ( (event->state & GDK_MOD1_MASK) == GDK_MOD1_MASK ) {
      vfo_id_step(1 - active_receiver->id, -1);
    } else {
      vfo_step(-1);
    }

#else

    // Nicht-macOS: nur Shift beschleunigt
    if ( (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK ) {
      vfo_step(-10);
    } else {
      vfo_step(-1);
    }

#endif
    break;

  case GDK_KEY_u:
  case GDK_KEY_Right:
#ifdef __APPLE__

    // Shift + Option
    if ( (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK)) == (GDK_SHIFT_MASK | GDK_MOD1_MASK) ) {
      vfo_id_step(1 - active_receiver->id, 10);
    }
    // nur Shift
    else if ( (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK ) {
      vfo_step(10);
    }
    // nur Option
    else if ( (event->state & GDK_MOD1_MASK) == GDK_MOD1_MASK ) {
      vfo_id_step(1 - active_receiver->id, 1);
    } else {
      vfo_step(1);
    }

#else

    // Nicht-macOS: nur Shift beschleunigt
    if ( (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK ) {
      vfo_step(10);
    } else {
      vfo_step(1);
    }

#endif
    break;

  //
  // Suggestion of Richard: using U and D for changing
  // the frequency of the "other" VFO in large steps
  // (useful for split operation)
  //
  case  GDK_KEY_U:
    vfo_id_step(1 - active_receiver->id, 10);
    break;

  case  GDK_KEY_D:
    vfo_id_step(1 - active_receiver->id, -10);
    break;

  case GDK_KEY_q:
    if (event->state & GDK_CONTROL_MASK) {
      stop_program();
      _exit(0);
    }

    break;

  // add an idea from DH0DM: press key [s] decrease the VFO step, press key [S] increase the VFO step
  case GDK_KEY_s: {
    int i = vfo_get_stepindex(active_receiver->id);
    vfo_set_step_from_index(active_receiver->id, --i);
    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case GDK_KEY_S: {
    int i = vfo_get_stepindex(active_receiver->id);
    vfo_set_step_from_index(active_receiver->id, ++i);
    g_idle_add(ext_vfo_update, NULL);
  }
  break;
#if defined (__AUTOG__)

  case GDK_KEY_g: {
    autogain_is_adjusted = 0;
    g_idle_add(ext_vfo_update, NULL);
  }
  break;

  case GDK_KEY_G: {
    set_rf_gain(active_receiver->id, 14.0);
    autogain_is_adjusted = 0;
    g_idle_add(ext_vfo_update, NULL);
  }
  break;
#endif

  //
  // This is a contribution of Ron, it uses a keypad for
  // entering a frequency
  //
  case GDK_KEY_KP_0:
    vfo_num_pad(0, active_receiver->id);
    break;

  case GDK_KEY_KP_1:
    vfo_num_pad(1, active_receiver->id);
    break;

  case GDK_KEY_KP_2:
    vfo_num_pad(2, active_receiver->id);
    break;

  case GDK_KEY_KP_3:
    vfo_num_pad(3, active_receiver->id);
    break;

  case GDK_KEY_KP_4:
    vfo_num_pad(4, active_receiver->id);
    break;

  case GDK_KEY_KP_5:
    vfo_num_pad(5, active_receiver->id);
    break;

  case GDK_KEY_KP_6:
    vfo_num_pad(6, active_receiver->id);
    break;

  case GDK_KEY_KP_7:
    vfo_num_pad(7, active_receiver->id);
    break;

  case GDK_KEY_KP_8:
    vfo_num_pad(8, active_receiver->id);
    break;

  case GDK_KEY_KP_9:
    vfo_num_pad(9, active_receiver->id);
    break;

  case GDK_KEY_KP_Divide:
    vfo_num_pad(-1, active_receiver->id);
    break;

  case GDK_KEY_KP_Multiply:
    vfo_num_pad(-2, active_receiver->id);
    break;

  case GDK_KEY_KP_Add:
    vfo_num_pad(-3, active_receiver->id);
    break;

  case GDK_KEY_KP_Enter:
    vfo_num_pad(-4, active_receiver->id);
    break;
#if defined (__APPLE__)

  case GDK_KEY_comma:
#else
  case GDK_KEY_KP_Decimal:
  case GDK_KEY_KP_Separator:
#endif
    vfo_num_pad(-5, active_receiver->id);
    break;

  case GDK_KEY_KP_Subtract:
    vfo_num_pad(-6, active_receiver->id);
    break;

  default:
    // not intercepted, so handle downstream
    ret = FALSE;
    break;
  }

  g_idle_add(ext_vfo_update, NULL);
  return ret;
}

// cppcheck-suppress constParameterCallback
gboolean main_delete (GtkWidget *widget) {
  if (radio != NULL) {
    stop_program();
  }

  _exit(0);
}

static GdkPixbuf *create_pixbuf_from_data(void) {
  GInputStream *mem_stream;
  GdkPixbuf *pixbuf, *scaled_pixbuf;
  GError *error = NULL;
  mem_stream = g_memory_input_stream_new_from_data(trx_logo, trx_logo_len, NULL);
  pixbuf = gdk_pixbuf_new_from_stream(mem_stream, NULL, &error);

  if (!pixbuf) {
    g_printerr("ERROR loading pic: %s\n", error->message);
    g_error_free(error);
    g_object_unref(mem_stream);
    return NULL;
  }

  // pic scaling
  scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, 100, 100, GDK_INTERP_BILINEAR);
  g_object_unref(pixbuf);  // free original-pixbuf
  g_object_unref(mem_stream);
  return scaled_pixbuf;
}

static int init(void *data) {
  char wisdom_directory[1025];
  char text[1024];
  t_print("%s\n", __func__);
  t_print("LC_ALL=%s\n", setlocale(LC_ALL, NULL));
  t_print("LC_NUMERIC=%s\n", setlocale(LC_NUMERIC, NULL));
  audio_get_cards();
  {
    GdkDisplay *dpy = gdk_display_get_default();
    /* Wayland: named cursors sind stabiler */
    cursor_arrow = gdk_cursor_new_from_name(dpy, "default");

    if (!cursor_arrow) { cursor_arrow = gdk_cursor_new(GDK_ARROW); }

    cursor_watch = gdk_cursor_new_from_name(dpy, "wait");

    if (!cursor_watch) { cursor_watch = gdk_cursor_new(GDK_WATCH); }

    gdk_window_set_cursor(gtk_widget_get_window(top_window), cursor_watch);
  }
  //
  // Let WDSP (via FFTW) check for wisdom file in current dir
  // If there is one, the "wisdom thread" takes no time
  // Depending on the WDSP version, the file is wdspWisdom or wdspWisdom00.
  //
  (void) getcwd(text, sizeof(text));
  snprintf(wisdom_directory, sizeof(wisdom_directory), "%s/", text);
  t_print("Securing wisdom file in directory: %s\n", wisdom_directory);
  status_text("Checking FFTW Wisdom file ...");
  wisdom_running = 1;
  pthread_create(&wisdom_thread_id, NULL, wisdom_thread, wisdom_directory);

  while (wisdom_running) {
    // wait for the wisdom thread to complete, meanwhile
    // handling any GTK events.
    usleep(100000); // 100ms

    while (gtk_events_pending ()) {
      gtk_main_iteration ();
    }

    snprintf(text, sizeof(text), "Please do not close this window until wisdom plans are completed ...\n\n... %s",
             wisdom_get_status());
    status_text(text);
  }

  //
  // When widsom plans are complete, start discovery process
  //
  g_timeout_add(100, delayed_discovery, NULL);
  return 0;
}

/* optional: Cursor-Objekte später freigeben, z.B. am Programmende */
/* g_object_unref(cursor_arrow); g_object_unref(cursor_watch); */

static void activate_deskhpsdr(GtkApplication *app, gpointer data) {
  /*
  #if !defined(__WAYLAND__)

    if (g_getenv("DESKHPSDR_WAYLAND_NOTICE")) {
      show_error_dialog(
        "\nYou are using Wayland as X11 backend, which is not supported by deskHPSDR.\n\n"
        "Using Xorg instead Wayland as X11 backend is strongly recommended.\n\n"
        "The Wayland X11 backend break some GTK API functions that deskHPSDR\n"
        "relies on. deskHPSDR is able to run under Wayland, but some GTK APIs\n"
        "do not behave as expected, which results in restricted functionality.\n\n"
        "If you agree and understand, select Continue to proceed.");
    }

  #endif
  */
  // Hier setzen wir den GTK-Mainloop-Thread
  deskhpsdr_main_thread = pthread_self();
#ifdef MIDI
  g_mutex_init(&vfo_timer.lock);
  g_mutex_init(&vfoa_timer.lock);
  g_mutex_init(&vfob_timer.lock);
#endif
  char text[2048];
  char config_directory[1024];
  (void) getcwd(config_directory, sizeof(config_directory));
  const char *x11_be = g_getenv("GDK_BACKEND");
  g_strlcat(config_directory, "/", 1024);
  t_print("Build: %s (Branch: %s, Commit: %s, Date: %s)\n", build_version, build_branch, build_commit, build_date);
  t_print("GTK+ version %u.%u.%u\n", gtk_major_version, gtk_minor_version, gtk_micro_version);
  uname(&unameData);

  if (x11_be && *x11_be != '\0') {
    t_print("X11 Backend: %s\n", x11_be);
  } else {
    t_print("X11 Backend not set.\n");
  }

  t_print("sysname: %s\n", unameData.sysname);
  t_print("nodename: %s\n", unameData.nodename);
  t_print("release: %s\n", unameData.release);
  t_print("version: %s\n", unameData.version);
  t_print("machine: %s\n", unameData.machine);
  GdkDisplay *display = gdk_display_get_default();

  if (display == NULL) {
    t_print("no default display!\n");
    _exit(0);
  }

  screen = gdk_display_get_default_screen(display);
#ifdef __linux__
  t_print("Forcing GTK theme to Adwaita\n");
  gtk_settings_set_string_property(
    gtk_settings_get_default(),
    "gtk-theme-name",
    "Adwaita",
    "application"
  );
#else
  t_print("Non-Linux system – skipping GTK theme override\n");
#endif

  if (!is_theme_adwaita()) {
    t_print("Note: Adwaita GTK theme is not active. Display of some widgets can be limited.\n");
  } else {
    t_print("Note: Adwaita GTK theme is active. Thats the recommend setting.\n");
  }

  if (screen == NULL) {
    t_print("no default screen!\n");
    _exit(0);
  }

  // Jetzt erst CSS laden, damit es das Theme übersteuert
  load_css();
  //
  // Create top window with minimum size
  //
  t_print("create top level window\n");
  top_window = gtk_application_window_new (app);
  gtk_widget_set_size_request(top_window, 100, 100);
  const char *used_theme = get_current_gtk_theme();
  char _title[128];
  snprintf(_title, sizeof(_title), "%s by DL1BZ %s [GTK Theme: %s]  WDSP Version %d.%02d", PGNAME, build_version,
           used_theme,
           GetWDSPVersion() / 100, GetWDSPVersion() % 100);
  gtk_window_set_title (GTK_WINDOW (top_window), _title);
#if defined(__APPLE__)
  gtk_window_set_hide_titlebar_when_maximized(GTK_WINDOW(top_window), TRUE);
#endif
  //
  // do not use GTK_WIN_POS_CENTER_ALWAYS, since this will let the
  // window jump back to the center each time the window is
  // re-created, e.g. in reconfigure_radio()
  //
  // Note: enabling "resizable" leads to strange behaviour in the
  //       Wayland window manager so we  suppress this. All resize
  //       events are "programmed" and not "user intervention"
  //       anyway.
  //
  gtk_window_set_position(GTK_WINDOW(top_window), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable(GTK_WINDOW(top_window), FALSE);
  gtk_window_set_deletable(GTK_WINDOW(top_window), FALSE); // remove close button in main window
  //
  // Get the position of the top window, and then determine
  // to which monitor this position belongs.
  //
  int x = 0, y = 0;

  if (!use_wayland) {
    gtk_window_get_position(GTK_WINDOW(top_window), &x, &y);
    this_monitor = gdk_screen_get_monitor_at_point(screen, x, y);
  } else {
    /* Wayland: Positionsabfragen sind unzuverlässig → Primary Monitor nehmen */
    int pm = gdk_screen_get_primary_monitor(screen);
    this_monitor = (pm >= 0) ? pm : 0;
  }

  t_print("Monitor Number within Screen=%d\n", this_monitor);
  //
  // Determine the size of "our" monitor
  //
  GdkRectangle rect;
  gdk_screen_get_monitor_geometry(screen, this_monitor, &rect);
  screen_width = rect.width;
  screen_height = rect.height;
  t_print("Monitor: width=%d height=%d\n", screen_width, screen_height);
  display_width  = 1280;
  display_height = 600;
  full_screen    = 0;

  //
  // Go to full-screen mode by default, if the screen size is approx. 800*480
  //
  if (screen_width > 780 && screen_width < 820 && screen_height > 460 && screen_height < 500) {
    full_screen = 1;
    display_width = screen_width;
    display_height = screen_height;
  }

  t_print("display_width=%d display_height=%d\n", display_width, display_height);

  if (full_screen) {
    t_print("full screen\n");

    if (use_wayland) {
      /* Wayland: Monitor-Auswahl liegt beim Compositor */
      gtk_window_fullscreen(GTK_WINDOW(top_window));
    } else {
      gtk_window_fullscreen_on_monitor(GTK_WINDOW(top_window), screen, this_monitor);
    }
  }

  // load the TRX logo now only from the included trx_logo.h
  GtkWidget *trx_logo_widget = gtk_image_new_from_pixbuf(create_pixbuf_from_data());
  g_signal_connect (top_window, "delete-event", G_CALLBACK (main_delete), NULL);
  //
  // We want to use the space-bar as an alternative to go to TX
  //
  gtk_widget_add_events(top_window, GDK_KEY_PRESS_MASK);
  g_signal_connect(top_window, "key_press_event", G_CALLBACK(keypress_cb), NULL);
  t_print("create grid\n");
  topgrid = gtk_grid_new();
  int row = 0;
  int col = 0;
  // we make the first startup windows smaller, looks better
  gtk_widget_set_size_request(topgrid, display_width * 0.7, display_height * 0.8);
  gtk_grid_set_row_homogeneous(GTK_GRID(topgrid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(topgrid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(topgrid), 10);
  t_print("add grid\n");
  gtk_container_add (GTK_CONTAINER (top_window), topgrid);
  t_print("add image to grid\n");
  gtk_grid_attach(GTK_GRID(topgrid), trx_logo_widget, col, row, 1, 2);
  t_print("create deskHPSDR label\n");
  //----------------------------------------------------------------------------------
  col++;
  snprintf(text, sizeof(text),
           "\ndeskHPSDR by Heiko Amft, DL1BZ (dl1bz@bzsax.de)\n");
  GtkWidget *deskhpsdr_label = gtk_label_new(text);
  gtk_widget_set_name(deskhpsdr_label, "big_txt");
  gtk_widget_set_halign(deskhpsdr_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), deskhpsdr_label, col, row, 3, 1);
  //----------------------------------------------------------------------------------
  row++;
  snprintf(text, sizeof(text),
           "Open Source Ham Radio SDR Transceiver Frontend Application\n"
           "compatible with OpenHPSDR Protocols 1 and 2");
  GtkWidget *deskhpsdr_sub_label = gtk_label_new(text);
  gtk_widget_set_name(deskhpsdr_sub_label, "med_bold_txt");
  gtk_widget_set_halign(deskhpsdr_sub_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), deskhpsdr_sub_label, col, row, 3, 1);
  //----------------------------------------------------------------------------------
  row++;
  snprintf(text, sizeof(text),
           "& partially Soapy API (only with limited device support)\n");
  GtkWidget *deskhpsdr_soapy_sub_label = gtk_label_new(text);
  gtk_widget_set_name(deskhpsdr_soapy_sub_label, "small_bold_txt_blue");
  gtk_widget_set_halign(deskhpsdr_soapy_sub_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), deskhpsdr_soapy_sub_label, col, row, 3, 1);
  //----------------------------------------------------------------------------------
  row += 2;
  t_print("create build label\n");
  snprintf(text, sizeof(text),
           "Version %s (build %s from %s branch)\ncompiled with %s\nActivated Compiler Macros:\n%s\nCurrent WDSP version: %d.%02d\nSelected Audio module: %s\nWorking Directory: %s\nX11 backend: %s",
           build_version, build_date, build_branch, __VERSION__, build_options, GetWDSPVersion() / 100, GetWDSPVersion() % 100,
           build_audio, config_directory, x11_be);
  GtkWidget *build_date_label = gtk_label_new(text);
  gtk_widget_set_name(build_date_label, "med_txt");
  gtk_widget_set_halign(build_date_label, GTK_ALIGN_START);
  t_print("add build label to grid\n");
  gtk_grid_attach(GTK_GRID(topgrid), build_date_label, col, row, 3, 1);
  row++;
  t_print("create status\n");
  status_label = gtk_label_new(NULL);
  gtk_widget_set_name(status_label, "med_txt");
  gtk_widget_set_halign(status_label, GTK_ALIGN_START);
  t_print("add status to grid\n");
  gtk_grid_attach(GTK_GRID(topgrid), status_label, col, row, 3, 1);
  gtk_widget_show_all(top_window);
  t_print("g_idle_add: init\n");
  g_idle_add(init, NULL);
}

int main(int argc, char **argv) {
#if !defined(__WAYLAND__)
  enforce_x11_backend_policy();
#endif
  GtkApplication *deskhpsdr;
  int rc;
  char name[1024];

  //
  // If invoked with -V, print version and FPGA firmware compatibility information
  //
  if (argc >= 2 && !strcmp("-V", argv[1])) {
    uname(&unameData);
    fprintf(stderr, "deskHPSDR version %s [%s] (branch %s - commit %s), built date %s with %s\n", build_version,
            unameData.machine,
            build_branch, build_commit, build_date, __VERSION__);
    fprintf(stderr, "Compile-time options      : %sAudioModule=%s\n", build_options, build_audio);
#ifdef SATURN
    fprintf(stderr, "SATURN min:max minor FPGA : %d:%d\n", saturn_minor_version_min(), saturn_minor_version_max());
    fprintf(stderr, "SATURN min:max major FPGA : %d:%d\n", saturn_major_version_min(), saturn_major_version_max());
#endif
    exit(0);
  }

  //
  // The following call will most likely fail (until this program
  // has the privileges to reduce the nice value). But if the
  // privilege is there, it may help to run deskHPSDR at a lower nice
  // value.
  //
  startup(argv[0]);
  setlocale(LC_ALL, "C");
  rc = getpriority(PRIO_PROCESS, 0);
  t_print("Base priority on startup: %d\n", rc);
  setpriority(PRIO_PROCESS, 0, -10);
  rc = getpriority(PRIO_PROCESS, 0);
  t_print("Base priority after adjustment: %d\n", rc);
  t_print("%s: init global cURL...\n", __func__);
  curl_global_init(CURL_GLOBAL_ALL);
  toolset_init();
  snprintf(name, 1024, "org.dl1bz.deskhpsdr.pid%d", getpid());
  t_print("%s: gtk_application_new: %s -> X11 backend use Wayland ? : %d\n", __func__, name, use_wayland);
  gtk_disable_setlocale();  // keep having a decimal point as a decimal point
  deskhpsdr = gtk_application_new(name, G_APPLICATION_FLAGS_NONE);
  g_signal_connect(deskhpsdr, "activate", G_CALLBACK(activate_deskhpsdr), NULL);
  rc = g_application_run(G_APPLICATION(deskhpsdr), argc, argv);
  t_print("exiting ...\n");
  g_object_unref(deskhpsdr);
#ifdef MIDI
  g_mutex_clear(&vfo_timer.lock);
  g_mutex_clear(&vfoa_timer.lock);
  g_mutex_clear(&vfob_timer.lock);
#endif
  return rc;
}

int fatal_error(void *data) {
  //
  // This replaces the calls to exit. It now emits
  // a GTK modal dialog waiting for user response.
  // After this response, the program exits.
  //
  // The red color chosen for the first string should
  // work both on the dark and light themes.
  //
  // Note this must only be called from the "main thread", that is,
  // you can only invoke this function via g_idle_add()
  //
  const gchar *msg = (gchar *) data;
  static int quit = 0;

  if (quit) {
    return 0;
  }

  quit = 1;

  if (top_window) {
    GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
    GtkWidget *dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW(top_window),
                        flags,
                        GTK_MESSAGE_ERROR,
                        GTK_BUTTONS_CLOSE,
                        "<span color='red' size='x-large' weight='bold'>deskHPSDR termination due to fatal error:</span>"
                        "\n\n<span size='x-large'>   %s</span>\n\n",
                        msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
  }

  exit(1);
}
