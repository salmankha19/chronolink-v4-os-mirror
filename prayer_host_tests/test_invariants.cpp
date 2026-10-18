/* Invariants over a whole year at UK/Northern latitudes with the default AUTO rule.
   Catches: Fajr==Isha, Isha wrapping past midnight, midnight before Isha, day-to-day jumps. */
#include "prayertimes/PrayerTimes.hpp"
#include <stdio.h>
#include <math.h>
#include <string.h>
using namespace prayertimes;

struct Counter { const char* name; int n; char first[48]; };
static Counter C[] = {
  {"invalid result",0,""},{"order: fajr<sunrise<dhuhr<asr<maghrib<isha",0,""},{"Isha-Maghrib gap in [0.5h,8h]",0,""},
  {"Isha->next Fajr gap >= 0.5h",0,""},{"midnight in (maghrib, next sunrise)",0,""},{"midnight >= isha",0,""},
  {"day-to-day jump > 20 min (fajr/isha)",0,""},{"day-to-day midnight jump > 20 min",0,""}};
enum { INVALID, ORDER, ISHAGAP, NIGHTGAP, MIDRANGE, MIDISHA, JUMP, MIDJUMP };
static void fail(int k, const char* ctx) { if (!C[k].n) snprintf(C[k].first,sizeof C[k].first,"%s",ctx); C[k].n++; }

static int days_in_year(int y){ return (y%4==0&&(y%100!=0||y%400==0))?366:365; }
static void ymd(int y,int doy,int*m,int*d){ static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31}; int dm[12]; memcpy(dm,md,sizeof dm); if(days_in_year(y)==366) dm[1]=29; int i=0; while(doy>dm[i]){doy-=dm[i];i++;} *m=i+1; *d=doy; }

int main() {
    const struct { const char* n; const Method* m; } methods[] = {{"MWL",&MWL},{"ISNA",&ISNA},{"EGYPTIAN",&EGYPTIAN},{"KARACHI",&KARACHI},{"UMM_AL_QURA",&UMM_AL_QURA},{"JAFARIA",&JAFARIA}};
    const double lats[] = {40.0, 48.0, 51.06, 52.7075, 55.86, 59.9};
    long total = 0;
    for (double lat : lats) for (auto& me : methods) {
        PrayerTimes prev{}; bool havePrev = false;
        for (int doy = 1; doy <= 365; doy++) {
            int m,d; ymd(2026,doy,&m,&d);
            char ctx[48]; snprintf(ctx,sizeof ctx,"lat %.2f %s 2026-%02d-%02d",lat,me.n,m,d);
            PrayerTimes t = PrayerTimesCalculator::calculate({2026,m,d},{lat,0.0},0.0,*me.m,HighLatitudeRule::AUTO);
            total++;
            if (!t.valid) { fail(INVALID,ctx); havePrev=false; continue; }
            if (!(t.fajr < t.sunrise && t.sunrise < t.dhuhr && t.dhuhr < t.asr && t.asr < t.maghrib && t.maghrib < t.isha)) fail(ORDER,ctx);
            double ig = t.isha - t.maghrib; if (ig < 0.5 - 1e-9 || ig > 8.0) fail(ISHAGAP,ctx);
            double ng = (t.fajr + 24.0) - t.isha; if (ng < 0.5 - 1e-9) fail(NIGHTGAP,ctx);
            if (!(t.midnight > t.maghrib && t.midnight < t.sunrise + 24.0)) fail(MIDRANGE,ctx);
            if (t.midnight < t.isha - 1e-9) fail(MIDISHA,ctx);
            if (havePrev && (fabs(t.fajr-prev.fajr) > 20.0/60 || fabs(t.isha-prev.isha) > 20.0/60)) fail(JUMP,ctx);
            if (havePrev && fabs(t.midnight-prev.midnight) > 20.0/60) fail(MIDJUMP,ctx);
            prev = t; havePrev = true;
        }
    }
    int bad = 0;
    printf("checked %ld city/method/day combinations (lat 40..59.9, 6 methods, AUTO rule)\n\n", total);
    for (auto& c : C) { printf("%-46s %6d%s%s\n", c.name, c.n, c.n?"   first: ":"", c.n?c.first:""); bad += c.n; }
    printf("\n=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
