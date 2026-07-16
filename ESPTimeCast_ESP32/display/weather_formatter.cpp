#include "weather_formatter.h"

String formatWeatherText(const String &temperature,
                         int humidity,
                         bool showHumidity,
                         char temperatureUnitSymbol) {
  if (showHumidity && humidity >= 0) {
    return temperature + " " + String(min(humidity, 99)) + "%";
  }
  return temperature + temperatureUnitSymbol;
}
