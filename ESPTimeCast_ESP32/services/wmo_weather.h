#pragma once

struct WmoWeatherCondition {
  const char *description;
  char icon;
};

WmoWeatherCondition getWmoWeatherCondition(int code, bool isDay);
