#include "uptime_store.h"

#include <LittleFS.h>

namespace {
constexpr char kUptimePath[] = "/uptime.dat";
unsigned long accumulatedSeconds = 0;
unsigned long sessionStartMillis = 0;
}

void loadUptime() {
  accumulatedSeconds = 0;
  sessionStartMillis = millis();

  if (!LittleFS.exists(kUptimePath)) {
    Serial.println(F("[UPTIME] No previous uptime file found. Starting from 0."));
    return;
  }

  File file = LittleFS.open(kUptimePath, "r");
  if (!file) {
    Serial.println(F("[UPTIME] Failed to open /uptime.dat for reading."));
    return;
  }

  accumulatedSeconds = file.parseInt();
  file.close();
  Serial.printf("[UPTIME] Loaded accumulated uptime: %lu seconds (%.2f hours)\n",
                accumulatedSeconds, accumulatedSeconds / 3600.0);
}

void resetUptimeSession() {
  sessionStartMillis = millis();
}

void saveUptime() {
  accumulatedSeconds = getTotalRuntimeSeconds();
  sessionStartMillis = millis();

  File file = LittleFS.open(kUptimePath, "w");
  if (!file) {
    Serial.println(F("[UPTIME] Failed to write /uptime.dat"));
    return;
  }

  file.print(accumulatedSeconds);
  file.close();
  Serial.printf("[UPTIME] Saved accumulated uptime: %s\n", formatTotalRuntime().c_str());
}

unsigned long getTotalRuntimeSeconds() {
  return accumulatedSeconds + (millis() - sessionStartMillis) / 1000;
}

String formatTotalRuntime() {
  const unsigned long seconds = getTotalRuntimeSeconds();
  const unsigned int hours = seconds / 3600;
  const unsigned int minutes = (seconds % 3600) / 60;
  const unsigned int remainder = seconds % 60;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", hours, minutes, remainder);
  return String(buffer);
}

String formatUptime(unsigned long seconds) {
  const unsigned long days = seconds / 86400;
  const unsigned long hours = (seconds % 86400) / 3600;
  const unsigned long minutes = (seconds % 3600) / 60;
  const unsigned long remainder = seconds % 60;

  char buffer[64];
  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu", days, hours, minutes, remainder);
  } else {
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, remainder);
  }
  return String(buffer);
}
