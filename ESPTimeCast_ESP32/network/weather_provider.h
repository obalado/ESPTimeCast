#pragma once

#include <Arduino.h>

enum class WeatherProvider {
  OpenMeteo,
  OpenWeatherMap
};

WeatherProvider parseWeatherProvider(const String &value);
const char *weatherProviderName(WeatherProvider provider);
bool validWeatherCoordinates(const String &latitude, const String &longitude);
