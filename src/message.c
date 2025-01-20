/* Copyright (C)
* 2023 - Christoph van Wüllen, DL1YCF
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

/*
 * Hook for logging messages to the output file
 *
 * This can be redirected with any method g_print()
 * can be re-directed.
 *
 * t_print
 *           is a g_print() but it puts a time stamp in front.
 * t_perror
 *           is a perror() replacement, it puts a time stamp in font
 *           and reports via g_print
 *
 * Note ALL messages of the program should go through these two functions
 * so it is easy to either silence them completely, or routing them to
 * a separate window for debugging purposes.
 */

#include <gdk/gdk.h>
#include <stdarg.h>
#include <errno.h>

/*
void t_print(const gchar *format, ...) {
  va_list(args);
  va_start(args, format);
  struct timespec ts;
  double now;
  static double starttime;
  static int first = 1;
  char line[1024];
  clock_gettime(CLOCK_MONOTONIC, &ts);
  now = ts.tv_sec + 1E-9 * ts.tv_nsec;

  if (first) {
    first = 0;
    starttime = now;
  }

  //
  // After 11 days, the time reaches 999999.999 so we simply wrap around
  //
  if (now - starttime >= 999999.995) { starttime += 1000000.0; }

  //
  // We have to use vsnprintf to handle the varargs stuff
  // g_print() seems to be thread-safe but call it only ONCE.
  //
  vsnprintf(line, 1024, format, args);
  g_print("%10.3f %s", now - starttime, line);
}
*/

void t_print(const gchar *format, ...) {
  va_list args;
  va_start(args, format);
  struct timespec ts;
  double now;
  static double starttime;
  static int first = 1;
  char line[1024];
  char time_str[16];
  clock_gettime(CLOCK_MONOTONIC, &ts);
  now = ts.tv_sec + 1E-9 * ts.tv_nsec;

  if (first) {
    first = 0;
    starttime = now;
  }

  //
  // After 11 days, the time reaches 999999.999 so we simply wrap around
  //
  if (now - starttime >= 999999.995) {
    starttime += 1000000.0;
  }

  //
  // Berechnung von hh:mm:ss.mmm (Millisekunden)
  //
  double elapsed_time = now - starttime;
  int hours = (int)(elapsed_time / 3600);
  int minutes = (int)((elapsed_time - (hours * 3600)) / 60);
  double seconds = elapsed_time - (hours * 3600) - (minutes * 60);
  int millisec = (int)((seconds - (int)seconds) * 1000); // Millisekunden
  // Formatierte Zeit in den String schreiben
  snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d.%03d", hours, minutes, (int)seconds, millisec);
  //
  // Wir nutzen vsnprintf, um die varargs zu verarbeiten
  // g_print() wird einmalig aufgerufen, um Thread-Sicherheit zu gewährleisten
  //
  vsnprintf(line, sizeof(line), format, args);
  g_print("%s %s", time_str, line);
  va_end(args);
}


void t_perror(const gchar *string) {
  t_print("%s: %s\n", string, strerror(errno));
}
