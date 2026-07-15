#pragma once

#include "network_result.h"
#include "weather_models.h"

class OpenMeteoService {
 public:
  NetworkResult fetch(const WeatherRequest &request, WeatherData &output);

 private:
  String buildUrl(const WeatherRequest &request) const;
};
