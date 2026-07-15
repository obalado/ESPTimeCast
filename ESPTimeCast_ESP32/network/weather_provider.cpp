#include "weather_provider.h"

#include <cerrno>
#include <cstdlib>

namespace {
bool parseCoordinate(const String &value, double &result) {
  String trimmed = value;
  trimmed.trim();
  if (trimmed.isEmpty()) return false;
  char *end = nullptr;
  errno = 0;
  result = std::strtod(trimmed.c_str(), &end);
  return errno == 0 && end != trimmed.c_str() && *end == '\0';
}
}

WeatherProvider parseWeatherProvider(const String &value) {
  if (value.equalsIgnoreCase("openmeteo")) return WeatherProvider::OpenMeteo;
  return WeatherProvider::OpenWeatherMap;
}

const char *weatherProviderName(WeatherProvider provider) {
  return provider == WeatherProvider::OpenMeteo ? "openmeteo" : "openweathermap";
}

bool validWeatherCoordinates(const String &latitude, const String &longitude) {
  double latitudeValue = 0.0;
  double longitudeValue = 0.0;
  return parseCoordinate(latitude, latitudeValue) &&
         parseCoordinate(longitude, longitudeValue) &&
         latitudeValue >= -90.0 && latitudeValue <= 90.0 &&
         longitudeValue >= -180.0 && longitudeValue <= 180.0;
}
