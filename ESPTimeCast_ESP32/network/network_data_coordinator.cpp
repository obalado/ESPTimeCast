#include "network_data_coordinator.h"

#include <WiFi.h>
#include <new>

#include "../services/sns_utils.h"

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

void NetworkDataCoordinator::configure(const WeatherRequest &weatherRequest, const String &snsSource) {
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
  if (snsSource_ != snsSource) snsSource_ = snsSource;
}

void NetworkDataCoordinator::update(bool connected, bool timeSynchronized, unsigned long connectedAt) {
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

  const SnsType type = detectSnsType(snsSource_);
  if (type == SNS_NIGHTSCOUT && timeSynchronized) updateNightscout(now, type);
  if (type == SNS_YOUTUBE || type == SNS_INSTAGRAM || type == SNS_RSS) updateSns(now, type);
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

bool NetworkDataCoordinator::takeNightscoutUpdate() {
  const bool changed = nightscoutChanged_;
  nightscoutChanged_ = false;
  return changed;
}

bool NetworkDataCoordinator::takeSnsUpdate() {
  const bool changed = snsChanged_;
  snsChanged_ = false;
  return changed;
}

const WeatherData &NetworkDataCoordinator::weather() const {
  return weatherData_;
}

const NightscoutData &NetworkDataCoordinator::nightscout() const {
  return nightscoutData_;
}

const SnsData &NetworkDataCoordinator::sns() const {
  return snsData_;
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

void NetworkDataCoordinator::updateNightscout(unsigned long now, SnsType type) {
  (void)type;
  if (now < nightscoutBackoffUntil_) return;
  if (nightscoutData_.glucose != -1 && now - lastNightscoutFetch_ < kNightscoutInterval) return;
  if (!gate_.tryAcquire()) return;

  NightscoutData candidate = nightscoutData_;
  const NetworkResult result = nightscoutService_.fetch({ snsSource_ }, candidate);
  gate_.release();
  lastNightscoutFetch_ = millis();
  nightscoutData_.useMmol = candidate.useMmol;

  if (result.ok()) {
    nightscoutData_ = candidate;
    nightscoutFailCount_ = 0;
    nightscoutChanged_ = true;
    return;
  }

  if (result.error == NetworkError::NotFound) {
    nightscoutBackoffUntil_ = millis() + 86400000UL;
    nightscoutFailCount_ = 0;
  } else if (result.error == NetworkError::RateLimited) {
    nightscoutBackoffUntil_ = millis() + 1800000UL;
    nightscoutFailCount_ = 0;
  } else if (result.error == NetworkError::HttpError) {
    ++nightscoutFailCount_;
    Serial.printf("[NIGHTSCOUT] HTTP failure %d\n", nightscoutFailCount_);
    if (nightscoutFailCount_ >= 3) {
      const unsigned long backoff = nightscoutFailCount_ >= 6 ? 1800000UL : 300000UL;
      Serial.printf("[NIGHTSCOUT] %d consecutive failures, backing off %lu min.\n",
                    nightscoutFailCount_, backoff / 60000UL);
      nightscoutBackoffUntil_ = millis() + backoff;
    }
  }
  nightscoutChanged_ = true;
}

void NetworkDataCoordinator::updateSns(unsigned long now, SnsType type) {
  if (lastSnsFetch_ != 0 && now - lastSnsFetch_ < kSnsInterval) return;
  if (!gate_.tryAcquire()) return;

  SnsData candidate = snsData_;
  const NetworkResult result = snsService_.fetch({ type, snsSource_ }, candidate);
  gate_.release();
  lastSnsFetch_ = millis();
  if (result.ok()) {
    snsData_ = candidate;
    snsChanged_ = true;
  }
}
