#include "openweathermap_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cstdlib>
#include <time.h>

#include "../services/weather_utils.h"

String OpenWeatherMapService::buildUrl(const WeatherRequest &request) const {
#if defined(CONFIG_IDF_TARGET_ESP32S2)
  String base = "http://api.openweathermap.org/data/2.5/weather?";
#else
  String base = "https://api.openweathermap.org/data/2.5/weather?";
#endif

  const float latitude = std::atof(request.city.c_str());
  const float longitude = std::atof(request.country.c_str());
  const bool coordinatesValid = isNumber(request.city.c_str()) &&
                                isNumber(request.country.c_str()) &&
                                latitude >= -90.0f && latitude <= 90.0f &&
                                longitude >= -180.0f && longitude <= 180.0f;

  String city = request.city;
  String country = request.country;
  city.replace(" ", "%20");
  country.replace(" ", "%20");

  if (coordinatesValid) {
    base += "lat=" + String(latitude, 8) + "&lon=" + String(longitude, 8);
  } else if (isFiveDigitZip(request.city.c_str()) && request.country.equalsIgnoreCase("US")) {
    base += "zip=" + request.city + "," + request.country;
  } else {
    base += "q=" + city + "," + country;
  }

  base += "&appid=" + request.apiKey;
  base += "&units=" + request.units;

  String apiLanguage = request.language;
  if (apiLanguage == "eo" || apiLanguage == "ga" || apiLanguage == "sw" || apiLanguage == "ja") {
    apiLanguage = "en";
  }
  base += "&lang=" + apiLanguage;
  return base;
}

NetworkResult OpenWeatherMapService::fetch(const WeatherRequest &request, WeatherData &output) {
  Serial.println(F("[WEATHER] Fetching weather data..."));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WEATHER] Skipped: WiFi not connected"));
    return { NetworkError::Disconnected, 0 };
  }
  if (request.apiKey.length() != 32) {
    Serial.println(F("[WEATHER] Skipped: Invalid API key (must be exactly 32 characters)"));
    return { NetworkError::InvalidConfig, 0 };
  }
  if (request.city.isEmpty() || request.country.isEmpty()) {
    Serial.println(F("[WEATHER] Skipped: City or Country is empty."));
    return { NetworkError::InvalidConfig, 0 };
  }

  Serial.println(F("[WEATHER] Connecting to OpenWeatherMap..."));
  const String url = buildUrl(request);
  Serial.print(F("[WEATHER] URL: "));
  Serial.println(url);

  HTTPClient http;
#if defined(CONFIG_IDF_TARGET_ESP32S2)
  WiFiClient client;
  client.stop();
  yield();
  http.begin(client, url);
#else
  WiFiClientSecure client;
  client.stop();
  yield();
  client.setInsecure();
  http.begin(client, url);
#endif
  http.setTimeout(10000);

  Serial.println(F("[WEATHER] Sending GET request..."));
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[WEATHER] HTTP GET failed, error code: %d, reason: %s\n",
                  httpCode, http.errorToString(httpCode).c_str());
    http.end();
    return { NetworkError::HttpError, httpCode };
  }

  Serial.println(F("[WEATHER] HTTP 200 OK. Reading payload..."));
  const String payload = http.getString();
  http.end();
  Serial.println(F("[WEATHER] Response received."));
  Serial.print(F("[WEATHER] Payload: "));
  Serial.println(payload);

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.print(F("[WEATHER] JSON parse error: "));
    Serial.println(error.f_str());
    return { NetworkError::ParseError, httpCode };
  }

  if (!document[F("main")][F("temp")].is<float>()) {
    Serial.println(F("[WEATHER] Temperature not found in JSON payload"));
    return { NetworkError::ParseError, httpCode };
  }

  const float temperature = document[F("main")][F("temp")];
  output.temperature = String(static_cast<int>(round(temperature))) + char(176);
  output.available = true;
  output.fetched = true;
  output.provider = "openweathermap";
  output.apparentTemperature = "";
  output.weatherCode = -1;
  output.mainDescription = "";
  output.detailedDescription = "";
  output.icon = "";
  Serial.printf("[WEATHER] Temp: %d°\n", static_cast<int>(round(temperature)));

  if (document[F("main")][F("humidity")].is<int>()) {
    output.humidity = document[F("main")][F("humidity")];
    Serial.printf("[WEATHER] Humidity: %d%%\n", output.humidity);
  } else {
    output.humidity = -1;
  }

  if (document[F("weather")].is<JsonArray>()) {
    JsonObject weather = document[F("weather")][0];
    if (!weather[F("main")].isNull()) output.mainDescription = weather[F("main")].as<String>();
    if (!weather[F("description")].isNull()) output.detailedDescription = weather[F("description")].as<String>();
    if (!weather[F("icon")].isNull()) output.icon = String(getWeatherIconChar(weather[F("icon")].as<String>()));
  } else {
    Serial.println(F("[WEATHER] Weather description not found in JSON payload"));
  }

  if (document[F("sys")].is<JsonObject>()) {
    JsonObject system = document[F("sys")];
    if (system[F("sunrise")].is<time_t>() && system[F("sunset")].is<time_t>()) {
      const time_t sunriseUtc = system[F("sunrise")].as<time_t>();
      const time_t sunsetUtc = system[F("sunset")].as<time_t>();
      long timezoneOffset = 0;
      struct tm localTime;
      const time_t now = time(nullptr);
      if (localtime_r(&now, &localTime)) timezoneOffset = mktime(&localTime) - now;

      const time_t sunriseLocal = sunriseUtc + timezoneOffset;
      const time_t sunsetLocal = sunsetUtc + timezoneOffset;
      struct tm sunrise;
      struct tm sunset;
      localtime_r(&sunriseLocal, &sunrise);
      localtime_r(&sunsetLocal, &sunset);
      output.sunriseHour = sunrise.tm_hour;
      output.sunriseMinute = sunrise.tm_min;
      output.sunsetHour = sunset.tm_hour;
      output.sunsetMinute = sunset.tm_min;
      Serial.printf("[WEATHER] Adjusted Sunrise/Sunset (local): %02d:%02d | %02d:%02d\n",
                    output.sunriseHour, output.sunriseMinute,
                    output.sunsetHour, output.sunsetMinute);
    } else {
      Serial.println(F("[WEATHER] Sunrise/Sunset not found in JSON."));
    }
  } else {
    Serial.println(F("[WEATHER] 'sys' object not found in JSON payload."));
  }

  return { NetworkError::None, httpCode };
}
