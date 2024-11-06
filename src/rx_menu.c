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
#include "mystring.h"

static GtkWidget *dialog = NULL;
static GtkWidget *local_audio_b = NULL;
static GtkWidget *output = NULL;

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    gtk_widget_destroy(tmp);
    sub_menu = NULL;
    active_menu  = NO_MENU;
    radio_save_state();
  }
}

static gboolean close_cb () {
  cleanup();
  return TRUE;
}

static void dither_cb(GtkWidget *widget, gpointer data) {
  active_receiver->dither = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_receive_specific();
}

static void random_cb(GtkWidget *widget, gpointer data) {
  active_receiver->random = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_receive_specific();
}

static void preamp_cb(GtkWidget *widget, gpointer data) {
  if (have_preamp) {
    active_receiver->preamp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  }
}

static void alex_att_cb(GtkWidget *widget, gpointer data) {
  if (have_alex_att) {
    int val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    radio_set_alex_attenuation(val);
  }
}

static void sample_rate_cb(GtkToggleButton *widget, gpointer data) {
  const char *p = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
  int samplerate;

  //
  // There are so many different possibilities for sample rates, so
  // we just "scanf" from the combobox text entry
  //
  if (sscanf(p, "%d", &samplerate) != 1) { return; }

  rx_change_sample_rate(active_receiver, samplerate);
}

static void adc_cb(GtkToggleButton *widget, gpointer data) {
  active_receiver->adc = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  rx_change_adc(active_receiver);
}

static void local_audio_cb(GtkWidget *widget, gpointer data) {
  t_print("local_audio_cb: rx=%d\n", active_receiver->id);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
    if (audio_open_output(active_receiver) == 0) {
      active_receiver->local_audio = 1;
    } else {
      t_print("local_audio_cb: audio_open_output failed\n");
      active_receiver->local_audio = 0;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
    }
  } else {
    if (active_receiver->local_audio) {
      active_receiver->local_audio = 0;
      audio_close_output(active_receiver);
    }
  }

  t_print("local_audio_cb: local_audio=%d\n", active_receiver->local_audio);
}

static void mute_audio_cb(GtkWidget *widget, gpointer data) {
  active_receiver->mute_when_not_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static void mute_radio_cb(GtkWidget *widget, gpointer data) {
  active_receiver->mute_radio = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static void adc0_filter_bypass_cb(GtkWidget *widget, gpointer data) {
  adc0_filter_bypass = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_high_priority();
}

static void adc1_filter_bypass_cb(GtkWidget *widget, gpointer data) {
  adc1_filter_bypass = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  schedule_high_priority();
}

//
// possible the device has been changed:
// call audo_close_output with old device, audio_open_output with new one
//
static void local_output_changed_cb(GtkWidget *widget, gpointer data) {
  int i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  if (active_receiver->local_audio) {
    audio_close_output(active_receiver);                     // audio_close with OLD device
  }

  if (i >= 0) {
    t_print("local_output_changed rx=%d %s\n", active_receiver->id, output_devices[i].name);
    STRLCPY(active_receiver->audio_name, output_devices[i].name, sizeof(active_receiver->audio_name));
  }

  if (active_receiver->local_audio) {
    if (audio_open_output(active_receiver) < 0) {           // audio_open with NEW device
      active_receiver->local_audio = 0;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (local_audio_b), FALSE);
    }
  }

  t_print("local_output_changed rx=%d local_audio=%d\n", active_receiver->id, active_receiver->local_audio);
}

static void audio_channel_cb(GtkWidget *widget, gpointer data) {
  int val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  switch (val) {
  case 0:
    active_receiver->audio_channel = STEREO;
    break;

  case 1:
    active_receiver->audio_channel = LEFT;
    break;

  case 2:
    active_receiver->audio_channel = RIGHT;
    break;
  }
}

void rx_menu(GtkWidget *parent) {
  int i;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  char title[64];
  snprintf(title, 64, "%s - Receive (RX%d VFO-%s)", PGNAME, active_receiver->id + 1, active_receiver->id == 0 ? "A" : "B");
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), FALSE);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button_press_event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, 0, 0, 1, 1);
  int row = 1;

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    switch (protocol) {
    case NEW_PROTOCOL: { // Sample rate in RX menu only for P2
      GtkWidget *sample_rate_label = gtk_label_new("Sample Rate");
      gtk_widget_set_name(sample_rate_label, "boldlabel");
      gtk_widget_set_halign(sample_rate_label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), sample_rate_label, 0, row, 1, 1);
      GtkWidget *sample_rate_combo_box = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "48000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "96000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "192000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "384000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "768000");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(sample_rate_combo_box), NULL, "1536000");

      switch (active_receiver->sample_rate) {
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

      my_combo_attach(GTK_GRID(grid), sample_rate_combo_box, 1, row, 1, 1);
      g_signal_connect(sample_rate_combo_box, "changed", G_CALLBACK(sample_rate_cb), NULL);
    }

    row++;
    break;
    }

    if (filter_board == ALEX && active_receiver->adc == 0 && have_alex_att) {
      //
      // The "Alex ATT" value is stored in receiver[0] no matter how the ADCs are selected
      //
      GtkWidget *alex_att_label = gtk_label_new("Alex Attenuator");
      gtk_widget_set_name(alex_att_label, "boldlabel");
      gtk_widget_set_halign(alex_att_label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), alex_att_label, 0, row, 1, 1);
      GtkWidget *alex_att_combo_box = gtk_combo_box_text_new();
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, " 0 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "10 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "20 dB");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(alex_att_combo_box), NULL, "30 dB");
      gtk_combo_box_set_active(GTK_COMBO_BOX(alex_att_combo_box), receiver[0]->alex_attenuation);
      my_combo_attach(GTK_GRID(grid), alex_att_combo_box, 1, row, 1, 1);
      g_signal_connect(alex_att_combo_box, "changed", G_CALLBACK(alex_att_cb), NULL);
      row++;
    }

    //
    // If there is more than one ADC, let the user associate an ADC
    // with the current receiver.
    //
    if (n_adc > 1) {
      GtkWidget *adc_label = gtk_label_new("Select ADC");
      gtk_widget_set_name(adc_label, "boldlabel");
      gtk_widget_set_halign(adc_label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), adc_label, 0, row, 1, 1);
      GtkWidget *adc_combo_box = gtk_combo_box_text_new();

      for (i = 0; i < n_adc; i++) {
        char label[32];
        snprintf(label, 32, "ADC-%d", i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(adc_combo_box), NULL, label);
      }

      gtk_combo_box_set_active(GTK_COMBO_BOX(adc_combo_box), active_receiver->adc);
      my_combo_attach(GTK_GRID(grid), adc_combo_box, 1, row, 1, 1);
      g_signal_connect(adc_combo_box, "changed", G_CALLBACK(adc_cb), NULL);
      row++;
    }

    //
    // The CHARLY25 board (with RedPitaya) has no support for preamp/dither/random
    // so those are left out. For Charly25, PreAmps and Alex Attenuator are controlled via
    // the sliders menu.
    //
    // Preamps are switchable only for the very first HPSDR radios
    //
    // We assume Alex attenuators are present if we have an ALEX board and no Orion2
    // (ANAN-7000/8000 do not have these), and if the RX is fed by the first ADC.
    //
    if (filter_board != CHARLY25) {
      GtkWidget *dither_b = gtk_check_button_new_with_label("Dither");
      gtk_widget_set_name(dither_b, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dither_b), active_receiver->dither);
      gtk_grid_attach(GTK_GRID(grid), dither_b, 0, row, 1, 1);
      g_signal_connect(dither_b, "toggled", G_CALLBACK(dither_cb), NULL);
      GtkWidget *random_b = gtk_check_button_new_with_label("Random");
      gtk_widget_set_name(random_b, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (random_b), active_receiver->random);
      gtk_grid_attach(GTK_GRID(grid), random_b, 1, row, 1, 1);
      g_signal_connect(random_b, "toggled", G_CALLBACK(random_cb), NULL);
      row++;

      if (have_preamp) {
        GtkWidget *preamp_b = gtk_check_button_new_with_label("Preamp");
        gtk_widget_set_name(preamp_b, "boldlabel");
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (preamp_b), active_receiver->preamp);
        gtk_grid_attach(GTK_GRID(grid), preamp_b, 0, row, 1, 1);
        g_signal_connect(preamp_b, "toggled", G_CALLBACK(preamp_cb), NULL);
        row++;
      }
    }
  }

  if (row < 4) { row = 4;}

  GtkWidget *mute_audio_b = gtk_check_button_new_with_label("Mute when not active");
  gtk_widget_set_name(mute_audio_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mute_audio_b), active_receiver->mute_when_not_active);
  gtk_widget_show(mute_audio_b);
  gtk_grid_attach(GTK_GRID(grid), mute_audio_b, 0, row, 2, 1);
  g_signal_connect(mute_audio_b, "toggled", G_CALLBACK(mute_audio_cb), NULL);
  GtkWidget *mute_radio_b = gtk_check_button_new_with_label("Mute Audio to Radio");
  gtk_widget_set_name(mute_radio_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mute_radio_b), active_receiver->mute_radio);
  gtk_widget_show(mute_radio_b);
  gtk_grid_attach(GTK_GRID(grid), mute_radio_b, 2, row, 1, 1);
  g_signal_connect(mute_radio_b, "toggled", G_CALLBACK(mute_radio_cb), NULL);
  row++;

  if (filter_board == ALEX) {
    GtkWidget *adc0_filter_bypass_b = gtk_check_button_new_with_label("Bypass ADC0 RX filters");
    gtk_widget_set_name(adc0_filter_bypass_b, "boldlabel");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (adc0_filter_bypass_b), adc0_filter_bypass);
    gtk_grid_attach(GTK_GRID(grid), adc0_filter_bypass_b, 0, row, 2, 1);
    g_signal_connect(adc0_filter_bypass_b, "toggled", G_CALLBACK(adc0_filter_bypass_cb), NULL);

    if (device == DEVICE_ORION2 || device == NEW_DEVICE_ORION2 || device == NEW_DEVICE_SATURN) {
      GtkWidget *adc1_filter_bypass_b = gtk_check_button_new_with_label("Bypass ADC1 RX filters");
      gtk_widget_set_name(adc1_filter_bypass_b, "boldlabel");
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (adc1_filter_bypass_b), adc1_filter_bypass);
      gtk_grid_attach(GTK_GRID(grid), adc1_filter_bypass_b, 2, row, 1, 1);
      g_signal_connect(adc1_filter_bypass_b, "toggled", G_CALLBACK(adc1_filter_bypass_cb), NULL);
    }
  }

  if (n_output_devices > 0) {
    local_audio_b = gtk_check_button_new_with_label("Local Audio Output:");
    gtk_widget_set_name(local_audio_b, "boldlabel");
    gtk_widget_set_halign(local_audio_b, GTK_ALIGN_START);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (local_audio_b), active_receiver->local_audio);
    gtk_widget_show(local_audio_b);
    gtk_grid_attach(GTK_GRID(grid), local_audio_b, 2, 1, 1, 1);
    g_signal_connect(local_audio_b, "toggled", G_CALLBACK(local_audio_cb), NULL);

    if (active_receiver->audio_device == -1) { active_receiver->audio_device = 0; }

    output = gtk_combo_box_text_new();

    for (i = 0; i < n_output_devices; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(output), NULL, output_devices[i].description);

      if (strcmp(active_receiver->audio_name, output_devices[i].name) == 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(output), i);
      }
    }

    i = gtk_combo_box_get_active(GTK_COMBO_BOX(output));

    if (i < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(output), 0);
      STRLCPY(active_receiver->audio_name, output_devices[0].name, sizeof(active_receiver->audio_name));
    }

    my_combo_attach(GTK_GRID(grid), output, 2, 2, 1, 1);
    g_signal_connect(output, "changed", G_CALLBACK(local_output_changed_cb), NULL);
    GtkWidget *channel = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Stereo");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Left");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(channel), NULL, "Right");

    switch (active_receiver->audio_channel) {
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

    my_combo_attach(GTK_GRID(grid), channel, 2, 3, 1, 1);
    g_signal_connect(channel, "changed", G_CALLBACK(audio_channel_cb), NULL);
  }

  gtk_container_add(GTK_CONTAINER(content), grid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}
