#include "weather_service.h"

NetworkResult WeatherService::fetch(const WeatherRequest &request, WeatherData &output) {
  if (request.provider == WeatherProvider::OpenMeteo) {
    return openMeteo_.fetch(request, output);
  }
  return openWeatherMap_.fetch(request, output);
}
