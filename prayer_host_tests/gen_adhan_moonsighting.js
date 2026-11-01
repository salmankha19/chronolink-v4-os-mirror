// Regenerate the Moonsighting reference:
//   npm install adhan@4.4.3
//   TZ=UTC node gen_adhan_moonsighting.js > moonsighting_adhan.csv   (then re-add the 3 comment lines)
const a=require('adhan');
const places=[['Makkah',21.42,39.83],['NewYork',40.71,-74.0],['Horsham',51.06,-0.33],['Shrewsbury',52.7075,-2.7527],['Glasgow',55.86,-4.25],['Shetland',60.15,-1.15]];
for(const [n,lat,lon] of places){
  for(let doy=0;doy<365;doy+=5){
    const d=new Date(Date.UTC(2026,0,1+doy)); const date=new Date(2026,d.getUTCMonth(),d.getUTCDate()); // run with TZ=UTC
    const p=a.CalculationMethod.MoonsightingCommittee(); p.madhab=a.Madhab.Shafi;
    const t=new a.PrayerTimes(new a.Coordinates(lat,lon),date,p);
    const base=Date.UTC(2026,d.getUTCMonth(),d.getUTCDate())/1000;
    const f=x=>Math.round(x.getTime()/1000-base);
    console.log([n,lat,lon,2026,d.getUTCMonth()+1,d.getUTCDate(),f(t.fajr),f(t.sunrise),f(t.dhuhr),f(t.asr),f(t.sunset),f(t.maghrib),f(t.isha)].join(','));
  }}
