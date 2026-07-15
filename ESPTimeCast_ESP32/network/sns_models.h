#pragma once

#include <Arduino.h>

#include "../services/sns_utils.h"

struct SnsRequest {
  SnsType type = SNS_NTP;
  String source;
};

struct SnsData {
  long youtubeSubscribers = -1;
  long instagramFollowers = -1;
  String rssTitle;
};
