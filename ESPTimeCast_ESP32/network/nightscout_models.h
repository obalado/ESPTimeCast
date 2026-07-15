#pragma once

#include <Arduino.h>
#include <time.h>

struct NightscoutRequest {
  String url;
};

struct NightscoutData {
  int glucose = -1;
  String direction = "?";
  time_t readingTime = 0;
  bool useMmol = false;
};
