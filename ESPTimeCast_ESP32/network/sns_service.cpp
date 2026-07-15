#include "sns_service.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <cctype>

#include "../services/sns_utils.h"

namespace {
String urlEncode(const String &value) {
  String encoded;
  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value.charAt(index);
    if (std::isalnum(static_cast<unsigned char>(character))) {
      encoded += character;
    } else {
      const char low = (character & 0x0f) < 10 ? (character & 0x0f) + '0' : (character & 0x0f) - 10 + 'A';
      const char highValue = (character >> 4) & 0x0f;
      const char high = highValue < 10 ? highValue + '0' : highValue - 10 + 'A';
      encoded += '%';
      encoded += high;
      encoded += low;
    }
  }
  return encoded;
}

NetworkResult getBridge(const String &url, String &payload) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.setUserAgent("ESPTimeCast-Firmware");
  http.setTimeout(4000);
  const int status = http.GET();
  if (status == 200) {
    payload = http.getString();
    payload.trim();
    http.end();
    return { NetworkError::None, status };
  }
  Serial.printf("[SNS] HTTP failed! Code: %d, Message: %s\n", status, http.errorToString(status).c_str());
  http.end();
  return { NetworkError::HttpError, status };
}
}

NetworkResult SnsService::fetch(const SnsRequest &request, SnsData &output) {
  switch (request.type) {
    case SNS_YOUTUBE: return fetchYouTube(request.source, output);
    case SNS_INSTAGRAM: return fetchInstagram(request.source, output);
    case SNS_RSS: return fetchRss(request.source, output);
    default: return { NetworkError::InvalidConfig, 0 };
  }
}

NetworkResult SnsService::fetchYouTube(const String &source, SnsData &output) {
  const String rawUrl = stripUrlParam(source, "show_every");
  String targetId;
  const int atIndex = rawUrl.indexOf('@');
  if (atIndex != -1) targetId = rawUrl.substring(atIndex);
  else if (rawUrl.indexOf("channel/") != -1) targetId = rawUrl.substring(rawUrl.indexOf("channel/") + 8);
  else targetId = rawUrl;

  const String bridgeUrl = "http://esptimecast.com/youtube-bridge.php?id=" + targetId;
  Serial.println("[YOUTUBE] Fetching via PHP bridge: " + bridgeUrl);
  String payload;
  const NetworkResult result = getBridge(bridgeUrl, payload);
  if (!result.ok()) return result;

  const int keyIndex = payload.indexOf("\"subscribers\":");
  if (keyIndex == -1) {
    Serial.println(F("[YOUTUBE] Failed to find 'subscribers' key in JSON payload"));
    return { NetworkError::ParseError, result.httpStatus };
  }
  String value = payload.substring(keyIndex + 14);
  value.replace("}", "");
  value.trim();
  const long subscribers = value.toInt();
  if (subscribers < 0) {
    Serial.println(F("[YOUTUBE] Bridge JSON reported an error count (-1)"));
    return { NetworkError::ParseError, result.httpStatus };
  }
  output.youtubeSubscribers = subscribers;
  Serial.printf("[YOUTUBE] Subscribers fetched from JSON: %ld\n", subscribers);
  return result;
}

NetworkResult SnsService::fetchInstagram(const String &source, SnsData &output) {
  const String rawUrl = stripUrlParam(source, "show_every");
  String username;
  const int instagramIndex = rawUrl.indexOf("instagram.com/");
  if (instagramIndex != -1) username = rawUrl.substring(instagramIndex + 14);
  else username = rawUrl;
  if (username.startsWith("@")) username = username.substring(1);
  const int slashIndex = username.indexOf('/');
  if (slashIndex != -1) username = username.substring(0, slashIndex);
  const int queryIndex = username.indexOf('?');
  if (queryIndex != -1) username = username.substring(0, queryIndex);
  username.trim();

  const String bridgeUrl = "http://esptimecast.com/instagram-bridge.php?username=" + username;
  Serial.println("[INSTAGRAM] Fetching via PHP bridge: " + bridgeUrl);
  String payload;
  const NetworkResult result = getBridge(bridgeUrl, payload);
  if (!result.ok()) return result;

  const int keyIndex = payload.indexOf("\"followers\":");
  if (keyIndex == -1) {
    Serial.println(F("[INSTAGRAM] Failed to find 'followers' key in JSON payload"));
    return { NetworkError::ParseError, result.httpStatus };
  }
  String value = payload.substring(keyIndex + 12);
  value.replace("}", "");
  value.trim();
  const long followers = value.toInt();
  if (followers < 0) {
    Serial.println(F("[INSTAGRAM] Bridge JSON reported an error/not-found count (-1)"));
    return { NetworkError::ParseError, result.httpStatus };
  }
  output.instagramFollowers = followers;
  Serial.printf("[INSTAGRAM] Followers fetched from JSON: %ld\n", followers);
  return result;
}

NetworkResult SnsService::fetchRss(const String &source, SnsData &output) {
  const String feedUrl = stripUrlParam(source, "show_every");
  const String bridgeUrl = "http://esptimecast.com/rss-bridge.php?url=" + urlEncode(feedUrl);
  Serial.println("[RSS] Fetching via PHP bridge: " + bridgeUrl);
  String payload;
  const NetworkResult result = getBridge(bridgeUrl, payload);
  if (!result.ok()) return result;

  const bool error = payload == "RSS ERROR" || payload == "INVALID RSS" ||
                     payload == "NO ENTRY" || payload == "FORBIDDEN" ||
                     payload == "NO URL" || payload == "INVALID URL";
  if (error || payload.isEmpty()) {
    Serial.println("[RSS] Bridge returned error: " + payload);
    return { NetworkError::ParseError, result.httpStatus };
  }
  output.rssTitle = payload;
  Serial.println("[RSS] Title fetched: " + payload);
  return result;
}
