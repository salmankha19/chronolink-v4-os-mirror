/* Moonsighting Committee method vs adhan-js (the library the implementation claims parity with).
 * Reference: moonsighting_adhan.csv (gen_adhan_moonsighting.js). Usage: test_moonsighting file.csv [tol_seconds]
 * Asr is not compared: adhan-js's Asr is up to ~3 min off the astronomical value at 60 N (checked against PyEphem);
 * Asr accuracy is covered by test_golden against PyEphem. Dhuhr (+5 min) and Maghrib (+3 min) ARE compared, because
 * those adjustments are part of the method. */
#include "prayertimes/PrayerTimes.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
using namespace prayertimes;

struct Agg { char key[48]; int n; double maxabs; int bad; };
static Agg g[64]; static int ng = 0;
static Agg* agg(const char* place, const char* ev) {
    char k[48]; snprintf(k, sizeof k, "%s|%s", place, ev);
    for (int i = 0; i < ng; i++) if (!strcmp(g[i].key, k)) return &g[i];
    strcpy(g[ng].key, k); g[ng].n = 0; g[ng].maxabs = 0; g[ng].bad = 0; return &g[ng++];
}
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "moonsighting_adhan.csv";
    double tol = argc > 2 ? atof(argv[2]) : 90.0;
    FILE* f = fopen(path, "r"); if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    static const char* ev[] = {"fajr", "sunrise", "dhuhr", "asr", "sunset", "maghrib", "isha"};
    char line[256]; int rows = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char place[32]; double lat, lon; int y, m, d; double r[7];
        if (sscanf(line, "%31[^,],%lf,%lf,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf", place, &lat, &lon, &y, &m, &d,
                   &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6]) != 13) continue;
        PrayerTimes t = PrayerTimesCalculator::calculate({y, m, d}, {lat, lon}, 0.0, MOONSIGHTING, HighLatitudeRule::AUTO);
        if (!t.valid) { printf("INVALID %s %d-%02d-%02d\n", place, y, m, d); return 1; }
        double v[7] = {t.fajr, t.sunrise, t.dhuhr, t.asr, t.sunset, t.maghrib, t.isha};
        for (int i = 0; i < 7; i++) {
            if (i == 3) continue;                              // Asr: see header comment
            double e = fmod(v[i] * 3600.0 - r[i], 86400.0);
            if (e > 43200) e -= 86400;
            if (e < -43200) e += 86400;
            Agg* a = agg(place, ev[i]); a->n++; if (fabs(e) > a->maxabs) a->maxabs = fabs(e); if (fabs(e) > tol) a->bad++;
        }
        rows++;
    }
    fclose(f);
    int fails = 0; double worst = 0;
    printf("%-12s %-8s %4s %11s %6s\n", "place", "event", "n", "max|err| s", "> tol");
    for (int i = 0; i < ng; i++) {
        char p[32], e[16]; sscanf(g[i].key, "%31[^|]|%15s", p, e);
        if (g[i].bad || !strcmp(e, "fajr") || !strcmp(e, "isha")) printf("%-12s %-8s %4d %11.1f %6d\n", p, e, g[i].n, g[i].maxabs, g[i].bad);
        fails += g[i].bad; if (g[i].maxabs > worst) worst = g[i].maxabs;
    }
    printf("rows=%d tolerance=%.0fs worst=%.1fs rows_over_tol=%d => %s\n", rows, tol, worst, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
