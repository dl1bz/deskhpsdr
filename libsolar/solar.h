#ifndef SOLAR_H
#define SOLAR_H

typedef struct {
    int sunspots;
    float solarflux;
    int aindex;
    int kindex;
    char updated[64];
} SolarData;

SolarData fetch_solar_data();

#endif // SOLAR_H
