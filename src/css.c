/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
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
#include "css.h"
#include "message.h"

//////////////////////////////////////////////////////////////////////////////////////////
//
// Normally one wants to inherit everything from the GTK theme.
// In some cases, however, this does not work too well. But the
// principle here is to change as little as possible.
//
// Here is a list of CSS definitions we make here:
//
// boldlabel           This is used to write texts in menus and the slider area,
//                     therefore it contains 3px border
//
// slider1             Used for slider and zoompan areas for small  screen width
// slider2             Used for slider and zoompan areas for medium screen width
// slider3             Used for slider and zoompan areas for large  screen width
//
// big_txt             This is a large bold text. Used for the "pi label" on the
//                     discovery screen, and the "Start" button there
//
// med_txt             This is a large text. Used for the status bar, etc.
//
// small_txt           This is a small text, used where space is scarce.
//
// close_button        Style for the "Close" button in the menus, so it can be
//                     easily recognized
//
// small_button        15px text with minimal paddding. Used in menus where one wants
//                     to make the buttons narrow.
//
// medium_button       the same as small_button but with 20px
//
// large_button        the same as small_button but with 25px
//
// small_toggle_button Used for the buttons in action dialogs, and the filter etc.
//                     menus where the current choice needs proper high-lighting
//
// popup_scale         Used to define the slider that "pops up" when e.g. AF volume
//                     is changed via GPIO/MIDI but no sliders are on display
//
// checkbutton         THe standard button is Very difficult to see on RaspPi with
//                     a light GTK theme. So we use our own, and draw a grey border
//                     so this should be OK for both the light and dark theme.
//
// radiobutton         see checkbutton.
//
//////////////////////////////////////////////////////////////////////////////////////////
//
// Note on font sizes:
//
// A RaspPi has different default settings whether you have a small screen, a medium
// screen or a large screen (RaspBerry Menu ==> Preferences ==> Appearances Settings).
//
// For a fixed size such as the height of the Sliders or Zoompan area, we therefore
// MUST specify the font size. If not, it may happen that the sliders cannot be
// displayed on a large screen
//
//////////////////////////////////////////////////////////////////////////////////////////
char *css =
  "  * { font-family: Tahoma, Sans; }\n"
  "  combobox { font-size: 15px; }\n"
  "  button   { font-size: 15px; }\n"
  "  checkbutton label { font-size: 15px; }\n"
  "  spinbutton { font-size: 15px; }\n"
  "  radiobutton label  { font-size: 15px; }\n"
  "  scale { font-size: 15px; }\n"
  "  entry { font-size: 15px; }\n"
  "  notebook { font-size: 15px; }\n"
  "  #boldlabel {\n"
  "    padding: 3px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-weight: bold;\n"
  "    font-size: 15px;\n"
  "  }\n"
  "  #slider1   {\n"
  "    padding: 3px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-weight: bold;\n"
  "    font-size: 16px;\n"
  "  }\n"
  "  #slider2   {\n"
  "    padding: 3px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-weight: normal;\n"
  "    font-size: 18px;\n"
  "  }\n"
  "  #slider3   {\n"
  "    padding: 3px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-weight: normal;\n"
  "    font-size: 22px;\n"
  "  }\n"
  "  #big_txt {\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 22px;\n"
  "    font-weight: bold;\n"
  "    }\n"
  "  #med_txt {\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 18px;\n"
  "    }\n"
  "  #small_txt {\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-weight: bold;\n"
  "    font-size: 12px;\n"
  "    }\n"
  "  #close_button {\n"
  "    padding: 5px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 15px;\n"
  "    font-weight: bold;\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  #small_button {\n"
  "    padding: 1px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 15px;\n"
  "    }\n"
  "  #medium_button {\n"
  "    padding: 1px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 20px;\n"
  "    }\n"
  "  #large_button {\n"
  "    padding: 1px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 25px;\n"
  "    }\n"
  "  #small_button_with_border {\n"
  "    padding: 3px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 15px;\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  #small_toggle_button {\n"
  "    padding: 1px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 15px;\n"
  "    background-image: none;\n"
  "    }\n"
  "  #small_toggle_button:checked {\n"
  "    padding: 1px;\n"
  "    font-family: Tahoma, Sans;\n"
  "    font-size: 15px;\n"
  "    background-image: none;\n"
  "    background-color: rgb(100%, 20%, 20%);\n"    // background if selected
  "    color: rgb(100%,100%,100%);\n"               // text if selected
  "    }\n"
  "  #popup_scale slider {\n"
  "    background: rgb(  0%,  0%, 100%);\n"         // Slider handle
  "    }\n"
  "  #popup_scale trough {\n"
  "    background: rgb( 50%,50%, 100%);\n"         // Slider bar
  "    }\n"
  "  #popup_scale value {\n"
  "    color: rgb(100%, 10%, 10%);\n"              // digits
  "    font-size: 15px;\n"
  "    }\n"
  "  checkbutton check {\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  radiobutton radio {\n"
  "    border: 1px solid rgb(50%, 50%, 50%);\n"
  "    }\n"
  "  headerbar { min-height: 0px; padding: 0px; margin: 0px; font-size: 15px; font-family: Tahoma, Sans; }\n"
  ;

//
// If CSS loading from a file named default.css is successful, take that
// Otherwise load the default settings hard-coded above
//
void load_css() {
  GtkCssProvider *provider;
  GdkDisplay *display;
  GdkScreen *screen;
  GError *error;
  int rc;
  provider = gtk_css_provider_new ();
  display = gdk_display_get_default ();
  screen = gdk_display_get_default_screen (display);
  gtk_style_context_add_provider_for_screen (screen,
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  error = NULL;
  rc = gtk_css_provider_load_from_path (provider, "default.css", &error);
  g_clear_error(&error);

  if (rc) {
    t_print("%s: CSS data loaded from file default.css\n", __FUNCTION__);
  } else {
    t_print("%s: failed to load CSS data from file default.css\n", __FUNCTION__);
    rc = gtk_css_provider_load_from_data(provider, css, -1, &error);
    g_clear_error(&error);

    if (rc) {
      t_print("%s: hard-wired CSS data successfully loaded\n", __FUNCTION__);
    } else {
      t_print("%s: failed to load hard-wired CSS data\n", __FUNCTION__);
    }
  }

  g_object_unref (provider);
}
