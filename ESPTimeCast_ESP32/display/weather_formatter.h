#pragma once

#include <Arduino.h>

String formatWeatherText(const String &temperature,
                         int humidity,
                         bool showHumidity,
                         char temperatureUnitSymbol);
