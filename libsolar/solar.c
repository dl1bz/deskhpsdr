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
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#define URL "https://www.hamqsl.com/solarxml.php"

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

SolarData fetch_solar_data() {
  SolarData data = {0};
  data.sunspots = -1; // Errorindicator
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
  curl_easy_setopt(curl, CURLOPT_URL, URL);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Timeout-Schutz
  CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    fprintf(stderr, "curl Error: %s\n", curl_easy_strerror(res));
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
  // curl_global_cleanup();  call only one time per program run -> moved to exit_menu.c
  return data;
}
