#include "open_meteo_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cctype>

#include "../services/wmo_weather.h"
#include "weather_provider.h"

namespace {
String urlEncode(const String &value) {
  String encoded;
  for (size_t index = 0; index < value.length(); ++index) {
    const unsigned char character = static_cast<unsigned char>(value.charAt(index));
    if (std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
      encoded += static_cast<char>(character);
    } else {
      const char hex[] = "0123456789ABCDEF";
      encoded += '%';
      encoded += hex[(character >> 4) & 0x0f];
      encoded += hex[character & 0x0f];
    }
  }
  return encoded;
}

bool parseIsoTime(const String &value, int &hour, int &minute) {
  const int separator = value.indexOf('T');
  if (separator < 0 || value.length() < static_cast<unsigned int>(separator + 6) ||
      value.charAt(separator + 3) != ':' ||
      !std::isdigit(static_cast<unsigned char>(value.charAt(separator + 1))) ||
      !std::isdigit(static_cast<unsigned char>(value.charAt(separator + 2))) ||
      !std::isdigit(static_cast<unsigned char>(value.charAt(separator + 4))) ||
      !std::isdigit(static_cast<unsigned char>(value.charAt(separator + 5)))) return false;
  hour = value.substring(separator + 1, separator + 3).toInt();
  minute = value.substring(separator + 4, separator + 6).toInt();
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}
}

String OpenMeteoService::buildUrl(const WeatherRequest &request) const {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += request.latitude;
  url += "&longitude=";
  url += request.longitude;
  url += "&daily=sunrise,sunset";
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,is_day";
  url += "&temperature_unit=";
  url += request.units == "imperial" ? "fahrenheit" : "celsius";
  url += "&timezone=";
  url += urlEncode(request.timezone.isEmpty() ? String("auto") : request.timezone);
  url += "&forecast_days=1";
  return url;
}

NetworkResult OpenMeteoService::fetch(const WeatherRequest &request, WeatherData &output) {
  Serial.println(F("[WEATHER] Fetching from Open-Meteo..."));
  if (WiFi.status() != WL_CONNECTED) return { NetworkError::Disconnected, 0 };
  if (!validWeatherCoordinates(request.latitude, request.longitude)) {
    Serial.println(F("[WEATHER] Open-Meteo requires valid latitude and longitude."));
    return { NetworkError::InvalidConfig, 0 };
  }

  const String url = buildUrl(request);
  Serial.println(F("[WEATHER] Connecting to Open-Meteo..."));
  Serial.print(F("[WEATHER] URL: "));
  Serial.println(url);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(10000);
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[WEATHER] Open-Meteo request failed: %d, %s\n",
                  httpCode, http.errorToString(httpCode).c_str());
    http.end();
    client.stop();
    return { NetworkError::HttpError, httpCode };
  }

  const String payload = http.getString();
  http.end();
  client.stop();
  Serial.print(F("[WEATHER] Open-Meteo payload: "));
  Serial.println(payload);

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.print(F("[WEATHER] Open-Meteo JSON parse error: "));
    Serial.println(error.f_str());
    return { NetworkError::ParseError, httpCode };
  }
  if (document["error"] | false) {
    Serial.print(F("[WEATHER] Open-Meteo error: "));
    Serial.println(document["reason"] | "unknown error");
    return { NetworkError::HttpError, httpCode };
  }

  JsonObject current = document["current"];
  if (current.isNull() || current["temperature_2m"].isNull()) {
    Serial.println(F("[WEATHER] Open-Meteo current temperature missing."));
    return { NetworkError::ParseError, httpCode };
  }

  const float temperature = current["temperature_2m"].as<float>();
  output.temperature = String(static_cast<int>(round(temperature))) + char(176);
  output.humidity = current["relative_humidity_2m"] | -1;
  if (!current["apparent_temperature"].isNull()) {
    const float apparent = current["apparent_temperature"].as<float>();
    output.apparentTemperature = String(static_cast<int>(round(apparent))) + char(176);
  } else {
    output.apparentTemperature = "";
  }

  output.weatherCode = current["weather_code"] | -1;
  output.isDay = (current["is_day"] | 1) != 0;
  const WmoWeatherCondition condition = getWmoWeatherCondition(output.weatherCode, output.isDay);
  output.mainDescription = condition.description;
  output.detailedDescription = condition.description;
  output.icon = String(condition.icon);

  JsonObject daily = document["daily"];
  if (!daily.isNull() && daily["sunrise"].is<JsonArray>() && daily["sunset"].is<JsonArray>() &&
      daily["sunrise"].size() > 0 && daily["sunset"].size() > 0) {
    const String sunrise = daily["sunrise"][0].as<String>();
    const String sunset = daily["sunset"][0].as<String>();
    int parsedSunriseHour = 0;
    int parsedSunriseMinute = 0;
    int parsedSunsetHour = 0;
    int parsedSunsetMinute = 0;
    if (parseIsoTime(sunrise, parsedSunriseHour, parsedSunriseMinute) &&
        parseIsoTime(sunset, parsedSunsetHour, parsedSunsetMinute)) {
      output.sunriseHour = parsedSunriseHour;
      output.sunriseMinute = parsedSunriseMinute;
      output.sunsetHour = parsedSunsetHour;
      output.sunsetMinute = parsedSunsetMinute;
      Serial.printf("[WEATHER] Open-Meteo Sunrise/Sunset: %02d:%02d | %02d:%02d\n",
                    output.sunriseHour, output.sunriseMinute,
                    output.sunsetHour, output.sunsetMinute);
    } else {
      Serial.println(F("[WEATHER] Open-Meteo sunrise/sunset format invalid."));
    }
  } else {
    Serial.println(F("[WEATHER] Open-Meteo sunrise/sunset missing."));
  }

  output.provider = "openmeteo";
  output.available = true;
  output.fetched = true;
  Serial.printf("[WEATHER] Open-Meteo: %s, humidity %d%%, code %d\n",
                output.temperature.c_str(), output.humidity, output.weatherCode);
  return { NetworkError::None, httpCode };
}
