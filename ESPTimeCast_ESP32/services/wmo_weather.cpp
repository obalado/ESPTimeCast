#include "wmo_weather.h"

WmoWeatherCondition getWmoWeatherCondition(int code, bool isDay) {
  if (code == 0) return { "clear sky", isDay ? '\x0C' : '\xA8' };
  if (code == 1) return { "mainly clear", '\x0D' };
  if (code == 2) return { "partly cloudy", '\x0D' };
  if (code == 3) return { "overcast", '\x0D' };
  if (code == 45 || code == 48) return { "fog", '\xB9' };
  if (code >= 51 && code <= 57) return { "drizzle", '\x10' };
  if (code >= 61 && code <= 67) return { "rain", '\x10' };
  if (code >= 71 && code <= 77) return { "snow", '\x12' };
  if (code >= 80 && code <= 82) return { "rain showers", '\x10' };
  if (code == 85 || code == 86) return { "snow showers", '\x12' };
  if (code >= 95 && code <= 99) return { "thunderstorm", '\x11' };
  return { "unknown weather", '\x0D' };
}
