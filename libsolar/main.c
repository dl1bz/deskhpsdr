#include <stdio.h>
#include "solar.h"

int main() {
  SolarData sd = fetch_solar_data();

  if (sd.sunspots == -1) {
    fprintf(stderr, "Fehler beim Abrufen der Solar-Daten\n");
    return 1;
  }

  printf("  Solar Report:\n");
  printf("  Sunspots:   %d\n", sd.sunspots);
  printf("  Solar Flux: %.1f\n", sd.solarflux);
  printf("  A-Index:    %d\n", sd.aindex);
  printf("  K-Index:    %d\n", sd.kindex);
  printf("  Updated:    %s\n", sd.updated);
  return 0;
}
