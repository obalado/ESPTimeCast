#include "sns_utils.h"

SnsType detectSnsType(const String &value) {
  if (value.indexOf("youtube.com") != -1 || value.indexOf("youtu.be") != -1) return SNS_YOUTUBE;
  if (value.indexOf("instagram.com") != -1) return SNS_INSTAGRAM;

  String pathOnly = value;
  const int queryPosition = pathOnly.indexOf('?');
  if (queryPosition != -1) pathOnly = pathOnly.substring(0, queryPosition);

  if (value.indexOf("feed") != -1 || value.indexOf("/rss") != -1 || value.indexOf("/atom") != -1 || pathOnly.endsWith(".rss") || pathOnly.endsWith(".atom") || pathOnly.endsWith(".xml")) return SNS_RSS;
  if (value.startsWith("https://")) return SNS_NIGHTSCOUT;
  return SNS_NTP;
}

String stripUrlParam(String url, const String &paramName) {
  const String needle = paramName + "=";
  int index = url.indexOf(needle);
  if (index == -1) return url;

  const int valueEnd = url.indexOf('&', index + needle.length());
  const char before = (index > 0) ? url.charAt(index - 1) : 0;
  if (before == '?' || before == '&') index--;

  if (valueEnd == -1) {
    url.remove(index);
  } else if (before == '?') {
    url.remove(index + 1, valueEnd - index);
  } else {
    url.remove(index, valueEnd - index);
  }
  return url;
}

int parseBridgeShowEvery(const String &url) {
  const int index = url.indexOf("show_every=");
  if (index == -1) return 1;

  String value = url.substring(index + 11);
  const int end = value.indexOf('&');
  if (end != -1) value = value.substring(0, end);
  value.trim();

  const int parsed = value.toInt();
  return parsed >= 1 ? parsed : 1;
}
