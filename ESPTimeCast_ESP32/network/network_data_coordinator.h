#pragma once

#include <Arduino.h>

#include "network_request_gate.h"
#include "weather_service.h"

class NetworkDataCoordinator {
 public:
  void initialize(const WeatherData &weatherData);
  void configure(const WeatherRequest &weatherRequest);
  void update(bool connected, unsigned long connectedAt);
  void requestWeatherRefresh();

  bool busy() const;
  bool takeWeatherUpdate();
  const WeatherData &weather() const;

 private:
  void updateWeather(unsigned long now, unsigned long connectedAt);
  void collectWeatherResult();
  static void weatherTaskEntry(void *parameter);

  static constexpr unsigned long kWeatherInterval = 300000UL;

  WeatherService weatherService_;
  NetworkRequestGate gate_;

  WeatherRequest weatherRequest_;
  WeatherData weatherData_;

  bool weatherFetchInitiated_ = false;
  bool weatherRefreshRequested_ = false;
  bool weatherChanged_ = false;
  bool weatherTaskRunning_ = false;
  void *pendingWeatherResult_ = nullptr;
  portMUX_TYPE weatherResultMux_ = portMUX_INITIALIZER_UNLOCKED;
  unsigned long lastWeatherFetch_ = 0;
};
