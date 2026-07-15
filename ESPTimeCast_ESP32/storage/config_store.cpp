#include "config_store.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

void saveCustomMessageToConfig(const char *message) {
  Serial.println(F("[CONFIG] Updating customMessage in config.json..."));

  JsonDocument document;
  File configFile = LittleFS.open("/config.json", "r");
  if (configFile) {
    const DeserializationError error = deserializeJson(document, configFile);
    configFile.close();
    if (error) {
      Serial.print(F("[CONFIG] Error reading existing config: "));
      Serial.println(error.f_str());
    }
  }

  document["customMessage"] = message;
  if (LittleFS.exists("/config.json")) LittleFS.rename("/config.json", "/config.bak");

  File output = LittleFS.open("/config.json", "w");
  if (!output) {
    Serial.println(F("[CONFIG] ERROR: Failed to open /config.json for writing"));
    return;
  }

  const size_t bytesWritten = serializeJson(document, output);
  output.close();
  Serial.printf("[CONFIG] Saved customMessage='%s' (%u bytes written)\n", message, bytesWritten);
}

void saveSunriseSunsetToConfig(int sunriseHour, int sunriseMinute,
                                  int sunsetHour, int sunsetMinute) {
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) return;

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, configFile);
  configFile.close();
  if (error) {
    Serial.println(F("[WEATHER] JSON parse error when saving updated sunrise/sunset"));
    return;
  }

  const bool changed = document["sunriseHour"].as<int>() != sunriseHour ||
                       document["sunriseMinute"].as<int>() != sunriseMinute ||
                       document["sunsetHour"].as<int>() != sunsetHour ||
                       document["sunsetMinute"].as<int>() != sunsetMinute;
  if (!changed) {
    Serial.println(F("[WEATHER] Sunrise/Sunset unchanged, skipping config save."));
    return;
  }

  document["sunriseHour"] = sunriseHour;
  document["sunriseMinute"] = sunriseMinute;
  document["sunsetHour"] = sunsetHour;
  document["sunsetMinute"] = sunsetMinute;

  File output = LittleFS.open("/config.json", "w");
  if (!output) {
    Serial.println(F("[WEATHER] Failed to write updated sunrise/sunset to config.json"));
    return;
  }
  serializeJsonPretty(document, output);
  output.close();
  Serial.println(F("[WEATHER] SAVED NEW sunrise/sunset to config.json (Values changed)"));
}
