#pragma once

#include <Arduino.h>

struct WeatherRequest {
  String apiKey;
  String city;
  String country;
  String units;
  String language;
};

struct WeatherData {
  bool available = false;
  bool fetched = false;
  String temperature;
  String mainDescription;
  String detailedDescription;
  String icon;
  int humidity = -1;
  int sunriseHour = 6;
  int sunriseMinute = 0;
  int sunsetHour = 18;
  int sunsetMinute = 0;
};
