#include "wmo_weather.h"

WmoWeatherCondition getWmoWeatherCondition(int code, bool isDay) {
  if (code == 0) return { "clear", isDay ? '\x0C' : '\xA8' };
  if (code == 1) return { "mclr", '\x0D' };
  if (code == 2) return { "pcldy", '\x0D' };
  if (code == 3) return { "ovcst", '\x0D' };
  if (code == 45 || code == 48) return { "fog", '\xB9' };
  if (code >= 51 && code <= 57) return { "drzl", '\x10' };
  if (code >= 61 && code <= 67) return { "rain", '\x10' };
  if (code >= 71 && code <= 77) return { "snow", '\x12' };
  if (code >= 80 && code <= 82) return { "rshwr", '\x10' };
  if (code == 85 || code == 86) return { "sshwr", '\x12' };
  if (code >= 95 && code <= 99) return { "storm", '\x11' };
  return { "unkwn", '\x0D' };
}
