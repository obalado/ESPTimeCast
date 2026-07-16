#include "display_mode.h"

const DisplayMode kDisplayModeOrder[] = {
  DisplayMode::Clock,
  DisplayMode::Date,
  DisplayMode::Weather,
  DisplayMode::WeatherDescription,
  DisplayMode::Countdown,
  DisplayMode::Bridge,
  DisplayMode::Message,
};

const size_t kDisplayModeOrderCount = sizeof(kDisplayModeOrder) / sizeof(kDisplayModeOrder[0]);

const char *displayModeName(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::Clock: return "CLOCK";
    case DisplayMode::Weather: return "WEATHER";
    case DisplayMode::WeatherDescription: return "WEATHER DESC";
    case DisplayMode::Countdown: return "COUNTDOWN";
    case DisplayMode::Bridge: return "BRIDGE";
    case DisplayMode::Date: return "DATE";
    case DisplayMode::Message: return "CUSTOM MESSAGE";
    case DisplayMode::Timer: return "TIMER";
  }
  return "UNKNOWN";
}

bool parseDisplayMode(const String &value, DisplayMode &mode) {
  String normalized = value;
  normalized.trim();
  normalized.toLowerCase();

  if (normalized == "0" || normalized == "clock") mode = DisplayMode::Clock;
  else if (normalized == "1" || normalized == "weather") mode = DisplayMode::Weather;
  else if (normalized == "2" || normalized == "weather_desc" || normalized == "description") mode = DisplayMode::WeatherDescription;
  else if (normalized == "3" || normalized == "countdown") mode = DisplayMode::Countdown;
  else if (normalized == "4" || normalized == "bridge" || normalized == "nightscout" ||
           normalized == "youtube" || normalized == "instagram" || normalized == "rss") mode = DisplayMode::Bridge;
  else if (normalized == "5" || normalized == "date") mode = DisplayMode::Date;
  else if (normalized == "6" || normalized == "message") mode = DisplayMode::Message;
  else if (normalized == "7" || normalized == "timer") mode = DisplayMode::Timer;
  else return false;

  return true;
}
