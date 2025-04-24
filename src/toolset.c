/* Copyright (C)
*
* 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <netdb.h>
#include <math.h>
#include <time.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <pthread.h>

#include "toolset.h"
#include "solar.h"
#include "message.h"

GMutex solar_data_mutex;

int sunspots = -1;
int a_index = -1;
int k_index = -1;
int solar_flux = -1;
char geomagfield[32];
char xray[16];

static gboolean is_minute_marker(int interval) {
  static int last_minute = -1;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  // Intervall prüfen und anpassen
  interval = (interval < 1) ? 5 : (interval > 59) ? 45 : interval;

  if ((t->tm_min % interval == 0) && (t->tm_min != last_minute)) {
    last_minute = t->tm_min;
    return TRUE;
  }

  return FALSE;
}

// HTTPS-Verfügbarkeit prüfen mit optionalem Zertifikats-Check
int https_ok(const char* hostname, int mit_cert_check) {
  SSL_CTX* ctx = NULL;
  SSL* ssl = NULL;
  int server = -1;
  struct hostent* host;
  struct sockaddr_in addr;
  int erfolg = 0; // 0 = fehlgeschlagen, 1 = erfolgreich
  // OpenSSL initialisieren
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
  ctx = SSL_CTX_new(TLS_client_method());

  if (!ctx) {
    ERR_print_errors_fp(stderr);
    return 0;
  }

  // Wenn Zertifikatsprüfung gewünscht, Standard-Zertifikatsstore laden
  if (mit_cert_check) {
    if (!SSL_CTX_set_default_verify_paths(ctx)) {
      fprintf(stderr, "Konnte CA-Zertifikate nicht laden\n");
      SSL_CTX_free(ctx);
      return 0;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
  }

  // Hostname auflösen
  host = gethostbyname(hostname);

  if (!host) {
    SSL_CTX_free(ctx);
    return 0;
  }

  // TCP-Socket erstellen und verbinden
  server = socket(AF_INET, SOCK_STREAM, 0);

  if (server < 0) {
    SSL_CTX_free(ctx);
    return 0;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(443);
  addr.sin_addr = *((struct in_addr*)host->h_addr);
  memset(&(addr.sin_zero), 0, 8);

  if (connect(server, (struct sockaddr * )&addr, sizeof(addr)) < 0) {
    close(server);
    SSL_CTX_free(ctx);
    return 0;
  }

  // SSL erstellen und mit Socket verbinden
  ssl = SSL_new(ctx);
  SSL_set_fd(ssl, server);
  // Hostname für SNI setzen (Server Name Indication)
  SSL_set_tlsext_host_name(ssl, hostname);

  // TLS-Handshake
  if (SSL_connect(ssl) != 1) {
    // Fehlerausgabe bei Debug-Zwecken aktivieren
    // ERR_print_errors_fp(stderr);
    goto cleanup;
  }

  // Zertifikat überprüfen, falls aktiviert
  if (mit_cert_check) {
    long verif = SSL_get_verify_result(ssl);

    if (verif != X509_V_OK) {
      fprintf(stderr, "Zertifikat ungültig: %s\n", X509_verify_cert_error_string(verif));
      goto cleanup;
    }
  }

  erfolg = 1; // Alles ok
cleanup:

  if (ssl) { SSL_free(ssl); }

  if (server >= 0) { close(server); }

  if (ctx) { SSL_CTX_free(ctx); }

  return erfolg;
}

/*
// get Solar Data without threading -> can block the GTK main thread/GUI -> bad
void assign_solar_data(int is_dbg) {
  time_t now = time(NULL);
  const char* host = "www.hamqsl.com";

  if (https_ok(host, 0)) {
    SolarData sd = fetch_solar_data();
    sunspots = sd.sunspots;
    solar_flux = (int)sd.solarflux;
    a_index = sd.aindex;
    k_index = sd.kindex;
    g_strlcpy(geomagfield, sd.geomagfield, sizeof(sd.geomagfield));
    g_strlcpy(xray, sd.xray, sizeof(sd.xray));

    if (is_dbg) {
      t_print("%s fetch data from %s at %s", __FUNCTION__, host, ctime(&now));
      t_print("%s Sunspots: %d, Flux: %d, A: %d, K: %d, X:%s, GMF:%s\n", __FUNCTION__, sunspots, solar_flux, a_index, k_index,
              xray, geomagfield);
    }
  } else {
    t_print("%s failed: host %s not reachable\n", __FUNCTION__, host);
  }
}
*/

static void *solar_thread_func(void *arg) {
  int is_dbg = GPOINTER_TO_INT(arg);
  time_t now = time(NULL);
  const char* host = "www.hamqsl.com";

  if (https_ok(host, 0)) {
    // Lokale Kopie holen
    SolarData sd = fetch_solar_data();

    // Ergebnis sichern – mit Mutex schützen
    if (sd.sunspots != -1) {  // we got valid solar data
      g_mutex_lock(&solar_data_mutex);
      sunspots = sd.sunspots;
      solar_flux = (int)sd.solarflux;
      a_index = sd.aindex;
      k_index = sd.kindex;
      g_strlcpy(geomagfield, sd.geomagfield, sizeof(sd.geomagfield));
      g_strlcpy(xray, sd.xray, sizeof(sd.xray));
      g_mutex_unlock(&solar_data_mutex);

      if (is_dbg) {
        t_print("fetch data from %s at %s", host, ctime(&now));
        t_print("Sunspots: %d, Flux: %d, A: %d, K: %d, X:%s, GMF:%s\n",
                sunspots, solar_flux, a_index, k_index, xray, geomagfield);
      }
    } else {
      t_print("%s: ERROR: invalid data from %s at %s", __FUNCTION__, host, ctime(&now));
    }
  } else {
    t_print("%s failed: host %s at %s not reachable\n", __FUNCTION__, host, ctime(&now));
  }

  return NULL;
}

// get Solar Data with threading -> best solution
static void assign_solar_data_async(int is_dbg) {
  pthread_t solar_thread;

  if (pthread_create(&solar_thread, NULL, solar_thread_func, GINT_TO_POINTER(is_dbg)) == 0) {
    pthread_detach(solar_thread); // kein join nötig
  } else {
    t_print("%s: ERROR: solar_data_fetch thread not started...\n", __FUNCTION__);
  }
}

void check_and_run(int is_dbg) {
  static struct timespec last_check = {0};
  static gboolean first_run = TRUE;
  static int aller_x_min = 5; // jede 5min
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);  // Hochauflösende monotone Uhr
  // Zeitdifferenz in Millisekunden berechnen
  long diff_ms = (now.tv_sec - last_check.tv_sec) * 1000 +
                 (now.tv_nsec - last_check.tv_nsec) / 1000000;

  if (diff_ms >= 200) {
    last_check = now;

    // Beim ersten Mal oder bei neuer x-Minuten-Marke
    if (first_run || is_minute_marker(aller_x_min)) {
      // assign_solar_data(is_dbg);
      assign_solar_data_async(is_dbg); // nicht mehr direkt aufrufen! jetzt als Thread
      first_run = FALSE;
    }
  }
}

// Funktion zum Kürzen des Textes
const char* truncate_text(const char* text, size_t max_length) {
  static char truncated[128];  // Ein statisches Array für den gekürzten Text

  if (strlen(text) > max_length) {
    g_strlcpy(truncated, text, max_length + 1);  // Sicheres Kopieren des Textes
  } else {
    g_strlcpy(truncated, text, sizeof(truncated));  // Sicheres Kopieren des Textes
  }

  return truncated;
}

char* truncate_text_malloc(const char* text, size_t max_length) {
  size_t len = strlen(text);

  if (len > max_length) { len = max_length; }

  char* truncated = g_malloc(len + 1);  // +1 für '\0'
  g_strlcpy(truncated, text, len + 1);  // sicheres Kopieren
  return truncated;  // muss mit g_free() freigegeben werden
}

char* truncate_text_3p(const char* text, size_t max_length) {
  size_t len = strlen(text);

  if (len <= max_length) {
    // Text passt komplett – einfach kopieren
    return g_strdup(text);
  }

  // Für "..." brauchen wir Platz: 3 Zeichen
  if (max_length < 3) {
    // Nicht genug Platz für Text + Ellipsis – gib einfach leeren String zurück
    return g_strdup("");
  }

  size_t cut_len = max_length - 3;  // Platz für Text ohne die drei Punkte
  char* truncated = g_malloc(max_length + 1);  // +1 für '\0'
  g_strlcpy(truncated, text, cut_len + 1);     // +1, weil g_strlcpy inkl. Nullbyte
  strcat(truncated, "...");  // Anhängen
  return truncated;  // Muss mit g_free() freigegeben werden
}

gboolean check_and_run_idle_cb(gpointer data) {
  int arg = GPOINTER_TO_INT(data);
  check_and_run(arg);
  return FALSE; // Nur einmal ausführen
}
