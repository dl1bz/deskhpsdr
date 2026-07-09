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
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "new_menu.h"
#include "rx_menu.h"
#include "band.h"
#include "discovered.h"
#include "filter.h"
#include "radio.h"
#include "receiver.h"
#include "sliders.h"
#include "new_protocol.h"
#include "message.h"
#include "rigctl.h"
#include "ext.h"
#include "tci.h"

static GtkWidget *dialog = NULL;
static GtkWidget *autogain_b;
static GtkWidget *autogain_time_b;
static GtkWidget *rx_menu_headerbar = NULL;
static GtkWidget *rx_menu_stack = NULL;
static GtkWidget *rx_menu_tab_buttons[3] = { NULL, NULL, NULL };
static gboolean rx_menu_updating_tabs = FALSE;

enum {
  RX_MENU_TAB_RX1 = 0,
  RX_MENU_TAB_RX2 = 1,
  RX_MENU_TAB_OPTIONS = 2,
  RX_MENU_TAB_COUNT = 3
};

static void cleanup(void) {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    rx_menu_headerbar = NULL;
    rx_menu_stack = NULL;
    for (int i = 0; i < RX_MENU_TAB_COUNT; i++) {
      rx_menu_tab_buttons[i] = NULL;
    }
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb(void) {
  cleanup();
  return TRUE;
}

static void response_cb(GtkDialog *dlg, gint response_id, gpointer data) {
  (void)dlg;
  (void)response_id;
  (void)data;
  cleanup();
}

static const char *rx_menu_tab_name(gint page_num) {
  switch (page_num) {
  case RX_MENU_TAB_RX1:
    return "RX1";
  case RX_MENU_TAB_RX2:
    return "RX2";
  case RX_MENU_TAB_OPTIONS:
    return "Options";
  default:
    return "";
  }
}

static void rx_menu_update_title(GtkWidget *titlebar, gint page_num) {
  char title[96];
  const char *tab = rx_menu_tab_name(page_num);
  if (page_num == RX_MENU_TAB_RX1 && receiver[0] != NULL) {
    snprintf(title, sizeof(title), "%s - Receive - RX1 - ADC%d", PGNAME, receiver[0]->adc);
  } else if (page_num == RX_MENU_TAB_RX2 && receivers > 1 && receiver[1] != NULL) {
    snprintf(title, sizeof(title), "%s - Receive - RX2 - ADC%d", PGNAME, receiver[1]->adc);
  } else if (tab[0] != '\0') {
    snprintf(title, sizeof(title), "%s - Receive - %s", PGNAME, tab);
  } else {
    snprintf(title, sizeof(title), "%s - Receive", PGNAME);
  }
  if (dialog != NULL) {
    gtk_window_set_title(GTK_WINDOW(dialog), title);
  }
  if (GTK_IS_HEADER_BAR(titlebar)) {
    gtk_header_bar_set_title(GTK_HEADER_BAR(titlebar), title);
  }
}

static const char *rx_menu_stack_name(gint page_num) {
  switch (page_num) {
  case RX_MENU_TAB_RX1:
    return "rx1";
  case RX_MENU_TAB_RX2:
    return "rx2";
  case RX_MENU_TAB_OPTIONS:
    return "options";
  default:
    return "rx1";
  }
}

static void rx_menu_select_tab(gint page_num) {
  int i;
  if (page_num < 0 || page_num >= RX_MENU_TAB_COUNT) {
    page_num = RX_MENU_TAB_RX1;
  }
  if (page_num == RX_MENU_TAB_RX2 && receivers <= 1) {
    page_num = RX_MENU_TAB_RX1;
  }
  rx_menu_updating_tabs = TRUE;
  if (rx_menu_stack != NULL) {
    gtk_stack_set_visible_child_name(GTK_STACK(rx_menu_stack), rx_menu_stack_name(page_num));
  }
  for (i = 0; i < RX_MENU_TAB_COUNT; i++) {
    if (rx_menu_tab_buttons[i] != NULL) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rx_menu_tab_buttons[i]), i == page_num);
    }
  }
  rx_menu_updating_tabs = FALSE;
  rx_menu_update_title(rx_menu_headerbar, page_num);
}

static void rx_menu_tab_button_cb(GtkToggleButton *button, gpointer data) {
  gint page_num = GPOINTER_TO_INT(data);
  if (rx_menu_updating_tabs) {
    return;
  }
  if (gtk_toggle_button_get_active(button)) {
    rx_menu_select_tab(page_num);
  } else {
    rx_menu_updating_tabs = TRUE;
    gtk_toggle_button_set_active(button, TRUE);
    rx_menu_updating_tabs = FALSE;
  }
}

static GtkWidget *rx_menu_tab_button_new(const char *label, gint page_num) {
  GtkWidget *button = gtk_toggle_button_new_with_label(label);
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
  gtk_widget_set_size_request(button, 110, 34);
  gtk_widget_set_halign(button, GTK_ALIGN_START);
  g_signal_connect(button, "toggled", G_CALLBACK(rx_menu_tab_button_cb), GINT_TO_POINTER(page_num));
  return button;
}

static GtkWidget *rx_menu_grid_new(void) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
  return grid;
}

static GtkWidget *rx_menu_section_new(const char *title, GtkWidget **section_grid) {
  GtkWidget *frame = gtk_frame_new(title);
  gtk_widget_set_hexpand(frame, TRUE);
  gtk_widget_set_halign(frame, GTK_ALIGN_FILL);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
  GtkWidget *grid = rx_menu_grid_new();
  gtk_container_add(GTK_CONTAINER(frame), grid);
  if (section_grid != NULL) {
    *section_grid = grid;
  }
  return frame;
}

static RECEIVER *rx_from_data(gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;
  return rx != NULL ? rx : active_receiver;
}

static gboolean rx_menu_original_protocol_global_dither_random(void) {
  return protocol == ORIGINAL_PROTOCOL;
}

static void rx_menu_sync_original_protocol_dither_random_from_effective(void) {
  int dither = 0;
  int random = 0;

  if (!rx_menu_original_protocol_global_dither_random() || receiver[0] == NULL) {
    return;
  }

  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL) {
      dither |= receiver[i]->dither;
      random |= receiver[i]->random;
    }
  }

  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL) {
      receiver[i]->dither = dither;
      receiver[i]->random = random;
    }
  }
}

static void rx_menu_set_original_protocol_dither_all(int enabled) {
  if (!rx_menu_original_protocol_global_dither_random()) {
    return;
  }

  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL) {
      receiver[i]->dither = enabled;
    }
  }
}

static void rx_menu_set_original_protocol_random_all(int enabled) {
  if (!rx_menu_original_protocol_global_dither_random()) {
    return;
  }

  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL) {
      receiver[i]->random = enabled;
    }
  }
}

static void rx_menu_sync_shared_sample_rate(RECEIVER *rx, int sample_rate) {
  if (rx == NULL || protocol != ORIGINAL_PROTOCOL || n_adc > 1 || receivers <= 1) {
    return;
  }

  for (int i = 0; i < receivers; i++) {
    if (receiver[i] != NULL && receiver[i] != rx && receiver[i]->sample_rate != sample_rate) {
      rx_change_sample_rate(receiver[i], sample_rate);
    }
  }
}

#if defined (__AUTOG__)
static void autogain_cb(GtkWidget *widget, gpointer data) {
  autogain_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  update_slider_autogain_btn();
  if (autogain_enabled) {
    gtk_widget_set_sensitive(autogain_time_b, TRUE);
  } else {
    gtk_widget_set_sensitive(autogain_time_b, FALSE);
  }
  launch_autogain_hl2();
  g_idle_add(ext_vfo_update, NULL);
}

static void autogain_time_cb(GtkWidget *widget, gpointer data) {
  autogain_time_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  // update_slider_autogain_btn();
  restart_autogain_hl2();
  g_idle_add(ext_vfo_update, NULL);
}

#endif

static void dither_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  int enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  rx->dither = enabled;
  if (rx_menu_original_protocol_global_dither_random()) {
    rx_menu_set_original_protocol_dither_all(enabled);
  }
  schedule_receive_specific();
}

static void random_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  int enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  rx->random = enabled;
  if (rx_menu_original_protocol_global_dither_random()) {
    rx_menu_set_original_protocol_random_all(enabled);
  }
  schedule_receive_specific();
}

static void preamp_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  rx->preamp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void alex_att_cb(GtkWidget *widget, gpointer data) {
  if (have_alex_att) {
    int val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    radio_set_alex_attenuation(val);
  }
}

static void sample_rate_cb(GtkToggleButton *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  const char *p = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  int samplerate;
  //
  // There are so many different possibilities for sample rates, so
  // we just "scanf" from the combobox text entry
  //
  if (p == NULL || sscanf(p, "%d", &samplerate) != 1) { return; }
  rx_change_sample_rate(rx, samplerate);
  rx_menu_sync_shared_sample_rate(rx, samplerate);
}

static void adc_cb(GtkToggleButton *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  rx->adc = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  rx_change_adc(rx);
}

static void local_audio_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  t_print("local_audio_cb: rx=%d\n", rx->id);
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    if (audio_open_output(rx) == 0) {
      rx->local_audio = 1;
    } else {
      t_print("local_audio_cb: audio_open_output failed\n");
      rx->local_audio = 0;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
    }
  } else {
    if (rx->local_audio) {
      rx->local_audio = 0;
      audio_close_output(rx);
    }
  }
  t_print("local_audio_cb: local_audio=%d\n", rx->local_audio);
}

static void mute_audio_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  rx->mute_when_not_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void mute_radio_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  rx->mute_radio = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void adc0_filter_bypass_cb(GtkWidget *widget, gpointer data) {
  adc0_filter_bypass = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_high_priority();
}

static void adc1_filter_bypass_cb(GtkWidget *widget, gpointer data) {
  adc1_filter_bypass = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_high_priority();
}

#ifdef __APPLE__
static void wheel_present_options_cb(GtkWidget *widget, gpointer data) {
  (void)data;
  int enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (receiver[0] != NULL) {
    receiver[0]->wheel_present = enabled;
  }
  if (receivers > 1 && receiver[1] != NULL) {
    receiver[1]->wheel_present = enabled;
  }
}
#endif

//
// possible the device has been changed:
// call audio_close_output with old device, audio_open_output with new one
//
static void local_output_changed_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  GtkWidget *local_audio_b = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "local-audio-button"));
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  if (rx->local_audio) {
    audio_close_output(rx);                     // audio_close with OLD device
  }
  if (i >= 0) {
    t_print("local_output_changed rx=%d %s\n", rx->id, output_devices[i].name);
    g_strlcpy(rx->audio_name, output_devices[i].name, sizeof(rx->audio_name));
  }
  if (rx->local_audio) {
    if (audio_open_output(rx) < 0) {           // audio_open with NEW device
      rx->local_audio = 0;
      if (local_audio_b != NULL) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(local_audio_b), FALSE);
      }
    }
    update_slider_binaural_btn();
  }
  t_print("local_output_changed rx=%d local_audio=%d\n", rx->id, rx->local_audio);
}

static void audio_channel_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  int val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  switch (val) {
  case 0:
    rx->audio_channel = STEREO;
    break;
  case 1:
    rx->audio_channel = LEFT;
    break;
  case 2:
    rx->audio_channel = RIGHT;
    break;
  }
}

static void digi_offset_u_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  rx->digi_offset_u = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  rx_frequency_changed(rx);
  tci_digu_offset_changed();
}

static void digi_offset_l_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = rx_from_data(data);
  rx->digi_offset_l = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  rx_frequency_changed(rx);
  tci_digl_offset_changed();
}

static void digi_offset_rtty_cb(GtkWidget *widget, gpointer data) {
  GtkWidget *digu = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "digu-offset"));
  GtkWidget *digl = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "digl-offset"));
  (void)data;
  if (digu != NULL) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(digu), 1500.0);
  }
  if (digl != NULL) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(digl), 2210.0);
  }
}

static void digi_offset_off_cb(GtkWidget *widget, gpointer data) {
  GtkWidget *digu = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "digu-offset"));
  GtkWidget *digl = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "digl-offset"));
  (void)data;
  if (digu != NULL) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(digu), 0.0);
  }
  if (digl != NULL) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(digl), 0.0);
  }
}

static void add_sample_rate_control(GtkWidget *grid, RECEIVER *rx, int *row) {
  char label[32];
  snprintf(label, sizeof(label), "Sample Rate RX%d", rx->id + 1);
  GtkWidget *sample_rate_label = gtk_label_new(label);
  gtk_widget_set_name(sample_rate_label, "boldlabel");
  gtk_widget_set_halign(sample_rate_label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), sample_rate_label, 0, *row, 1, 1);
  GtkWidget *sample_rate_combo_box = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "48000");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "96000");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "192000");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "384000");
  if (hermes_mode != HERMES_MODE_BRICK) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "768000");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "1536000");
  }
  switch (rx->sample_rate) {
  case 48000:
    gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 0);
    break;
  case 96000:
    gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 1);
    break;
  case 192000:
    gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 2);
    break;
  case 384000:
    gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 3);
    break;
  case 768000:
    gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 4);
    break;
  case 1536000:
    gtk_combo_box_set_active(GTK_COMBO_BOX(sample_rate_combo_box), 5);
    break;
  }
  my_combo_attach(GTK_GRID(grid), sample_rate_combo_box, 1, *row, 1, 1);
  if (diversity_enabled && rx->id == 1) {
    gtk_widget_set_sensitive(sample_rate_combo_box, FALSE);
    gtk_widget_set_tooltip_text(sample_rate_combo_box,
                                "RX2 sample rate follows RX1 while diversity is enabled.");
  }
  g_signal_connect(sample_rate_combo_box, "changed", G_CALLBACK(sample_rate_cb), rx);
  (*row)++;
}

static void add_adc_control(GtkWidget *grid, RECEIVER *rx, int *row) {
  int i;
  gboolean adc_controlled_by_matrix = (protocol == NEW_PROTOCOL &&
                                       (device == NEW_DEVICE_HERMES ||
                                        device == NEW_DEVICE_ANGELIA));
  if (n_adc > 1 && !adc_controlled_by_matrix) {
    GtkWidget *adc_label = gtk_label_new("Select ADC");
    gtk_widget_set_name(adc_label, "boldlabel");
    gtk_widget_set_halign(adc_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), adc_label, 0, *row, 1, 1);
    GtkWidget *adc_combo_box = gtk_combo_box_text_new();
    for (i = 0; i < n_adc; i++) {
      char label[32];
      snprintf(label, 32, "ADC-%d", i);
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(adc_combo_box), NULL, label);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(adc_combo_box), rx->adc);
    my_combo_attach(GTK_GRID(grid), adc_combo_box, 1, *row, 1, 1);
    g_signal_connect(adc_combo_box, "changed", G_CALLBACK(adc_cb), rx);
    (*row)++;
  } else if (n_adc > 1 && adc_controlled_by_matrix) {
    GtkWidget *adc_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(adc_label), "<b>RX ADC assignment in ADC/DDC Menu</b>");
    gtk_label_set_justify(GTK_LABEL(adc_label), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(adc_label), 0.5);
    // gtk_widget_set_name(adc_label, "boldlabel");
    gtk_widget_set_halign(adc_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), adc_label, 0, *row, 2, 1);
    (*row)++;
  }
}

static void add_dither_random_controls(GtkWidget *grid, RECEIVER *rx, int *row) {
  gboolean original_protocol_rx2_global = rx_menu_original_protocol_global_dither_random() && rx->id > 0;

  if (!have_dither) {
    return;
  }

  rx_menu_sync_original_protocol_dither_random_from_effective();

  // We assume Dither/Random are either both available or both not available
  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
    GtkWidget *dither_b = gtk_check_button_new_with_label("HL2 Band Volts / Dither Bit");
    gtk_widget_set_name(dither_b, "boldlabel");
    gtk_widget_set_tooltip_text(dither_b, "activate Band Voltage output at the Hermes Lite 2");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dither_b), rx->dither);
    gtk_widget_set_sensitive(dither_b, !original_protocol_rx2_global);
    gtk_grid_attach(GTK_GRID(grid), dither_b, 0, *row, 1, 1);
    if (original_protocol_rx2_global) {
      gtk_widget_set_tooltip_text(dither_b, "P1 dither is global and is controlled from RX1.");
    }
    g_signal_connect(dither_b, "toggled", G_CALLBACK(dither_cb), rx);
    GtkWidget *random_b = gtk_check_button_new_with_label("Random Bit");
    gtk_widget_set_name(random_b, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(random_b), rx->random);
    gtk_widget_set_sensitive(random_b, !original_protocol_rx2_global);
    gtk_grid_attach(GTK_GRID(grid), random_b, 1, *row, 1, 1);
    if (original_protocol_rx2_global) {
      gtk_widget_set_tooltip_text(random_b, "P1 random is global and is controlled from RX1.");
    }
    g_signal_connect(random_b, "toggled", G_CALLBACK(random_cb), rx);
  } else {
    GtkWidget *dither_b = gtk_check_button_new_with_label("Dither Bit");
    gtk_widget_set_name(dither_b, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dither_b), rx->dither);
    gtk_widget_set_sensitive(dither_b, !original_protocol_rx2_global);
    gtk_grid_attach(GTK_GRID(grid), dither_b, 0, *row, 1, 1);
    if (original_protocol_rx2_global) {
      gtk_widget_set_tooltip_text(dither_b, "P1 dither is global and is controlled from RX1.");
    }
    g_signal_connect(dither_b, "toggled", G_CALLBACK(dither_cb), rx);
    GtkWidget *random_b = gtk_check_button_new_with_label("Random Bit");
    gtk_widget_set_name(random_b, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(random_b), rx->random);
    gtk_widget_set_sensitive(random_b, !original_protocol_rx2_global);
    gtk_grid_attach(GTK_GRID(grid), random_b, 1, *row, 1, 1);
    if (original_protocol_rx2_global) {
      gtk_widget_set_tooltip_text(random_b, "P1 random is global and is controlled from RX1.");
    }
    g_signal_connect(random_b, "toggled", G_CALLBACK(random_cb), rx);
  }
  (*row)++;
}

#if defined (__AUTOG__)
static void add_hl2_autogain_controls(GtkWidget *grid, int *row) {
  if (device != DEVICE_HERMES_LITE2 && device != NEW_DEVICE_HERMES_LITE2) {
    return;
  }
  autogain_b = gtk_check_button_new_with_label("HL2 ADC Auto Gain RxPGA");
  gtk_widget_set_name(autogain_b, "boldlabel_blue");
  gtk_widget_set_tooltip_text(autogain_b,
                              "Activate RF Gain Automatic:\nControl and set the ADC to max. 75% level\nfor protect ADC against overflows");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autogain_b), autogain_enabled);
  gtk_grid_attach(GTK_GRID(grid), autogain_b, 0, *row, 1, 1);
  g_signal_connect(autogain_b, "toggled", G_CALLBACK(autogain_cb), NULL);
  (*row)++;
  autogain_time_b = gtk_check_button_new_with_label("HL2 Auto Gain time-regulated");
  gtk_widget_set_name(autogain_time_b, "boldlabel_blue");
  gtk_widget_set_tooltip_text(autogain_time_b,
                              "Re-adjust RF Gain Automatic every 30s\nIf OFF, RF Gain Automatic adjust only one-time\nif band was changed");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autogain_time_b), autogain_time_enabled);
  gtk_widget_set_sensitive(autogain_time_b, autogain_enabled ? TRUE : FALSE);
  gtk_grid_attach(GTK_GRID(grid), autogain_time_b, 0, *row, 1, 1);
  g_signal_connect(autogain_time_b, "toggled", G_CALLBACK(autogain_time_cb), NULL);
  (*row)++;
}
#endif

static void add_preamp_control(GtkWidget *grid, RECEIVER *rx, int *row) {
  if (have_preamp) {
    GtkWidget *preamp_b = gtk_check_button_new_with_label("Preamp");
    gtk_widget_set_name(preamp_b, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preamp_b), rx->preamp);
    gtk_grid_attach(GTK_GRID(grid), preamp_b, 0, *row, 1, 1);
    g_signal_connect(preamp_b, "toggled", G_CALLBACK(preamp_cb), rx);
    (*row)++;
  }
}

static void add_mute_radio_control_at(GtkWidget *grid, RECEIVER *rx, int row) {
  if (protocol == ORIGINAL_PROTOCOL || protocol  == NEW_PROTOCOL) {
    GtkWidget *mute_radio_b = gtk_check_button_new_with_label("Mute Audio to Radio");
    gtk_widget_set_name(mute_radio_b, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mute_radio_b), rx->mute_radio);
    gtk_widget_show(mute_radio_b);
    gtk_grid_attach(GTK_GRID(grid), mute_radio_b, 2, row, 1, 1);
    g_signal_connect(mute_radio_b, "toggled", G_CALLBACK(mute_radio_cb), rx);
  }
}

static void add_mute_controls(GtkWidget *grid, RECEIVER *rx, int *row) {
  GtkWidget *mute_audio_b = gtk_check_button_new_with_label("Mute when not active");
  gtk_widget_set_name(mute_audio_b, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mute_audio_b), rx->mute_when_not_active);
  gtk_widget_show(mute_audio_b);
  gtk_grid_attach(GTK_GRID(grid), mute_audio_b, 0, *row, 2, 1);
  g_signal_connect(mute_audio_b, "toggled", G_CALLBACK(mute_audio_cb), rx);
  (*row)++;
}

static void add_digi_offset_controls(GtkWidget *grid, RECEIVER *rx, int *row) {
  GtkWidget *digi_offset_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(digi_offset_box, GTK_ALIGN_CENTER);
  GtkWidget *digi_offset_u_label = gtk_label_new("DIGU Offset");
  gtk_widget_set_name(digi_offset_u_label, "boldlabel");
  GtkWidget *digi_offset_u = gtk_spin_button_new_with_range(0.0, 4000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(digi_offset_u), rx->digi_offset_u);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(digi_offset_u), TRUE);
  gtk_widget_set_tooltip_text(digi_offset_u, "DIGU audio offset in Hz");
  g_signal_connect(digi_offset_u, "value-changed", G_CALLBACK(digi_offset_u_cb), rx);
  GtkWidget *digi_offset_l_label = gtk_label_new("DIGL Offset");
  gtk_widget_set_name(digi_offset_l_label, "boldlabel");
  GtkWidget *digi_offset_l = gtk_spin_button_new_with_range(0.0, 4000.0, 10.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(digi_offset_l), rx->digi_offset_l);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(digi_offset_l), TRUE);
  gtk_widget_set_tooltip_text(digi_offset_l, "DIGL audio offset in Hz");
  g_signal_connect(digi_offset_l, "value-changed", G_CALLBACK(digi_offset_l_cb), rx);
  gtk_box_pack_start(GTK_BOX(digi_offset_box), digi_offset_u_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(digi_offset_box), digi_offset_u, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(digi_offset_box), gtk_label_new("Hz"), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(digi_offset_box), digi_offset_l_label, FALSE, FALSE, 16);
  gtk_box_pack_start(GTK_BOX(digi_offset_box), digi_offset_l, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(digi_offset_box), gtk_label_new("Hz"), FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), digi_offset_box, 0, *row, 3, 1);
  (*row)++;
  GtkWidget *digi_offset_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_set_halign(digi_offset_button_box, GTK_ALIGN_CENTER);
  GtkWidget *digi_offset_rtty_b = gtk_button_new_with_label("Offset RTTY");
  gtk_widget_set_tooltip_text(digi_offset_rtty_b, "Set DIGU/DIGL offsets for RTTY");
  g_object_set_data(G_OBJECT(digi_offset_rtty_b), "digu-offset", digi_offset_u);
  g_object_set_data(G_OBJECT(digi_offset_rtty_b), "digl-offset", digi_offset_l);
  g_signal_connect(digi_offset_rtty_b, "clicked", G_CALLBACK(digi_offset_rtty_cb), NULL);
  gtk_box_pack_start(GTK_BOX(digi_offset_button_box), digi_offset_rtty_b, FALSE, FALSE, 0);
  GtkWidget *digi_offset_off_b = gtk_button_new_with_label("Offset OFF");
  gtk_widget_set_tooltip_text(digi_offset_off_b, "Disable DIGU/DIGL offsets\n"
                                                 "Recommended for all FT mode");
  g_object_set_data(G_OBJECT(digi_offset_off_b), "digu-offset", digi_offset_u);
  g_object_set_data(G_OBJECT(digi_offset_off_b), "digl-offset", digi_offset_l);
  g_signal_connect(digi_offset_off_b, "clicked", G_CALLBACK(digi_offset_off_cb), NULL);
  gtk_box_pack_start(GTK_BOX(digi_offset_button_box), digi_offset_off_b, FALSE, FALSE, 0);
  gtk_grid_attach(GTK_GRID(grid), digi_offset_button_box, 0, *row, 3, 1);
  (*row)++;
}

static void add_local_audio_controls_at(GtkWidget *grid, RECEIVER *rx, int *row,
                                        int label_row, int output_row, int channel_row) {
  int i;
  if (n_output_devices <= 0) {
    return;
  }
  GtkWidget *local_audio_b = gtk_check_button_new_with_label("Use Local Audio Output:");
  gtk_widget_set_name(local_audio_b, "boldlabel");
  gtk_widget_set_halign(local_audio_b, GTK_ALIGN_START);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(local_audio_b), rx->local_audio);
  gtk_widget_show(local_audio_b);
  gtk_grid_attach(GTK_GRID(grid), local_audio_b, 2, label_row, 1, 1);
  g_signal_connect(local_audio_b, "toggled", G_CALLBACK(local_audio_cb), rx);
  if (rx->audio_device == -1) { rx->audio_device = 0; }
  GtkWidget *output = gtk_combo_box_text_new();
  for (i = 0; i < n_output_devices; i++) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(output), NULL, output_devices[i].description);
    if (strcmp(rx->audio_name, output_devices[i].name) == 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(output), i);
    }
  }
  i = gtk_combo_box_get_active(GTK_COMBO_BOX(output));
  if (i < 0) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(output), 0);
    g_strlcpy(rx->audio_name, output_devices[0].name, sizeof(rx->audio_name));
  }
  g_object_set_data(G_OBJECT(output), "local-audio-button", local_audio_b);
  my_combo_attach(GTK_GRID(grid), output, 2, output_row, 1, 1);
  g_signal_connect(output, "changed", G_CALLBACK(local_output_changed_cb), rx);
  GtkWidget *channel = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Stereo / Mono Downmix (L+R)");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Left Channel only");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Right Channel only");
  switch (rx->audio_channel) {
  case STEREO:
    gtk_combo_box_set_active(GTK_COMBO_BOX(channel), 0);
    break;
  case LEFT:
    gtk_combo_box_set_active(GTK_COMBO_BOX(channel), 1);
    break;
  case RIGHT:
    gtk_combo_box_set_active(GTK_COMBO_BOX(channel), 2);
    break;
  }
  my_combo_attach(GTK_GRID(grid), channel, 2, channel_row, 1, 1);
  g_signal_connect(channel, "changed", G_CALLBACK(audio_channel_cb), rx);
  if (*row <= label_row) { *row = label_row + 1; }
  if (*row <= output_row) { *row = output_row + 1; }
  if (*row <= channel_row) { *row = channel_row + 1; }
}

static void add_local_audio_controls(GtkWidget *grid, RECEIVER *rx, int *row) {
  int audio_row = *row;
  add_local_audio_controls_at(grid, rx, row, audio_row, audio_row + 1, audio_row + 2);
}


static GtkWidget *build_general_page(void) {
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width(GTK_CONTAINER(page), 12);
  gtk_widget_set_hexpand(page, TRUE);
  gtk_widget_set_vexpand(page, TRUE);
  GtkWidget *hardware_grid = NULL;
  GtkWidget *hardware_frame = rx_menu_section_new("Hardware", &hardware_grid);
  int row = 0;
  gboolean have_hardware_controls = FALSE;
  t_print("%s: have_alex_att = %d\n", __func__, have_alex_att);
  if (filter_board == ALEX && receiver[0] != NULL && receiver[0]->adc == 0 && have_alex_att) {
    //
    // The "Alex ATT" value is stored in receiver[0] no matter how the ADCs are selected
    //
    GtkWidget *alex_att_label = gtk_label_new("Alex Attenuator");
    gtk_widget_set_name(alex_att_label, "boldlabel");
    gtk_widget_set_halign(alex_att_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(hardware_grid), alex_att_label, 0, row, 1, 1);
    GtkWidget *alex_att_combo_box = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, " 0 dB");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "10 dB");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "20 dB");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "30 dB");
    gtk_combo_box_set_active(GTK_COMBO_BOX(alex_att_combo_box), receiver[0]->alex_attenuation);
    my_combo_attach(GTK_GRID(hardware_grid), alex_att_combo_box, 1, row, 1, 1);
    g_signal_connect(alex_att_combo_box, "changed", G_CALLBACK(alex_att_cb), NULL);
    row++;
    have_hardware_controls = TRUE;
  }
  if (filter_board == ALEX) {
    GtkWidget *adc0_filter_bypass_b = gtk_check_button_new_with_label("Bypass ADC0 RX filters");
    gtk_widget_set_name(adc0_filter_bypass_b, "boldlabel");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(adc0_filter_bypass_b), adc0_filter_bypass);
    gtk_grid_attach(GTK_GRID(hardware_grid), adc0_filter_bypass_b, 0, row, 2, 1);
    g_signal_connect(adc0_filter_bypass_b, "toggled", G_CALLBACK(adc0_filter_bypass_cb), NULL);
    if (device == DEVICE_ORION2 || device == NEW_DEVICE_ORION2 || device == NEW_DEVICE_SATURN) {
      GtkWidget *adc1_filter_bypass_b = gtk_check_button_new_with_label("Bypass ADC1 RX filters");
      gtk_widget_set_name(adc1_filter_bypass_b, "boldlabel");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(adc1_filter_bypass_b), adc1_filter_bypass);
      gtk_grid_attach(GTK_GRID(hardware_grid), adc1_filter_bypass_b, 2, row, 1, 1);
      g_signal_connect(adc1_filter_bypass_b, "toggled", G_CALLBACK(adc1_filter_bypass_cb), NULL);
    }
    row++;
    have_hardware_controls = TRUE;
  }
#if defined (__AUTOG__)
  if (device == DEVICE_HERMES_LITE2 || device == NEW_DEVICE_HERMES_LITE2) {
    add_hl2_autogain_controls(hardware_grid, &row);
    have_hardware_controls = TRUE;
  }
#endif
  if (!have_hardware_controls) {
    GtkWidget *label = gtk_label_new("No global hardware controls available");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(hardware_grid), label, 0, row, 1, 1);
  }
  gtk_box_pack_start(GTK_BOX(page), hardware_frame, FALSE, FALSE, 0);
#ifdef __APPLE__
  GtkWidget *operation_grid = NULL;
  GtkWidget *operation_frame = rx_menu_section_new("Operation", &operation_grid);
  row = 0;
  GtkWidget *wheel_present_btn = gtk_check_button_new_with_label("using Mouse with Wheel");
  gtk_widget_set_name(wheel_present_btn, "boldlabel_blue");
  gtk_widget_set_tooltip_text(wheel_present_btn,
                              "macOS only:\nIf ON, the mouse wheel can change the frequency\nIf OFF, you need to press OPTION + mouse wheel for frequency change");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wheel_present_btn),
                               receiver[0] != NULL ? receiver[0]->wheel_present : 1);
  gtk_grid_attach(GTK_GRID(operation_grid), wheel_present_btn, 0, row, 1, 1);
  g_signal_connect(wheel_present_btn, "toggled", G_CALLBACK(wheel_present_options_cb), NULL);
  gtk_box_pack_start(GTK_BOX(page), operation_frame, FALSE, FALSE, 0);
#endif
  return page;
}

static GtkWidget *build_rx_page(RECEIVER *rx) {
  GtkWidget *grid = rx_menu_grid_new();
  int row = 0;
  if (rx == NULL) {
    GtkWidget *label = gtk_label_new("Receiver not available");
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    return grid;
  }
  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    switch (protocol) {
    case ORIGINAL_PROTOCOL:
    case NEW_PROTOCOL:
      add_sample_rate_control(grid, rx, &row);
      break;
    }
    add_adc_control(grid, rx, &row);
    add_dither_random_controls(grid, rx, &row);
    add_preamp_control(grid, rx, &row);
  }
  add_mute_controls(grid, rx, &row);
  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    /*
     * Keep the local-audio block in a fixed order independent of
     * device-specific controls on the left side:
     *   row 0: output device
     *   row 1: channel/downmix
     *   row 2: local audio enable
     *   row 3: mute audio to radio
     */
    add_local_audio_controls_at(grid, rx, &row, 2, 0, 1);
    add_mute_radio_control_at(grid, rx, 3);
    if (row < 4) {
      row = 4;
    }
  } else {
    add_local_audio_controls(grid, rx, &row);
  }
  add_digi_offset_controls(grid, rx, &row);
  return grid;
}

void rx_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  win_set_bgcolor(dialog, &mwin_bgcolor);
  rx_menu_headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), rx_menu_headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(rx_menu_headerbar), TRUE);
  rx_menu_update_title(rx_menu_headerbar, RX_MENU_TAB_RX1);
  g_signal_connect(dialog, "delete_event", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "destroy", G_CALLBACK(close_cb), NULL);
  g_signal_connect(dialog, "response", G_CALLBACK(response_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_container_set_border_width(GTK_CONTAINER(content), 0);
  GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top(outer, 8);
  gtk_widget_set_margin_bottom(outer, 8);
  gtk_widget_set_margin_start(outer, 8);
  gtk_widget_set_margin_end(outer, 8);
  for (int i = 0; i < RX_MENU_TAB_COUNT; i++) {
    rx_menu_tab_buttons[i] = NULL;
  }
  GtkWidget *tabbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_halign(tabbar, GTK_ALIGN_START);
  rx_menu_tab_buttons[RX_MENU_TAB_RX1] = rx_menu_tab_button_new("RX1", RX_MENU_TAB_RX1);
  gtk_box_pack_start(GTK_BOX(tabbar), rx_menu_tab_buttons[RX_MENU_TAB_RX1], FALSE, FALSE, 0);
  if (receivers > 1) {
    rx_menu_tab_buttons[RX_MENU_TAB_RX2] = rx_menu_tab_button_new("RX2", RX_MENU_TAB_RX2);
    gtk_box_pack_start(GTK_BOX(tabbar), rx_menu_tab_buttons[RX_MENU_TAB_RX2], FALSE, FALSE, 0);
  }
  rx_menu_tab_buttons[RX_MENU_TAB_OPTIONS] = rx_menu_tab_button_new("Options", RX_MENU_TAB_OPTIONS);
  gtk_box_pack_start(GTK_BOX(tabbar), rx_menu_tab_buttons[RX_MENU_TAB_OPTIONS], FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(outer), tabbar, FALSE, FALSE, 0);
  rx_menu_stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(rx_menu_stack), GTK_STACK_TRANSITION_TYPE_NONE);
  gtk_widget_set_hexpand(rx_menu_stack, TRUE);
  gtk_widget_set_vexpand(rx_menu_stack, TRUE);
  gtk_stack_add_named(GTK_STACK(rx_menu_stack), build_rx_page(receiver[0]), "rx1");
  if (receivers > 1) {
    gtk_stack_add_named(GTK_STACK(rx_menu_stack), build_rx_page(receiver[1]), "rx2");
  }
  gtk_stack_add_named(GTK_STACK(rx_menu_stack), build_general_page(), "options");
  gtk_box_pack_start(GTK_BOX(outer), rx_menu_stack, TRUE, TRUE, 0);
  GtkWidget *close_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(close_box, GTK_ALIGN_CENTER);
  GtkWidget *close_button = gtk_button_new_with_label("Close");
  gtk_widget_set_size_request(close_button, 90, 32);
  g_signal_connect(close_button, "clicked", G_CALLBACK(close_cb), NULL);
  gtk_box_pack_start(GTK_BOX(close_box), close_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(outer), close_box, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(content), outer);
  sub_menu = dialog;
  rx_menu_select_tab(RX_MENU_TAB_RX1);
  gtk_widget_show_all(dialog);
}
