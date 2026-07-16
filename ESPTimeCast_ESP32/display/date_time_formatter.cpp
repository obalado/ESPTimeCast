#include "date_time_formatter.h"

#include "../days_lookup.h"
#include "../months_lookup.h"

namespace {
bool languageUsesDayFirst(const String &language) {
  const char *dayFirstLanguages[] = {
    "af", "cs", "da", "de", "eo", "es", "et", "fi", "fr", "ga",
    "hr", "hu", "it", "lt", "lv", "nl", "no", "pl", "pt", "ro",
    "ru", "sk", "sl", "sr", "sv", "sw", "tr"
  };
  for (const char *candidate : dayFirstLanguages) {
    if (language.equalsIgnoreCase(candidate)) return true;
  }
  return false;
}

String spaceCharacters(const String &value) {
  String output;
  output.reserve(value.length() * 2);
  for (size_t index = 0; index < value.length(); ++index) {
    output += value[index];
    if (index + 1 < value.length()) output += ' ';
  }
  return output;
}
}

String formatLookupText(const char *rawText, bool useCustomFont) {
  String input(rawText);
  String output;

  bool isMultiByte = false;
  for (size_t index = 0; index < input.length(); ++index) {
    if (static_cast<uint8_t>(input[index]) > 127) {
      isMultiByte = true;
      break;
    }
  }

  if (isMultiByte) {
    output = input;
  } else {
    if (!useCustomFont) input.toUpperCase();
    const String separator = useCustomFont ? "\016" : " ";
    for (size_t index = 0; index < input.length(); ++index) {
      output += input[index];
      if (index + 1 < input.length()) output += separator;
    }
  }

  output += "  ";
  return output;
}

String formatClockText(const tm &timeInfo,
                       const char *language,
                       bool twelveHour,
                       bool showDayOfWeek,
                       bool animatedSeconds,
                       bool colonVisible,
                       bool useCustomFont) {
  char baseTime[9];
  if (twelveHour) {
    int hour = timeInfo.tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(baseTime, sizeof(baseTime), "%d:%02d", hour, timeInfo.tm_min);
  } else {
    snprintf(baseTime, sizeof(baseTime), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
  }

  char displayedTime[12];
  if (!showDayOfWeek && animatedSeconds) {
    snprintf(displayedTime, sizeof(displayedTime), "%s:%02d", baseTime, timeInfo.tm_sec);
  } else if (!showDayOfWeek) {
    snprintf(displayedTime, sizeof(displayedTime), "  %s  ", baseTime);
  } else {
    strlcpy(displayedTime, baseTime, sizeof(displayedTime));
  }

  String result;
  if (showDayOfWeek) {
    const char *const *days = getDaysOfWeek(language);
    result = formatLookupText(days[timeInfo.tm_wday], useCustomFont);
  }
  result += spaceCharacters(String(displayedTime));

  if (showDayOfWeek && animatedSeconds && !colonVisible) result.replace(":", " ");
  return result;
}

bool validDisplayDate(const tm &timeInfo) {
  return timeInfo.tm_year >= 120 && timeInfo.tm_mday > 0 &&
         timeInfo.tm_mon >= 0 && timeInfo.tm_mon <= 11;
}

String formatDateText(const tm &timeInfo, const char *language, bool useCustomFont) {
  const char *const *months = getMonthsOfYear(language);
  const String month = formatLookupText(months[timeInfo.tm_mon], useCustomFont);
  const String day = spaceCharacters(String(timeInfo.tm_mday));
  const String languageValue(language);

  if (languageValue == "ja") return month + day + " ±";
  if (languageUsesDayFirst(languageValue)) return day + "  " + month;
  return month + day;
}
