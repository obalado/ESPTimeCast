#include "network_data_coordinator.h"

#include <WiFi.h>
#include <new>

namespace {
struct WeatherTaskContext {
  NetworkDataCoordinator *owner;
  WeatherRequest request;
  WeatherData candidate;
  NetworkResult result;
};
}

void NetworkDataCoordinator::initialize(const WeatherData &weatherData) {
  weatherData_ = weatherData;
}

void NetworkDataCoordinator::configure(const WeatherRequest &weatherRequest) {
  if (weatherRequest_.provider != weatherRequest.provider ||
      weatherRequest_.apiKey != weatherRequest.apiKey ||
      weatherRequest_.city != weatherRequest.city ||
      weatherRequest_.country != weatherRequest.country ||
      weatherRequest_.latitude != weatherRequest.latitude ||
      weatherRequest_.longitude != weatherRequest.longitude ||
      weatherRequest_.units != weatherRequest.units ||
      weatherRequest_.language != weatherRequest.language ||
      weatherRequest_.timezone != weatherRequest.timezone) {
    weatherRequest_ = weatherRequest;
  }
}

void NetworkDataCoordinator::update(bool connected, unsigned long connectedAt) {
  collectWeatherResult();
  if (!connected) {
    weatherFetchInitiated_ = false;
    weatherRefreshRequested_ = false;
    if (weatherData_.fetched) {
      weatherData_.fetched = false;
      weatherChanged_ = true;
    }
    return;
  }

  const unsigned long now = millis();
  updateWeather(now, connectedAt);
}

void NetworkDataCoordinator::requestWeatherRefresh() {
  weatherRefreshRequested_ = true;
}

bool NetworkDataCoordinator::busy() const {
  return gate_.busy();
}

bool NetworkDataCoordinator::takeWeatherUpdate() {
  const bool changed = weatherChanged_;
  weatherChanged_ = false;
  return changed;
}

const WeatherData &NetworkDataCoordinator::weather() const {
  return weatherData_;
}

void NetworkDataCoordinator::updateWeather(unsigned long now, unsigned long connectedAt) {
  // Do not count connection-stabilization time as a fetch attempt. Otherwise the
  // initial request is postponed for the full refresh interval.
  if (now - connectedAt < 5000 || weatherTaskRunning_) return;

  if (weatherFetchInitiated_ && !weatherRefreshRequested_ &&
      now - lastWeatherFetch_ < kWeatherInterval) return;
  if (!gate_.tryAcquire()) return;

  WeatherTaskContext *context = new (std::nothrow) WeatherTaskContext{
    this, weatherRequest_, weatherData_, {}
  };
  if (context == nullptr) {
    Serial.println(F("[WEATHER] Failed to allocate async request context."));
    weatherRefreshRequested_ = false;
    weatherFetchInitiated_ = true;
    lastWeatherFetch_ = millis() - kWeatherInterval + 30000UL;
    gate_.release();
    return;
  }

  if (weatherRefreshRequested_) Serial.println(F("[LOOP] Immediate weather fetch requested by web server."));
  else if (!weatherFetchInitiated_) Serial.println(F("[LOOP] Initial weather fetch."));
  else Serial.println(F("[LOOP] Regular interval weather fetch."));

  weatherRefreshRequested_ = false;
  weatherFetchInitiated_ = true;
  weatherTaskRunning_ = true;
  weatherData_.fetched = false;
  weatherChanged_ = true;

  const BaseType_t created = xTaskCreate(
    weatherTaskEntry, "weather-fetch", 12288, context, 1, nullptr);
  if (created != pdPASS) {
    Serial.println(F("[WEATHER] Failed to create async request task."));
    delete context;
    weatherTaskRunning_ = false;
    lastWeatherFetch_ = millis() - kWeatherInterval + 30000UL;
    gate_.release();
  }
}

void NetworkDataCoordinator::weatherTaskEntry(void *parameter) {
  WeatherTaskContext *context = static_cast<WeatherTaskContext *>(parameter);
  context->result = context->owner->weatherService_.fetch(context->request, context->candidate);

  portENTER_CRITICAL(&context->owner->weatherResultMux_);
  context->owner->pendingWeatherResult_ = context;
  portEXIT_CRITICAL(&context->owner->weatherResultMux_);

  vTaskDelete(nullptr);
}

void NetworkDataCoordinator::collectWeatherResult() {
  WeatherTaskContext *context = nullptr;
  portENTER_CRITICAL(&weatherResultMux_);
  context = static_cast<WeatherTaskContext *>(pendingWeatherResult_);
  pendingWeatherResult_ = nullptr;
  portEXIT_CRITICAL(&weatherResultMux_);
  if (context == nullptr) return;

  gate_.release();
  weatherTaskRunning_ = false;
  lastWeatherFetch_ = millis();

  if (context->result.ok()) {
    weatherData_ = context->candidate;
  } else {
    // Keep last successful snapshot available. Display can continue showing stale
    // data until next refresh instead of disappearing after one transient error.
    weatherData_.fetched = false;
  }
  weatherChanged_ = true;
  delete context;
}
