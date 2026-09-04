/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
* 2024-2026 - Heiko Amft, DL1BZ (Project deskHPSDR)
*
* SPDX-License-Identifier: GPL-3.0-or-later
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "property.h"
#include "radio.h"
#include "message.h"

PROPERTY *properties = NULL;

void clearProperties(void) {
  PROPERTY *next;
  while (properties != NULL) {
    next = properties->next_property;
    g_free(properties->name);
    g_free(properties->value);
    g_free(properties);
    properties = next;
  }
}

/* --------------------------------------------------------------------------*/
/**
* @brief Load Properties
*
* @param filename
*/
void loadProperties(const char *filename) {
  FILE* f = fopen(filename, "r");
  PROPERTY* property;
  // t_print("loadProperties: %s\n", filename);
  int lines = 0;
  clearProperties();
  /////////////////////////////////////////////////////////////////////////////////////////
  //
  // TEMPORARY HOOK:
  // On Saturn XDMA, the name of the props file has originally been derived from
  // the mac address of its eth0 card. This has been changed to a fixed name now,
  // namely saturn.xdma.props.
  //
  // So, if this file does not exists, we try to load from a file derived from the mac
  // address. saveProperties later will use the new file name so this hook should be
  // used only once. So after some time, all users will have their props file
  // converted to the new name.
  //
  if (f == NULL && !strcmp(filename, "saturn.xdma.props")) {
    char oldstyle_path[128];
    snprintf(oldstyle_path, sizeof(oldstyle_path), "%02X-%02X-%02X-%02X-%02X-%02X.props",
             radio->info.network.mac_address[0],
             radio->info.network.mac_address[1],
             radio->info.network.mac_address[2],
             radio->info.network.mac_address[3],
             radio->info.network.mac_address[4],
             radio->info.network.mac_address[5]);
    f = fopen(oldstyle_path, "r");
  }
  //
  /////////////////////////////////////////////////////////////////////////////////////////
  if (f) {
    double version = -1;
    GString *line = g_string_new(NULL);
    int c;
    while ((c = fgetc(f)) != EOF) {
      if (c != '\n') {
        g_string_append_c(line, (char)c);
        continue;
      }
      lines++;
      if (line->len > 0 && line->str[0] != '#') {
        char *separator = strchr(line->str, '=');
        // Beware of "illegal" lines in corrupted files
        if (separator != NULL && separator != line->str && separator[1] != '\0') {
          *separator = '\0';
          const char *name = line->str;
          const char *value = separator + 1;
          property = malloc(sizeof(PROPERTY));
          property->name = g_strdup(name);
          property->value = g_strdup(value);
          property->next_property = properties;
          properties = property;
          if (strcmp(name, "property_version") == 0) {
            version = atof(value);
          }
        }
      }
      g_string_truncate(line, 0);
    }
    // Process a final line that does not end in a newline.
    if (line->len > 0) {
      lines++;
      if (line->str[0] != '#') {
        char *separator = strchr(line->str, '=');
        if (separator != NULL && separator != line->str && separator[1] != '\0') {
          *separator = '\0';
          const char *name = line->str;
          const char *value = separator + 1;
          property = malloc(sizeof(PROPERTY));
          property->name = g_strdup(name);
          property->value = g_strdup(value);
          property->next_property = properties;
          properties = property;
          if (strcmp(name, "property_version") == 0) {
            version = atof(value);
          }
        }
      }
    }
    g_string_free(line, TRUE);
    if (version >= 0.0 && version != PROPERTY_VERSION) {
      clearProperties();
      t_print("loadProperties: version=%f expected version=%f ignoring\n", version, PROPERTY_VERSION);
    }
    fclose(f);
  }
  t_print("loadProperties: %s, lines read: %d\n", filename, lines);
}

/* --------------------------------------------------------------------------*/
/**
* @brief Save Properties
*
* @param filename
*/
void saveProperties(const char *filename) {
  PROPERTY* property;
  char version[32];
  char *tmpname = g_strdup_printf("%s.tmp.XXXXXX", filename);
  int fd = mkstemp(tmpname);
  if (fd < 0) {
    t_print("can't create temporary property file for %s\n", filename);
    g_free(tmpname);
    return;
  }
  FILE *f = fdopen(fd, "w");
  if (!f) {
    t_print("can't open temporary property file for %s\n", filename);
    close(fd);
    unlink(tmpname);
    g_free(tmpname);
    return;
  }
  snprintf(version, sizeof(version), "%0.2f", PROPERTY_VERSION);
  setProperty("property_version", version);
  gboolean ok = TRUE;
  property = properties;
  while (property && ok) {
    if (fprintf(f, "%s=%s\n", property->name, property->value) < 0) {
      ok = FALSE;
    }
    property = property->next_property;
  }
  if (ok && fflush(f) != 0) {
    ok = FALSE;
  }
  if (ok && fsync(fd) != 0) {
    ok = FALSE;
  }
  if (fclose(f) != 0) {
    ok = FALSE;
  }
  if (ok && rename(tmpname, filename) == 0) {
    g_free(tmpname);
    return;
  }
  t_print("can't save %s\n", filename);
  unlink(tmpname);
  g_free(tmpname);
}

/* --------------------------------------------------------------------------*/
/**
* @brief Get Properties
*
* @param name
*
* @return
*/
char *getProperty(const char *name) {
  char *value = NULL;
  PROPERTY* property = properties;
  while (property) {
    if (strcmp(name, property->name) == 0) {
      value = property->value;
      break;
    }
    property = property->next_property;
  }
  return value;
}

/* --------------------------------------------------------------------------*/
/**
* @brief Set Properties
*
* @param name
* @param value
*/
void setProperty(const char *name, const char *value) {
  PROPERTY* property = properties;
  while (property) {
    if (strcmp(name, property->name) == 0) {
      break;
    }
    property = property->next_property;
  }
  if (property) {
    // just update
    g_free(property->value);
    property->value = g_strdup(value);
  } else {
    // new property
    property = malloc(sizeof(PROPERTY));
    property->name = g_strdup(name);
    property->value = g_strdup(value);
    property->next_property = properties;
    properties = property;
  }
}

//
// Utility function myatof
//
// Now we force the C locale, but data in the props file may still have been written
// out using local conventions (e.g. a comma instead of a decimal point in Germany)
// To handle (at least) this case, all commas in the input string are replaced by
// decimal points and then this is fed to atof()
//
double myatof(const char *string) {
  if (!string || !*string) {
    return 0.0;
  }
  char *lstr = g_strdup(string);
  double ret;
  for (char *cp = lstr; *cp; cp++) {
    if (*cp == ',') {
      *cp = '.';
    }
  }
  ret = atof(lstr);
  g_free(lstr);
  return ret;
}
