#pragma once

#include <Arduino.h>

bool isNumber(const char *value);
bool isFiveDigitZip(const char *value);
char getWeatherIconChar(const String &iconCode);
