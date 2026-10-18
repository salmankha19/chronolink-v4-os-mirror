/* C-API test: units, unwrapped Isha/midnight, argument Validation. */
#include <stdlib.h>
#include "svc_prayer_engine.h"
#include <stdio.h>
#include <math.h>
static int bad = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); bad++; } } while (0)
int main() {
    svc_prayer_times_t t;
    // Shrewsbury, 21 June 2026, BST (+1), MWL, AUTO
    esp_err_t e = svc_prayer_engine_calculate(2026,6,21, 52.7075,-2.7527, 1.0, SVC_PRAYER_METHOD_MWL, SVC_PRAYER_MADHAB_STANDARD, SVC_PRAYER_HIGHLAT_AUTO, &t);
    CHECK(e == ESP_OK && t.valid, "Shrewsbury 21 Jun valid");
    printf("21 Jun Shrewsbury: fajr=%u sunrise=%u dhuhr=%u asr=%u maghrib=%u isha=%u midnight=%u\n",t.fajr_sec,t.sunrise_sec,t.dhuhr_sec,t.asr_sec,t.maghrib_sec,t.isha_sec,t.midnight_sec);
    CHECK(t.isha_sec > t.maghrib_sec, "isha_sec must be after maghrib_sec (no wrap to a small number)");
    CHECK(t.isha_sec < 172800, "isha_sec within 0..172799");
    CHECK(t.midnight_sec >= t.isha_sec, "midnight not before isha");
    CHECK(t.fajr_sec < t.sunrise_sec, "fajr before sunrise");
    CHECK(llabs((long long)t.fajr_sec + 86400 - (long long)t.isha_sec) > 1800, "Fajr and Isha are at least 30 min apart");
    // Hanafi Asr later than standard
    svc_prayer_times_t h; svc_prayer_engine_calculate(2026,6,21,52.7075,-2.7527,1.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_HANAFI,SVC_PRAYER_HIGHLAT_AUTO,&h);
    CHECK(h.asr_sec > t.asr_sec, "Hanafi Asr later than standard");
    // Validation
    CHECK(svc_prayer_engine_calculate(2026,6,21, 91.0,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t)==ESP_ERR_INVALID_ARG,"lat 91 rejected");
    CHECK(svc_prayer_engine_calculate(2026,6,21, NAN,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t)==ESP_ERR_INVALID_ARG,"NaN lat rejected");
    CHECK(svc_prayer_engine_calculate(2026,6,21, 52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,nullptr)==ESP_ERR_INVALID_ARG,"NULL out rejected");
    // Polar day: Tromso, midsummer -> invalid state, zeroed output
    e = svc_prayer_engine_calculate(2026,6,21, 69.65,18.96,2.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t);
    CHECK(e == ESP_ERR_INVALID_STATE && !t.valid && t.fajr_sec == 0, "polar day -> INVALID_STATE");
    // --- date validation (31 Feb, 29 Feb non-leap) ---
    CHECK(svc_prayer_engine_calculate(2026,2,31,52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t)==ESP_ERR_INVALID_ARG,"31 Feb rejected");
    CHECK(svc_prayer_engine_calculate(2026,2,29,52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t)==ESP_ERR_INVALID_ARG,"29 Feb 2026 rejected");
    CHECK(svc_prayer_engine_calculate(2028,2,29,52,0,0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t)==ESP_OK,"29 Feb 2028 accepted");
    // --- Asr-yellow sentinel: 62N midwinter, sun never reaches +5 deg ---
    e = svc_prayer_engine_calculate(2026,12,21,62.0,10.0,1.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t);
    CHECK(e==ESP_OK && t.valid && t.asr_yellow_sec==UINT32_MAX, "asr_yellow sentinel at 62N midwinter");
    // normal latitude: asr_yellow is a real time
    svc_prayer_engine_calculate(2026,10,17,52.7075,-2.7527,1.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t);
    CHECK(t.asr_yellow_sec!=UINT32_MAX && t.asr_yellow_sec>t.asr_sec && t.asr_yellow_sec<t.maghrib_sec, "asr_yellow between asr and maghrib");
    // --- Jafaria reachable ---
    e = svc_prayer_engine_calculate(2026,10,17,35.69,51.39,3.5,SVC_PRAYER_METHOD_JAFARIA,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t);
    CHECK(e==ESP_OK && t.valid, "Jafaria calculates");
    // --- Shrewsbury across the year: isha always after maghrib, in range ---
    int wrap=0; for (int doy=0; doy<365; doy++) { static const int md[]={31,28,31,30,31,30,31,31,30,31,30,31}; int m=0,d=doy; while(d>=md[m]){d-=md[m];m++;}
        svc_prayer_engine_calculate(2026,m+1,d+1,52.7075,-2.7527,0.0,SVC_PRAYER_METHOD_MWL,SVC_PRAYER_MADHAB_STANDARD,SVC_PRAYER_HIGHLAT_AUTO,&t);
        if (!t.valid || t.isha_sec <= t.maghrib_sec || t.isha_sec >= 172800) wrap++; }
    CHECK(wrap==0, "Shrewsbury 2026: Isha after Maghrib on all 365 days");
    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}