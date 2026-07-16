#pragma once

#include <Arduino.h>

#include "network_request_gate.h"
#include "nightscout_service.h"
#include "sns_service.h"
#include "weather_service.h"

class NetworkDataCoordinator {
 public:
  void initialize(const WeatherData &weatherData);
  void configure(const WeatherRequest &weatherRequest, const String &snsSource);
  void update(bool connected, bool timeSynchronized, unsigned long connectedAt);
  void requestWeatherRefresh();

  bool busy() const;
  bool takeWeatherUpdate();
  bool takeNightscoutUpdate();
  bool takeSnsUpdate();

  const WeatherData &weather() const;
  const NightscoutData &nightscout() const;
  const SnsData &sns() const;

 private:
  void updateWeather(unsigned long now, unsigned long connectedAt);
  void collectWeatherResult();
  static void weatherTaskEntry(void *parameter);
  void updateNightscout(unsigned long now, SnsType type);
  void updateSns(unsigned long now, SnsType type);

  static constexpr unsigned long kWeatherInterval = 300000UL;
  static constexpr unsigned long kNightscoutInterval = 150000UL;
  static constexpr unsigned long kSnsInterval = 3600000UL;

  WeatherService weatherService_;
  NightscoutService nightscoutService_;
  SnsService snsService_;
  NetworkRequestGate gate_;

  WeatherRequest weatherRequest_;
  String snsSource_;
  WeatherData weatherData_;
  NightscoutData nightscoutData_;
  SnsData snsData_;

  bool weatherFetchInitiated_ = false;
  bool weatherRefreshRequested_ = false;
  bool weatherChanged_ = false;
  bool nightscoutChanged_ = false;
  bool snsChanged_ = false;
  bool weatherTaskRunning_ = false;
  void *pendingWeatherResult_ = nullptr;
  portMUX_TYPE weatherResultMux_ = portMUX_INITIALIZER_UNLOCKED;
  unsigned long lastWeatherFetch_ = 0;
  unsigned long lastNightscoutFetch_ = 0;
  unsigned long lastSnsFetch_ = 0;
  unsigned long nightscoutBackoffUntil_ = 0;
  int nightscoutFailCount_ = 0;
};
