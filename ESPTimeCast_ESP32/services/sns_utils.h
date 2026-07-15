#pragma once

#include <Arduino.h>

enum SnsType {
  SNS_NTP,
  SNS_NIGHTSCOUT,
  SNS_YOUTUBE,
  SNS_INSTAGRAM,
  SNS_RSS
};

SnsType detectSnsType(const String &value);
String stripUrlParam(String url, const String &paramName);
int parseBridgeShowEvery(const String &url);
