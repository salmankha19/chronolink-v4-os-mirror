/* Golden test: engine vs PyEphem reference (golden.csv). Usage: test_golden golden.csv [tol_seconds] [--no-fail] */
#include "prayertimes/PrayerTimes.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
using namespace prayertimes;

static const Method* find_method(const char* n) {
    if (!strcmp(n,"MWL")) return &MWL;
    if (!strcmp(n,"ISNA")) return &ISNA;
    if (!strcmp(n,"EGYPTIAN")) return &EGYPTIAN;
    if (!strcmp(n,"KARACHI")) return &KARACHI;
    if (!strcmp(n,"UMM_AL_QURA")) return &UMM_AL_QURA;
    if (!strcmp(n,"JAFARIA")) return &JAFARIA;
    return nullptr;
}
struct Agg { char key[64]; int n; double maxabs; double sumabs; int bad; };
static Agg g[256]; static int ng = 0;
static Agg* agg(const char* c, const char* e) {
    char k[64]; snprintf(k, sizeof k, "%s|%s", c, e);
    for (int i=0;i<ng;i++) if (!strcmp(g[i].key,k)) return &g[i];
    strcpy(g[ng].key,k); g[ng].n=0; g[ng].maxabs=0; g[ng].sumabs=0; g[ng].bad=0; return &g[ng++];
}
int main(int argc, char** argv) {
    const char* path = argc>1 ? argv[1] : "golden.csv";
    double tol = (argc>2 && argv[2][0] != '-') ? atof(argv[2]) : 30.0;
    bool nofail = false; for (int i=1;i<argc;i++) if (!strcmp(argv[i],"--no-fail")) nofail = true;
    FILE* f = fopen(path,"r"); if (!f) { fprintf(stderr,"cannot open %s\n",path); return 2; }
    char line[256]; int rows=0, skipped=0;
    while (fgets(line,sizeof line,f)) {
        if (line[0]=='#' || line[0]=='\n') continue;
        char cs[48], meth[24], ev[16]; double lat,lon,tz,shadow,ref; int y,m,d;
        if (sscanf(line,"%47[^,],%lf,%lf,%lf,%d,%d,%d,%23[^,],%lf,%15[^,],%lf",cs,&lat,&lon,&tz,&y,&m,&d,meth,&shadow,ev,&ref)!=11) continue;
        const Method* base = find_method(meth); if (!base) continue;
        Method mm = *base; mm.asr_shadow_factor = shadow;
        // Pure angles: any high-latitude limiting is tested separately.
        PrayerTimes t = PrayerTimesCalculator::calculate({y,m,d},{lat,lon},tz,mm,HighLatitudeRule::NONE);
        if (!t.valid) { skipped++; continue; }
        double v = NAN;
        if (!strcmp(ev,"fajr")) v=t.fajr; else if (!strcmp(ev,"sunrise")) v=t.sunrise; else if (!strcmp(ev,"dhuhr")) v=t.dhuhr;
        else if (!strcmp(ev,"asr")) v=t.asr; else if (!strcmp(ev,"sunset")) v=t.sunset; else if (!strcmp(ev,"maghrib")) v=t.maghrib;
        else if (!strcmp(ev,"isha")) v=t.isha;
        if (isnan(v)) continue;
        double e = (v - ref) * 3600.0;
        e = fmod(e, 86400.0); if (e > 43200) e -= 86400; if (e < -43200) e += 86400;   // modulo-24h compare
        Agg* a = agg(cs, ev); a->n++; a->sumabs += fabs(e); if (fabs(e) > a->maxabs) a->maxabs = fabs(e); if (fabs(e) > tol) a->bad++;
        rows++;
    }
    fclose(f);
    printf("%-22s %-8s %5s %10s %10s %6s\n","case","event","n","max|err|s","mean|err|s","> tol");
    int fails = 0; double worst = 0;
    for (int i=0;i<ng;i++) {
        char c[48], e[16]; sscanf(g[i].key,"%47[^|]|%15s",c,e);
        printf("%-22s %-8s %5d %10.1f %10.1f %6d\n",c,e,g[i].n,g[i].maxabs,g[i].sumabs/g[i].n,g[i].bad);
        fails += g[i].bad; if (g[i].maxabs > worst) worst = g[i].maxabs;
    }
    printf("\nrows=%d skipped(invalid)=%d tolerance=%.0fs worst=%.1fs rows_over_tol=%d => %s\n", rows, skipped, tol, worst, fails, fails? "FAIL":"PASS");
    return (fails && !nofail) ? 1 : 0;
}
