#pragma once

#include <Arduino.h>
#include "weather_provider.h"

struct WeatherRequest {
  WeatherProvider provider = WeatherProvider::OpenWeatherMap;
  String apiKey;
  String city;
  String country;
  String latitude;
  String longitude;
  String units;
  String language;
  String timezone;
};

struct WeatherData {
  bool available = false;
  bool fetched = false;
  String temperature;
  String mainDescription;
  String detailedDescription;
  String icon;
  String apparentTemperature;
  int weatherCode = -1;
  bool isDay = true;
  String provider;
  int humidity = -1;
  int sunriseHour = 6;
  int sunriseMinute = 0;
  int sunsetHour = 18;
  int sunsetMinute = 0;
};
