#pragma once

#include <Arduino.h>
#include <time.h>

String formatLookupText(const char *rawText, bool useCustomFont);
String formatClockText(const tm &timeInfo,
                       const char *language,
                       bool twelveHour,
                       bool showDayOfWeek,
                       bool animatedSeconds,
                       bool colonVisible,
                       bool useCustomFont);
String formatDateText(const tm &timeInfo, const char *language, bool useCustomFont);
bool validDisplayDate(const tm &timeInfo);
