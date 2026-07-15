#include "weather_utils.h"

#include <cctype>
#include <cstring>

bool isNumber(const char *value) {
  for (size_t index = 0; value[index] != '\0'; ++index) {
    const unsigned char character = static_cast<unsigned char>(value[index]);
    if (!std::isdigit(character) && character != '.' && character != '-') return false;
  }
  return true;
}

bool isFiveDigitZip(const char *value) {
  if (std::strlen(value) != 5) return false;
  for (size_t index = 0; index < 5; ++index) {
    if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
  }
  return true;
}

char getWeatherIconChar(const String &iconCode) {
  if (iconCode.startsWith("01")) return iconCode.endsWith("n") ? '\xA8' : '\x0C';
  if (iconCode.startsWith("02")) return '\x0D';
  if (iconCode.startsWith("03")) return '\x0D';
  if (iconCode.startsWith("04")) return '\x0D';
  if (iconCode.startsWith("09")) return '\x10';
  if (iconCode.startsWith("10")) return '\x10';
  if (iconCode.startsWith("11")) return '\x11';
  if (iconCode.startsWith("13")) return '\x12';
  if (iconCode.startsWith("50")) return '\xB9';
  return '\x0D';
}
