#include "nightscout_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../services/sns_utils.h"

NetworkResult NightscoutService::fetch(const NightscoutRequest &request, NightscoutData &output) {
  if (WiFi.status() != WL_CONNECTED) return { NetworkError::Disconnected, 0 };
  if (ESP.getFreeHeap() < 10000) return { NetworkError::LowMemory, 0 };

  String url = request.url;
  output.useMmol = url.indexOf("mmol=1") != -1;
  if (output.useMmol) url = stripUrlParam(url, "mmol");
  url = stripUrlParam(url, "show_every");
  if (url.indexOf("count=") == -1) url += (url.indexOf('?') == -1) ? "?count=1" : "&count=1";

  Serial.printf("[NIGHTSCOUT] Fetching%s direct\n", output.useMmol ? " (mmol)" : " (mg/dL)");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.begin(client, url);
  https.setTimeout(8000);
  const int httpCode = https.GET();

  if (httpCode == HTTP_CODE_OK) {
    WiFiClient *stream = https.getStreamPtr();
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, *stream);
    if (!error && document.is<JsonArray>() && document.size() > 0) {
      JsonObject reading = document[0].as<JsonObject>();
      if (!reading["sgv"].isNull()) output.glucose = reading["sgv"].as<int>();
      else if (!reading["glucose"].isNull()) output.glucose = reading["glucose"].as<int>();
      else output.glucose = -1;
      output.direction = reading["direction"] | "?";
      const long long dateMilliseconds = reading["date"] | 0LL;
      if (dateMilliseconds > 0) output.readingTime = dateMilliseconds / 1000;
      Serial.printf("[NIGHTSCOUT] Fetched: %d (%s) %s\n",
                    output.glucose,
                    output.useMmol ? "will display as mmol" : "mg/dL",
                    output.direction.c_str());
      https.end();
      client.stop();
      delay(100);
      return { NetworkError::None, httpCode };
    }

    Serial.println(F("[NIGHTSCOUT] Failed to parse JSON"));
    https.end();
    client.stop();
    delay(100);
    return { NetworkError::ParseError, httpCode };
  }

  if (httpCode == 404) {
    Serial.println(F("[NIGHTSCOUT] Bridge: invalid URL (404). Pausing fetch for 24h."));
    https.end();
    client.stop();
    delay(100);
    return { NetworkError::NotFound, httpCode };
  }
  if (httpCode == 429) {
    Serial.println(F("[NIGHTSCOUT] Bridge: rate limited (429). Backing off 30 min."));
    https.end();
    client.stop();
    delay(100);
    return { NetworkError::RateLimited, httpCode };
  }

  Serial.printf("[NIGHTSCOUT] HTTPS failed: %s\n", https.errorToString(httpCode).c_str());
  https.end();
  client.stop();
  delay(100);
  return { NetworkError::HttpError, httpCode };
}
