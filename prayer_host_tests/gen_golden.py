#!/usr/bin/env python3
"""Generate golden.csv: reference prayer-time events from PyEphem (independent of the C++ code).

  pip install ephem scipy
  python3 gen_golden.py > golden.csv

Reference conventions: no refraction model beyond the standard -0.833 deg for
sunrise/sunset (pressure 0), twilight angles measured to the sun's centre,
Asr by the shadow-factor definition using the true declination at that time.
Fajr/Isha rows are emitted only when the angle is reached with >= 1 deg to
spare (ill-conditioned near-threshold days are covered by the invariants test).
CSV: case,lat,lon,tz,year,month,day,method,shadow,event,ref_hours
"""
import math, datetime as dt, sys
import ephem
from scipy.optimize import brentq, minimize_scalar

CASES = [  # name, lat, lon, tz(h, fixed all year), method, shadow
    ("Shrewsbury-MWL",   52.7075,  -2.7527, 0.0,  "MWL",         1.0),
    ("Shrewsbury-KHI-H", 52.7075,  -2.7527, 0.0,  "KARACHI",     2.0),
    ("Horsham-MWL",      51.0600,  -0.3300, 0.0,  "MWL",         1.0),
    ("NewYork-ISNA",     40.7100, -74.0000,-5.0,  "ISNA",        1.0),
    ("Karachi-KHI-H",    24.8600,  67.0100, 5.0,  "KARACHI",     2.0),
    ("Makkah-UQU",       21.4200,  39.8300, 3.0,  "UMM_AL_QURA", 1.0),
    ("Cairo-EGY",        30.0400,  31.2400, 2.0,  "EGYPTIAN",    1.0),
    ("Tehran-JAF",       35.6900,  51.3900, 3.5,  "JAFARIA",     1.0),
    ("Sydney-MWL",      -33.8700, 151.2100,10.0,  "MWL",         1.0),
]
ANG = {"MWL":(18,17,0,0),"ISNA":(15,15,0,0),"EGYPTIAN":(19.5,17.5,0,0),
       "KARACHI":(18,18,0,0),"UMM_AL_QURA":(18.5,0,90,0),"JAFARIA":(16,14,0,4)}
STEP_H = 1/6.0  # 10 min scan

def make_obs(lat, lon):
    o = ephem.Observer(); o.lat = str(lat); o.lon = str(lon)
    o.pressure = 0; o.elevation = 0; o.horizon = '0'
    return o

def alt_dec(o, utc):
    o.date = utc; s = ephem.Sun(o)
    return math.degrees(s.alt), math.degrees(s.dec)

def run():
    print("# case,lat,lon,tz,year,month,day,method,shadow,event,ref_hours")
    days = [dt.date(2026,1,1) + dt.timedelta(days=i) for i in range(0, 365, 3)]
    for name, lat, lon, tz, method, shadow in CASES:
        fa, ia, iv, ma = ANG[method]
        o = make_obs(lat, lon)
        for d in days:
            base = dt.datetime(d.year, d.month, d.day) - dt.timedelta(hours=tz)
            hours = [i * STEP_H for i in range(int(24/STEP_H) + 1)]
            alts = [alt_dec(o, base + dt.timedelta(hours=h))[0] for h in hours]
            def A(h): return alt_dec(o, base + dt.timedelta(hours=h))[0]
            def cross(target_fn, lo, hi, up):
                f = lambda h: A(h) - target_fn(h)
                for i in range(len(hours)-1):
                    h0, h1 = hours[i], hours[i+1]
                    if h1 < lo or h0 > hi: continue
                    a, b = f(h0), f(h1)
                    if (up and a < 0 <= b) or ((not up) and a > 0 >= b):
                        return brentq(f, h0, h1, xtol=1e-7)
                return None
            imax = max(range(len(alts)), key=lambda i: alts[i])
            noon_h = hours[imax]
            tr = minimize_scalar(lambda h: -A(h), bounds=(noon_h-0.5, noon_h+0.5), method='bounded', options={'xatol':1e-7}).x
            amin = min(alts)
            def emit(ev, h):
                if h is not None:
                    print(f"{name},{lat},{lon},{tz},{d.year},{d.month},{d.day},{method},{shadow},{ev},{h:.6f}")
            emit("dhuhr", tr)
            sr = cross(lambda h: -0.833, 0, tr, True); ss = cross(lambda h: -0.833, tr, 24, False)
            emit("sunrise", sr); emit("sunset", ss)
            if sr is None or ss is None: continue
            if amin < -fa - 1.0:
                emit("fajr", cross(lambda h: -fa, 0, tr, True))
            if iv > 0:
                emit("isha", ss + iv/60.0)
            elif amin < -ia - 1.0:
                emit("isha", cross(lambda h: -ia, tr, 24, False))
            if ma > 0:
                emit("maghrib", cross(lambda h: -ma, tr, 24, False))
            def asr_target(h):
                dec = alt_dec(o, base + dt.timedelta(hours=h))[1]
                return math.degrees(math.atan2(1.0, shadow + math.tan(math.radians(abs(lat - dec)))))
            emit("asr", cross(asr_target, tr, 24, False))

if __name__ == "__main__":
    run()
