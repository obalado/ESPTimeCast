#include "network_data_coordinator.h"

#include <WiFi.h>

#include "../services/sns_utils.h"

void NetworkDataCoordinator::initialize(const WeatherData &weatherData) {
  weatherData_ = weatherData;
}

void NetworkDataCoordinator::configure(const WeatherRequest &weatherRequest, const String &snsSource) {
  if (weatherRequest_.apiKey != weatherRequest.apiKey ||
      weatherRequest_.city != weatherRequest.city ||
      weatherRequest_.country != weatherRequest.country ||
      weatherRequest_.units != weatherRequest.units ||
      weatherRequest_.language != weatherRequest.language) {
    weatherRequest_ = weatherRequest;
  }
  if (snsSource_ != snsSource) snsSource_ = snsSource;
}

void NetworkDataCoordinator::update(bool connected, bool timeSynchronized, unsigned long connectedAt) {
  if (!connected) {
    weatherFetchInitiated_ = false;
    weatherRefreshRequested_ = false;
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
  if (weatherFetchInitiated_ && !weatherRefreshRequested_ && now - lastWeatherFetch_ <= kWeatherInterval) return;

  if (weatherRefreshRequested_) Serial.println(F("[LOOP] Immediate weather fetch requested by web server."));
  else if (!weatherFetchInitiated_) Serial.println(F("[LOOP] Initial weather fetch."));
  else Serial.println(F("[LOOP] Regular interval weather fetch."));

  weatherRefreshRequested_ = false;
  weatherFetchInitiated_ = true;
  weatherData_.fetched = false;

  if (now - connectedAt < 5000) {
    Serial.println(F("[WEATHER] Skipped: Network just reconnected. Letting it stabilize..."));
    lastWeatherFetch_ = now;
    weatherChanged_ = true;
    return;
  }
  if (!gate_.tryAcquire()) return;

  WeatherData candidate = weatherData_;
  const NetworkResult result = weatherService_.fetch(weatherRequest_, candidate);
  gate_.release();
  lastWeatherFetch_ = millis();

  if (result.ok()) {
    weatherData_ = candidate;
  } else {
    weatherData_.available = false;
    weatherData_.fetched = false;
  }
  weatherChanged_ = true;
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
