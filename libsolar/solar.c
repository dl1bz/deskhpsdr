/* Copyright (C)
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
#include "solar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#define SOLAR_URL "https://www.hamqsl.com/solarxml.php"
#define MUF_URL "https://www.ionosonde.iap-kborn.de/ionogram.htm"
#define ES6_URL "https://dxrobot.gooddx.net/eskiplog50.htm"

struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t total = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
  char *ptr = realloc(mem->memory, mem->size + total + 1);
  if (!ptr) { return 0; }
  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, total);
  mem->size += total;
  mem->memory[mem->size] = 0;
  return total;
}


static int node_is_table_row(xmlNode *node) {
  return node && node->type == XML_ELEMENT_NODE && xmlStrcasecmp(node->name, BAD_CAST "tr") == 0;
}

static int text_has_50mhz_frequency(const char *text) {
  if (!text) { return 0; }
  for (const char *p = text; *p; p++) {
    if (p[0] != '5' || p[1] != '0') { continue; }
    const char *q = p + 2;
    while (*q == ' ' || *q == '\t') { q++; }
    if (*q != '.' && *q != ',') { continue; }
    q++;
    if (*q >= '0' && *q <= '9') { return 1; }
  }
  return 0;
}

#define ES6_MAX_UNIQUE_ROWS 512
#define ES6_ALERT_MAX_AGE_MINUTES 360

typedef struct {
  int spots;
  int unique;
  uint64_t hashes[ES6_MAX_UNIQUE_ROWS];
  int marker_found;
  int marker_status;
  time_t marker_time;
} Es6Stats;

static uint64_t es6_row_hash(const char *text) {
  uint64_t hash = UINT64_C(1469598103934665603);
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    hash ^= *p;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static int ascii_case_contains(const char *text, const char *needle) {
  if (!text || !needle || !*needle) { return 0; }
  size_t needle_len = strlen(needle);
  for (const char *p = text; *p; p++) {
    size_t i = 0;
    while (i < needle_len && p[i] &&
           tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == needle_len) { return 1; }
  }
  return 0;
}

static int month_number(const char *month) {
  static const char *months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  for (int i = 0; i < 12; i++) {
    if (tolower((unsigned char)month[0]) == tolower((unsigned char)months[i][0]) &&
        tolower((unsigned char)month[1]) == tolower((unsigned char)months[i][1]) &&
        tolower((unsigned char)month[2]) == tolower((unsigned char)months[i][2])) {
      return i + 1;
    }
  }
  return 0;
}

/* Convert a UTC civil date to seconds since 1970-01-01 without changing process TZ. */
static int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned month_prime = month > 2 ? month - 3 : month + 9;
  const unsigned doy = (153 * month_prime + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static int parse_es6_row_time(const char *text, time_t *timestamp) {
  if (!text || !timestamp) { return 0; }
  for (const char *p = text; *p; p++) {
    if (!isdigit((unsigned char)p[0])) { continue; }
    int hhmm = 0;
    int day = 0;
    int year = 0;
    char month_text[4] = {0};
    if (sscanf(p, "%4d %d %3s %d", &hhmm, &day, month_text, &year) != 4) { continue; }
    int month = month_number(month_text);
    int hour = hhmm / 100;
    int minute = hhmm % 100;
    if (month == 0 || year < 2000 || year > 2200 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      continue;
    }
    int64_t seconds = days_from_civil(year, (unsigned)month, (unsigned)day) * 86400 +
                      hour * 3600 + minute * 60;
    *timestamp = (time_t)seconds;
    return 1;
  }
  return 0;
}

static void inspect_es6_row(xmlNode *row, Es6Stats *stats) {
  xmlChar *content = xmlNodeGetContent(row);
  if (!content) { return; }
  const char *text = (const char *)content;

  if (text_has_50mhz_frequency(text)) {
    uint64_t hash = es6_row_hash(text);
    int duplicate = 0;
    stats->spots++;
    for (int i = 0; i < stats->unique; i++) {
      if (stats->hashes[i] == hash) {
        duplicate = 1;
        break;
      }
    }
    if (!duplicate && stats->unique < ES6_MAX_UNIQUE_ROWS) {
      stats->hashes[stats->unique++] = hash;
    }
  }

  int marker_status = -1;
  if (ascii_case_contains(text, "50 MHz E-skip alert issued by DXrobot")) {
    marker_status = 1;
  } else if (ascii_case_contains(text, "50 MHz E-skip opening is over")) {
    marker_status = 0;
  }
  if (marker_status >= 0) {
    time_t marker_time;
    if (parse_es6_row_time(text, &marker_time) &&
        (!stats->marker_found || marker_time > stats->marker_time)) {
      stats->marker_found = 1;
      stats->marker_status = marker_status;
      stats->marker_time = marker_time;
    }
  }

  xmlFree(content);
}

static void count_es6_rows(xmlNode *node, Es6Stats *stats) {
  for (xmlNode *cur = node; cur; cur = cur->next) {
    if (node_is_table_row(cur)) { inspect_es6_row(cur, stats); }
    count_es6_rows(cur->children, stats);
  }
}

static int parse_es6_status(const char *html, size_t size, int *spots, int *unique,
                            char *marker, size_t marker_size, int *age_minutes) {
  htmlDocPtr doc = htmlReadMemory(html, (int)size, ES6_URL, NULL,
                                  HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
  if (!doc) { return -1; }
  Es6Stats stats = {0};
  xmlNode *root = xmlDocGetRootElement(doc);
  if (root) { count_es6_rows(root, &stats); }
  xmlFreeDoc(doc);
  if (spots) { *spots = stats.spots; }
  if (unique) { *unique = stats.unique; }
  if (age_minutes) { *age_minutes = -1; }

  if (stats.marker_found) {
    time_t now = time(NULL);
    int age = (int)((now - stats.marker_time) / 60);
    if (age < 0) { age = 0; }
    if (age_minutes) { *age_minutes = age; }
    if (stats.marker_status > 0 && age > ES6_ALERT_MAX_AGE_MINUTES) {
      if (marker && marker_size > 0) { snprintf(marker, marker_size, "stale-alert"); }
      return 0;
    }
    if (marker && marker_size > 0) {
      snprintf(marker, marker_size, "%s", stats.marker_status > 0 ? "alert" : "over");
    }
    return stats.marker_status;
  }

  if (marker && marker_size > 0) { snprintf(marker, marker_size, "fallback"); }
  /* Keep spot counting as fallback if DXRobot changes or omits its markers. */
  return stats.unique >= 2 ? 1 : 0;
}

int fetch_es6_status(int *spots, int *unique, char *marker, size_t marker_size, int *age_minutes) {
  int status = -1;
  if (marker && marker_size > 0) { marker[0] = '\0'; }
  if (age_minutes) { *age_minutes = -1; }
  struct MemoryStruct chunk = {malloc(1), 0};
  if (!chunk.memory) {
    fprintf(stderr, "fetch_es6_status: memory error\n");
    return status;
  }
  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "fetch_es6_status: curl init error\n");
    free(chunk.memory);
    return status;
  }
  curl_easy_setopt(curl, CURLOPT_URL, ES6_URL);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "deskHPSDR");
  curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "fetch_es6_status: curl error %d: %s\n", (int)res, curl_easy_strerror(res));
    goto cleanup;
  }
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code != 200) {
    fprintf(stderr, "fetch_es6_status: HTTP error %ld\n", response_code);
    goto cleanup;
  }
  status = parse_es6_status(chunk.memory, chunk.size, spots, unique,
                            marker, marker_size, age_minutes);
cleanup:
  curl_easy_cleanup(curl);
  free(chunk.memory);
  return status;
}

static float parse_muf3000(const char *html, size_t size) {
  htmlDocPtr doc = htmlReadMemory(html, (int)size, MUF_URL, NULL,
                                  HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
  if (!doc) { return -1.0f; }
  xmlNode *root = xmlDocGetRootElement(doc);
  xmlChar *content = root ? xmlNodeGetContent(root) : NULL;
  float muf = -1.0f;
  if (content) {
    const char *marker = strstr((const char *)content, "MUF(3000)F2");
    if (marker) {
      marker = strstr(marker, "[MHz]");
      if (marker) {
        marker += strlen("[MHz]");
        while (*marker == ' ' || *marker == '\t' || *marker == '\r' || *marker == '\n') { marker++; }
        char *end = NULL;
        float value = strtof(marker, &end);
        if (end != marker && value > 0.0f && value < 100.0f) { muf = value; }
      }
    }
    xmlFree(content);
  }
  xmlFreeDoc(doc);
  return muf;
}

static float fetch_muf_data(void) {
  float muf = -1.0f;
  struct MemoryStruct chunk = {malloc(1), 0};
  if (!chunk.memory) {
    fprintf(stderr, "fetch_muf_data: memory error\n");
    return muf;
  }
  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "fetch_muf_data: curl init error\n");
    free(chunk.memory);
    return muf;
  }
  curl_easy_setopt(curl, CURLOPT_URL, MUF_URL);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "deskHPSDR");
  curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "fetch_muf_data: curl error %d: %s\n", (int)res, curl_easy_strerror(res));
    goto cleanup;
  }
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code != 200) {
    fprintf(stderr, "fetch_muf_data: HTTP error %ld\n", response_code);
    goto cleanup;
  }
  muf = parse_muf3000(chunk.memory, chunk.size);
cleanup:
  curl_easy_cleanup(curl);
  free(chunk.memory);
  return muf;
}

SolarData fetch_solar_data(void) {
  SolarData data = {0};
  data.sunspots = -1; // Errorindicator
  data.muf = -1.0f;
  struct MemoryStruct chunk = {malloc(1), 0};
  if (!chunk.memory) {
    fprintf(stderr, "Memory Error\n");
    return data;
  }
  CURL *curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "curl Init Error\n");
    free(chunk.memory);
    return data;
  }
  // curl_global_init(CURL_GLOBAL_ALL);  // call only one time per program run -> moved to main.c
  curl_easy_setopt(curl, CURLOPT_URL, SOLAR_URL);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "deskHPSDR");
  curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Timeout-Schutz
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "fetch_solar_data: curl error %d: %s\n", (int)res, curl_easy_strerror(res));
    goto cleanup;
  }
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  if (response_code != 200) {
    fprintf(stderr, "HTTP Error: Code %ld\n", response_code);
    goto cleanup;
  }
  xmlDoc *doc = xmlReadMemory(chunk.memory, chunk.size, NULL, NULL, 0);
  if (!doc) {
    fprintf(stderr, "Error XML Parsing\n");
    goto cleanup;
  }
  xmlNode *root = xmlDocGetRootElement(doc);
  for (xmlNode *n = root->children; n; n = n->next) {
    if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, "solardata") == 0) {
      for (xmlNode *c = n->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) { continue; }
        xmlChar *val = xmlNodeGetContent(c);
        if (!val) { continue; }
        if (strcmp((char *)c->name, "sunspots") == 0) {
          data.sunspots = atoi((char *)val);
        } else if (strcmp((char *)c->name, "solarflux") == 0) {
          data.solarflux = atof((char *)val);
        } else if (strcmp((char *)c->name, "aindex") == 0) {
          data.aindex = atoi((char *)val);
        } else if (strcmp((char *)c->name, "kindex") == 0) {
          data.kindex = atoi((char *)val);
        } else if (strcmp((char *)c->name, "updated") == 0) {
          strncpy(data.updated, (char *)val, sizeof(data.updated) - 1);
        } else if (strcmp((char *)c->name, "xray") == 0) {
          strncpy(data.xray, (char *)val, sizeof(data.xray) - 1);
        } else if (strcmp((char *)c->name, "geomagfield") == 0) {
          strncpy(data.geomagfield, (char *)val, sizeof(data.geomagfield) - 1);
        }
        xmlFree(val);
      }
    }
  }
  xmlFreeDoc(doc);
cleanup:
  curl_easy_cleanup(curl);
  free(chunk.memory);
  data.muf = fetch_muf_data();
  // curl_global_cleanup();  call only one time per program run -> moved to exit_menu.c
  return data;
}
