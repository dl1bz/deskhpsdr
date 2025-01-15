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
#include <string.h>

#include "new_menu.h"
#include "pa_menu.h"
#include "band.h"
#include "radio.h"
#include "vfo.h"
#include "message.h"

static GtkWidget *dialog = NULL;

static GtkWidget *calibgrid;

//
// we need all these "spin" widgets as a static variable
// to continously update their displayed values during
// a "single shot" calibration
//
static GtkWidget *spin[11];

static void reset_cb(GtkWidget *widget, gpointer data);

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

static void pa_value_changed_cb(GtkWidget *widget, gpointer data) {
  BAND *band = (BAND *)data;
  band->pa_calibration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
  int txvfo = vfo_get_tx_vfo();
  int b = vfo[txvfo].band;
  const BAND *current = band_get_band(b);

  if (band == current) {
    radio_calc_drive_level();
  }
}

static void tx_out_of_band_cb(GtkWidget *widget, gpointer data) {
  tx_out_of_band_allowed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void trim_changed_cb(GtkWidget *widget, gpointer data) {
  int i = GPOINTER_TO_INT(data);
  int k, flag;
  flag = 0;

  //
  // The 'flag' indicates that we do a single-shot calibration,
  // that is, the pa_trim[] values reflect a constant
  // factor and the last pa_trim[] value is changed.
  // In a single-shot calibration, change all the "lower" pa_trim
  // values to maintain the constant factor, and update the
  // text fields of the spinners.
  //
  if (i == 10) {
    flag = 1;

    for (k = 1; k < 10; k++) {
      double fac = ((double) k * pa_trim[10]) / ( 10.0 * pa_trim[k]);

      if ( fac < 0.99 || fac > 1.01) { flag = 0; }
    }
  }

  pa_trim[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

  if (flag) {
    // note that we have i==10 if the flag is nonzero.
    for (k = 1; k < 10; k++) {
      pa_trim[k] = 0.1 * k * pa_trim[10];
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin[k]), (double)pa_trim[k]);
    }
  }
}

static void show_W(int watts, gboolean reset) {
  int i;
  int col, row;
  int units;
  char text[16];
  double increment = 0.1 * watts;

  if (reset) {
    for (i = 0; i < 11; i++) {
      pa_trim[i] = i * increment;
    }
  }

  if (watts <= 1) {
    units = 0;
  } else if (watts <= 5) {
    units = 1;
  } else {
    units = 2;
  }

  row = 1;
  col = 0;

  for (i = 1; i < 11; i++) {
    switch (units) {
    case 0:
      snprintf(text, 16, "%0.3fW", i * increment);
      break;

    case 1:
      snprintf(text, 16, "%0.1fW", i * increment);
      break;

    case 2:
      snprintf(text, 16, "%dW", (int) (i * increment));
      break;
    }

    GtkWidget *label = gtk_label_new(text);
    gtk_widget_set_name(label, "boldlabel");
    gtk_grid_attach(GTK_GRID(calibgrid), label, col++, row, 1, 1);

    //
    // We *need* a maximum value for the spinner, but a quite large
    // value does not harm. So we allow up to 5 times the nominal
    // value.
    //
    switch (units) {
    case 0:
      spin[i] = gtk_spin_button_new_with_range(0.001, (double)(5 * i * increment), 0.001);
      break;

    case 1:
      spin[i] = gtk_spin_button_new_with_range(0.1, (double)(5 * i * increment), 0.1);
      break;

    case 2:
      spin[i] = gtk_spin_button_new_with_range(1.0, (double)(5 * i * increment), 1.0);
      break;
    }

    gtk_grid_attach(GTK_GRID(calibgrid), spin[i], col++, row, 1, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin[i]), (double)pa_trim[i]);
    g_signal_connect(spin[i], "value_changed", G_CALLBACK(trim_changed_cb), GINT_TO_POINTER(i));

    if (col == 4) {
      row++;
      col = 0;
    }
  }
}

static void clear_W() {
  int i;

  for (i = 0; i < 10; i++) {
    gtk_grid_remove_row(GTK_GRID(calibgrid), 1);
    spin[i] = NULL;
  }
}

static void new_calib(gboolean flag) {
  show_W(pa_power_list[pa_power], flag);
  GtkWidget *reset_b = gtk_button_new_with_label("Reset");
  gtk_grid_attach(GTK_GRID(calibgrid), reset_b, 0, 6, 4, 1);
  g_signal_connect(reset_b, "button-press-event", G_CALLBACK(reset_cb), NULL);
}

static void reset_cb(GtkWidget *widget, gpointer data) {
  clear_W();
  new_calib(TRUE);
  gtk_widget_show_all(calibgrid);
}

static void max_power_changed_cb(GtkWidget *widget, gpointer data) {
  pa_power = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  t_print("max_power_changed_cb: %d\n", pa_power_list[pa_power]);
  clear_W();
  new_calib(TRUE);
  gtk_widget_show_all(calibgrid);
}

void pa_menu(GtkWidget *parent) {
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
#if defined (__LDESK__)
  char _title[32];
  snprintf(_title, 32, "%s - PA Calibration", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
#else
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "piHPSDR - PA Calibration");
#endif
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *notebook = gtk_notebook_new();
  GtkWidget *grid0 = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid0), 10);
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect (close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid0), close_b, 0, 0, 1, 1);
  GtkWidget *max_power_label = gtk_label_new("MAX Power");
  gtk_widget_set_name(max_power_label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid0), max_power_label, 1, 0, 1, 1);
  GtkWidget *max_power_b = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "1W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "5W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "10W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "30W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "50W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "100W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "200W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "500W");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(max_power_b), NULL, "1KW");
  gtk_combo_box_set_active(GTK_COMBO_BOX(max_power_b), pa_power);
  my_combo_attach(GTK_GRID(grid0), max_power_b, 2, 0, 1, 1);
  g_signal_connect(max_power_b, "changed", G_CALLBACK(max_power_changed_cb), NULL);
  GtkWidget *tx_out_of_band_b = gtk_check_button_new_with_label("Transmit out of band");
  gtk_widget_set_name(tx_out_of_band_b, "boldlabel");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tx_out_of_band_b), tx_out_of_band_allowed);
  gtk_widget_show(tx_out_of_band_b);
  gtk_grid_attach(GTK_GRID(grid0), tx_out_of_band_b, 3, 0, 1, 1);
  g_signal_connect(tx_out_of_band_b, "toggled", G_CALLBACK(tx_out_of_band_cb), NULL);

  if (protocol == ORIGINAL_PROTOCOL || protocol == NEW_PROTOCOL) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing (GTK_GRID(grid), 10);
    int bands = radio_max_band();
    int b = 0;

    if (tx_out_of_band_allowed) {
      //
      // If out-of-band TXing is allowed, we need a PA calibration value
      // for the "general" band. Note that if out-of-band TX is allowed
      // while the menu is open, this will not appear (one has to close
      // and re-open the menu).
      //
      BAND *band = band_get_band(bandGen);
      GtkWidget *band_label = gtk_label_new(band->title);
      gtk_widget_set_name(band_label, "boldlabel");
      gtk_widget_show(band_label);
      gtk_grid_attach(GTK_GRID(grid), band_label, (b / 6) * 2, (b % 6) + 1, 1, 1);
      GtkWidget *pa_r = gtk_spin_button_new_with_range(38.8, 100.0, 0.1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(pa_r), (double)band->pa_calibration);
      gtk_widget_show(pa_r);
      gtk_grid_attach(GTK_GRID(grid), pa_r, ((b / 6) * 2) + 1, (b % 6) + 1, 1, 1);
      g_signal_connect(pa_r, "value_changed", G_CALLBACK(pa_value_changed_cb), band);
      b++;
    }

    for (int i = 0; i <= bands; i++) {
      BAND *band = band_get_band(i);
      GtkWidget *band_label = gtk_label_new(band->title);
      gtk_widget_set_name(band_label, "boldlabel");
      gtk_widget_show(band_label);
      gtk_grid_attach(GTK_GRID(grid), band_label, (b / 6) * 2, (b % 6) + 1, 1, 1);
      GtkWidget *pa_r = gtk_spin_button_new_with_range(38.8, 100.0, 0.1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(pa_r), (double)band->pa_calibration);
      gtk_widget_show(pa_r);
      gtk_grid_attach(GTK_GRID(grid), pa_r, ((b / 6) * 2) + 1, (b % 6) + 1, 1, 1);
      g_signal_connect(pa_r, "value_changed", G_CALLBACK(pa_value_changed_cb), band);
      b++;
    }

    for (int i = BANDS; i < BANDS + XVTRS; i++) {
      BAND *band = band_get_band(i);

      if (strlen(band->title) > 0) {
        GtkWidget *band_label = gtk_label_new(band->title);
        gtk_widget_set_name(band_label, "boldlabel");
        gtk_widget_show(band_label);
        gtk_grid_attach(GTK_GRID(grid), band_label, (b / 6) * 2, (b % 6) + 1, 1, 1);
        GtkWidget *pa_r = gtk_spin_button_new_with_range(38.8, 100.0, 0.1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(pa_r), (double)band->pa_calibration);
        gtk_widget_show(pa_r);
        gtk_grid_attach(GTK_GRID(grid), pa_r, ((b / 6) * 2) + 1, (b % 6) + 1, 1, 1);
        g_signal_connect(pa_r, "value_changed", G_CALLBACK(pa_value_changed_cb), band);
        b++;
      }
    }

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid, gtk_label_new("Calibrate"));
  }

  calibgrid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(calibgrid), 10);
  new_calib(FALSE);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), calibgrid, gtk_label_new("Watt Meter Calibrate"));
  gtk_grid_attach(GTK_GRID(grid0), notebook, 0, 1, 6, 1);
  gtk_container_add(GTK_CONTAINER(content), grid0);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
}

