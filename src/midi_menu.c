/* Copyright (C)
* 2020 - John Melton, G0ORX/N6LYT
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
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

#include "main.h"
#include "discovered.h"
#include "mode.h"
#include "filter.h"
#include "band.h"
#include "receiver.h"
#include "transmitter.h"
#include "receiver.h"
#include "adc.h"
#include "dac.h"
#include "radio.h"
#include "actions.h"
#include "action_dialog.h"
#include "midi.h"
#include "alsa_midi.h"
#include "new_menu.h"
#include "midi_menu.h"
#include "property.h"
#include "message.h"

int midiIgnoreCtrlPairs = 0;

enum {
  EVENT_COLUMN = 0,
  CHANNEL_COLUMN,
  NOTE_COLUMN,
  TYPE_COLUMN,
  ACTION_COLUMN,
  BSTR_COLUMN,
  N_COLUMNS
};

static GtkWidget *dialog = NULL;

static GtkListStore *store;
static GtkWidget *view;
static GtkWidget *scrolled_window = NULL;
static gulong selection_signal_id;
static GtkTreeModel *model;
static GtkTreeIter iter;
struct desc *current_cmd;

static GtkWidget *newEvent;
static GtkWidget *newChannel;
static GtkWidget *newNote;
static GtkWidget *newVal;
static GtkWidget *newType;
static GtkWidget *newMin;
static GtkWidget *newMax;
static GtkWidget *newAction;
static GtkWidget *delete_b;
static GtkWidget *clear_b;
static GtkWidget *device_b[MAX_MIDI_DEVICES];

static enum MIDIevent thisEvent;
static int thisChannel;
static int thisNote;
static int thisVal;
static int thisMin;
static int thisMax;
static int thisDelay;
static int thisVfl1, thisVfl2;
static int thisFl1,  thisFl2;
static int thisLft1, thisLft2;
static int thisRgt1, thisRgt2;
static int thisFr1,  thisFr2;
static int thisVfr1, thisVfr2;

static GtkWidget *WheelContainer;
// *INDENT-OFF*
static GtkWidget *set_vfl1, *set_vfl2;
static GtkWidget *set_fl1,  *set_fl2;
static GtkWidget *set_lft1, *set_lft2;
static GtkWidget *set_rgt1, *set_rgt2;
static GtkWidget *set_fr1,  *set_fr2;
static GtkWidget *set_vfr1, *set_vfr2;
// *INDENT-ON*

static enum ACTIONtype thisType;
static int thisAction;

//
// While choosing an action in the dialog, new incoming events
// shall not have any effect
//
static int ignore_incoming_events = 0;

enum {
  UPDATE_NEW = 0,
  UPDATE_CURRENT,
  UPDATE_EXISTING
};

static int updatePanel(int state);
static void updateDescription();
static void load_store(void);

static char *Event2String(enum MIDIevent event);
static enum MIDIevent String2Event(const char *str);
static char *Type2String(enum ACTIONtype type);
static enum ACTIONtype String2Type(const char *str);

static void cleanup() {
  if (dialog != NULL) {
    GtkWidget *tmp = dialog;
    dialog = NULL;
    configure_midi_device(FALSE);
    ignore_incoming_events = 0;
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

static void ignore_cb(GtkWidget *widget, gpointer data) {
  midiIgnoreCtrlPairs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void device_cb(GtkWidget *widget, gpointer data) {
  int ind = GPOINTER_TO_INT(data);
  int val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if (val == 1) {
    register_midi_device(ind);
  } else {
    close_midi_device(ind);
  }

  // take care button remains un-checked if opening failed
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), midi_devices[ind].active);
}

static void update_wheelparams(gpointer user_data) {
  //
  // Task: show or hide WheelContainer depending on whether
  //       thre current type is a wheel. If it is a wheel,
  //       set spin buttons to current values.
  //
  if (thisType == MIDI_WHEEL) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_vfl1 ), (double) thisVfl1 );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_vfl2 ), (double) thisVfl2 );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_fl1  ), (double) thisFl1  );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_fl2  ), (double) thisFl2  );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_lft1 ), (double) thisLft1 );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_lft2 ), (double) thisLft2 );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_rgt1 ), (double) thisRgt1 );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_rgt2 ), (double) thisRgt2 );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_fr1  ), (double) thisFr1  );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_fr2  ), (double) thisFr2  );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_vfr1 ), (double) thisVfr1 );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_vfr2 ), (double) thisVfr2 );
    gtk_widget_show(WheelContainer);
  } else {
    gtk_widget_hide(WheelContainer);
  }
}

static void type_changed_cb(GtkWidget *widget, gpointer data) {
  // update actions available for the type
  const gchar *type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));

  if (type == NULL) {
    //
    // This happens if the combo-box is cleared in updatePanel()
    //
    return;
  }

  //t_print("%s: type=%s action=%d type=%d\n",__FUNCTION__,type,thisAction,thisType);
  thisType = String2Type(type);

  //
  // If the type changed, the current action may no longer be allowed
  //
  if (ActionTable[thisAction].type && thisType == 0) {
    thisAction = NO_ACTION;
  }

  gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[thisAction].str);
  update_wheelparams(NULL);
}

static gboolean action_cb(GtkWidget *widget, gpointer data) {
  if (thisType == TYPE_NONE) { return TRUE; }

  //t_print("%s: type=%d action=%d\n", __FUNCTION__, thisType, thisAction);
  ignore_incoming_events = 1;
  thisAction = action_dialog(dialog, thisType, thisAction);
  //t_print("%s: new action=%d\n", __FUNCTION__, thisAction);
  gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[thisAction].str);
  updateDescription();
  ignore_incoming_events = 0;
  return TRUE;
}

static void row_inserted_cb(GtkTreeModel *tree_model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data) {
  //t_print("%s\n",__FUNCTION__);
  gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), path, NULL, FALSE);
}

static void tree_selection_changed_cb (GtkTreeSelection *selection, gpointer data) {
  char *str_event;
  char *str_channel;
  char *str_note;
  char *str_type;
  char *str_action;
  gtk_widget_set_sensitive(delete_b, FALSE);
  gtk_widget_set_sensitive(clear_b, FALSE);

  if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
    gtk_widget_set_sensitive(delete_b, TRUE);
    gtk_widget_set_sensitive(clear_b, TRUE);
    gtk_tree_model_get(model, &iter, EVENT_COLUMN, &str_event, -1);
    gtk_tree_model_get(model, &iter, CHANNEL_COLUMN, &str_channel, -1);
    gtk_tree_model_get(model, &iter, NOTE_COLUMN, &str_note, -1);
    gtk_tree_model_get(model, &iter, TYPE_COLUMN, &str_type, -1);
    gtk_tree_model_get(model, &iter, BSTR_COLUMN, &str_action, -1);

    if (str_event != NULL && str_channel != NULL && str_note != NULL && str_type != NULL && str_action != NULL) {
      thisEvent = String2Event(str_event);
      thisChannel = atoi(str_channel);
      thisNote = atoi(str_note);
      thisVal = 0;
      thisMin = 127;
      thisMax = 0;
      thisType = String2Type(str_type);
      thisAction = NO_ACTION;

      for (int i = 0; i < ACTIONS; i++) {
        if (strcmp(ActionTable[i].button_str, str_action) == 0 && (thisType & ActionTable[i].type)) {
          thisAction = ActionTable[i].action;
          break;
        }
      }

      updatePanel(UPDATE_EXISTING);
    }
  }
}

static void find_current_cmd() {
  struct desc *cmd;
  cmd = MidiCommandsTable[thisNote];

  //
  // Find the first command in the MIDI table which has the same channel
  // and the same type, but do not look at the action.
  //
  while (cmd != NULL) {
    if ((cmd->channel == thisChannel) && cmd->event == thisEvent) {
      break;
    }

    cmd = cmd->next;
  }

  current_cmd = cmd;
}

static void wheelparam_cb(GtkWidget *widget, gpointer user_data) {
  int what = GPOINTER_TO_INT(user_data);
  int val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
  int newval = val;

  if (thisType != MIDI_WHEEL) {
    // we should never arrive here
    return;
  }

  switch (what) {
  case 1:  // Delay
    thisDelay = newval;
    break;

  case 2:  // Very fast Left 1
    if (newval > thisVfl2) { newval = thisVfl2; }

    thisVfl1 = newval;
    break;

  case 3:  // Very fast Left 2
    if (newval < thisVfl1) { newval = thisVfl1; }

    thisVfl2 = newval;
    break;

  case 4:  // Fast Left 1
    if (newval > thisFl2) { newval = thisFl2; }

    thisFl1 = newval;
    break;

  case 5:  // Fast Left 2
    if (newval < thisFl1) { newval = thisFl1; }

    thisFl2 = newval;
    break;

  case 6:  // Left 1
    if (newval > thisLft2) { newval = thisLft2; }

    thisLft1 = newval;
    break;

  case 7:  // Left 2
    if (newval < thisLft1) { newval = thisLft1; }

    thisLft2 = newval;
    break;

  case 8:  // Right 1
    if (newval > thisRgt2) { newval = thisRgt2; }

    thisRgt1 = newval;
    break;

  case 9:  // Right 2
    if (newval < thisRgt1) { newval = thisRgt1; }

    thisRgt2 = newval;
    break;

  case 10:  // Fast Right 1
    if (newval > thisFr2) { newval = thisFr2; }

    thisFr1 = newval;
    break;

  case 11:  // Fast Right2
    if (newval < thisFr1) { newval = thisFr1; }

    thisFr2 = newval;
    break;

  case 12:  // Very fast Right 1
    if (newval > thisVfr2) { newval = thisVfr2; }

    thisVfr1 = newval;
    break;

  case 13:  // Very fast Right 2
    if (newval < thisVfr1) { newval = thisVfr1; }

    thisVfr2 = newval;
    break;
  }

  //
  // If we have changed the value because we kept thisVfl2 >= thisVfl1 etc,
  // update the spin button
  //
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), (double) newval);
}

static void clear_cb(GtkWidget *widget, gpointer user_data) {
  gtk_list_store_clear(store);
  MidiReleaseCommands();
}

static void add_store(int key, const struct desc *cmd) {
  char str_channel[64];
  char str_note[64];
  char str_action[64];
  char *cp;
  snprintf(str_channel, 64, "%d", cmd->channel);
  snprintf(str_note, 64, "%d", key);
  gtk_list_store_prepend(store, &iter);
  // convert line breaks to spaces for window
  g_strlcpy(str_action, ActionTable[cmd->action].str, 64);
  cp = str_action;

  while (*cp) {
    if (*cp == '\n') { *cp = ' '; }

    cp++;
  }

  gtk_list_store_set(store, &iter,
                     EVENT_COLUMN, Event2String(cmd->event),
                     CHANNEL_COLUMN, str_channel,
                     NOTE_COLUMN, str_note,
                     TYPE_COLUMN, Type2String(cmd->type),
                     ACTION_COLUMN, str_action,
                     BSTR_COLUMN, ActionTable[cmd->action].button_str,
                     -1);

  if (scrolled_window != NULL) {
    GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW(scrolled_window));

    //t_print("%s: adjustment=%f lower=%f upper=%f\n",__FUNCTION__,gtk_adjustment_get_value(adjustment),gtk_adjustment_get_lower(adjustment),gtk_adjustment_get_upper(adjustment));
    if (gtk_adjustment_get_value(adjustment) != 0.0) {
      gtk_adjustment_set_value(adjustment, 0.0);
    }
  }
}

static void load_store() {
  struct desc *cmd;
  gtk_list_store_clear(store);

  for (int i = 127; i >= 0; i--) {
    cmd = MidiCommandsTable[i];

    while (cmd != NULL) {
      add_store(i, cmd);
      cmd = cmd->next;
    }
  }
}

static void updateDescription() {
  char str_channel[64];
  char str_note[64];
  int  addFlag = 0;
  //
  // Add or update a command, both in the MIDI data base and in the
  // sub-window
  //
  find_current_cmd();

  if (current_cmd == NULL) {
    //
    // This is a new Note/Event combination, so we need a new entry
    //
    current_cmd = (struct desc *) malloc(sizeof(struct desc));
    current_cmd->next = NULL;
    addFlag = 1;
  }

  //
  // Modify or initialize the command
  //
  current_cmd->channel = thisChannel;
  current_cmd->type   = thisType;
  current_cmd->event  = thisEvent;
  current_cmd->action = thisAction;
  current_cmd->vfl1  = thisVfl1;
  current_cmd->vfl2  = thisVfl2;
  current_cmd->fl1   = thisFl1;
  current_cmd->fl2   = thisFl2;
  current_cmd->lft1  = thisLft1;
  current_cmd->lft2  = thisLft2;
  current_cmd->rgt1  = thisRgt1;
  current_cmd->rgt2  = thisRgt2;
  current_cmd->fr1   = thisFr1;
  current_cmd->fr2   = thisFr2;
  current_cmd->vfr1  = thisVfr1;
  current_cmd->vfr2  = thisVfr2;
  snprintf(str_channel, 64, "%d", thisChannel);
  snprintf(str_note, 64, "%d", thisNote);

  if (addFlag) {
    MidiAddCommand(thisNote, current_cmd);
    add_store(thisNote, current_cmd);
  } else {
    char str_action[64];
    char *cp;
    // convert line breaks to spaces for window
    g_strlcpy(str_action, ActionTable[thisAction].str, 64);
    cp = str_action;

    while (*cp) {
      if (*cp == '\n') { *cp = ' '; }

      cp++;
    }

    gtk_list_store_set(store, &iter,
                       EVENT_COLUMN, Event2String(thisEvent),
                       CHANNEL_COLUMN, str_channel,
                       NOTE_COLUMN, str_note,
                       TYPE_COLUMN, Type2String(thisType),
                       ACTION_COLUMN, str_action,
                       BSTR_COLUMN, ActionTable[thisAction].button_str,
                       -1);
  }
}

static void delete_cb(GtkButton *widget, GdkEventButton *event, gpointer user_data) {
  struct desc *previous_cmd;
  struct desc *next_cmd;

  //t_print("%s: thisNote=%d current_cmd=%p\n", __FUNCTION__, thisNote, current_cmd);
  if (current_cmd == NULL) {
    t_print("%s: current_cmd is NULL!\n", __FUNCTION__);
    return;
  }

  // remove from MidiCommandsTable
  if (MidiCommandsTable[thisNote] == current_cmd) {
    MidiCommandsTable[thisNote] = current_cmd->next;
    g_free(current_cmd);
    current_cmd = NULL;
  } else {
    previous_cmd = MidiCommandsTable[thisNote];

    while (previous_cmd->next != NULL) {
      next_cmd = previous_cmd->next;

      if (next_cmd == current_cmd) {
        previous_cmd->next = next_cmd->next;
        g_free(next_cmd);
        current_cmd = NULL; // note next_cmd == current_cmd
        break;
      }

      previous_cmd = next_cmd;
    }
  }

  // remove from list store. This triggers "tree selection changed"
  gtk_list_store_remove(store, &iter);
}

void midi_menu(GtkWidget *parent) {
  int col;
  int row;
  int height;
  GtkCellRenderer *renderer;
  GtkWidget *lbl;
  //
  // MIDI stays in "configure" mode until this menu is closed
  //
  ignore_incoming_events = 0;
  configure_midi_device(TRUE);
  thisEvent = EVENT_NONE;
  dialog = gtk_dialog_new();
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
  GtkWidget *headerbar = gtk_header_bar_new();
  gtk_window_set_titlebar(GTK_WINDOW(dialog), headerbar);
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
  char _title[32];
  snprintf(_title, 32, "%s - MIDI", PGNAME);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), _title);
  g_signal_connect (dialog, "delete_event", G_CALLBACK (close_cb), NULL);
  g_signal_connect (dialog, "destroy", G_CALLBACK (close_cb), NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(grid), 2);
  row = 0;
  col = 0;
  GtkWidget *close_b = gtk_button_new_with_label("Close");
  gtk_widget_set_name(close_b, "close_button");
  g_signal_connect(close_b, "button-press-event", G_CALLBACK(close_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), close_b, col, row, 1, 1);
  col++;
  get_midi_devices();

  if (n_midi_devices > 0) {
    GtkWidget *devices_label = gtk_label_new("MIDI device(s)");
    gtk_widget_set_name(devices_label, "boldlabel");
    gtk_widget_set_halign(devices_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), devices_label, col, row, 2, 1);
    //
    // Now put the device checkboxes in columns 3 (width: 1), 4 (width: 3), 7 (width: 1)
    // and make as many rows as necessary. Note that there will rarely be more than 2
    // MIDI devices.
    //
    col = 3;
    int width = 1;

    for (int i = 0; i < n_midi_devices; i++) {
      device_b[i] = gtk_check_button_new_with_label(midi_devices[i].name);
      gtk_widget_set_name(device_b[i], "boldlabel");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(device_b[i]), midi_devices[i].active);
      gtk_grid_attach(GTK_GRID(grid), device_b[i], col, row, width, 1);

      switch (col) {
      case 3:
        col = 4;
        width = 3;
        break;

      case 4:
        col = 7;
        width = 1;
        break;

      case 7:
        col = 3;
        width = 1;
        row++;
        break;
      }

      g_signal_connect(device_b[i], "toggled", G_CALLBACK(device_cb), GINT_TO_POINTER(i));
      gtk_widget_show(device_b[i]);
    }

    //
    // Row containing device checkboxes is partially filled,
    // advance to next one.
    if (col > 3) {
      row++;
    }
  } else {
    GtkWidget *devices_label = gtk_label_new("No MIDI devices found!");
    gtk_widget_set_name(devices_label, "boldlabel");
    gtk_widget_set_halign(devices_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), devices_label, col, row, 4, 1);
    row++;
  }

  row++;
  col = 0;
  GtkWidget *label = gtk_label_new("Event");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  label = gtk_label_new("Channel");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  label = gtk_label_new("Note");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  label = gtk_label_new("Type");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  label = gtk_label_new("Value");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  label = gtk_label_new("Min");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  label = gtk_label_new("Max");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  label = gtk_label_new("Action");
  gtk_widget_set_name(label, "boldlabel");
  gtk_grid_attach(GTK_GRID(grid), label, col++, row, 1, 1);
  row++;
  col = 0;
  newEvent = gtk_label_new(NULL);
  gtk_grid_attach(GTK_GRID(grid), newEvent, col++, row, 1, 1);
  newChannel = gtk_label_new(NULL);
  gtk_grid_attach(GTK_GRID(grid), newChannel, col++, row, 1, 1);
  newNote = gtk_label_new(NULL);
  gtk_grid_attach(GTK_GRID(grid), newNote, col++, row, 1, 1);
  newType = gtk_combo_box_text_new();
  my_combo_attach(GTK_GRID(grid), newType, col++, row, 1, 1);
  g_signal_connect(newType, "changed", G_CALLBACK(type_changed_cb), NULL);
  newVal = gtk_label_new(NULL);
  gtk_grid_attach(GTK_GRID(grid), newVal, col++, row, 1, 1);
  newMin = gtk_label_new(NULL);
  gtk_grid_attach(GTK_GRID(grid), newMin, col++, row, 1, 1);
  newMax = gtk_label_new(NULL);
  gtk_grid_attach(GTK_GRID(grid), newMax, col++, row, 1, 1);
  newAction = gtk_button_new_with_label("    ");
  g_signal_connect(newAction, "button-press-event", G_CALLBACK(action_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), newAction, col, row, 3, 1);
  row++;
  clear_b = gtk_button_new_with_label("Delete All");
  gtk_grid_attach(GTK_GRID(grid), clear_b, 0, row, 1, 1);
  g_signal_connect(clear_b, "clicked", G_CALLBACK(clear_cb), NULL);
  delete_b = gtk_button_new_with_label("Delete");
  g_signal_connect(delete_b, "button-press-event", G_CALLBACK(delete_cb), NULL);
  gtk_grid_attach(GTK_GRID(grid), delete_b, 1, row, 1, 1);
  GtkWidget *ignore_b = gtk_check_button_new_with_label("Ignore Controller Pairs");
  gtk_widget_set_name(ignore_b, "boldlabel");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ignore_b), midiIgnoreCtrlPairs);
  gtk_grid_attach(GTK_GRID(grid), ignore_b, 3, row, 3, 1);
  g_signal_connect(ignore_b, "toggled", G_CALLBACK(ignore_cb), NULL);
  row++;
  col = 0;
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  height = display_height - 180 -  15 * ((n_midi_devices + 1) / 3);

  if (height > 400) { height = 400; }

  gtk_widget_set_size_request(scrolled_window, 400, height);
  view = gtk_tree_view_new();
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Event", renderer, "text", EVENT_COLUMN, NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Channel", renderer, "text", CHANNEL_COLUMN, NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "NOTE", renderer, "text", NOTE_COLUMN, NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "TYPE", renderer, "text", TYPE_COLUMN, NULL);
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "ACTION", renderer, "text", ACTION_COLUMN, NULL);
  store = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_STRING);
  load_store();
  gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(store));
  gtk_container_add(GTK_CONTAINER(scrolled_window), view);
  gtk_grid_attach(GTK_GRID(grid), scrolled_window, col, row, 5, 10);
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
  g_signal_connect(model, "row-inserted", G_CALLBACK(row_inserted_cb), NULL);
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
  selection_signal_id = g_signal_connect(G_OBJECT(selection), "changed", G_CALLBACK(tree_selection_changed_cb), NULL);
  //
  // Place a fixed container to hold the wheel parameters
  // and create sub-grid
  //
  col = 5;
  WheelContainer = gtk_fixed_new();
  gtk_widget_set_size_request(WheelContainer, 300, 300 - 15 * ((n_midi_devices + 1) / 3));
  gtk_grid_attach(GTK_GRID(grid), WheelContainer, col, row, 6, 10);
  //
  // Showing/hiding the container may resize-the columns of the main grid,
  // and causing other elements to move around. Therefore create a further
  // "dummy" frame that is always shown. The dummy must have the same width
  // and a small height.
  //
  GtkWidget *DummyContainer = gtk_fixed_new();
  gtk_widget_set_size_request(DummyContainer, 300, 1);
  gtk_grid_attach(GTK_GRID(grid), DummyContainer, col, row, 6, 1);
  GtkWidget *WheelGrid = gtk_grid_new();
  gtk_grid_set_column_spacing (GTK_GRID(WheelGrid), 2);
  col = 0;
  row = 0;
  // the new-line in the label get some space between the text and the spin buttons
  lbl = gtk_label_new("Configure WHEEL parameters");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_size_request(lbl, 300, 30);
  gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(lbl, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(WheelGrid), lbl, col, row, 3, 1);
  //
  // Finally, put wheel config elements into the wheel grid
  //
  row++;
  col = 0;
  lbl = gtk_label_new("Left <<<");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(WheelGrid), lbl, col, row, 1, 1);
  col++;
  set_vfl1 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_vfl1, col, row, 1, 1);
  g_signal_connect(set_vfl1, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(2));
  col++;
  set_vfl2 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_vfl2, col, row, 1, 1);
  g_signal_connect(set_vfl2, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(3));
  row++;
  col = 0;
  lbl = gtk_label_new("Left <<");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(WheelGrid), lbl, col, row, 1, 1);
  col++;
  set_fl1 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_fl1, col, row, 1, 1);
  g_signal_connect(set_fl1, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(4));
  col++;
  set_fl2 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_fl2, col, row, 1, 1);
  g_signal_connect(set_fl2, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(5));
  row++;
  col = 0;
  lbl = gtk_label_new("Left <");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(WheelGrid), lbl, col, row, 1, 1);
  col++;
  set_lft1 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_lft1, col, row, 1, 1);
  g_signal_connect(set_lft1, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(6));
  col++;
  set_lft2 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_lft2, col, row, 1, 1);
  g_signal_connect(set_lft2, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(7));
  row++;
  col = 0;
  lbl = gtk_label_new("Right >");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(WheelGrid), lbl, col, row, 1, 1);
  col++;
  set_rgt1 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_rgt1, col, row, 1, 1);
  g_signal_connect(set_rgt1, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(8));
  col++;
  set_rgt2 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_rgt2, col, row, 1, 1);
  g_signal_connect(set_rgt2, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(9));
  row++;
  col = 0;
  lbl = gtk_label_new("Right >>");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(WheelGrid), lbl, col, row, 1, 1);
  col++;
  set_fr1 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_fr1, col, row, 1, 1);
  g_signal_connect(set_fr1, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(10));
  col++;
  set_fr2 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_fr2, col, row, 1, 1);
  g_signal_connect(set_fr2, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(11));
  row++;
  col = 0;
  lbl = gtk_label_new("Right >>>");
  gtk_widget_set_name(lbl, "boldlabel");
  gtk_widget_set_halign(lbl, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(WheelGrid), lbl, col, row, 1, 1);
  col++;
  set_vfr1 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_vfr1, col, row, 1, 1);
  g_signal_connect(set_vfr1, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(12));
  col++;
  set_vfr2 = gtk_spin_button_new_with_range(-1.0, 127.0, 1.0);
  gtk_grid_attach(GTK_GRID(WheelGrid), set_vfr2, col, row, 1, 1);
  g_signal_connect(set_vfr2, "value-changed", G_CALLBACK(wheelparam_cb), GINT_TO_POINTER(13));
  gtk_container_add(GTK_CONTAINER(content), grid);
  gtk_container_add(GTK_CONTAINER(WheelContainer), WheelGrid);
  sub_menu = dialog;
  gtk_widget_show_all(dialog);
  //
  // Hide "accept from any source" checkbox
  // (made visible only if config is checked)
  gtk_widget_hide(WheelContainer);
  gtk_widget_set_sensitive(delete_b, FALSE);
  gtk_widget_set_sensitive(clear_b, FALSE);
}

static int updatePanel(int state) {
  gchar text[32];

  switch (state) {
  case UPDATE_NEW:
    gtk_label_set_text(GTK_LABEL(newEvent), Event2String(thisEvent));
    snprintf(text, 32, "%d", thisChannel);
    gtk_label_set_text(GTK_LABEL(newChannel), text);
    snprintf(text, 32, "%d", thisNote);
    gtk_label_set_text(GTK_LABEL(newNote), text);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(newType));

    switch (thisEvent) {
    case MIDI_NOTE:
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "KEY");
      gtk_widget_set_sensitive(newType, FALSE);
      break;

    case MIDI_CTRL:
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "WHEEL");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "KNOB/SLIDER");
      gtk_widget_set_sensitive(newType, TRUE);
      break;

    case MIDI_PITCH:
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "KNOB/SLIDER");
      gtk_widget_set_sensitive(newType, FALSE);
      break;

    default:
      // This cannot happen
      t_print("%s: Unknown Event in UPDATE_NEW\n", __FUNCTION__);
    }

    gtk_combo_box_set_active (GTK_COMBO_BOX(newType), 0);
    gtk_button_set_label(GTK_BUTTON(newAction), ActionTable[thisAction].str);
    snprintf(text, 32, "%d", thisVal);
    gtk_label_set_text(GTK_LABEL(newVal), text);
    snprintf(text, 32, "%d", thisMin);
    gtk_label_set_text(GTK_LABEL(newMin), text);
    snprintf(text, 32, "%d", thisMax);
    gtk_label_set_text(GTK_LABEL(newMax), text);
    break;

  case UPDATE_CURRENT:
    snprintf(text, 32, "%d", thisVal);
    gtk_label_set_text(GTK_LABEL(newVal), text);
    snprintf(text, 32, "%d", thisMin);
    gtk_label_set_text(GTK_LABEL(newMin), text);
    snprintf(text, 32, "%d", thisMax);
    gtk_label_set_text(GTK_LABEL(newMax), text);
    break;

  case UPDATE_EXISTING:
    gtk_label_set_text(GTK_LABEL(newEvent), Event2String(thisEvent));
    snprintf(text, 32, "%d", thisChannel);
    gtk_label_set_text(GTK_LABEL(newChannel), text);
    snprintf(text, 32, "%d", thisNote);
    gtk_label_set_text(GTK_LABEL(newNote), text);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(newType));

    switch (thisEvent) {
    case MIDI_NOTE:
      thisType = MIDI_KEY;
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "KEY");
      gtk_combo_box_set_active (GTK_COMBO_BOX(newType), 0);
      gtk_widget_set_sensitive(newType, FALSE);
      break;

    case MIDI_CTRL:
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "WHEEL");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "KNOB/SLIDER");

      if (thisType == MIDI_KNOB) {
        gtk_combo_box_set_active (GTK_COMBO_BOX(newType), 1);
      } else {
        thisType = MIDI_WHEEL;
        gtk_combo_box_set_active (GTK_COMBO_BOX(newType), 0);
      }

      gtk_widget_set_sensitive(newType, TRUE);
      break;

    case MIDI_PITCH:
      thisType = MIDI_KNOB;
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(newType), NULL, "KNOB/SLIDER");
      gtk_combo_box_set_active (GTK_COMBO_BOX(newType), 0);
      gtk_widget_set_sensitive(newType, FALSE);
      break;

    default:
      // cannot happen
      t_print("%s: Unknown event in  UPDATE_EXISTING\n", __FUNCTION__);
      break;
    }

    snprintf(text, 32, "%d", thisVal);
    gtk_label_set_text(GTK_LABEL(newVal), text);
    snprintf(text, 32, "%d", thisMin);
    gtk_label_set_text(GTK_LABEL(newMin), text);
    snprintf(text, 32, "%d", thisMax);
    gtk_label_set_text(GTK_LABEL(newMax), text);
    find_current_cmd();

    //t_print("%s: current_cmd %p\n", __FUNCTION__, current_cmd);
    if (current_cmd != NULL) {
      thisVfl1  = current_cmd->vfl1;
      thisVfl2  = current_cmd->vfl2;
      thisFl1   = current_cmd->fl1;
      thisFl2   = current_cmd->fl2;
      thisLft1  = current_cmd->lft1;
      thisLft2  = current_cmd->lft2;
      thisRgt1  = current_cmd->rgt1;
      thisRgt2  = current_cmd->rgt2;
      thisFr1   = current_cmd->fr1;
      thisFr2   = current_cmd->fr2;
      thisVfr1  = current_cmd->vfr1;
      thisVfr2  = current_cmd->vfr2;
    }

    update_wheelparams(NULL);
    break;
  }

  return 0;
}

typedef struct MYEVENT {
  enum MIDIevent event;
  int            channel;
  int            note;
  int            val;
} myevent;

int ProcessNewMidiConfigureEvent(void * data) {
  //
  // This is now running in the GTK idle queue
  //
  const myevent *mydata = (myevent *) data;
  enum MIDIevent event = mydata->event;
  int  channel = mydata->channel;
  int  note = mydata->note;
  int  val = mydata->val;
  char *str_event;
  char *str_channel;
  char *str_note;
  char *str_type;
  char *str_action;
  g_free(data);

  if (event == thisEvent && channel == thisChannel && note == thisNote) {
    thisVal = val;

    if (val < thisMin) { thisMin = val; }

    if (val > thisMax) { thisMax = val; }

    updatePanel(UPDATE_CURRENT);
  } else {
    thisEvent = event;
    thisChannel = channel;
    thisNote = note;
    thisVal = val;
    thisMin = val;
    thisMax = val;
    thisType = TYPE_NONE;
    thisAction = NO_ACTION;
    //
    // set default values for wheel parameters
    //
    thisDelay =  0;
    thisVfl1  = -1;
    thisVfl2  = -1;
    thisFl1   = -1;
    thisFl2   = -1;
    thisLft1  =  0;
    thisLft2  = 63;
    thisRgt1  = 65;
    thisRgt2  = 127;
    thisFr1   = -1;
    thisFr2   = -1;
    thisVfr1  = -1;
    thisVfr2  = -1;
    //
    // search tree to see if there is already an event in the list
    // which is the same (matching channel/event/note).
    //
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);

    while (valid) {
      gtk_tree_model_get(model, &iter, EVENT_COLUMN, &str_event, -1);
      gtk_tree_model_get(model, &iter, CHANNEL_COLUMN, &str_channel, -1);
      gtk_tree_model_get(model, &iter, NOTE_COLUMN, &str_note, -1);
      gtk_tree_model_get(model, &iter, TYPE_COLUMN, &str_type, -1);
      gtk_tree_model_get(model, &iter, BSTR_COLUMN, &str_action, -1);

      if (str_event != NULL && str_channel != NULL && str_note != NULL && str_type != NULL && str_action != NULL) {
        enum MIDIevent tree_event;
        int tree_channel;
        int tree_note;
        tree_event = String2Event(str_event);
        tree_channel = atoi(str_channel);
        tree_note = atoi(str_note);

        if (thisEvent == tree_event && thisChannel == tree_channel && thisNote == tree_note) {
          thisVal = 0;
          thisMin = 0;
          thisMax = 0;
          thisType = String2Type(str_type);
          thisAction = NO_ACTION;

          for (int i = 0; i < ACTIONS; i++) {
            if (!strcmp(ActionTable[i].button_str, str_action) && (ActionTable[i].type & thisType)) {
              thisAction = ActionTable[i].action;
              break;
            }
          }

          gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), gtk_tree_model_get_path(model, &iter), NULL, FALSE);
          updatePanel(UPDATE_EXISTING);
          gtk_widget_set_sensitive(delete_b, TRUE);
          gtk_widget_set_sensitive(clear_b, TRUE);
          return 0;
        }
      }

      valid = gtk_tree_model_iter_next(model, &iter);
    }

    //
    // This is a new event
    //
    updatePanel(UPDATE_NEW);
  }

  return 0;
}

void NewMidiConfigureEvent(enum MIDIevent event, int channel, int note, int val) {
  if (ignore_incoming_events) {
    return;
  }

  //
  // Sometimes a "heart beat" from a device might be useful. Therefore, we resert
  // channel=16 note=0 for this purpose and filter this out here
  //
  if (event == MIDI_NOTE && channel == 15 && note == 0) {
    return;
  }

  //
  // Put it into the idle queue so we can directly use GTK
  //
  myevent *data = g_new(myevent, 1);
  data->event = event;
  data->channel = channel;
  data->note = note;
  data->val = val;
  //t_print("%s: Event=%d Chan=%d Note=%d Val=%d\n", __FUNCTION__, event, channel, note, val);
  g_idle_add(ProcessNewMidiConfigureEvent, data);
}

void midiSaveState() {
  char name[128];
  char value[128];
  struct desc *cmd;
  int entry;
  int i;
  entry = 0;
  SetPropI0("midiIgnoreCtrlPairs", midiIgnoreCtrlPairs);

  for (i = 0; i < n_midi_devices; i++) {
    if (midi_devices[i].active) {
      SetPropS1("mididevice[%d].name", entry, midi_devices[i].name);
      entry++;
    }
  }

  // the value i=128 is for the PitchBend
  for (i = 0; i < 129; i++) {
    cmd = MidiCommandsTable[i];
    entry = -1;

    while (cmd != NULL) {
      entry++;
      int channel = cmd->channel;
      //t_print("%s:  channel=%d key=%d entry=%d event=%s type=%s action=%s\n",__FUNCTION__,channel,i,entry, Event2String(cmd->event),Type2String(cmd->type),ActionTable[cmd->action].str);
      SetPropI2("midi[%d].entry[%d].channel", i, entry,                      channel);
      SetPropS3("midi[%d].entry[%d].channel[%d].event", i, entry, channel,   Event2String(cmd->event));
      SetPropS3("midi[%d].entry[%d].channel[%d].type", i, entry, channel,    Type2String(cmd->type));
      SetPropA3("midi[%d].entry[%d].channel[%d].action", i, entry, channel,  cmd->action);

      //
      // For wheels, also store the additional parameters,
      //
      if (cmd->type == MIDI_WHEEL) {
        SetPropI3("midi[%d].entry[%d].channel[%d].vfl1", i, entry, channel,       cmd->vfl1);
        SetPropI3("midi[%d].entry[%d].channel[%d].vfl2", i, entry, channel,       cmd->vfl2);
        SetPropI3("midi[%d].entry[%d].channel[%d].fl1", i, entry, channel,        cmd->fl1);
        SetPropI3("midi[%d].entry[%d].channel[%d].fl2", i, entry, channel,        cmd->fl2);
        SetPropI3("midi[%d].entry[%d].channel[%d].lft1", i, entry, channel,       cmd->lft1);
        SetPropI3("midi[%d].entry[%d].channel[%d].lft2", i, entry, channel,       cmd->lft2);
        SetPropI3("midi[%d].entry[%d].channel[%d].rgt1", i, entry, channel,       cmd->rgt1);
        SetPropI3("midi[%d].entry[%d].channel[%d].rgt2", i, entry, channel,       cmd->rgt2);
        SetPropI3("midi[%d].entry[%d].channel[%d].fr1", i, entry, channel,        cmd->fr1);
        SetPropI3("midi[%d].entry[%d].channel[%d].fr2", i, entry, channel,        cmd->fr2);
        SetPropI3("midi[%d].entry[%d].channel[%d].vfr1", i, entry, channel,       cmd->vfr1);
        SetPropI3("midi[%d].entry[%d].channel[%d].vfr2", i, entry, channel,       cmd->vfr2);
      }

      cmd = cmd->next;
    }

    if (entry != -1) {
      snprintf(name, 128, "midi[%d].entries", i);
      snprintf(value, 128, "%d", entry + 1);
      setProperty(name, value);
    }
  }
}

void midiRestoreState() {
  char str[128];
  int channel;
  int event;
  int type;
  int action;
  int vfl1, vfl2;
  int fl1, fl2;
  int lft1, lft2;
  int rgt1, rgt2;
  int fr1, fr2;
  int vfr1, vfr2;
  int i, j;
  get_midi_devices();
  MidiReleaseCommands();
  //t_print("%s\n",__FUNCTION__);
  GetPropI0("midiIgnoreCtrlPairs", midiIgnoreCtrlPairs);

  //
  // Note this is too early to open the MIDI devices, since the
  // radio has not yet fully been configured. Therefore, only
  // set the "active" flag, and the devices will be opened in
  // radio.c when it is appropriate
  //
  for (i = 0; i < MAX_MIDI_DEVICES; i++) {
    g_strlcpy(str, "NO_MIDI_DEVICE_FOUND", 128);
    GetPropS1("mididevice[%d].name", i,  str);

    for (j = 0; j < n_midi_devices; j++) {
      if (strcmp(midi_devices[j].name, str) == 0) {
        midi_devices[j].active = 1;
        t_print("%s: MIDI device %s active=%d\n", __FUNCTION__, str, midi_devices[j].active);
      }
    }
  }

  // the value i=128 is for the PitchBend
  for (i = 0; i < 129; i++) {
    int entries = -1;
    GetPropI1("midi[%d].entries", i, entries);

    for (int entry = 0; entry < entries; entry++) {
      channel = -1;
      GetPropI2("midi[%d].entry[%d].channel", i, entry,      channel);

      if (channel < 0) { continue; }

      g_strlcpy(str, "NONE", 128);
      GetPropS3("midi[%d].entry[%d].channel[%d].event", i, entry, channel, str);
      event = String2Event(str);
      g_strlcpy(str, "NONE", 128);
      GetPropS3("midi[%d].entry[%d].channel[%d].type", i, entry, channel, str);
      type  = String2Type(str);
      action = NO_ACTION;
      GetPropA3("midi[%d].entry[%d].channel[%d].action", i, entry, channel, action);
      //
      // Look for "wheel" parameters. For those not found,
      // use default value
      //
      vfl1 = -1;
      vfl2 = -1;
      fl1 = -1;
      fl2 = -1;
      lft1 = 0;
      lft2 = 63;
      rgt1 = 65;
      rgt2 = 127;
      fr1 = -1;
      fr2 = -1;
      vfr1 = -1;
      vfr2 = -1;

      if (type == MIDI_WHEEL) {
        GetPropI3("midi[%d].entry[%d].channel[%d].vfl1", i, entry, channel,  vfl1);
        GetPropI3("midi[%d].entry[%d].channel[%d].vfl2", i, entry, channel,  vfl2);
        GetPropI3("midi[%d].entry[%d].channel[%d].fl1", i, entry, channel,   fl1);
        GetPropI3("midi[%d].entry[%d].channel[%d].fl2", i, entry, channel,   fl2);
        GetPropI3("midi[%d].entry[%d].channel[%d].lft1", i, entry, channel,  lft1);
        GetPropI3("midi[%d].entry[%d].channel[%d].lft2", i, entry, channel,  lft2);
        GetPropI3("midi[%d].entry[%d].channel[%d].rgt1", i, entry, channel,  rgt1);
        GetPropI3("midi[%d].entry[%d].channel[%d].rgt2", i, entry, channel,  rgt2);
        GetPropI3("midi[%d].entry[%d].channel[%d].fr1", i, entry, channel,   fr1);
        GetPropI3("midi[%d].entry[%d].channel[%d].fr2", i, entry, channel,   fr2);
        GetPropI3("midi[%d].entry[%d].channel[%d].vfr1", i, entry, channel,  vfr1);
        GetPropI3("midi[%d].entry[%d].channel[%d].vfr2", i, entry, channel,  vfr2);
      }

      //
      // Construct descriptor and add to the list of MIDI commands
      //
      struct desc *desc = (struct desc *) malloc(sizeof(struct desc));
      desc->next     = NULL;
      desc->action   = action; // MIDIaction
      desc->type     = type;   // MIDItype
      desc->event    = event;  // MIDIevent
      desc->vfl1     = vfl1;
      desc->vfl2     = vfl2;
      desc->fl1      = fl1;
      desc->fl2      = fl2;
      desc->lft1     = lft1;
      desc->lft2     = lft2;
      desc->rgt1     = rgt1;
      desc->rgt2     = rgt2;
      desc->fr1      = fr1;
      desc->fr2      = fr2;
      desc->vfr1     = vfr1;
      desc->vfr2     = vfr2;
      desc->channel  = channel;
      MidiAddCommand(i, desc);
    }
  }
}

//
// Utility functions to convert enums to human-readable strings
//
static char *Event2String(enum MIDIevent event) {
  switch (event) {
  case EVENT_NONE:
  default:
    return "NONE";
    break;

  case MIDI_NOTE:
    return "NOTE";
    break;

  case MIDI_CTRL:
    return "CTRL";
    break;

  case MIDI_PITCH:
    return "PITCH";
    break;
  }
}

static enum MIDIevent String2Event(const char *str) {
  if (!strcmp(str, "NOTE"))  { return MIDI_NOTE;  }

  if (!strcmp(str, "CTRL"))  { return MIDI_CTRL;  }

  if (!strcmp(str, "PITCH")) { return MIDI_PITCH; }

  return EVENT_NONE;
}

static char *Type2String(enum ACTIONtype type) {
  switch (type) {
  case TYPE_NONE:
  default:
    return "NONE";
    break;

  case MIDI_KEY:
    return "KEY";
    break;

  case MIDI_KNOB:
    return "KNOB/SLIDER";
    break;

  case MIDI_WHEEL:
    return "WHEEL";
    break;
  }
}

static enum ACTIONtype String2Type(const char *str) {
  if (!strcmp(str, "KEY"        )) { return MIDI_KEY;   }

  if (!strcmp(str, "KNOB/SLIDER")) { return MIDI_KNOB;  }

  if (!strcmp(str, "WHEEL"      )) { return MIDI_WHEEL; }

  return TYPE_NONE;
}
