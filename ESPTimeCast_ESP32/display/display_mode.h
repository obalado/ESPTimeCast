#pragma once

#include <Arduino.h>

// Stable numeric IDs. Keep values compatible with HTTP API and saved integrations.
enum class DisplayMode : uint8_t {
  Clock = 0,
  Weather = 1,
  WeatherDescription = 2,
  Countdown = 3,
  Reserved = 4,
  Date = 5,
  Message = 6,
  Timer = 7,
};

static_assert(static_cast<uint8_t>(DisplayMode::Clock) == 0, "Clock API ID changed");
static_assert(static_cast<uint8_t>(DisplayMode::Weather) == 1, "Weather API ID changed");
static_assert(static_cast<uint8_t>(DisplayMode::WeatherDescription) == 2, "Description API ID changed");
static_assert(static_cast<uint8_t>(DisplayMode::Countdown) == 3, "Countdown API ID changed");
static_assert(static_cast<uint8_t>(DisplayMode::Reserved) == 4, "Reserved API ID changed");
static_assert(static_cast<uint8_t>(DisplayMode::Date) == 5, "Date API ID changed");
static_assert(static_cast<uint8_t>(DisplayMode::Message) == 6, "Message API ID changed");
static_assert(static_cast<uint8_t>(DisplayMode::Timer) == 7, "Timer API ID changed");

constexpr uint8_t displayModeId(DisplayMode mode) {
  return static_cast<uint8_t>(mode);
}

constexpr DisplayMode displayModeFromId(uint8_t id) {
  return static_cast<DisplayMode>(id);
}

extern const DisplayMode kDisplayModeOrder[];
extern const size_t kDisplayModeOrderCount;

const char *displayModeName(DisplayMode mode);
bool parseDisplayMode(const String &value, DisplayMode &mode);
