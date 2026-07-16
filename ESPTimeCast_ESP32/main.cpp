/*
ESPTimeCast™

Copyright (c) 2026 M-Factory

This software is source-available for personal, non-commercial use only.
It is not open source.

See LICENSE.txt for full terms.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <esp_sntp.h>
#include <time.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "version.h"
#include "mfactoryfont.h"
#include "display/display_mode.h"
#include "display/display_controller.h"
#include "display/date_time_formatter.h"
#include "display/weather_formatter.h"
#include "storage/pin_store.h"
#include "tz_lookup.h"      // Timezone lookup, do not duplicate mapping here!
#include "index_html.h"     // Web UI
#include "network/network_data_coordinator.h"
#include "storage/uptime_store.h"
#include "storage/config_store.h"


// ============================
// LEGACY fallback pins (used ONLY for migration)
// ============================
#if defined(CONFIG_IDF_TARGET_ESP32S2)
#define L_CLK 7
#define L_CS 11
#define L_DATA 12

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define L_CLK 18
#define L_CS 16
#define L_DATA 17

#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define L_CLK 4
#define L_CS 10
#define L_DATA 6

#elif defined(ESP32)
#define L_CLK 18
#define L_CS 23
#define L_DATA 5

#else
#error "Unsupported board!"
#endif

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4


int CLK_PIN;
int CS_PIN;
int DATA_PIN;
MD_Parola P = MD_Parola(HARDWARE_TYPE, L_DATA, L_CLK, L_CS, MAX_DEVICES);
AsyncWebServer server(80);
NetworkDataCoordinator networkData;

// --- Global Scroll Speed Settings ---
const int GENERAL_SCROLL_SPEED = 85;  // Default: Adjust this for Weather Description and Countdown Label (e.g., 50 for faster, 200 for slower)
int IP_SCROLL_SPEED = 115;            // Default: Adjust this for the IP Address display (slower for readability)
int messageScrollSpeed = 85;          // default fallback

// Central controller owns mode IDs, previous mode, rotation position, and timing.
DisplayController displayController;


// --- Device identity ---
const char *DEFAULT_HOSTNAME = "esptimecast";
const char *DEFAULT_AP_PASSWORD = "12345678";
const char *DEFAULT_AP_SSID = "ESPTimeCast";
String deviceHostname = DEFAULT_HOSTNAME;

// WiFi and configuration globals
char ssid[32] = "";
char password[64] = "";
char openWeatherApiKey[64] = "";
char openWeatherCity[64] = "";
char openWeatherCountry[64] = "";
char weatherProvider[20] = "openmeteo";
char weatherLatitude[24] = "";
char weatherLongitude[24] = "";
char weatherUnits[12] = "metric";
char timeZone[64] = "";
char language[8] = "en";
unsigned long lastWifiConnectTime = 0;
String mainDesc = "";
String detailedDesc = "";
bool credentialsExist() {
  return (strlen(ssid) > 0);
}
bool isRebooting = false;

// Timing and display settings
unsigned long clockDuration = 10000;
unsigned long weatherDuration = 5000;
unsigned long dateDuration = 5000;
bool displayOff = false;
int brightness = 7;
int lastBrightnessBeforeOff = 7;  // remembers brightness to restore on display_on
bool flipDisplay = false;
bool twelveHourToggle = false;
bool showDayOfWeek = true;
bool showDate = false;
bool showHumidity = false;
bool colonBlinkEnabled = true;
char ntpServer1[64] = "pool.ntp.org";
char ntpServer2[64] = "time.nist.gov";
char customMessage[121] = "";
char lastPersistentMessage[128] = "";
int messageDisplaySeconds;
int messageScrollTimes;
unsigned long messageStartTime = 0;
int currentScrollCount = 0;
int currentDisplayCycleCount = 0;

// Dimming
bool dimmingEnabled = false;
bool displayOffByDimming = false;
bool displayOffByBrightness = false;
int dimStartHour = 18;  // 6pm default
int dimStartMinute = 0;
int dimEndHour = 8;  // 8am default
int dimEndMinute = 0;
int dimBrightness = 2;            // Dimming level (0-15)
bool autoDimmingEnabled = false;  // true if using sunrise/sunset
int sunriseHour = 6;
int sunriseMinute = 0;
int sunsetHour = 18;
int sunsetMinute = 0;
bool clockOnlyDuringDimming = false;
bool configDirty = false;
unsigned long lastBrightnessChange = 0;
const unsigned long saveDelay = 1200;  // 1.2 seconds
int startTotal, endTotal;
bool dimActive = false;

//Countdown Globals
bool countdownEnabled = false;
time_t countdownTargetTimestamp = 0;  // Unix timestamp
char countdownLabel[64] = "";         // Label for the countdown
bool isDramaticCountdown = true;      // Default to the dramatic countdown mode
int countdownSegment = 0;
unsigned long segmentStartTime = 0;

// Runtime uptime persistence timer
unsigned long lastUptimeLog = 0;

struct OtaState {
  bool updating = false;
  bool restartPending = false;
  unsigned long restartRequestedAt = 0;
};
OtaState otaState;

// State management
bool weatherCycleStarted = false;
WiFiClient client;
const byte DNS_PORT = 53;
DNSServer dnsServer;
bool rotationEnabled = true;

String currentTemp = "";
String apparentTemperature = "";
int currentWeatherCode = -1;
String weatherDescription = "";
String weatherIcon = "";
bool showWeatherDescription = false;
bool weatherAvailable = false;
bool weatherFetched = false;
bool isAPMode = false;
char tempSymbol = '\006';

unsigned long &lastSwitch = displayController.switchedAt;
unsigned long lastColonBlink = 0;
int &displayMode = displayController.current;
int &prevDisplayMode = displayController.previous;
bool clockScrollDone = false;
int currentHumidity = -1;
bool ntpSyncSuccessful = false;

// NTP Synchronization State Machine
enum NtpState {
  NTP_IDLE,
  NTP_SYNCING,
  NTP_SUCCESS,
  NTP_FAILED
};
NtpState ntpState = NTP_IDLE;
unsigned long ntpStartTime = 0;
const int ntpTimeout = 30000;  // 30 seconds
const int maxNtpRetries = 30;
int ntpRetryCount = 0;
unsigned long lastNtpStatusPrintTime = 0;
const unsigned long ntpStatusPrintInterval = 1000;  // Print status every 1 seconds (adjust as needed)

// Non-blocking IP display globals
bool showingIp = false;
int ipDisplayCount = 0;
const int ipDisplayMax = 1;  // As per working copy for how long IP shows
String pendingIpToShow = "";

// Countdown display state - NEW
bool countdownScrolling = false;
unsigned long countdownScrollEndTime = 0;
unsigned long countdownStaticStartTime = 0;  // For last-day static display

// --- Inmediate countdown finish ---
bool countdownFinished = false;                       // Tracks if the countdown has permanently finished
bool countdownShowFinishedMessage = false;            // Flag to indicate "TIMES UP" message is active
unsigned long countdownFinishedMessageStartTime = 0;  // Timer for the 10-second message duration
unsigned long lastFlashToggleTime = 0;                // For controlling the flashing speed
bool currentInvertState = false;                      // Current state of display inversion for flashing
static bool hourglassPlayed = false;

// Weather Description Mode handling
unsigned long descStartTime = 0;  // For static description
bool descScrolling = false;
const unsigned long descriptionDuration = 3000;    // 3s for short text
static unsigned long descScrollEndTime = 0;        // for post-scroll delay (re-used for scroll timing)
const unsigned long descriptionScrollPause = 300;  // 300ms pause after scroll

// Custom message globals
bool forceMessageRestart = false;
bool messageBigNumbers = false;
bool allowInterrupt = true;

// Custom font for days and months
bool useCustomFont = true;

// Timer
bool timerActive = false;
int timerSubState = 0;  // 0: Timer Clock, 1: Message
bool timerPaused = false;
bool timerFinished = false;
unsigned long timerRemainingAtPause = 0;
unsigned long timerOriginalDuration = 0;  // For RESTART command
unsigned long timerFinishStartTime = 0;
unsigned long timerEndTime = 0;
bool isStopwatch = false;

// Pomodoro
bool isPomodoroActive = false;
unsigned long pomodoroWorkMs = 0;                  // Work phase duration in ms
unsigned long pomodoroBreakMs = 0;                 // Break phase duration in ms
bool pomodoroInBreak = false;                      // false = work phase, true = break phase
int pomodoroSession = 1;                           // 1–4, resets after long break
unsigned long pomodoroLongBreakMs = 15 * 60000UL;  // default 15 min

int global_scrolltimes = 0;  // Persisted from HTTP request
int global_msgSeconds = 0;


// --- Configurable Buttons (4x, stored in config.json) ---
struct BtnCfg {
  int pin = -1;
  String shortAct;
  String longAct;
};
BtnCfg btnCfg[4];
#define BTN_LONG_MS 800

// --- Forward declarations ---
void advanceDisplayMode(bool forced = false);
void previousDisplayMode(bool forced = false);
void goToMode(const String &target);
bool handlePomodoroCommand(String cmd);
bool handleTimerCommand(String cmd);
String cleanTextForDisplay(String str);
void executeAction(const String &action, const String &value);
void handleBrightnessChange(int newBrightness, bool isFromUI);
void setupButtons();
bool isModeAvailable(DisplayMode mode);
bool saveCountdownConfig(bool enabled, time_t targetTimestamp, const String &label);
bool saveConfigRuntime();
bool weatherConfigurationValid();
void showTimerMode7();

// --- Safe WiFi credential and API getters ---
const char *getSafeSsid() {
  if (isAPMode && strlen(ssid) == 0) {
    return "";
  } else {
    return isAPMode ? "********" : ssid;
  }
}

const char *getSafePassword() {
  if (strlen(password) == 0) {  // No password set yet — return empty string for fresh install
    return "";
  } else {  // Password exists — mask it in the web UI
    return "********";
  }
}

const char *getSafeApiKey() {
  if (strlen(openWeatherApiKey) == 0) {
    return "";
  } else {
    return "********************************";  // Always masked, even in AP mode
  }
}

bool weatherConfigurationValid() {
  const WeatherProvider provider = parseWeatherProvider(String(weatherProvider));
  if (provider == WeatherProvider::OpenMeteo) {
    return validWeatherCoordinates(String(weatherLatitude), String(weatherLongitude));
  }
  return strlen(openWeatherApiKey) == 32 &&
         strlen(openWeatherCity) > 0 && strlen(openWeatherCountry) > 0;
}

// Scroll flipped
textEffect_t getEffectiveScrollDirection(textEffect_t desiredDirection, bool isFlipped) {
  if (isFlipped) {
    // If the display is horizontally flipped, reverse the horizontal scroll direction
    if (desiredDirection == PA_SCROLL_LEFT) {
      return PA_SCROLL_RIGHT;
    } else if (desiredDirection == PA_SCROLL_RIGHT) {
      return PA_SCROLL_LEFT;
    }
  }
  return desiredDirection;
}


// -----------------------------------------------------------------------------
// Configuration Load & Save
// -----------------------------------------------------------------------------
void loadConfig() {
  Serial.println(F("[CONFIG] Loading configuration..."));

  // Check if config.json exists, if not, create default
  if (!LittleFS.exists("/config.json")) {
    Serial.println(F("[CONFIG] config.json not found, creating with defaults..."));
    JsonDocument doc;
    doc[F("ssid")] = "";
    doc[F("password")] = "";
    doc[F("openWeatherApiKey")] = "";
    doc[F("openWeatherCity")] = "";
    doc[F("openWeatherCountry")] = "";
    doc[F("weatherUnits")] = "metric";
    doc[F("weatherProvider")] = "openmeteo";
    doc[F("weatherLatitude")] = "";
    doc[F("weatherLongitude")] = "";
    doc[F("clockDuration")] = 10000;
    doc[F("weatherDuration")] = 5000;
    doc[F("dateDuration")] = 5000;
    doc[F("timeZone")] = "";
    doc[F("language")] = "en";
    doc[F("brightness")] = brightness;
    doc[F("lastBrightnessBeforeOff")] = brightness;
    doc[F("flipDisplay")] = flipDisplay;
    doc[F("twelveHourToggle")] = twelveHourToggle;
    doc[F("showDayOfWeek")] = showDayOfWeek;
    doc[F("showDate")] = false;
    doc[F("showHumidity")] = showHumidity;
    doc[F("colonBlinkEnabled")] = colonBlinkEnabled;
    doc[F("ntpServer1")] = ntpServer1;
    doc[F("ntpServer2")] = ntpServer2;
    doc[F("dimmingEnabled")] = dimmingEnabled;
    doc[F("dimStartHour")] = dimStartHour;
    doc[F("dimStartMinute")] = dimStartMinute;
    doc[F("dimEndHour")] = dimEndHour;
    doc[F("dimEndMinute")] = dimEndMinute;
    doc[F("dimBrightness")] = dimBrightness;
    doc[F("showWeatherDescription")] = showWeatherDescription;

    // --- Automatic dimming defaults ---
    doc[F("autoDimmingEnabled")] = autoDimmingEnabled;
    doc[F("sunriseHour")] = sunriseHour;
    doc[F("sunriseMinute")] = sunriseMinute;
    doc[F("sunsetHour")] = sunsetHour;
    doc[F("sunsetMinute")] = sunsetMinute;
    doc[F("clockOnlyDuringDimming")] = false;

    // Add countdown defaults when creating a new config.json
    JsonObject countdownObj = doc["countdown"].to<JsonObject>();
    countdownObj["enabled"] = false;
    countdownObj["targetTimestamp"] = 0;
    countdownObj["label"] = "";
    countdownObj["isDramaticCountdown"] = true;


    File f = LittleFS.open("/config.json", "w");
    if (f) {
      serializeJsonPretty(doc, f);
      f.close();
      Serial.println(F("[CONFIG] Default config.json created."));
    } else {
      Serial.println(F("[ERROR] Failed to create default config.json"));
    }
  }

  Serial.println(F("[CONFIG] Attempting to open config.json for reading."));
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println(F("[ERROR] Failed to open config.json for reading. Cannot load config."));
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.print(F("[ERROR] JSON parse failed during load: "));
    Serial.println(error.f_str());
    return;
  }

  bool configChanged = false;

  if (doc["hostname"].is<const char *>()) {
    deviceHostname = doc["hostname"].as<String>();
    Serial.print(F("[CONFIG] Loaded hostname: "));
    Serial.println(deviceHostname);
  }

  strlcpy(ssid, doc["ssid"] | "", sizeof(ssid));
  strlcpy(password, doc["password"] | "", sizeof(password));
  strlcpy(openWeatherApiKey, doc["openWeatherApiKey"] | "", sizeof(openWeatherApiKey));
  strlcpy(openWeatherCity, doc["openWeatherCity"] | "", sizeof(openWeatherCity));
  strlcpy(openWeatherCountry, doc["openWeatherCountry"] | "", sizeof(openWeatherCountry));
  strlcpy(weatherUnits, doc["weatherUnits"] | "metric", sizeof(weatherUnits));

  if (doc["weatherProvider"].is<const char *>()) {
    const String storedProvider = doc["weatherProvider"].as<String>();
    const WeatherProvider provider = parseWeatherProvider(storedProvider);
    strlcpy(weatherProvider, weatherProviderName(provider), sizeof(weatherProvider));
    if (storedProvider != weatherProvider) {
      doc["weatherProvider"] = weatherProvider;
      configChanged = true;
      Serial.println(F("[CONFIG] Normalized invalid weather provider."));
    }
  } else {
    // Existing installations keep OpenWeatherMap until user explicitly switches.
    strlcpy(weatherProvider, "openweathermap", sizeof(weatherProvider));
    doc["weatherProvider"] = weatherProvider;
    configChanged = true;
    Serial.println(F("[CONFIG] Migrated: preserving OpenWeatherMap provider."));
  }
  strlcpy(weatherLatitude, doc["weatherLatitude"] | "", sizeof(weatherLatitude));
  strlcpy(weatherLongitude, doc["weatherLongitude"] | "", sizeof(weatherLongitude));
  if (!validWeatherCoordinates(String(weatherLatitude), String(weatherLongitude)) &&
      validWeatherCoordinates(String(openWeatherCity), String(openWeatherCountry))) {
    strlcpy(weatherLatitude, openWeatherCity, sizeof(weatherLatitude));
    strlcpy(weatherLongitude, openWeatherCountry, sizeof(weatherLongitude));
    doc["weatherLatitude"] = weatherLatitude;
    doc["weatherLongitude"] = weatherLongitude;
    configChanged = true;
    Serial.println(F("[CONFIG] Migrated legacy weather coordinates."));
  }
  strlcpy(customMessage, doc["customMessage"] | "", sizeof(customMessage));
  strlcpy(lastPersistentMessage, customMessage, sizeof(lastPersistentMessage));
  clockDuration = doc["clockDuration"] | 10000;
  weatherDuration = doc["weatherDuration"] | 5000;
  dateDuration = doc["dateDuration"] | weatherDuration;
  strlcpy(timeZone, doc["timeZone"] | "Etc/UTC", sizeof(timeZone));
  if (doc["language"].is<const char *>()) {
    strlcpy(language, doc["language"], sizeof(language));
  } else {
    strlcpy(language, "en", sizeof(language));
    Serial.println(F("[CONFIG] 'language' key not found in config.json, defaulting to 'en'."));
  }

  brightness = doc["brightness"] | 7;
  displayOff = doc["displayOff"] | false;
  lastBrightnessBeforeOff = doc["lastBrightnessBeforeOff"] | 7;
  flipDisplay = doc["flipDisplay"] | false;
  twelveHourToggle = doc["twelveHourToggle"] | false;
  showDayOfWeek = doc["showDayOfWeek"] | true;
  showDate = doc["showDate"] | false;
  showHumidity = doc["showHumidity"] | false;
  colonBlinkEnabled = doc["colonBlinkEnabled"] | true;
  showWeatherDescription = doc["showWeatherDescription"] | false;

  // --- Dimming settings ---
  if (doc["dimmingEnabled"].is<bool>()) {
    dimmingEnabled = doc["dimmingEnabled"].as<bool>();
  } else {
    String de = doc["dimmingEnabled"].as<String>();
    dimmingEnabled = (de == "true" || de == "1" || de == "on");
  }

  String de = doc["dimmingEnabled"].as<String>();
  dimmingEnabled = (de == "true" || de == "on" || de == "1");

  dimStartHour = doc["dimStartHour"] | 18;
  dimStartMinute = doc["dimStartMinute"] | 0;
  dimEndHour = doc["dimEndHour"] | 8;
  dimEndMinute = doc["dimEndMinute"] | 0;
  dimBrightness = doc["dimBrightness"] | 0;

  // safely handle both numeric or string "Off" for dimBrightness
  if (doc["dimBrightness"].is<int>()) {
    dimBrightness = doc["dimBrightness"].as<int>();
  } else {
    String val = doc["dimBrightness"].as<String>();
    if (val.equalsIgnoreCase("off")) dimBrightness = -1;
    else dimBrightness = val.toInt();
  }

  // --- Automatic dimming ---
  if (!doc["autoDimmingEnabled"].isNull()) {
    if (doc["autoDimmingEnabled"].is<bool>()) {
      autoDimmingEnabled = doc["autoDimmingEnabled"].as<bool>();
    } else {
      String val = doc["autoDimmingEnabled"].as<String>();
      autoDimmingEnabled = (val == "true" || val == "1" || val == "on");
    }
  } else {
    autoDimmingEnabled = false;  // default if key missing
  }

  sunriseHour = doc["sunriseHour"] | 6;
  sunriseMinute = doc["sunriseMinute"] | 0;
  sunsetHour = doc["sunsetHour"] | 18;
  sunsetMinute = doc["sunsetMinute"] | 0;

  strlcpy(ntpServer1, doc["ntpServer1"] | "pool.ntp.org", sizeof(ntpServer1));
  String configuredNtpServer2 = doc["ntpServer2"] | "time.nist.gov";
  configuredNtpServer2.trim();
  if (configuredNtpServer2.startsWith("http://") || configuredNtpServer2.startsWith("https://")) {
    strlcpy(ntpServer2, "time.nist.gov", sizeof(ntpServer2));
    doc["ntpServer2"] = ntpServer2;
    configChanged = true;
    Serial.println(F("[CONFIG] Removed legacy integration URL; restored secondary NTP default."));
  } else {
    strlcpy(ntpServer2, configuredNtpServer2.c_str(), sizeof(ntpServer2));
  }

  if (strcmp(weatherUnits, "imperial") == 0)
    tempSymbol = '\007';
  else
    tempSymbol = '\006';


  // --- COUNTDOWN CONFIG LOADING ---
  if (doc["countdown"].is<JsonObject>()) {
    JsonObject countdownObj = doc["countdown"];

    countdownEnabled = countdownObj["enabled"] | false;
    countdownTargetTimestamp = countdownObj["targetTimestamp"] | 0;
    isDramaticCountdown = countdownObj["isDramaticCountdown"] | true;

    JsonVariant labelVariant = countdownObj["label"];
    if (labelVariant.isNull() || !labelVariant.is<const char *>()) {
      strcpy(countdownLabel, "");
    } else {
      const char *labelTemp = labelVariant.as<const char *>();
      size_t labelLen = strlen(labelTemp);
      if (labelLen >= sizeof(countdownLabel)) {
        Serial.println(F("[CONFIG] label from JSON too long, truncating."));
      }
      strlcpy(countdownLabel, labelTemp, sizeof(countdownLabel));
    }
    countdownFinished = false;
  } else {
    countdownEnabled = false;
    countdownTargetTimestamp = 0;
    strcpy(countdownLabel, "");
    isDramaticCountdown = true;
    Serial.println(F("[CONFIG] Countdown object not found, defaulting to disabled."));
    countdownFinished = false;
  }

  // --- CLOCK-ONLY-DURING-DIMMING LOADING ---
  if (!doc["clockOnlyDuringDimming"].isNull()) {
    clockOnlyDuringDimming = doc["clockOnlyDuringDimming"].as<bool>();
  } else {
    clockOnlyDuringDimming = false;
    doc["clockOnlyDuringDimming"] = clockOnlyDuringDimming;
    configChanged = true;
    Serial.println(F("[CONFIG] Migrated: added clockOnlyDuringDimming default."));
  }


  // --- Save migrated config if needed ---
  if (configChanged) {
    Serial.println(F("[CONFIG] Saving migrated config.json"));

    File f = LittleFS.open("/config.json", "w");
    if (f) {
      serializeJsonPretty(doc, f);
      f.close();
      Serial.println(F("[CONFIG] Migration saved successfully."));
    } else {
      Serial.println(F("[ERROR] Failed to save migrated config.json"));
    }
  }

  // --- BUTTON CONFIG LOADING ---
  if (doc["buttons"].is<JsonArray>()) {
    JsonArray arr = doc["buttons"].as<JsonArray>();

    for (int i = 0; i < 4 && i < (int)arr.size(); i++) {
      btnCfg[i].pin = arr[i]["pin"] | -1;
      btnCfg[i].shortAct = arr[i]["shortAction"] | "";
      btnCfg[i].longAct = arr[i]["longAction"] | "";

      if (btnCfg[i].pin >= 0) {
        Serial.printf("[BUTTON] Button %d → GPIO %d | short=%s | long=%s\n",
                      i + 1,
                      btnCfg[i].pin,
                      btnCfg[i].shortAct.c_str(),
                      btnCfg[i].longAct.c_str());
      }
    }
  }

  Serial.println(F("[CONFIG] Configuration loaded."));
}


// -----------------------------------------------------------------------------
// Network Identity
// -----------------------------------------------------------------------------
void setupHostname() {
#if defined(ESP8266)
  WiFi.hostname(deviceHostname);
#elif defined(ESP32)
  WiFi.setHostname(deviceHostname.c_str());
#endif
}


// -----------------------------------------------------------------------------
// WiFi Setup
// -----------------------------------------------------------------------------
void connectWiFi() {
  Serial.println(F("[WIFI] Connecting to WiFi..."));

  if (!credentialsExist()) {
    Serial.println(F("[WIFI] No saved credentials. Starting AP mode directly."));
    WiFi.mode(WIFI_AP);
    WiFi.disconnect(true);
    delay(100);

    setupHostname();

    if (strlen(DEFAULT_AP_PASSWORD) < 8) {
      WiFi.softAP(DEFAULT_AP_SSID);
      Serial.println(F("[WIFI] AP Mode started (no password, too short)."));
    } else {
      WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
      Serial.println(F("[WIFI] AP Mode started."));
    }

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.print(F("[WIFI] AP IP address: "));
    Serial.println(WiFi.softAPIP());
    isAPMode = true;

    WiFiMode_t mode = WiFi.getMode();
    Serial.printf("[WIFI] WiFi mode after setting AP: %s\n",
                  mode == WIFI_OFF ? "OFF" : mode == WIFI_STA    ? "STA ONLY"
                                           : mode == WIFI_AP     ? "AP ONLY"
                                           : mode == WIFI_AP_STA ? "AP + STA (Error!)"
                                                                 : "UNKNOWN");

    Serial.println(F("[WIFI] AP Mode Started"));
    return;
  }

  // If credentials exist, attempt STA connection
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
#ifdef ESP8266
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
#ifdef ESP32
  WiFi.setSleep(false);
#endif
  setupHostname();
  WiFi.disconnect();  // Ensure a clean slate
  delay(100);         // The "Radio Breathing Room"
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();

  const unsigned long timeout = 30000;
  const int maxRetries = 3;
  int retryCount = 0;
  unsigned long animTimer = 0;
  int animFrame = 0;
  bool animating = true;

  while (animating) {
    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WIFI] Connected: " + WiFi.localIP().toString());
      isAPMode = false;

      WiFiMode_t mode = WiFi.getMode();
      Serial.printf("[WIFI] WiFi mode after STA connection: %s\n",
                    mode == WIFI_OFF ? "OFF" : mode == WIFI_STA    ? "STA ONLY"
                                             : mode == WIFI_AP     ? "AP ONLY"
                                             : mode == WIFI_AP_STA ? "AP + STA (Error!)"
                                                                   : "UNKNOWN");

      // --- IP Display initiation ---
      if (LittleFS.exists("/update_success.txt")) {
        // Use (char) to force the raw font glyphs and avoid the newline bug
        pendingIpToShow = String((char)10) + (char)11 + (char)32 + (char)173 + String(FIRMWARE_VERSION);
        LittleFS.remove("/update_success.txt");
        IP_SCROLL_SPEED = 70;
      } else {
        pendingIpToShow = WiFi.localIP().toString();
        IP_SCROLL_SPEED = 115;
        // Replace all dots with your custom font code 184
        for (int i = 0; i < pendingIpToShow.length(); i++) {
          if (pendingIpToShow[i] == '.') {
            pendingIpToShow[i] = 184;
          }
        }
      }

      showingIp = true;
      ipDisplayCount = 0;
      P.displayClear();
      P.setCharSpacing(1);
      textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
      P.displayScroll(pendingIpToShow.c_str(), PA_CENTER, actualScrollDirection, IP_SCROLL_SPEED);
      // --- END IP Display initiation ---

      animating = false;  // Exit the connection loop
      break;
    } else if (now - startAttemptTime >= timeout) {

      if (retryCount < maxRetries - 1) {
        retryCount++;
        Serial.printf("[WIFI] Attempt failed. Retrying (%d/%d)...\n", retryCount + 1, maxRetries);

        WiFi.disconnect();
        delay(500);
        WiFi.begin(ssid, password);
        startAttemptTime = millis();  // reset timeout timer
      } else {
        Serial.println(F("[WIFI] All attempts failed. Starting AP mode..."));

        WiFi.mode(WIFI_AP);
        WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
        Serial.print(F("[WIFI] AP IP address: "));
        Serial.println(WiFi.softAPIP());
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        isAPMode = true;

        WiFiMode_t mode = WiFi.getMode();
        Serial.printf("[WIFI] WiFi mode after STA failure and setting AP: %s\n",
                      mode == WIFI_OFF ? "OFF" : mode == WIFI_STA    ? "STA ONLY"
                                               : mode == WIFI_AP     ? "AP ONLY"
                                               : mode == WIFI_AP_STA ? "AP + STA (Error!)"
                                                                     : "UNKNOWN");

        animating = false;
        Serial.println(F("[WIFI] AP Mode Started"));
        break;
      }
    }
    if (now - animTimer > 750) {
      animTimer = now;
      P.setTextAlignment(PA_CENTER);
      switch (animFrame % 3) {
        case 0:
          {
            String s = "\003 ";
            s += char(169);
            P.print(s.c_str());
            break;
          }

        case 1:
          {
            String s = "\003 ";
            s += char(170);
            P.print(s.c_str());
            break;
          }

        case 2:
          {
            String s = "\003 ";
            s += char(171);
            P.print(s.c_str());
            break;
          }
      }
      animFrame++;
    }
    delay(1);
  }
}

// -----------------------------------------------------------------------------
// mDNS
// -----------------------------------------------------------------------------
void setupMDNS() {
  MDNS.end();

  bool mdnsStarted = MDNS.begin(deviceHostname.c_str());

  if (mdnsStarted) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[WIFI] mDNS started: http://%s.local\n", deviceHostname.c_str());
  } else {
    Serial.println("[WIFI] mDNS failed to start");
  }
}

// -----------------------------------------------------------------------------
// Time / NTP Functions
// -----------------------------------------------------------------------------
void setupTime() {
  if (!isAPMode) {
    Serial.println(F("[TIME] Starting NTP sync"));
  }

  configTime(0, 0, ntpServer1, ntpServer2);

  // Set the Time Zone
  setenv("TZ", ianaToPosix(timeZone), 1);
  tzset();

  // Initialize state flags to begin synchronization tracking
  ntpState = NTP_SYNCING;
  ntpStartTime = millis();
  ntpRetryCount = 0;
  ntpSyncSuccessful = false;
}


// -----------------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------------
void printConfigToSerial() {
  Serial.println(F("========= Loaded Configuration ========="));
  Serial.print(F("WiFi SSID: "));
  Serial.println(ssid);
  Serial.print(F("WiFi Password: "));
  Serial.println(password);
  Serial.print(F("OpenWeather City: "));
  Serial.println(openWeatherCity);
  Serial.print(F("OpenWeather Country: "));
  Serial.println(openWeatherCountry);
  Serial.print(F("OpenWeather API Key: "));
  Serial.println(openWeatherApiKey);
  Serial.print(F("Weather Provider: "));
  Serial.println(weatherProvider);
  Serial.print(F("Weather Latitude/Longitude: "));
  Serial.printf("%s, %s\n", weatherLatitude, weatherLongitude);
  Serial.print(F("Temperature Unit: "));
  Serial.println(weatherUnits);
  Serial.print(F("Clock duration: "));
  Serial.println(clockDuration);
  Serial.print(F("Weather duration: "));
  Serial.println(weatherDuration);
  Serial.print(F("TimeZone (IANA): "));
  Serial.println(timeZone);
  Serial.print(F("Days of the Week/Weather description language: "));
  Serial.println(language);
  Serial.print(F("Brightness: "));
  Serial.println(brightness);
  Serial.print(F("Flip Display: "));
  Serial.println(flipDisplay ? "Yes" : "No");
  Serial.print(F("Show 12h Clock: "));
  Serial.println(twelveHourToggle ? "Yes" : "No");
  Serial.print(F("Show Day of the Week: "));
  Serial.println(showDayOfWeek ? "Yes" : "No");
  Serial.print(F("Show Date: "));
  Serial.println(showDate ? "Yes" : "No");
  Serial.print(F("Show Weather Description: "));
  Serial.println(showWeatherDescription ? "Yes" : "No");
  Serial.print(F("Show Humidity: "));
  Serial.println(showHumidity ? "Yes" : "No");
  Serial.print(F("Blinking colon: "));
  Serial.println(colonBlinkEnabled ? "Yes" : "No");
  Serial.print(F("NTP Server 1: "));
  Serial.println(ntpServer1);
  Serial.print(F("NTP Server 2: "));
  Serial.println(ntpServer2);

  // ---------------------------------------------------------------------------
  // DIMMING SECTION
  // ---------------------------------------------------------------------------
  Serial.print(F("Automatic Dimming: "));
  Serial.println(autoDimmingEnabled ? "Enabled" : "Disabled");
  Serial.print(F("Custom Dimming: "));
  Serial.println(dimmingEnabled ? "Enabled" : "Disabled");
  Serial.print(F("Clock only during dimming: "));
  Serial.println(clockOnlyDuringDimming ? "Yes" : "No");

  if (autoDimmingEnabled) {
    // --- Automatic (Sunrise/Sunset) dimming mode ---
    if ((sunriseHour == 6 && sunriseMinute == 0) && (sunsetHour == 18 && sunsetMinute == 0)) {
      Serial.println(F("Automatic Dimming Schedule: Sunrise/Sunset Data not available yet (waiting for weather update)"));
    } else {
      Serial.printf("Automatic Dimming Schedule: Sunrise: %02d:%02d → Sunset: %02d:%02d\n",
                    sunriseHour, sunriseMinute, sunsetHour, sunsetMinute);

      time_t now_time = time(nullptr);
      struct tm localTime;
      localtime_r(&now_time, &localTime);

      int curTotal = localTime.tm_hour * 60 + localTime.tm_min;
      int startTotal = sunsetHour * 60 + sunsetMinute;
      int endTotal = sunriseHour * 60 + sunriseMinute;

      bool autoActive = (startTotal < endTotal)
                          ? (curTotal >= startTotal && curTotal < endTotal)
                          : (curTotal >= startTotal || curTotal < endTotal);

      Serial.printf("Current Auto-Dimming Status: %s\n", autoActive ? "ACTIVE" : "Inactive");
      Serial.printf("Dimming Brightness (night): %d\n", dimBrightness);
    }
  } else {
    // --- Manual (Custom Schedule) dimming mode ---
    Serial.printf("Custom Dimming Schedule: %02d:%02d → %02d:%02d\n",
                  dimStartHour, dimStartMinute, dimEndHour, dimEndMinute);
    Serial.printf("Dimming Brightness: %d\n", dimBrightness);
  }

  Serial.print(F("Countdown Enabled: "));
  Serial.println(countdownEnabled ? "Yes" : "No");
  Serial.print(F("Countdown Target Timestamp: "));
  Serial.println(countdownTargetTimestamp);
  Serial.print(F("Countdown Label: "));
  Serial.println(countdownLabel);
  Serial.print(F("Dramatic Countdown Display: "));
  Serial.println(isDramaticCountdown ? "Yes" : "No");
  Serial.print(F("Custom Message: "));
  Serial.println(customMessage);
  Serial.print(F("Total Runtime: "));
  if (getTotalRuntimeSeconds() > 0) {
    Serial.println(formatTotalRuntime());
  } else {
    Serial.println(F("No runtime recorded yet."));
  }

  Serial.print(F("Hostname: "));
  Serial.println(deviceHostname);

  Serial.printf("Matrix Pins: CLK=%d CS=%d DATA=%d\n",
                CLK_PIN, CS_PIN, DATA_PIN);

  Serial.println(F("Physical Buttons:"));

  bool hasButtons = false;

  for (int i = 0; i < 4; i++) {
    if (btnCfg[i].pin >= 0) {
      hasButtons = true;

      Serial.printf("  Button %d → GPIO %d | short=%s | long=%s\n",
                    i + 1,
                    btnCfg[i].pin,
                    btnCfg[i].shortAct.c_str(),
                    btnCfg[i].longAct.c_str());
    }
  }

  if (!hasButtons) {
    Serial.println(F("  None configured"));
  }

  Serial.println(F("========================================"));
  Serial.println();
}

void replaceIconTokens(String &msg, int &totalPixelWidth) {
  struct IconMap {
    const char *token;
    const char *glyph;
    int pixelWidth;
  };

  static const IconMap icons[] = {
    { "[NOTEMP]", "\x01", 25 },
    { "[NONTP]", "\x02", 20 },
    { "[WIFI]", "\x03", 13 },
    { "[INFO]", "\x04", 22 },
    { "[AP]", "\x05", 9 },
    { "[C]", "\x06", 4 },
    { "[F]", "\x07", 4 },
    { "[TIMEISUP]", "\x08", 32 },
    { "[TIMEISUPINVERTED]", "\x09", 32 },
    { "[SUNNY]", "\x0C", 8 },
    { "[CLOUDY]", "\x0D", 8 },
    { "[NODATA]", "\x0F", 23 },
    { "[RAINY]", "\x10", 8 },
    { "[THUNDER]", "\x11", 8 },
    { "[SNOWY]", "\x12", 8 },
    { "[WINDY]", "\x13", 8 },
    { "[CLOCK]", "\x14", 8 },
    { "[ALARM]", "\x15", 9 },
    { "[UPDATE]", "\x16", 8 },
    { "[BATTERYEMPTY]", "\x17", 8 },
    { "[BATTERY33]", "\x18", 8 },
    { "[BATTERY66]", "\x19", 8 },
    { "[BATTERYFULL]", "\x1A", 8 },
    { "[BOLT]", "\x1B", 4 },
    { "[HOUSE]", "\x1C", 7 },
    { "[TEMP]", "\x1D", 8 },
    { "[MUSICNOTE]", "\x1E", 7 },
    { "[PLAY]", "\x1F", 4 },
    { "[SPACE]", "\x20", 1 },
    { "[PAUSE]", "\x7F", 5 },
    { "[EURO]", "\x80", 5 },
    { "[SPEAKER]", "\x81", 8 },
    { "[SPEAKEROFF]", "\x82", 8 },
    { "[RED]", "\x83", 6 },
    { "[UP]", "\x86", 3 },
    { "[DOWN]", "\x88", 3 },
    { "[RIGHT]", "\x8B", 8 },
    { "[LEFT]", "\x8D", 8 },
    { "[TALK]", "\x8E", 7 },
    { "[HEART]", "\x8F", 7 },
    { "[CHECK]", "\x90", 5 },
    { "[INSTA]", "\x9B", 8 },
    { "[TV]", "\x9C", 11 },
    { "[YOUTUBE]", "\x9D", 8 },
    { "[BELL]", "\x9E", 6 },
    { "[LOCK]", "\x9F", 7 },
    { "[PERSON]", "\xA0", 6 },
    { "[HOURGLASS]", "\xA1", 5 },
    { "[HOURGLASS25]", "\xA2", 5 },
    { "[HOURGLASS75]", "\xA3", 5 },
    { "[HOURGLASSFULL]", "\xA4", 5 },
    { "[CAR]", "\xBB", 9 },
    { "[MAIL]", "\xA6", 9 },
    { "[CO2]", "\xA7", 13 },
    { "[MOON]", "\xA8", 9 },
    { "[SIGNAL1]", "\xA9", 8 },
    { "[SIGNAL2]", "\xAA", 8 },
    { "[SIGNAL3]", "\xAB", 8 },
    { "[DEG]", "\xB0", 3 },
    { "[SUNDAYJP]", "\xB1", 5 },
    { "[MONDAYJP]", "\xB2", 6 },
    { "[TUESDAYJP]", "\xB3", 7 },
    { "[WEDNESDAYJP]", "\xB4", 7 },
    { "[THURSDAYJP]", "\xB5", 7 },
    { "[FRIDAYJP]", "\xB6", 7 },
    { "[SATURDAYJP]", "\xB7", 7 },
    { "[MIST]", "\xB9", 7 }
  };

  // 1. Replace all tokens with glyphs first
  for (const auto &icon : icons) {
    msg.replace(icon.token, icon.glyph);
  }

  // 2. Calculate pixel width of the resulting string
  totalPixelWidth = 0;

  for (int i = 0; i < (int)msg.length(); i++) {
    bool isIcon = false;
    int charWidth = 0;
    unsigned char c = (unsigned char)msg[i];

    // Check for icons
    for (const auto &icon : icons) {
      if (c == (unsigned char)icon.glyph[0]) {
        charWidth = icon.pixelWidth;
        isIcon = true;
        break;
      }
    }

    if (!isIcon) {
      switch (c) {
        // --- 1 Pixel Wide ---
        case 32:  // Space
        case '!':
        case '.':
        case ':':
        case '\'':  // Single quote
        case '|':
        case 73:   // Capital 'I'
        case 184:  // Custom Dot (IP display)
          charWidth = 1;
          break;

        // --- 2 Pixels Wide ---
        case 40:  // (
        case 41:  // )
        case 59:  // ;
        case 91:  // [
        case 93:  // ]
        case ',':
          charWidth = 2;
          break;

        // --- 3 Pixels Wide ---
        case 34:
        case '?':
        case '-':
        case '_':  // Underscore
        case '/':
          charWidth = 3;
          break;

        // --- 4 Pixels Wide ---
        case 176:  // Degree symbol (Standard °)
          charWidth = 4;
          break;

        // --- 5 Pixels Wide ---
        case '#':
        case '&':
        case '$':
        case 0xA5:  //¥
        case '@':
        case '+':  // Moved here: 5px
          charWidth = 5;
          break;

        // --- 6 Pixels Wide ---
        case '%':
        case '~':
          charWidth = 6;
          break;

        // --- Default (Caps & Numbers) ---
        default:
          charWidth = 3;
          break;
      }
    }

    totalPixelWidth += charWidth;

    // Add 1px gap between characters/icons (except the last one)
    if (i < (int)msg.length() - 1) {
      totalPixelWidth += 1;
    }
  }
}

void handleCustomMessageLogic(AsyncWebServerRequest *request) {
  if (networkData.busy()) {
    Serial.println(F("[MESSAGE] Rejected: Network Busy"));
    AsyncWebServerResponse *busyResponse = request->beginResponse(503, "text/plain", "Network Busy");
    busyResponse->addHeader("Access-Control-Allow-Origin", "*");
    request->send(busyResponse);
    return;
  }

  if (request->hasArg("message")) {
    String msg = request->arg("message");
    msg.trim();

    bool isClearRequest = (msg.length() == 0);
    bool incomingAllowInterrupt = true;

    // Detect Source: Header or URL param
    bool isFromUI = (request->header("X-Source") == "UI") || (request->arg("source") == "UI");
    bool isFromAPI = !isFromUI;

    // 1. Interrupt (allowInterrupt / interrupt)
    if (request->hasArg("allowInterrupt")) {
      incomingAllowInterrupt = (request->arg("allowInterrupt") == "1");
    } else if (request->hasArg("interrupt")) {
      incomingAllowInterrupt = (request->arg("interrupt") == "1");
    }

    // 2. Seconds (seconds / duration)
    int rawSecs = request->hasArg("seconds") ? request->arg("seconds").toInt() : (request->hasArg("duration") ? request->arg("duration").toInt() : 0);
    int newMessageDisplaySeconds = constrain(rawSecs, 0, 3600);

    // 3. Scrolls (scrolltimes / scrolls / scroll_times)
    int rawScrolls = request->hasArg("scrolltimes") ? request->arg("scrolltimes").toInt() : (request->hasArg("scrolls") ? request->arg("scrolls").toInt() : (request->hasArg("scroll_times") ? request->arg("scroll_times").toInt() : 0));
    int newMessageScrollTimes = constrain(rawScrolls, 0, 100);

    // 4. Speed & Big Numbers
    bool newMessageBigNumbers = (request->arg("bignumbers") == "1");
    int rawSpeed = request->hasArg("speed") ? request->arg("speed").toInt() : GENERAL_SCROLL_SPEED;
    int localSpeed = constrain(rawSpeed, 10, 200);

    // PROTECTION: Clock-only dimming
    if (!isClearRequest && clockOnlyDuringDimming && dimActive) {
      Serial.printf("[MESSAGE] Rejected (Dimming Mode): '%s'\n", msg.c_str());
      request->send(409, "text/plain", "Clock-only dimming mode active");
      return;
    }

    // Handle Pomodoro Commands (checked before Timer)
    if (handlePomodoroCommand(msg)) {
      if (!allowInterrupt) {
        request->send(409, "text/plain", "Protected message active");
      } else if (clockOnlyDuringDimming && dimActive) {
        request->send(409, "text/plain", "Clock-only dimming active");
      } else {
        Serial.println(F("[MESSAGE] Pomodoro command executed."));
        request->send(200, "text/plain", "Pomodoro Command Executed");
      }
      return;
    }

    // Handle Timer Commands
    if (handleTimerCommand(msg)) {
      if (!allowInterrupt) {
        request->send(409, "text/plain", "Protected message active");
      } else if (clockOnlyDuringDimming && dimActive) {
        request->send(409, "text/plain", "Clock-only dimming active");
      } else {
        Serial.println(F("[MESSAGE] Timer command executed."));
        request->send(200, "text/plain", "Timer Command Executed");
      }
      return;
    }

    // PROTECTION: Timer Active
    if (timerActive && !isClearRequest && incomingAllowInterrupt) {
      Serial.println(F("[MESSAGE] Rejected: Timer is active."));
      request->send(409, "text/plain", "Timer active - use priority message");
      return;
    }

    // PROTECTION: Protected Message Running
    if (!isClearRequest && !allowInterrupt && incomingAllowInterrupt) {
      Serial.println(F("[MESSAGE] Rejected: Protected message running"));
      request->send(409, "text/plain", "Display busy");
      return;
    }

    String filtered = cleanTextForDisplay(msg);
    messageDisplaySeconds = newMessageDisplaySeconds;
    messageScrollTimes = newMessageScrollTimes;
    messageBigNumbers = newMessageBigNumbers;

    // --- LOG: Consolidated Intent (Before Saving/Execution) ---
    Serial.printf(
      "[MESSAGE] Source=%s | msg='%s' | seconds=%d | scrolls=%d | speed=%d | big=%d | allowInterrupt=%d\n",
      isFromUI ? "UI" : "API",
      filtered.c_str(),
      messageDisplaySeconds,
      messageScrollTimes,
      localSpeed,
      messageBigNumbers,
      incomingAllowInterrupt);

    if (timerActive && !isClearRequest) {
      displayMode = (messageScrollTimes == 0 && messageDisplaySeconds == 0) ? 6 : 7;
      lastSwitch = millis();
      forceMessageRestart = true;
    }

    // --- CLEAR MESSAGE ---
    if (isClearRequest) {
      allowInterrupt = true;
      forceMessageRestart = true;
      if (isFromUI) {
        customMessage[0] = '\0';
        lastPersistentMessage[0] = '\0';
        messageStartTime = 0;
        currentScrollCount = 0;
        messageDisplaySeconds = 0;
        messageScrollTimes = 0;
        displayMode = 0;
        prevDisplayMode = 6;
        lastSwitch = millis();
        clockScrollDone = false;
        saveCustomMessageToConfig("");
        Serial.println(F("[MESSAGE] Full Clear (UI) completed."));
        request->send(200, "text/plain", "CLEARED (UI)");
      } else {
        customMessage[0] = '\0';
        messageStartTime = 0;
        currentScrollCount = 0;
        messageDisplaySeconds = 0;
        messageScrollTimes = 0;

        if (strlen(lastPersistentMessage) > 0) {
          strlcpy(customMessage, lastPersistentMessage, sizeof(customMessage));
          messageScrollSpeed = GENERAL_SCROLL_SPEED;
          displayMode = 6;
          prevDisplayMode = 0;
          Serial.println(F("[MESSAGE] Temp message cleared. Persistent restored."));
          request->send(200, "text/plain", "CLEARED (API temporary, persistent restored)");
        } else {
          displayMode = 0;
          lastSwitch = millis();
          prevDisplayMode = 6;
          clockScrollDone = false;
          Serial.println(F("[MESSAGE] Temp message cleared. No persistent found."));
          request->send(200, "text/plain", "CLEARED (API temporary, no persistent)");
        }
      }
      return;
    }

    // --- STORE & ACTIVATE ---
    if (isFromAPI) {
      filtered.toCharArray(customMessage, sizeof(customMessage));
      messageScrollSpeed = localSpeed;
    } else {
      filtered.toCharArray(customMessage, sizeof(customMessage));
      strlcpy(lastPersistentMessage, customMessage, sizeof(lastPersistentMessage));
      messageScrollSpeed = GENERAL_SCROLL_SPEED;
      saveCustomMessageToConfig(lastPersistentMessage);
    }

    allowInterrupt = incomingAllowInterrupt;
    displayMode = 6;
    prevDisplayMode = 0;
    messageStartTime = millis();
    currentScrollCount = 0;
    clockScrollDone = false;
    forceMessageRestart = true;

    // --- FINAL RESPONSE ---
    String responseMsg = "OK (" + String(isFromUI ? "UI" : "API") + ")";
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", responseMsg);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  } else {
    Serial.println(F("[MESSAGE] Error: Missing message parameter"));
    request->send(400, "text/plain", "Missing message parameter");
  }
}

// -----------------------------------------------------------------------------
// Web Server and Captive Portal
// -----------------------------------------------------------------------------
void handleCaptivePortal(AsyncWebServerRequest *request);

void setupWebServer() {
  Serial.println(F("[WEBSERVER] Setting up web server..."));

  // 1. Global CORS headers (Required for Chrome Extension)
  // These headers allow the browser to verify the security policy for all routes.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, X-Source");

  // Root handler with BOTH CORS and Cache-Prevention
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(
      "text/html", sizeof(index_html) - 1,
      [](uint8_t *buffer, size_t maxLength, size_t index) -> size_t {
        const size_t remaining = sizeof(index_html) - 1 - index;
        const size_t length = remaining < maxLength ? remaining : maxLength;
        memcpy_P(buffer, index_html + index, length);
        return length;
      });
    // Anti-Caching Headers: Ensures the browser always fetches the latest UI
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    // CORS Header (Manual insurance for strict browser scrutiny)
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  });

  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /config.json"));
    File f = LittleFS.open("/config.json", "r");
    if (!f) {
      Serial.println(F("[WEBSERVER] Error opening /config.json"));
      request->send(500, "application/json", "{\"error\":\"Failed to open config.json\"}");
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.print(F("[WEBSERVER] Error parsing /config.json: "));
      Serial.println(err.f_str());
      request->send(500, "application/json", "{\"error\":\"Failed to parse config.json\"}");
      return;
    }

    // Always sanitize before sending to browser
    doc[F("ssid")] = getSafeSsid();
    doc[F("password")] = getSafePassword();
    doc[F("openWeatherApiKey")] = getSafeApiKey();
    doc[F("mode")] = isAPMode ? "ap" : "sta";

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /save"));
    JsonDocument doc;

    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      Serial.println(F("[WEBSERVER] Existing config.json found, loading for update..."));
      DeserializationError err = deserializeJson(doc, configFile);
      configFile.close();
      if (err) {
        Serial.print(F("[WEBSERVER] Error parsing existing config.json: "));
        Serial.println(err.f_str());
      }
    } else {
      Serial.println(F("[WEBSERVER] config.json not found, starting with empty doc for save."));
    }

    for (int i = 0; i < request->params(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      String n = p->name();
      String v = p->value();

      if (n == "brightness") doc[n] = v.toInt();
      else if (n == "clockDuration") doc[n] = v.toInt();
      else if (n == "weatherDuration") doc[n] = v.toInt();
      else if (n == "dateDuration") doc[n] = v.toInt();
      else if (n == "flipDisplay") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "twelveHourToggle") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "showDayOfWeek") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "showDate") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "showHumidity") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "colonBlinkEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "dimStartHour") doc[n] = v.toInt();
      else if (n == "dimStartMinute") doc[n] = v.toInt();
      else if (n == "dimEndHour") doc[n] = v.toInt();
      else if (n == "dimEndMinute") doc[n] = v.toInt();
      else if (n == "dimBrightness") {
        if (v == "Off" || v == "off") doc[n] = -1;
        else doc[n] = v.toInt();
      } else if (n == "showWeatherDescription") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "dimmingEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "clockOnlyDuringDimming") {
        doc[n] = (v == "true" || v == "on" || v == "1");
      } else if (n == "weatherUnits") doc[n] = v;
      else if (n == "hostname") doc[n] = v;
      else if (n == "password") {
        if (v != "********") {
          doc[n] = v;  // saves new password OR empty string (open network)
        } else {
          Serial.println(F("[SAVE] Password unchanged (masked)."));
        }
      } else if (n == "ssid") {
        if (v != "********" && v.length() > 0) {
          doc[n] = v;
        } else {
          Serial.println(F("[SAVE] SSID unchanged."));
        }
      } else if (n == "openWeatherApiKey") {
        if (v != "********************************") {  // ignore mask only
          doc[n] = v;                                   // save new key (even if empty)
          Serial.print(F("[SAVE] API key updated: "));
          Serial.println(v.length() == 0 ? "(empty)" : v);
        } else {
          Serial.println(F("[SAVE] API key unchanged (mask ignored)."));
        }
      } else {
        doc[n] = v;
      }
    }

    bool newCountdownEnabled = (request->hasParam("countdownEnabled", true) && (request->getParam("countdownEnabled", true)->value() == "true" || request->getParam("countdownEnabled", true)->value() == "on" || request->getParam("countdownEnabled", true)->value() == "1"));
    String countdownDateStr = request->hasParam("countdownDate", true) ? request->getParam("countdownDate", true)->value() : "";
    String countdownTimeStr = request->hasParam("countdownTime", true) ? request->getParam("countdownTime", true)->value() : "";
    String countdownLabelStr = request->hasParam("countdownLabel", true) ? request->getParam("countdownLabel", true)->value() : "";
    bool newIsDramaticCountdown = (request->hasParam("isDramaticCountdown", true) && (request->getParam("isDramaticCountdown", true)->value() == "true" || request->getParam("isDramaticCountdown", true)->value() == "on" || request->getParam("isDramaticCountdown", true)->value() == "1"));

    time_t newTargetTimestamp = 0;
    if (newCountdownEnabled && countdownDateStr.length() > 0 && countdownTimeStr.length() > 0) {
      int year = countdownDateStr.substring(0, 4).toInt();
      int month = countdownDateStr.substring(5, 7).toInt();
      int day = countdownDateStr.substring(8, 10).toInt();
      int hour = countdownTimeStr.substring(0, 2).toInt();
      int minute = countdownTimeStr.substring(3, 5).toInt();

      struct tm tm;
      tm.tm_year = year - 1900;
      tm.tm_mon = month - 1;
      tm.tm_mday = day;
      tm.tm_hour = hour;
      tm.tm_min = minute;
      tm.tm_sec = 0;
      tm.tm_isdst = -1;

      newTargetTimestamp = mktime(&tm);
      if (newTargetTimestamp == (time_t)-1) {
        Serial.println("[SAVE] Error converting countdown date/time to timestamp.");
        newTargetTimestamp = 0;
      } else {
        Serial.printf("[SAVE] Converted countdown target: %s -> %lu\n", countdownDateStr.c_str(), newTargetTimestamp);
      }
    }

    JsonObject countdownObj = doc["countdown"].to<JsonObject>();
    countdownObj["enabled"] = newCountdownEnabled;
    countdownObj["targetTimestamp"] = newTargetTimestamp;
    countdownObj["label"] = countdownLabelStr;
    countdownObj["isDramaticCountdown"] = newIsDramaticCountdown;

    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.printf("[SAVE] LittleFS total bytes: %llu, used bytes: %llu\n", LittleFS.totalBytes(), LittleFS.usedBytes());

    if (LittleFS.exists("/config.json")) {
      Serial.println(F("[SAVE] Renaming /config.json to /config.bak"));
      LittleFS.rename("/config.json", "/config.bak");
    }
    File f = LittleFS.open("/config.json", "w");
    if (!f) {
      Serial.println(F("[SAVE] ERROR: Failed to open /config.json for writing!"));
      JsonDocument errorDoc;
      errorDoc[F("error")] = "Failed to write config file.";
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }

    size_t bytesWritten = serializeJson(doc, f);
    Serial.printf("[SAVE] Bytes written to /config.json: %u\n", bytesWritten);
    f.close();
    Serial.println(F("[SAVE] /config.json file closed."));

    File verify = LittleFS.open("/config.json", "r");
    if (!verify) {
      Serial.println(F("[SAVE] ERROR: Failed to open /config.json for reading during verification!"));
      JsonDocument errorDoc;
      errorDoc[F("error")] = "Verification failed: Could not re-open config file.";
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }

    while (verify.available()) {
      verify.read();
    }
    verify.seek(0);

    JsonDocument test;
    DeserializationError err = deserializeJson(test, verify);
    verify.close();

    if (err) {
      Serial.print(F("[SAVE] Config corrupted after save: "));
      Serial.println(err.f_str());
      JsonDocument errorDoc;
      errorDoc[F("error")] = String("Config corrupted. Reboot cancelled. Error: ") + err.f_str();
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }

    Serial.println(F("[SAVE] Config verification successful."));
    JsonDocument okDoc;
    if (doc["hostname"].is<const char *>()) {
      deviceHostname = doc["hostname"].as<String>();
    }
    strlcpy(customMessage, doc["customMessage"] | "", sizeof(customMessage));
    okDoc[F("message")] = "Saved successfully. Rebooting...";
    String response;
    serializeJson(okDoc, response);
    request->send(200, "application/json", response);
    Serial.println(F("[WEBSERVER] Sending success response and scheduling reboot..."));

    request->onDisconnect([]() {
      Serial.println(F("[WEBSERVER] Client disconnected, rebooting ESP..."));
      saveUptime();
      isRebooting = true;
      delay(100);  // ensure file is written
      ESP.restart();
    });
  });

  server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /restore"));
    if (LittleFS.exists("/config.bak")) {
      File src = LittleFS.open("/config.bak", "r");
      if (!src) {
        Serial.println(F("[WEBSERVER] Failed to open /config.bak"));
        JsonDocument errorDoc;
        errorDoc[F("error")] = "Failed to open backup file.";
        String response;
        serializeJson(errorDoc, response);
        request->send(500, "application/json", response);
        return;
      }
      File dst = LittleFS.open("/config.json", "w");
      if (!dst) {
        src.close();
        Serial.println(F("[WEBSERVER] Failed to open /config.json for writing"));
        JsonDocument errorDoc;
        errorDoc[F("error")] = "Failed to open config for writing.";
        String response;
        serializeJson(errorDoc, response);
        request->send(500, "application/json", response);
        return;
      }

      while (src.available()) {
        dst.write(src.read());
      }
      src.close();
      dst.close();

      JsonDocument okDoc;
      okDoc[F("message")] = "✅ Backup restored! Device will now reboot.";
      String response;
      serializeJson(okDoc, response);
      request->send(200, "application/json", response);
      request->onDisconnect([]() {
        Serial.println(F("[WEBSERVER] Rebooting after restore..."));
        saveUptime();
        delay(100);  // ensure file is written
        ESP.restart();
      });

    } else {
      Serial.println(F("[WEBSERVER] No backup found"));
      JsonDocument errorDoc;
      errorDoc[F("error")] = "No backup found.";
      String response;
      serializeJson(errorDoc, response);
      request->send(404, "application/json", response);
    }
  });

  server.on("/ap_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print(F("[WEBSERVER] Request: /ap_status. isAPMode = "));
    Serial.println(isAPMode);
    String json = "{\"isAP\": ";
    json += (isAPMode) ? "true" : "false";
    json += "}";
    request->send(200, "application/json", json);
  });

  auto setHandler = [](AsyncWebServerRequest *request) {
    String action = request->url().substring(5);  // strips "/set_" → e.g. "brightness"
    String value = "";
    if (request->hasParam("value", true)) {
      value = request->getParam("value", true)->value();
    } else if (request->params() > 0) {
      value = request->getParam(0)->value();
    }
    executeAction(action, value);
    request->send(200, "application/json", "{\"ok\":true}");
  };

  server.on("/set_brightness", HTTP_POST, setHandler);
  server.on("/set_flip", HTTP_POST, setHandler);
  server.on("/set_twelvehour", HTTP_POST, setHandler);
  server.on("/set_dayofweek", HTTP_POST, setHandler);
  server.on("/set_showdate", HTTP_POST, setHandler);
  server.on("/set_humidity", HTTP_POST, setHandler);
  server.on("/set_colon_blink", HTTP_POST, setHandler);
  server.on("/set_language", HTTP_POST, setHandler);
  server.on("/set_weatherdesc", HTTP_POST, setHandler);
  server.on("/set_units", HTTP_POST, setHandler);
  server.on("/set_countdown_enabled", HTTP_POST, setHandler);
  server.on("/set_dramatic_countdown", HTTP_POST, setHandler);
  server.on("/set_clock_only_dimming", HTTP_POST, setHandler);

  // --- Custom Message Endpoint ---
  server.on("/set_custom_message", HTTP_ANY, [](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      AsyncWebServerResponse *response = request->beginResponse(200);
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-Source");
      request->send(response);
      return;
    }
    handleCustomMessageLogic(request);
  });


  // --- Physical Buttons: get config ---
  server.on("/get_buttons", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray used = doc["usedPins"].to<JsonArray>();
    used.add(CLK_PIN);
    used.add(CS_PIN);
    used.add(DATA_PIN);
    JsonArray btns = doc["buttons"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
      JsonObject b = btns.add<JsonObject>();
      b["pin"] = btnCfg[i].pin;
      b["shortAction"] = btnCfg[i].shortAct;
      b["longAction"] = btnCfg[i].longAct;
    }
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  // --- Physical Buttons: save config (config.json, no reboot) ---
  server.on("/save_buttons", HTTP_POST, [](AsyncWebServerRequest *request) {
    int forbidden[] = { CLK_PIN, CS_PIN, DATA_PIN };
    int newPins[4];
    String newShort[4], newLong[4];

    for (int i = 0; i < 4; i++) {
      String idx = String(i + 1);
      int pin = request->hasParam("btn" + idx + "_pin", true)
                  ? request->getParam("btn" + idx + "_pin", true)->value().toInt()
                  : -1;
      for (int f : forbidden) {
        if (pin == f) {
          pin = -1;
          break;
        }
      }
      newPins[i] = pin;
      newShort[i] = request->hasParam("btn" + idx + "_short", true)
                      ? request->getParam("btn" + idx + "_short", true)->value()
                      : "";
      newLong[i] = request->hasParam("btn" + idx + "_long", true)
                     ? request->getParam("btn" + idx + "_long", true)->value()
                     : "";
    }
    // Reject duplicate pins between buttons
    for (int i = 0; i < 4; i++)
      for (int j = i + 1; j < 4; j++)
        if (newPins[i] >= 0 && newPins[i] == newPins[j]) newPins[j] = -1;

    // Load existing config.json and patch the buttons key
    JsonDocument doc;
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      deserializeJson(doc, configFile);
      configFile.close();
    }

    doc.remove("buttons");
    JsonArray arr = doc["buttons"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
      JsonObject b = arr.add<JsonObject>();
      b["pin"] = newPins[i];
      b["shortAction"] = newShort[i];
      b["longAction"] = newLong[i];
      // Update in-memory state immediately
      btnCfg[i].pin = newPins[i];
      btnCfg[i].shortAct = newShort[i];
      btnCfg[i].longAct = newLong[i];
    }

    if (LittleFS.exists("/config.json")) LittleFS.rename("/config.json", "/config.bak");
    File f = LittleFS.open("/config.json", "w");
    if (f) {
      serializeJson(doc, f);
      f.close();
    }

    setupButtons();  // apply new pinMode() without reboot
    Serial.println(F("[BUTTON] Config saved to config.json."));
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      AsyncWebServerResponse *response = request->beginResponse(200);
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-Source");
      request->send(response);
      return;
    }
    handleCaptivePortal(request);
  });

  server.on(
    "/action", HTTP_ANY, [](AsyncWebServerRequest *request) {
      if (request->method() == HTTP_OPTIONS) {
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-Source");
        request->send(response);
        return;
      }

      // Capture 'message' from either URL (GET) or Body (POST)
      if (request->hasArg("message")) {
        handleCustomMessageLogic(request);
      } else {
        // Handle other actions (brightness, etc.)
        if (request->params() > 0) {
          executeAction(request->getParam(0)->name(), request->getParam(0)->value());
          request->send(200, "text/plain", "OK");
        } else {
          request->send(400, "text/plain", "No parameters found");
        }
      }
    },
    NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      // This empty body handler is CRITICAL for AsyncWebServer
      // to parse 'application/x-www-form-urlencoded' POST data!
    });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    int scanStatus = WiFi.scanComplete();

    // -2 means scan not triggered, -1 means scan in progress
    if (scanStatus < -1 || scanStatus == WIFI_SCAN_FAILED) {
      // Start the asynchronous scan
      WiFi.scanNetworks(true);
      request->send(202, "application/json", "{\"status\":\"processing\"}");
    } else if (scanStatus == -1) {
      // Scan is currently running
      request->send(202, "application/json", "{\"status\":\"processing\"}");
    } else {
      // Scan finished (scanStatus >= 0)
      String json = "[";
      for (int i = 0; i < scanStatus; ++i) {
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i));
        json += "}";
        if (i < scanStatus - 1) json += ",";
      }
      json += "]";

      // Clean up scan results from memory
      WiFi.scanDelete();
      request->send(200, "application/json", json);
    }
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    int scanStatus = WiFi.scanComplete();

    // -2 means scan not triggered, -1 means scan in progress
    if (scanStatus < -1 || scanStatus == WIFI_SCAN_FAILED) {
      // Start the asynchronous scan
      WiFi.scanNetworks(true);
      request->send(202, "application/json", "{\"status\":\"processing\"}");
    } else if (scanStatus == -1) {
      // Scan is currently running
      request->send(202, "application/json", "{\"status\":\"processing\"}");
    } else {
      // Scan finished (scanStatus >= 0)
      String json = "[";
      for (int i = 0; i < scanStatus; ++i) {
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i));
        json += "}";
        if (i < scanStatus - 1) json += ",";
      }
      json += "]";

      // Clean up scan results from memory
      WiFi.scanDelete();
      request->send(200, "application/json", json);
    }
  });

  server.on("/ip", HTTP_GET, [](AsyncWebServerRequest *request) {
    String ip;

    if (WiFi.getMode() == WIFI_AP) {
      ip = WiFi.softAPIP().toString();  // usually 192.168.4.1
    } else if (WiFi.isConnected()) {
      ip = WiFi.localIP().toString();
    } else {
      ip = "—";
    }

    request->send(200, "text/plain", ip);
  });

  server.on("/uptime", HTTP_GET, [](AsyncWebServerRequest *request) {
    // 1. Get Total Lifetime (from LittleFS)
    unsigned long totalSeconds = 0;
    if (LittleFS.exists("/uptime.dat")) {
      File f = LittleFS.open("/uptime.dat", "r");
      if (f) {
        totalSeconds = f.readString().toInt();
        f.close();
      }
    }

    // 2. Calculate Session Uptime (Time since boot)
    unsigned long sessionSeconds = millis() / 1000;

    // 3. Build the combined JSON
    String json = "{";
    json += "\"hostname\":\"" + deviceHostname + "\",";
    json += "\"total_seconds\":" + String(totalSeconds) + ",";
    json += "\"total_formatted\":\"" + formatUptime(totalSeconds) + "\",";
    json += "\"session_seconds\":" + String(sessionSeconds) + ",";
    json += "\"session_formatted\":\"" + formatUptime(sessionSeconds) + "\",";
    json += "\"version\":\"" FIRMWARE_VERSION "\"";
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;

    // --- Identity ---
    doc["id"] = deviceHostname;
    doc["version"] = FIRMWARE_VERSION;
    doc["hardware"] = "MAX7219_FC16";
#if defined(ESP32)
    doc["board"] = "ESP32";
#elif defined(ESP8266)
      doc["board"] = "ESP8266";
#else
      doc["board"] = "unknown";
#endif

    // --- Display & Mode ---
    doc["displayMode"] = displayMode;
    doc["displayBusy"] = (displayMode == 6 || displayMode == 7);
    doc["allowInterrupt"] = allowInterrupt;

    switch (displayMode) {
      case 0: doc["mode"] = "clock"; break;
      case 1: doc["mode"] = "weather"; break;
      case 2: doc["mode"] = "weather_desc"; break;
      case 3: doc["mode"] = "countdown"; break;
      case 5: doc["mode"] = "date"; break;
      case 6: doc["mode"] = "message"; break;
      case 7: doc["mode"] = "timer"; break;
      default: doc["mode"] = "cycling"; break;
    }

    doc["message"] = (strlen(customMessage) > 0) ? customMessage : "";
    doc["displayOff"] = displayOff;
    doc["brightness"] = brightness;
    doc["lastBrightnessBeforeOff"] = lastBrightnessBeforeOff;


    // --- Runtime ---
    doc["device_runtime"] = formatTotalRuntime();
    doc["session_runtime"] = millis() / 1000;
    doc["wifi_signal"] = WiFi.RSSI();
    doc["mdns_url"] = String(deviceHostname) + ".local";
    doc["time_synced"] = ntpSyncSuccessful;

    // --- Local Time & Epoch ---
    time_t nowTime = time(nullptr);
    struct tm timeinfo;
    localtime_r(&nowTime, &timeinfo);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    doc["localTime"] = String(buffer);
    doc["epochTime"] = static_cast<uint32_t>(nowTime);

    // --- Countdown ---
    JsonObject cd = doc["countdown"].to<JsonObject>();
    cd["enabled"] = countdownEnabled;
    cd["targetTimestamp"] = countdownTargetTimestamp;
    cd["label"] = String(countdownLabel);
    cd["isDramatic"] = isDramaticCountdown;

    long long remaining = static_cast<long long>(countdownTargetTimestamp) - static_cast<long long>(nowTime);
    cd["remaining"] = countdownEnabled ? (remaining > 0 ? remaining : 0) : 0;

    doc["countdownEnabled"] = countdownEnabled;
    doc["countdownLabel"] = String(countdownLabel);

    // --- Weather ---
    JsonObject weather = doc["weather"].to<JsonObject>();

    if (weatherAvailable && currentTemp.length() > 0) {
      weather["currentTemperature"] = String(currentTemp).toInt();
    } else {
      weather["currentTemperature"] = JsonVariant();
    }
    weather["weatherDescription"] = (weatherAvailable && weatherDescription.length() > 0)
                                      ? weatherDescription : JsonVariant();
    weather["icon"] = (weatherAvailable && weatherIcon.length() > 0)
                        ? weatherIcon : JsonVariant();
    weather["currentHumidity"] = (weatherAvailable && currentHumidity >= 0)
                                   ? currentHumidity : JsonVariant();
    weather["fresh"] = weatherFetched;
    weather["sunriseHour"] = weatherAvailable ? sunriseHour : JsonVariant();
    weather["sunriseMinute"] = weatherAvailable ? sunriseMinute : JsonVariant();
    weather["sunsetHour"] = weatherAvailable ? sunsetHour : JsonVariant();
    weather["sunsetMinute"] = weatherAvailable ? sunsetMinute : JsonVariant();
    weather["apparentTemperature"] = (weatherAvailable && apparentTemperature.length() > 0)
                                         ? String(apparentTemperature).toInt() : JsonVariant();
    weather["weatherCode"] = (weatherAvailable && currentWeatherCode >= 0)
                               ? currentWeatherCode : JsonVariant();
    weather["provider"] = String(weatherProvider);

    // --- Saved Config ---
    JsonObject config = doc["config"].to<JsonObject>();
    config["ssid"] = String(ssid);
    config["openWeatherApiKey"] = (strlen(openWeatherApiKey) > 0) ? "***HIDDEN***" : "";
    config["openWeatherCity"] = String(openWeatherCity);
    config["weatherUnits"] = String(weatherUnits);
    config["weatherProvider"] = String(weatherProvider);
    config["weatherLatitude"] = String(weatherLatitude);
    config["weatherLongitude"] = String(weatherLongitude);
    config["clockDuration"] = clockDuration;
    config["weatherDuration"] = weatherDuration;
    config["dateDuration"] = dateDuration;
    config["timeZone"] = String(timeZone);
    config["language"] = String(language);
    config["flipDisplay"] = flipDisplay;
    config["twelveHourToggle"] = twelveHourToggle;
    config["showDate"] = showDate;
    config["showHumidity"] = showHumidity;
    config["ntpServer1"] = String(ntpServer1);

    config["ntpServer2"] = String(ntpServer2);

    // --- Dimming ---
    JsonObject dimming = doc["dimming"].to<JsonObject>();
    dimming["dimmingEnabled"] = dimmingEnabled;
    dimming["dimStartHour"] = dimStartHour;
    dimming["dimStartMinute"] = dimStartMinute;
    dimming["dimEndHour"] = dimEndHour;
    dimming["dimEndMinute"] = dimEndMinute;
    dimming["autoDimmingEnabled"] = autoDimmingEnabled;
    dimming["clockOnlyDuringDimming"] = clockOnlyDuringDimming;


    // --- Pins ---
    JsonObject pins = doc["pins"].to<JsonObject>();
    pins["clk"] = CLK_PIN;
    pins["cs"] = CS_PIN;
    pins["data"] = DATA_PIN;

    // --- buttons ---
    JsonArray buttons = doc["buttons"].to<JsonArray>();

    for (int i = 0; i < 4; i++) {
      JsonObject b = buttons.add<JsonObject>();
      b["pin"] = btnCfg[i].pin;
      b["shortAction"] = btnCfg[i].shortAct;
      b["longAction"] = btnCfg[i].longAct;
    }

    String response;
    serializeJsonPretty(doc, response);
    AsyncWebServerResponse *res = request->beginResponse(200, "application/json", response);
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    request->send(res);
  });

  server.on("/export", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /export"));

    File f;
    if (LittleFS.exists("/config.json")) {
      f = LittleFS.open("/config.json", "r");
      Serial.println(F("[EXPORT] Using /config.json"));
    } else if (LittleFS.exists("/config.bak")) {
      f = LittleFS.open("/config.bak", "r");
      Serial.println(F("[EXPORT] /config.json not found, using /config.bak"));
    } else {
      request->send(404, "application/json", "{\"error\":\"No config found\"}");
      return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.print(F("[EXPORT] Error parsing config: "));
      Serial.println(err.f_str());
      request->send(500, "application/json", "{\"error\":\"Failed to parse config\"}");
      return;
    }

    // Only sanitize if NOT in AP mode
    if (!isAPMode) {
      doc["ssid"] = "********";
      doc["password"] = "********";
      doc["openWeatherApiKey"] = "********************************";
    }

    doc["mode"] = isAPMode ? "ap" : "sta";

    String jsonOut;
    serializeJsonPretty(doc, jsonOut);

    AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", jsonOut);
    resp->addHeader("Content-Disposition", "attachment; filename=\"config.json\"");
    request->send(resp);
  });

  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
      <head>
        <meta charset="UTF-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1" />
         <style>
            html{
                background: linear-gradient(135deg, #081f56 0%, #110f2e 50%, #441a65 100%);
                height: 100%;
            }

            body {
                border: solid 1px rgba(255, 255, 255, 0.12);
                transition: opacity 0.6s cubic-bezier(.4, 0, .2, 1);
                max-width: 300px;
                margin: 4rem auto;
                background: rgba(255, 255, 255, 0.04);
                border-radius: 24px;
                text-align: center;
                font-family: Roboto, system-ui;
                /* margin: 0; */
                padding: 2rem 1rem;
                color: #ffffff;
                background-repeat: no-repeat, repeat, repeat;
                line-height: 1.5;
                -webkit-font-smoothing: antialiased;
                -moz-osx-font-smoothing: grayscale;
                box-shadow: 0 10px 36px 0 rgba(40, 170, 255, 0.11), 0 2px 8px 0 rgba(44, 70, 110, 0.08);
              }
            
            h3 {
              margin-top: 0;
              }

            input::file-selector-button {
              background: #0ea5e9;
              color: white;
              padding: 0.9rem 1.8rem;
              font-size: 1rem;
              font-weight: 600;
              border: none;
              border-radius: 999px;
              cursor: pointer;
              text-align: center;
              transition: background 0.25s, transform 0.15s 
              ease-in-out;
              margin-right: 0.5rem;
            }
          </style>
      </head>
      <body>
        <h3>Upload config.json</h3>
        <form method="POST" action="/upload" enctype="multipart/form-data">
          <input type="file" name="file" accept=".json" id="fileInput" onchange="this.form.submit()">
        </form>
      </body>
    </html>
  )rawliteral";
    request->send(200, "text/html", html);
  });

  server.on(
    "/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
      String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
        <head>
          <meta charset="UTF-8" />
          <meta name="viewport" content="width=device-width, initial-scale=1" />
          <title>Upload Successful</title>
          <meta http-equiv="refresh" content="1; url=/" />
          <style>
            html{
                background: linear-gradient(135deg, #081f56 0%, #110f2e 50%, #441a65 100%);
                height: 100%;
            }

            body {
                border: solid 1px rgba(255, 255, 255, 0.12);
                transition: opacity 0.6s cubic-bezier(.4, 0, .2, 1);
                max-width: 300px;
                margin: 4rem auto;
                background: rgba(255, 255, 255, 0.04);
                border-radius: 24px;
                text-align: center;
                font-family: Roboto, system-ui;
                /* margin: 0; */
                padding: 2rem 1rem;
                color: #ffffff;
                background-repeat: no-repeat, repeat, repeat;
                line-height: 1.5;
                -webkit-font-smoothing: antialiased;
                -moz-osx-font-smoothing: grayscale;
                box-shadow: 0 10px 36px 0 rgba(40, 170, 255, 0.11), 0 2px 8px 0 rgba(44, 70, 110, 0.08);
              }
            
            h3 {
              margin-top: 0;
              }

            input::file-selector-button {
              background: #0ea5e9;
              color: white;
              padding: 0.9rem 1.8rem;
              font-size: 1rem;
              font-weight: 600;
              border: none;
              border-radius: 999px;
              cursor: pointer;
              text-align: center;
              transition: background 0.25s, transform 0.15s 
              ease-in-out;
              margin-right: 0.5rem;
            }
          </style>
        </head>
        <body>
          <h3>File uploaded successfully!</h3>
          <p>Returning to main page...</p>
        </body>
      </html>
    )rawliteral";
      request->send(200, "text/html", html);
      // Restart after short delay to let browser handle redirect
      request->onDisconnect([]() {
        delay(500);  // ensure response is sent
        ESP.restart();
      });
    },
    [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      static File f;
      if (index == 0) {
        f = LittleFS.open("/config.json", "w");  // start new file
      }
      if (f) f.write(data, len);  // write chunk
      if (final) f.close();       // finish file
    });

  server.on("/get_version", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"board\":\"" + String(BOARD_TYPE) + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/perform_update", HTTP_GET, [](AsyncWebServerRequest *request) {
    otaState.updating = true;

    // Immediate UI Feedback
    P.displayClear();
    P.setTextAlignment(PA_CENTER);
    delay(100);
    P.print((char)172);  // Show your download/update icon

    request->send(200, "application/json", "{\"status\":\"ready\"}");
  });

  server.on(
    "/upload_ota", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!Update.hasError()) {
        request->send(200, "text/plain", "OK");
        // Set flags to reboot in the main loop
        otaState.restartPending = true;
        otaState.restartRequestedAt = millis();
      } else {
        request->send(200, "text/plain", "FAIL");
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      // This runs for every chunk of data received
      if (!index) {
        Serial.printf("[OTA] Start: %s\n", filename.c_str());

#ifdef ESP8266
        Update.runAsync(true);  // Critical: Prevent __yield panic on ESP8266
#endif

        // Calculate max available space for the firmware
        uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSpace, U_FLASH)) {
          Update.printError(Serial);
        }
      }

      if (!Update.hasError()) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
        }
      }

      if (final) {
        if (Update.end(true)) {
          Serial.printf("[OTA] Finished: %u bytes\n", index + len);
          // CREATE THE "SUCCESS" FLAG
          File f = LittleFS.open("/update_success.txt", "w");
          if (f) {
            f.print("1");
            f.close();
          }
        } else {
          Update.printError(Serial);
        }
      }
    });

  server.on("/factory_reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    // If not in AP mode, block and return a 403 response
    if (!isAPMode) {
      request->send(403, "text/plain", "Factory reset only allowed in AP mode.");
      Serial.println(F("[RESET] Factory reset attempt blocked (not in AP mode)."));
      return;
    }
    const char *FACTORY_RESET_HTML = R"rawliteral(
      <!DOCTYPE html>
      <html>
        <head>
          <meta charset="UTF-8" />
          <meta name="viewport" content="width=device-width, initial-scale=1" />
          <title>Resetting Device</title>
          <style>
            html{
                background: linear-gradient(135deg, #081f56 0%, #110f2e 50%, #441a65 100%);
                height: 100%;
            }

            body {
                border: solid 1px rgba(255, 255, 255, 0.12);
                transition: opacity 0.6s cubic-bezier(.4, 0, .2, 1);
                max-width: 300px;
                margin: 4rem auto;
                background: rgba(255, 255, 255, 0.04);
                border-radius: 24px;
                text-align: center;
                font-family: Roboto, system-ui;
                /* margin: 0; */
                padding: 2rem 1rem;
                color: #ffffff;
                background-repeat: no-repeat, repeat, repeat;
                line-height: 1.5;
                -webkit-font-smoothing: antialiased;
                -moz-osx-font-smoothing: grayscale;
                box-shadow: 0 10px 36px 0 rgba(40, 170, 255, 0.11), 0 2px 8px 0 rgba(44, 70, 110, 0.08);
              }

            h3 { margin-top: 0; color: #ff9999; }
            p { font-size: 1.1em; }
            .warning { font-size: 1.2em; font-weight: bold; color: #fff; margin-top: 15px; }
          </style>
        </head>
        <body>
          <h3>Factory Reset Initiated</h3>
          <p>All saved configuration and Wi-Fi credentials are now being erased.</p>
          <hr style="margin: 15px 0; border: 0; border-top: 1px solid rgba(255,255,255,0.2);">
          <p class="warning"><span style="color: yellow;">⚠️</span> ACTION REQUIRED</p>
          <p>
            The device is rebooting and will be temporarily offline for about <strong>45 seconds</strong>.
            <br><br>
            <strong>Your browser will disconnect automatically.</strong>
          </p>
          <p>
            <strong>Next steps:</strong>
            <br>1. Wait about 45 seconds for the reboot to finish.<br>
            2. Reconnect your PC or phone to the Wi-Fi network: <strong>ESPTimeCast</strong>.<br>
            3. Open your browser and go to <strong>192.168.4.1</strong> to continue setup.
          </p>
        </body>
      </html>
    )rawliteral";
    request->send(200, "text/html", FACTORY_RESET_HTML);
    Serial.println(F("[RESET] Factory reset requested, initiating cleanup..."));

    // Use onDisconnect() to ensure the HTTP response is fully sent before the disruptive actions
    request->onDisconnect([]() {
      // Small delay to ensure the response buffer is flushed before file ops
      delay(500);

      // --- Remove configuration and uptime files ---
      const char *filesToRemove[] = { "/config.json", "/uptime.dat", "/index.html" };
      for (auto &file : filesToRemove) {
        if (LittleFS.exists(file)) {
          if (LittleFS.remove(file)) {
            Serial.printf("[RESET] Deleted %s\n", file);
          } else {
            Serial.printf("[RESET] ERROR deleting %s\n", file);
          }
        } else {
          Serial.printf("[RESET] %s not found, skipping delete.\n", file);
        }
      }

// --- Clear Wi-Fi credentials ---
#if defined(ESP8266)
      WiFi.disconnect(true);  // true = wipe credentials
#elif defined(ESP32)
        WiFi.disconnect(true, true);  // (erase=true, wifioff=true)
#endif

      Serial.println(F("[RESET] Factory defaults restored. Rebooting..."));
      delay(500);
      ESP.restart();
    });
  });

  server.onNotFound(handleCaptivePortal);
  server.begin();
  Serial.println(F("[WEBSERVER] Web server started"));
}

void handleCaptivePortal(AsyncWebServerRequest *request) {
  String uri = request->url();

  // Never interfere with real UI or API
  if (
    uri == "/" || uri == "/index.html" || uri.startsWith("/config") || uri.startsWith("/hostname") || uri.startsWith("/ip") || uri.endsWith(".json") || uri.endsWith(".js") || uri.endsWith(".css") || uri.endsWith(".png") || uri.endsWith(".ico")) {
    return;  // let normal handlers serve it
  }

  // Known captive portal probes → redirect
  if (
    uri == "/generate_204" || uri == "/gen_204" || uri == "/fwlink" || uri == "/hotspot-detect.html" || uri == "/ncsi.txt" || uri == "/cp/success.txt" || uri == "/library/test/success.html") {
    if (isAPMode) {
      IPAddress apIP = WiFi.softAPIP();
      String redirectUrl = "http://" + apIP.toString() + "/";
      //Serial.printf("[WEBSERVER] Captive probe %s → redirect\n", uri.c_str());
      request->redirect(redirectUrl);
      return;
    }
  }

  // Unknown URLs in AP mode → redirect (helps odd OSes like /chat)
  if (isAPMode) {
    IPAddress apIP = WiFi.softAPIP();
    String redirectUrl = "http://" + apIP.toString() + "/";
    Serial.printf("[WEBSERVER] Captive fallback redirect: %s\n", uri.c_str());
    request->redirect(redirectUrl);
    return;
  }

  // STA mode fallback
  request->send(404, "text/plain", "Not found");
}


String cleanTextForDisplay(String str) {
  // Serbian Cyrillic → Latin
  str.replace("а", "a");
  str.replace("б", "b");
  str.replace("в", "v");
  str.replace("г", "g");
  str.replace("д", "d");
  str.replace("ђ", "dj");
  str.replace("е", "e");
  str.replace("ё", "e");  // Russian
  str.replace("ж", "z");
  str.replace("з", "z");
  str.replace("и", "i");
  str.replace("й", "j");  // Russian
  str.replace("ј", "j");  // Serbian
  str.replace("к", "k");
  str.replace("л", "l");
  str.replace("љ", "lj");
  str.replace("м", "m");
  str.replace("н", "n");
  str.replace("њ", "nj");
  str.replace("о", "o");
  str.replace("п", "p");
  str.replace("р", "r");
  str.replace("с", "s");
  str.replace("т", "t");
  str.replace("ћ", "c");
  str.replace("у", "u");
  str.replace("ф", "f");
  str.replace("х", "h");
  str.replace("ц", "c");
  str.replace("ч", "c");
  str.replace("џ", "dz");
  str.replace("ш", "s");
  str.replace("щ", "sh");  // Russian
  str.replace("ы", "y");   // Russian
  str.replace("э", "e");   // Russian
  str.replace("ю", "yu");  // Russian
  str.replace("я", "ya");  // Russian

  // Latin diacritics → ASCII
  str.replace("å", "a");
  str.replace("ä", "a");
  str.replace("à", "a");
  str.replace("á", "a");
  str.replace("â", "a");
  str.replace("ã", "a");
  str.replace("ā", "a");
  str.replace("ă", "a");
  str.replace("ą", "a");

  str.replace("æ", "ae");

  str.replace("ç", "c");
  str.replace("č", "c");
  str.replace("ć", "c");

  str.replace("ď", "d");

  str.replace("é", "e");
  str.replace("è", "e");
  str.replace("ê", "e");
  str.replace("ë", "e");
  str.replace("ē", "e");
  str.replace("ė", "e");
  str.replace("ę", "e");

  str.replace("ğ", "g");
  str.replace("ģ", "g");

  str.replace("ĥ", "h");

  str.replace("í", "i");
  str.replace("ì", "i");
  str.replace("î", "i");
  str.replace("ï", "i");
  str.replace("ī", "i");
  str.replace("į", "i");

  str.replace("ĵ", "j");

  str.replace("ķ", "k");

  str.replace("ľ", "l");
  str.replace("ł", "l");

  str.replace("ñ", "n");
  str.replace("ń", "n");
  str.replace("ņ", "n");

  str.replace("ó", "o");
  str.replace("ò", "o");
  str.replace("ô", "o");
  str.replace("ö", "o");
  str.replace("õ", "o");
  str.replace("ø", "o");
  str.replace("ō", "o");
  str.replace("ő", "o");

  str.replace("œ", "oe");

  str.replace("ŕ", "r");

  str.replace("ś", "s");
  str.replace("š", "s");
  str.replace("ș", "s");
  str.replace("ŝ", "s");

  str.replace("ß", "ss");

  str.replace("ť", "t");
  str.replace("ț", "t");

  str.replace("ú", "u");
  str.replace("ù", "u");
  str.replace("û", "u");
  str.replace("ü", "u");
  str.replace("ū", "u");
  str.replace("ů", "u");
  str.replace("ű", "u");

  str.replace("ŵ", "w");

  str.replace("ý", "y");
  str.replace("ÿ", "y");
  str.replace("ŷ", "y");

  str.replace("ž", "z");
  str.replace("ź", "z");
  str.replace("ż", "z");

  str.toUpperCase();

  String result = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    unsigned char c = (unsigned char)str.charAt(i);  // Use unsigned for safety

    // MASTER FILTER: Expanded for Modern Smart Home Notifications
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr(" !.?:,;'\"-_+%/[]()#&¥$ ;|°@^~*=<>\t\n\r\\{}", c)) {
      result += (char)c;
    }
  }
  return result;  // Return the cleaned string
}











// -----------------------------------------------------------------------------
// Pomodoro Command Handler
// Syntax: [POMODORO W-B]  where W = work minutes (1-60), B = break minutes (1-60)
//         [POMODORO STOP]    — stop and return to clock
//         [POMODORO RESTART] — restart from the beginning of the work phase
// -----------------------------------------------------------------------------
bool handlePomodoroCommand(String cmd) {
  cmd.toUpperCase();
  if (cmd.indexOf("[POMODORO") == -1 && cmd.indexOf("[POM]") == -1 && cmd.indexOf("[POM ") == -1) return false;
  // Prevent starting or restarting Pomodoro while Clock-only Dimming is active
  if (clockOnlyDuringDimming && dimActive) {
    bool isStop = cmd.indexOf("[POM STOP]") != -1 || cmd.indexOf("[POMODORO STOP]") != -1;
    bool isPause = cmd.indexOf("[POM PAUSE]") != -1 || cmd.indexOf("[POMODORO PAUSE]") != -1;
    bool isResume = cmd.indexOf("[POM RESUME]") != -1 || cmd.indexOf("[POMODORO RESUME]") != -1;
    if (!isStop && !isPause && !isResume) {
      Serial.println(F("[POMODORO] Ignored: Clock-only Dimming is active."));
      return true;
    }
  }
  // Prevent starting/restarting Pomodoro while a protected message is active
  if (!allowInterrupt) {
    bool isStop = cmd.indexOf("[POM STOP]") != -1 || cmd.indexOf("[POMODORO STOP]") != -1;
    bool isPause = cmd.indexOf("[POM PAUSE]") != -1 || cmd.indexOf("[POMODORO PAUSE]") != -1;
    bool isResume = cmd.indexOf("[POM RESUME]") != -1 || cmd.indexOf("[POMODORO RESUME]") != -1;
    if (!isStop && !isPause && !isResume) {
      Serial.println(F("[POMODORO] Ignored: Protected message is active."));
      return true;
    }
  }

  // --- STOP ---
  if (cmd.indexOf("[POM STOP]") != -1 || cmd.indexOf("[POMODORO STOP]") != -1) {
    isPomodoroActive = false;
    pomodoroInBreak = false;
    timerActive = false;
    timerFinished = false;
    timerPaused = false;
    isStopwatch = false;
    displayMode = 0;
    prevDisplayMode = 6;
    clockScrollDone = false;
    forceMessageRestart = true;
    lastSwitch = millis();
    Serial.println(F("[POMODORO] Stopped. Returning to Clock."));
    return true;
  }

  // --- RESTART — restart from the beginning of the work phase ---
  if (cmd.indexOf("[POM RESTART]") != -1 || cmd.indexOf("[POMODORO RESTART]") != -1) {
    if (isPomodoroActive && pomodoroWorkMs > 0) {
      pomodoroInBreak = false;
      isStopwatch = false;
      timerOriginalDuration = pomodoroWorkMs;
      timerEndTime = millis() + pomodoroWorkMs;
      timerActive = true;
      timerPaused = false;
      timerFinished = false;
      timerSubState = 0;
      displayMode = 7;
      allowInterrupt = true;
      lastSwitch = millis();
      forceMessageRestart = true;
      Serial.println(F("[POMODORO] Restarted — beginning work phase."));
      return true;
    }
    return false;
  }

  // --- PAUSE ---
  if (cmd.indexOf("[POM PAUSE]") != -1 || cmd.indexOf("[POMODORO PAUSE]") != -1) {
    Serial.println(F("[POMODORO] Pause forwarded to timer."));
    return handleTimerCommand("[TIMER PAUSE]");
  }

  // --- RESUME ---
  if (cmd.indexOf("[POM RESUME]") != -1 || cmd.indexOf("[POMODORO RESUME]") != -1) {
    Serial.println(F("[POMODORO] Resume forwarded to timer."));
    return handleTimerCommand("[TIMER RESUME]");
  }

  // Shortcuts: [POM] or bare [POMODORO] (no params) default to 25-5-15
  String trimmed = cmd;
  trimmed.trim();
  if (cmd.indexOf("[POM]") != -1 || (cmd.indexOf("[POMODORO]") != -1 && cmd.indexOf("[POMODORO ") == -1)) {
    pomodoroWorkMs = 25 * 60000UL;
    pomodoroBreakMs = 5 * 60000UL;
    pomodoroLongBreakMs = 15 * 60000UL;
    pomodoroSession = 1;
    isPomodoroActive = true;
    pomodoroInBreak = false;
    isStopwatch = false;
    timerOriginalDuration = pomodoroWorkMs;
    timerEndTime = millis() + pomodoroWorkMs;
    timerActive = true;
    timerPaused = false;
    timerFinished = false;
    timerSubState = 0;
    displayMode = 7;
    allowInterrupt = true;
    lastSwitch = millis();
    forceMessageRestart = true;
    Serial.println(F("[POMODORO] Default 25-5-15 started."));
    return true;
  }

  // --- [POMODORO W-B] — parse work and break minutes ---
  int pomStart = cmd.indexOf("[POMODORO ") + 10;
  int pomEnd = cmd.indexOf("]", pomStart);
  if (pomEnd == -1) return false;
  String params = cmd.substring(pomStart, pomEnd);
  params.trim();

  int dash1 = params.indexOf('-');
  if (dash1 == -1) return false;
  int dash2 = params.indexOf('-', dash1 + 1);

  int workMin = params.substring(0, dash1).toInt();
  int breakMin = params.substring(dash1 + 1, dash2 == -1 ? params.length() : dash2).toInt();
  int longMin = (dash2 != -1) ? params.substring(dash2 + 1).toInt() : 15;

  if (workMin < 1) workMin = 1;
  if (workMin > 60) workMin = 60;
  if (breakMin < 1) breakMin = 1;
  if (breakMin > 60) breakMin = 60;
  if (longMin < 1) longMin = 1;
  if (longMin > 60) longMin = 60;

  pomodoroWorkMs = (unsigned long)workMin * 60000UL;
  pomodoroBreakMs = (unsigned long)breakMin * 60000UL;
  pomodoroLongBreakMs = (unsigned long)longMin * 60000UL;
  pomodoroSession = 1;
  isPomodoroActive = true;
  pomodoroInBreak = false;

  // Start the work-phase countdown
  isStopwatch = false;
  timerOriginalDuration = pomodoroWorkMs;
  timerEndTime = millis() + pomodoroWorkMs;
  timerActive = true;
  timerPaused = false;
  timerFinished = false;
  timerSubState = 0;
  displayMode = 7;
  allowInterrupt = true;
  lastSwitch = millis();
  forceMessageRestart = true;

  Serial.printf("[POMODORO] Started: %d min work / %d min break\n", workMin, breakMin);
  return true;
}

// Timer Helper
bool handleTimerCommand(String cmd) {
  cmd.toUpperCase();
  if (cmd.indexOf("[STOPWATCH]") != -1) cmd = "[TIMER STOPWATCH]";
  if (cmd.indexOf("[STOPWATCH PAUSE]") != -1) cmd = "[TIMER PAUSE]";
  if (cmd.indexOf("[STOPWATCH RESUME]") != -1) cmd = "[TIMER RESUME]";
  if (cmd.indexOf("[STOPWATCH STOP]") != -1) cmd = "[TIMER STOP]";
  if (cmd.indexOf("[STOPWATCH CLEAR]") != -1) cmd = "[TIMER CLEAR]";
  if (cmd.indexOf("[STOPWATCH RESTART]") != -1) cmd = "[TIMER STOPWATCH]";
  if (cmd.indexOf("[STOPWATCH RESET]") != -1) cmd = "[TIMER RESET]";
  if (cmd.indexOf("[TIMER") == -1) return false;

  int start = cmd.indexOf("[TIMER") + 6;
  int end = cmd.indexOf("]", start);
  String payload = cmd.substring(start, end);
  payload.trim();

  // Prevent starting or resuming timers while Clock-only Dimming is active
  if (clockOnlyDuringDimming && dimActive) {
    if (payload != "STOP" && payload != "CANCEL" && payload != "CLEAR" && payload != "PAUSE" && payload != "RESET") {
      Serial.println(F("[TIMER] Ignored: Clock-only Dimming is active."));
      return true;
    }
  }

  // Prevent starting or resuming timers while a protected message is active
  if (!allowInterrupt) {
    if (payload != "STOP" && payload != "CANCEL" && payload != "CLEAR" && payload != "PAUSE" && payload != "RESET") {
      Serial.println(F("[TIMER] Ignored: Protected message is active."));
      return true;
    }
  }

  if (payload == "STOP" || payload == "CANCEL") {
    if (isStopwatch) {
      // Stopwatch: STOP = PAUSE (keeps the time frozen; use CLEAR to reset)
      if (!timerPaused) {
        timerPaused = true;
        timerRemainingAtPause = millis() - timerEndTime;
      }
      Serial.println(F("[STOPWATCH] Paused. Use CLEAR to reset to zero."));
    } else {
      // Countdown timer / Pomodoro: STOP = fully stop and return to Clock
      timerActive = false;
      timerFinished = false;
      timerPaused = false;
      isStopwatch = false;
      displayMode = 0;
      prevDisplayMode = 6;
      clockScrollDone = false;
      forceMessageRestart = true;
      lastSwitch = millis();
      Serial.println(F("[TIMER] Stopped. Returning to Clock."));
    }
    return true;
  }

  if (payload == "RESET") {
    if (isStopwatch) {
      timerPaused = true;
      timerRemainingAtPause = 0;
      timerFinished = false;
      timerEndTime = millis();  // zero elapsed time
      displayMode = 7;
      allowInterrupt = true;
      lastSwitch = millis();
      forceMessageRestart = true;
      Serial.println(F("[STOPWATCH] Reset."));
      return true;
    }
    return false;
  }

  if (payload == "CLEAR") {
    bool wasStopwatch = isStopwatch;
    timerActive = false;
    timerPaused = false;
    timerFinished = false;
    isStopwatch = false;
    displayMode = 0;
    prevDisplayMode = 6;
    clockScrollDone = false;
    forceMessageRestart = true;
    lastSwitch = millis();
    Serial.println(wasStopwatch ? F("[STOPWATCH] Cleared. Returning to Clock.") : F("[TIMER] Cleared. Returning to Clock."));
    return true;
  }

  if (payload == "PAUSE") {
    if (timerActive && !timerPaused && !timerFinished) {
      timerPaused = true;
      timerRemainingAtPause = isStopwatch ? (millis() - timerEndTime) : (timerEndTime - millis());
    }
    return true;
  }

  if (payload == "RESUME" || payload == "START") {
    if (timerActive && timerPaused) {
      timerEndTime = isStopwatch ? millis() - timerRemainingAtPause   // rewind start time
                                 : millis() + timerRemainingAtPause;  // existing countdown logic
      timerPaused = false;
    }
    return true;
  }

  if (payload == "RESTART") {
    if (timerOriginalDuration > 0) {
      isStopwatch = false;
      timerEndTime = millis() + timerOriginalDuration;
      timerActive = true;
      timerPaused = false;
      timerFinished = false;
      displayMode = 7;
      allowInterrupt = true;
      timerSubState = 0;
      lastSwitch = millis();
      forceMessageRestart = true;
      return true;
    }
    return false;
  }

  if (payload == "STOPWATCH") {

    // Already running -> Pause
    if (timerActive && isStopwatch && !timerPaused) {
      timerPaused = true;
      timerRemainingAtPause = millis() - timerEndTime;
      return true;
    }

    // Already paused -> Resume
    if (timerActive && isStopwatch && timerPaused) {
      timerEndTime = millis() - timerRemainingAtPause;
      timerPaused = false;
      return true;
    }

    // Not active -> Start a new stopwatch
    isStopwatch = true;
    isPomodoroActive = false;
    pomodoroInBreak = false;
    timerActive = true;
    timerPaused = false;
    timerFinished = false;
    timerEndTime = millis();
    timerSubState = 0;
    displayMode = 7;
    allowInterrupt = true;
    lastSwitch = millis();
    forceMessageRestart = true;
    return true;
  }

  long totalMs = 0;
  String val = "";
  for (unsigned int i = 0; i < payload.length(); i++) {
    char c = payload.charAt(i);
    if (isDigit(c)) val += c;
    else {
      long num = val.toInt();
      if (c == 'H') totalMs += num * 3600000;
      else if (c == 'M') totalMs += num * 60000;
      else if (c == 'S') totalMs += num * 1000;
      val = "";
    }
  }
  if (val.length() > 0 && totalMs == 0) totalMs = val.toInt() * 60000;

  if (totalMs > 86400000) totalMs = 86400000;  // 24h Cap

  if (totalMs > 0) {
    isStopwatch = false;
    timerOriginalDuration = totalMs;
    timerEndTime = millis() + totalMs;
    isPomodoroActive = false;
    pomodoroInBreak = false;
    timerActive = true;
    timerPaused = false;
    timerFinished = false;
    timerSubState = 0;
    displayMode = 7;
    allowInterrupt = true;
    lastSwitch = millis();
    forceMessageRestart = true;
    return true;
  }
  return false;
}

void scheduleRuntimeConfigSave() {
  configDirty = true;
  lastBrightnessChange = millis();
}

//Actions handler
void executeAction(const String &action, const String &value) {
  if (value.length() > 0) {
    Serial.println("[ACTION] " + action + " = " + value);
  } else {
    Serial.println("[ACTION] " + action);
  }
  String v = value;
  v.trim();
  v.toLowerCase();
  bool hasValue = v.length() > 0;
  bool boolVal = (v == "1" || v == "true" || v == "on");

  // Display actions
  if (action == "next_mode") {
    advanceDisplayMode(true);

  } else if (action == "prev_mode") {
    previousDisplayMode(true);

  } else if (action == "brightness") {
    handleBrightnessChange(value.toInt(), false);

  } else if (action == "brightness_up") {
    handleBrightnessChange(displayOff ? lastBrightnessBeforeOff : constrain(brightness + 1, 0, 15), false);

  } else if (action == "brightness_down") {
    if (!displayOff) { handleBrightnessChange(constrain(brightness - 1, 0, 15), false); }

  } else if (action == "display_off") {
    handleBrightnessChange(-1, false);

  } else if (action == "display_on") {
    handleBrightnessChange(lastBrightnessBeforeOff, false);

  } else if (action == "flip" || action == "flip_display") {
    flipDisplay = hasValue ? boolVal : !flipDisplay;
    P.setZoneEffect(0, flipDisplay, PA_FLIP_UD);
    P.setZoneEffect(0, flipDisplay, PA_FLIP_LR);
    scheduleRuntimeConfigSave();

  } else if (action == "twelvehour" || action == "twelve_hour") {
    twelveHourToggle = hasValue ? boolVal : !twelveHourToggle;
    if (!hasValue && displayMode != 0) { goToMode("0"); }
    scheduleRuntimeConfigSave();

  } else if (action == "dayofweek" || action == "show_dayofweek") {
    showDayOfWeek = hasValue ? boolVal : !showDayOfWeek;
    if (!hasValue && displayMode != 0) { goToMode("0"); }
    scheduleRuntimeConfigSave();

  } else if (action == "showdate" || action == "show_date") {
    bool newVal = hasValue ? boolVal : !showDate;
    if (showDate && !newVal && displayMode == 5) { advanceDisplayMode(true); }
    showDate = newVal;
    if (!hasValue && showDate) { goToMode("5"); }
    scheduleRuntimeConfigSave();

  } else if (action == "colon_blink" || action == "animated_seconds") {
    colonBlinkEnabled = hasValue ? boolVal : !colonBlinkEnabled;
    if (!hasValue && displayMode != 0) { goToMode("0"); }
    scheduleRuntimeConfigSave();

  } else if (action == "humidity" || action == "show_humidity") {
    showHumidity = hasValue ? boolVal : !showHumidity;
    if (!hasValue) { goToMode("1"); }  // show the change on weather
    scheduleRuntimeConfigSave();

  } else if (action == "weatherdesc" || action == "show_weather_desc") {
    bool newVal = hasValue ? boolVal : !showWeatherDescription;
    if (showWeatherDescription && !newVal && displayMode == 2) { advanceDisplayMode(true); }
    showWeatherDescription = newVal;
    if (!hasValue && showWeatherDescription) { goToMode("2"); }  // only jump when turning ON
    scheduleRuntimeConfigSave();

  } else if (action == "units" || action == "imperial") {
    bool isImperial = (action == "imperial") ? (!hasValue ? true : boolVal) : (hasValue ? boolVal : strcmp(weatherUnits, "imperial") != 0);
    if (isImperial) {
      strcpy(weatherUnits, "imperial");
      tempSymbol = '\007';
    } else {
      strcpy(weatherUnits, "metric");
      tempSymbol = '\006';
    }
    networkData.requestWeatherRefresh();
    if (!hasValue) { goToMode("1"); }  // show the change on weather
    scheduleRuntimeConfigSave();

  } else if (action == "metric") {
    strcpy(weatherUnits, "metric");
    tempSymbol = '\006';
    networkData.requestWeatherRefresh();
    if (!hasValue) { goToMode("1"); }  // show the change on weather
    scheduleRuntimeConfigSave();

  } else if (action == "countdown_enabled" || action == "countdown") {
    bool newVal = hasValue ? boolVal : !countdownEnabled;
    if (countdownEnabled && !newVal && displayMode == 3) { advanceDisplayMode(true); }
    countdownEnabled = newVal;

  } else if (action == "dramatic_countdown") {
    bool newVal = hasValue ? boolVal : !isDramaticCountdown;
    if (isDramaticCountdown != newVal) {
      isDramaticCountdown = newVal;
      saveCountdownConfig(countdownEnabled, countdownTargetTimestamp, countdownLabel);
    }

  } else if (action == "clock_only_dimming") {
    clockOnlyDuringDimming = hasValue ? boolVal : !clockOnlyDuringDimming;
    scheduleRuntimeConfigSave();

  } else if (action == "go_to_mode") {
    goToMode(value);

  } else if (action == "enable_rotation") {
    rotationEnabled = hasValue ? boolVal : !rotationEnabled;
    if (rotationEnabled) advanceDisplayMode();

  }

  // Timer commands
  else if (action == "timer_stop" || action == "timer_cancel") {
    handleTimerCommand("[TIMER STOP]");
  } else if (action == "timer_pause") {
    handleTimerCommand("[TIMER PAUSE]");
  } else if (action == "timer_resume" || action == "timer_start") {
    handleTimerCommand("[TIMER RESUME]");
  } else if (action == "timer_restart") {
    if (!(clockOnlyDuringDimming && dimActive)) handleTimerCommand("[TIMER RESTART]");
  } else if (action == "timer") {
    if (!(clockOnlyDuringDimming && dimActive)) handleTimerCommand("[TIMER " + value + "]");
  } else if (action == "stopwatch" || action == "stopwatch_start") {
    if (!(clockOnlyDuringDimming && dimActive))
      handleTimerCommand("[STOPWATCH]");
  } else if (action == "stopwatch_pause") {
    handleTimerCommand("[STOPWATCH PAUSE]");
  } else if (action == "stopwatch_resume") {
    handleTimerCommand("[STOPWATCH RESUME]");
  } else if (action == "stopwatch_stop" || action == "stopwatch_cancel") {
    handleTimerCommand("[STOPWATCH STOP]");
  } else if (action == "stopwatch_restart") {
    if (!(clockOnlyDuringDimming && dimActive))
      handleTimerCommand("[STOPWATCH RESTART]");
  } else if (action == "stopwatch_reset") {
    handleTimerCommand("[STOPWATCH RESET]");
  } else if (action == "stopwatch_clear") {
    handleTimerCommand("[STOPWATCH CLEAR]");
  } else if (action == "pomodoro_start" || action == "pom") {
    if (!(clockOnlyDuringDimming && dimActive)) handlePomodoroCommand("[POMODORO]");
  } else if (action == "pomodoro") {
    if (!(clockOnlyDuringDimming && dimActive)) handlePomodoroCommand("[POMODORO " + value + "]");
  } else if (action == "pomodoro_stop") {
    handlePomodoroCommand("[POMODORO STOP]");
  } else if (action == "pomodoro_restart") {
    if (!(clockOnlyDuringDimming && dimActive)) handlePomodoroCommand("[POMODORO RESTART]");
  } else if (action == "pomodoro_pause") {
    handleTimerCommand("[TIMER PAUSE]");
  } else if (action == "pomodoro_resume") {
    handleTimerCommand("[TIMER RESUME]");

  } else if (action == "restart") {
    otaState.restartPending = true;
    otaState.restartRequestedAt = millis();

  } else if (action == "save") {
    configDirty = true;

  } else if (action == "language") {
    String lang = value;
    lang.trim();
    lang.toLowerCase();
    strlcpy(language, lang.c_str(), sizeof(language));
    networkData.requestWeatherRefresh();
    advanceDisplayMode();
    scheduleRuntimeConfigSave();

  } else if (action == "clear_message") {
    allowInterrupt = true;
    forceMessageRestart = true;
    customMessage[0] = '\0';
    messageStartTime = 0;
    currentScrollCount = 0;
    messageDisplaySeconds = 0;
    messageScrollTimes = 0;

    if (strlen(lastPersistentMessage) > 0) {
      strlcpy(customMessage, lastPersistentMessage, sizeof(customMessage));
      messageScrollSpeed = GENERAL_SCROLL_SPEED;
      displayMode = 6;
      prevDisplayMode = 0;
      Serial.println(F("[MESSAGE] clear_message: restored persistent message"));
    } else {
      displayMode = 0;
      prevDisplayMode = 6;
      clockScrollDone = false;
      Serial.println(F("[MESSAGE] clear_message: no persistent message, returning to clock"));
    }

  } else {
    Serial.println("[ACTION] Unknown action: " + action);
  }
}

void handleBrightnessChange(int newBrightness, bool isFromUI) {
  // --- CASE 1: Turn Display OFF ---
  if (newBrightness == -1) {
    if (!displayOff) {
      if (brightness >= 0) lastBrightnessBeforeOff = brightness;
      P.displayShutdown(true);
      P.displayClear();
      displayOff = true;
      brightness = -1;
      configDirty = true;
      lastBrightnessChange = millis();
      Serial.printf("[BRIGHTNESS] Display turned OFF via %s\n", isFromUI ? "UI" : "API");
    }
    return;
  }

  // --- CASE 2: Turn Display ON or Adjust ---
  newBrightness = constrain(newBrightness, 0, 15);

  if (newBrightness != brightness || displayOff) {
    bool wakingUp = displayOff;
    brightness = newBrightness;
    configDirty = true;
    lastBrightnessChange = millis();
    P.setIntensity(brightness);

    if (wakingUp) {
      advanceDisplayMode(true);
      P.displayShutdown(false);
      P.displayClear();
      displayOff = false;
      Serial.printf("[BRIGHTNESS] Display woke from OFF via %s to %d\n", isFromUI ? "UI" : "API", brightness);
    } else {
      Serial.printf("[BRIGHTNESS] Intensity set to %d via %s\n", brightness, isFromUI ? "UI" : "API");
    }
  }
}

void resetModeRenderState() {
  clockScrollDone = false;
  descScrolling = false;
  descScrollEndTime = 0;
  descStartTime = 0;
}

void goToMode(const String &target) {
  DisplayMode parsedMode;
  if (!parseDisplayMode(target, parsedMode)) {
    Serial.printf("[DISPLAY] go_to_mode: invalid target '%s'\n", target.c_str());
    return;
  }

  const int targetMode = displayModeId(parsedMode);
  if (!isModeAvailable(parsedMode)) {
    Serial.printf("[DISPLAY] go_to_mode: unavailable target '%s'\n", target.c_str());
    return;
  }

  // ---- CLEANUP CURRENT MODE ----
  if (displayMode == 3) {
    countdownSegment = 0;
    segmentStartTime = 0;
    countdownShowFinishedMessage = false;
    hourglassPlayed = false;
  }

  displayController.select(parsedMode, millis());

  // ---- RESET TARGET MODE STATE ----
  if (targetMode == 0) {
    prevDisplayMode = 6;
    clockScrollDone = false;
    P.displayReset();
    P.displayClear();
    delay(100);
  }

  if (displayMode == 6 || displayMode == 2 || displayMode == 3) {
    P.displayReset();
    P.displayClear();
  }

  // ---- RESET TARGET MODE STATE ----
  if (targetMode == 3) {
    countdownSegment = 0;
    segmentStartTime = 0;
    countdownShowFinishedMessage = false;
    hourglassPlayed = false;
  }


  resetModeRenderState();

  const char *previousName = (prevDisplayMode >= 0 && prevDisplayMode < 8)
                               ? displayModeName(displayModeFromId(prevDisplayMode)) : "UNKNOWN";
  Serial.printf("[DISPLAY] go_to_mode: %s (from %s)\n", displayModeName(parsedMode), previousName);
}


void setupButtons() {
  for (int i = 0; i < 4; i++) {
    if (btnCfg[i].pin >= 0) {
      pinMode(btnCfg[i].pin, INPUT_PULLUP);
      Serial.printf("[BUTTON] Button %d ready on GPIO %d\n", i + 1, btnCfg[i].pin);
    }
  }
}

void handleButtons() {
  static unsigned long lastPress[4] = { 0, 0, 0, 0 };
  static unsigned long pressStart[4] = { 0, 0, 0, 0 };
  static bool lastState[4] = { HIGH, HIGH, HIGH, HIGH };
  static bool longFired[4] = { false, false, false, false };

  for (int i = 0; i < 4; i++) {
    if (btnCfg[i].pin < 0) continue;
    bool cur = digitalRead(btnCfg[i].pin);

    if (cur == LOW && lastState[i] == HIGH) {  // press down
      pressStart[i] = millis();
      longFired[i] = false;
    }
    if (cur == LOW && !longFired[i]) {  // held
      if (millis() - pressStart[i] >= BTN_LONG_MS) {
        longFired[i] = true;
        if (btnCfg[i].longAct.length() > 0) {
          Serial.printf("[BUTTON] Button %d LONG → %s\n", i + 1, btnCfg[i].longAct.c_str());
          executeAction(btnCfg[i].longAct, "");
        }
      }
    }
    if (cur == HIGH && lastState[i] == LOW) {  // release
      if (!longFired[i] && millis() - lastPress[i] > 200) {
        lastPress[i] = millis();
        if (btnCfg[i].shortAct.length() > 0) {
          Serial.printf("[BUTTON] Button %d SHORT → %s\n", i + 1, btnCfg[i].shortAct.c_str());
          executeAction(btnCfg[i].shortAct, "");
        }
      }
    }
    lastState[i] = cur;
  }
}


// -----------------------------------------------------------------------------
// Main setup() and loop()
// -----------------------------------------------------------------------------
/*
DisplayMode key:
  0: Clock
  1: Weather
  2: Weather Description
  3: Countdown
  4: Reserved (removed integration mode)
  5: Date
  6: Custom Message
*/
void setup() {
  // pinMode(BUTTON_PIN, INPUT_PULLUP);  // ← Uncomment if using button

  Serial.begin(115200);
  delay(1000);
  esp_log_level_set("esp_littlefs", ESP_LOG_NONE);
  Serial.println();
  Serial.println(F("[SETUP] Starting setup..."));
#if defined(ARDUINO_USB_MODE) || defined(USB_SERIAL)
  Serial.setTxTimeoutMs(50);
  Serial.println(F("[SERIAL] USB CDC detected — TX timeout enabled"));
  delay(500);
#endif
  Serial.println(F("[FS] Mounting LittleFS (auto-format enabled)..."));
  if (!LittleFS.begin(true)) {
    Serial.println(F("[ERROR] LittleFS mount failed even after format. Halting."));
    while (true) {
      delay(1000);
      yield();
    }
  }
  Serial.println(F("[FS] LittleFS mounted and ready."));
  loadUptime();
  const PinConfig pins = loadPinConfig({ L_CLK, L_CS, L_DATA });
  CLK_PIN = pins.clock;
  CS_PIN = pins.chipSelect;
  DATA_PIN = pins.data;
  new (&P) MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
  P.begin();
  P.setCharSpacing(0);
  P.setFont(mFactory);
  loadConfig();
  WeatherData initialWeather;
  initialWeather.available = weatherAvailable;
  initialWeather.fetched = weatherFetched;
  initialWeather.temperature = currentTemp;
  initialWeather.mainDescription = mainDesc;
  initialWeather.detailedDescription = detailedDesc;
  initialWeather.icon = weatherIcon;
  initialWeather.humidity = currentHumidity;
  initialWeather.apparentTemperature = apparentTemperature;
  initialWeather.weatherCode = currentWeatherCode;
  initialWeather.provider = weatherProvider;
  initialWeather.sunriseHour = sunriseHour;
  initialWeather.sunriseMinute = sunriseMinute;
  initialWeather.sunsetHour = sunsetHour;
  initialWeather.sunsetMinute = sunsetMinute;
  networkData.initialize(initialWeather);
  setupButtons();
  P.setIntensity(brightness);
  if (displayOff) {
    P.displayShutdown(true);
    Serial.println(F("[SETUP] Display restored as OFF"));
  } else {
    P.displayShutdown(false);
  }
  P.setZoneEffect(0, flipDisplay, PA_FLIP_UD);
  P.setZoneEffect(0, flipDisplay, PA_FLIP_LR);

  Serial.println(F("[SETUP] Parola (LED Matrix) initialized"));

#if defined(ESP32)
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    const char *name = nullptr;
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        name = "GOT_IP";
        lastWifiConnectTime = millis();
        Serial.println("[WIFI EVENT] Re-initializing mDNS due to new IP.");
        setupMDNS();
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        name = "DISCONNECTED";
        MDNS.end();
        Serial.println("[WIFI EVENT] mDNS stopped.");
        break;
      default: return;  // ignore all other events
    }
    Serial.printf("[WIFI EVENT] %s (%d)\n", name, event);
  });

#elif defined(ESP8266)
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  mConnectHandler = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &ev) {
    Serial.println("[WIFI EVENT] Connected");
  });
  mDisConnectHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &ev) {
    if (isRebooting) {
      Serial.println(F("[WIFI] Disconnect ignored during reboot."));
      return;
    }
    Serial.printf("[WIFI EVENT] Disconnected (Reason: %d)\n", ev.reason);
    MDNS.end();
  });
  mGotIpHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &ev) {
    Serial.printf("[WIFI EVENT] GOT_IP - IP: %s\n", ev.ip.toString().c_str());
    lastWifiConnectTime = millis();
    setupMDNS();
  });
#endif
  connectWiFi();
  if (isAPMode) {
    Serial.println(F("[SETUP] WiFi connection failed. Device is in AP Mode."));
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[SETUP] WiFi connected successfully to local network."));
  } else {
    Serial.println(F("[SETUP] WiFi state is uncertain after connection attempt."));
  }
  if (!isAPMode && WiFi.status() == WL_CONNECTED) {
    setupMDNS();
  }
  setupWebServer();
  Serial.println(F("[SETUP] Webserver setup complete"));
  Serial.println(F("[SETUP] Setup complete"));
  Serial.println();
  setupTime();
#if !defined(ARDUINO_USB_MODE)
  printConfigToSerial();
#endif
  displayMode = 0;
  lastSwitch = millis() - (clockDuration - 500);
  lastColonBlink = millis();
  resetUptimeSession();
  saveUptime();
}

void advanceDisplayMode(bool forced) {
  if (!rotationEnabled && !forced) return;
  displayController.syncRotationIndex();
  // ---- DIMMING LOCK ----
  if (clockOnlyDuringDimming && dimActive) {
    if (displayMode != 0) {
      displayMode = 0;
      Serial.println(F("[DISPLAY] Dimming lock: Forcing Mode 0"));
    }
    return;
  }

  if (clockOnlyDuringDimming) {
    time_t now = time(nullptr);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    int curTotal = local_tm.tm_hour * 60 + local_tm.tm_min;
    int startTotal = -1, endTotal = -1;
    bool currentlyDimmed = false;

    if (autoDimmingEnabled) {
      startTotal = sunsetHour * 60 + sunsetMinute;
      endTotal = sunriseHour * 60 + sunriseMinute;
      currentlyDimmed = (startTotal < endTotal)
                          ? (curTotal >= startTotal && curTotal < endTotal)
                          : (curTotal >= startTotal || curTotal < endTotal);
    } else if (dimmingEnabled) {
      startTotal = dimStartHour * 60 + dimStartMinute;
      endTotal = dimEndHour * 60 + dimEndMinute;
      currentlyDimmed = (startTotal < endTotal)
                          ? (curTotal >= startTotal && curTotal < endTotal)
                          : (curTotal >= startTotal || curTotal < endTotal);
    }

    if (currentlyDimmed) {
      displayMode = 0;
      lastSwitch = millis();
      Serial.println(F("[DISPLAY] Staying in CLOCK (dimming active)"));
      return;
    }
  }

  // ---- RESET COUNTDOWN STATE WHEN LEAVING ----
  if (displayMode == 3) {
    countdownSegment = 0;
    segmentStartTime = 0;
  }


  // ---- SAFE ROTATION ENGINE ----
  for (size_t i = 0; i < kDisplayModeOrderCount; i++) {
    const DisplayMode nextDisplayMode = displayController.nextCandidate();


    if (isModeAvailable(nextDisplayMode)) {
      if (displayMode == 6 || displayMode == 2 || displayMode == 3) {
        // P.displayReset();
        // P.displayClear();
      }

      displayController.select(nextDisplayMode, millis());

      const char *previousName = (prevDisplayMode >= 0 && prevDisplayMode < 8)
                                   ? displayModeName(displayModeFromId(prevDisplayMode)) : "UNKNOWN";
      Serial.printf("[DISPLAY] Switching to display mode: %s (from %s)\n",
                    displayModeName(nextDisplayMode), previousName);

      resetModeRenderState();
      return;
    }
  }

  // ---- FALLBACK ----
  displayMode = 0;
  Serial.println(F("[DISPLAY] Fallback to CLOCK"));
}

void previousDisplayMode(bool forced) {
  if (!rotationEnabled && !forced) return;
  displayController.syncRotationIndex();
  if (clockOnlyDuringDimming && dimActive) {
    displayMode = 0;
    return;
  }

  if (displayMode == 3) {
    countdownSegment = 0;
    segmentStartTime = 0;
  }


  for (size_t i = 0; i < kDisplayModeOrderCount; i++) {
    const DisplayMode nextDisplayMode = displayController.previousCandidate();

    if (isModeAvailable(nextDisplayMode)) {
      if (displayMode == 6 || displayMode == 2 || displayMode == 3) {
        P.displayReset();
        P.displayClear();
      }

      displayController.select(nextDisplayMode, millis());

      const char *previousName = (prevDisplayMode >= 0 && prevDisplayMode < 8)
                                   ? displayModeName(displayModeFromId(prevDisplayMode)) : "UNKNOWN";
      Serial.printf("[DISPLAY] Switching to display mode: %s (from %s)\n",
                    displayModeName(nextDisplayMode), previousName);

      resetModeRenderState();
      return;
    }
  }

  displayMode = 0;
  Serial.println(F("[DISPLAY] Fallback to CLOCK"));
}

bool isModeAvailable(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::Clock: return true;
    case DisplayMode::Weather: return weatherConfigurationValid();
    case DisplayMode::WeatherDescription:
      return showWeatherDescription && weatherAvailable && weatherDescription.length() > 0;
    case DisplayMode::Countdown:
      return countdownEnabled && !countdownFinished && ntpSyncSuccessful;
    case DisplayMode::Reserved: return false;
    case DisplayMode::Date: return showDate && ntpSyncSuccessful;
    case DisplayMode::Message: return strlen(customMessage) > 0;
    case DisplayMode::Timer: return timerActive;
  }
  return false;
}

unsigned long displayModeDuration(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::Clock: return clockDuration;
    case DisplayMode::Weather: return weatherDuration;
    case DisplayMode::Date: return dateDuration;
    default: return 0;
  }
}

//config save after countdown finishes
bool saveCountdownConfig(bool enabled, time_t targetTimestamp, const String &label) {
  JsonDocument doc;

  File configFile = LittleFS.open("/config.json", "r");
  if (configFile) {
    DeserializationError err = deserializeJson(doc, configFile);
    configFile.close();
    if (err) {
      Serial.print(F("[saveCountdownConfig] Error parsing config.json: "));
      Serial.println(err.f_str());
      return false;
    }
  }

  JsonObject countdownObj = doc["countdown"].to<JsonObject>();
  countdownObj["enabled"] = enabled;
  countdownObj["targetTimestamp"] = targetTimestamp;
  countdownObj["label"] = label;
  countdownObj["isDramaticCountdown"] = isDramaticCountdown;
  doc.remove("countdownEnabled");
  doc.remove("countdownDate");
  doc.remove("countdownTime");
  doc.remove("countdownLabel");

  if (LittleFS.exists("/config.json")) {
    LittleFS.rename("/config.json", "/config.bak");
  }

  File f = LittleFS.open("/config.json", "w");
  if (!f) {
    Serial.println(F("[saveCountdownConfig] ERROR: Cannot write to /config.json"));
    return false;
  }

  size_t bytesWritten = serializeJson(doc, f);
  f.close();

  Serial.printf("[saveCountdownConfig] Config updated. %u bytes written.\n", bytesWritten);
  return true;
}

bool saveConfigRuntime() {

  JsonDocument doc;

  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println(F("[CONFIG] Failed to open config for reading"));
    return false;
  }

  DeserializationError err = deserializeJson(doc, configFile);
  configFile.close();

  if (err) {
    Serial.print(F("[CONFIG] JSON parse error: "));
    Serial.println(err.f_str());
    return false;
  }

  // Update only runtime-changing fields
  doc["brightness"] = brightness;
  doc["displayOff"] = displayOff;
  doc["lastBrightnessBeforeOff"] = lastBrightnessBeforeOff;
  doc["flipDisplay"] = flipDisplay;
  doc["twelveHourToggle"] = twelveHourToggle;
  doc["showDayOfWeek"] = showDayOfWeek;
  doc["showDate"] = showDate;
  doc["showHumidity"] = showHumidity;
  doc["colonBlinkEnabled"] = colonBlinkEnabled;
  doc["clockOnlyDuringDimming"] = clockOnlyDuringDimming;
  doc["showWeatherDescription"] = showWeatherDescription;
  doc["weatherUnits"] = weatherUnits;
  doc["language"] = language;

  File configFileWrite = LittleFS.open("/config.json", "w");
  if (!configFileWrite) {
    Serial.println(F("[CONFIG] Failed to open config for writing"));
    return false;
  }

  serializeJsonPretty(doc, configFileWrite);
  configFileWrite.close();

  Serial.println(F("[CONFIG] Runtime config saved"));
  return true;
}

void loop() {
  handleButtons();

  // --- WIFI RECONNECTION ---
  static unsigned long lastReconnectAttempt = 0;
  static unsigned long reconnectInterval = 5000;
  static bool wasConnected = true;

  if (!isRebooting) {
    if (WiFi.status() != WL_CONNECTED) {
      if (wasConnected) {
        Serial.println(F("[WIFI] Connection lost. Will attempt reconnection..."));
        wasConnected = false;
        reconnectInterval = 5000;
      }

      if (millis() - lastReconnectAttempt > reconnectInterval) {
        lastReconnectAttempt = millis();

        Serial.printf(
          "[WIFI] Reconnecting... (next attempt in %lus)\n",
          min(reconnectInterval * 2, 300000UL) / 1000);

        WiFi.disconnect();
        delay(100);

#ifdef ESP8266
        WiFi.begin(ssid, password);
#else
        WiFi.reconnect();
#endif

        reconnectInterval = min(reconnectInterval * 2, 300000UL);
      }
    } else if (!wasConnected) {
      Serial.println(F("[WIFI] Reconnected!"));
      wasConnected = true;
      reconnectInterval = 5000;
    }
  }

  if (timerActive && (displayMode != 7 && displayMode != 6)) {
    if (!(clockOnlyDuringDimming && dimActive)) {
      displayMode = 7;
      timerSubState = 0;
      lastSwitch = millis();
      forceMessageRestart = true;
    }
  }
  // 1. REBOOT HANDLER: Execute the restart outside of the Async callback
  if (otaState.restartPending && (millis() - otaState.restartRequestedAt > 2000)) {
    Serial.println(F("[SYSTEM] Rebooting now..."));
    ESP.restart();
  }
  // 2. OTA LOCK: If updating, yield to WiFi and stop everything else
  if (otaState.updating) {
    yield();
    return;
  }
  if (isAPMode) {
    dnsServer.processNextRequest();
    if (credentialsExist()) {
      static unsigned long apStartTime = 0;
      if (apStartTime == 0) apStartTime = millis();  // Mark the start time once

      // 3 Minutes = 180000 ms
      if (millis() - apStartTime > 180000) {
        Serial.println(F("[WIFI] AP Timeout: Saved credentials found. Rebooting to retry connection..."));
        delay(500);
        ESP.restart();
      }
    }
    // AP Mode animation
    static unsigned long apAnimTimer = 0;
    static int apAnimFrame = 0;
    unsigned long now = millis();
    if (now - apAnimTimer > 750) {
      apAnimTimer = now;
      apAnimFrame++;
    }
    P.setTextAlignment(PA_CENTER);
    switch (apAnimFrame % 3) {
      case 0:
        {
          char buf[] = { 5, ' ', (char)169, '\0' };
          P.print(buf);
          break;
        }
      case 1:
        {
          char buf[] = { 5, ' ', (char)170, '\0' };
          P.print(buf);
          break;
        }
      case 2:
        {
          char buf[] = { 5, ' ', (char)171, '\0' };
          P.print(buf);
          break;
        }
    }
    yield();
    return;
  }

  static bool colonVisible = true;
  const unsigned long colonBlinkInterval = 800;
  if (millis() - lastColonBlink > colonBlinkInterval) {
    colonVisible = !colonVisible;
    lastColonBlink = millis();
  }

  static unsigned long ntpAnimTimer = 0;
  static int ntpAnimFrame = 0;
  static bool tzSetAfterSync = false;



  // -----------------------------
  // Dimming (auto + manual)
  // -----------------------------
  time_t now_time = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now_time, &timeinfo);
  int curHour = timeinfo.tm_hour;
  int curMinute = timeinfo.tm_min;
  int curTotal = curHour * 60 + curMinute;

  if (autoDimmingEnabled) {
    startTotal = sunsetHour * 60 + sunsetMinute;
    endTotal = sunriseHour * 60 + sunriseMinute;
  } else if (dimmingEnabled) {
    startTotal = dimStartHour * 60 + dimStartMinute;
    endTotal = dimEndHour * 60 + dimEndMinute;
  } else {
    startTotal = endTotal = -1;  // not used
  }

  // -----------------------------
  // Check if dimming should be active
  // -----------------------------
  if (autoDimmingEnabled || dimmingEnabled) {
    if (startTotal < endTotal) {
      dimActive = (curTotal >= startTotal && curTotal < endTotal);
    } else {
      dimActive = (curTotal >= startTotal || curTotal < endTotal);  // overnight
    }
  }

  // -----------------------------
  // Apply brightness / display on-off
  // -----------------------------
  static bool lastDimActive = false;  // remembers last state
  int targetBrightness = dimActive ? dimBrightness : brightness;

  // Log only when transitioning
  if (dimActive != lastDimActive) {
    if (dimActive) {
      if (autoDimmingEnabled)
        Serial.printf("[DISPLAY] Automatic dimming setting brightness to %d\n", targetBrightness);
      else if (dimmingEnabled)
        Serial.printf("[DISPLAY] Custom dimming setting brightness to %d\n", targetBrightness);
    } else {
      Serial.println(F("[DISPLAY] Waking display (dimming end)"));
    }
    lastDimActive = dimActive;
  }

  // Apply brightness or shutdown
  if (targetBrightness == -1) {
    if (!displayOff) {
      Serial.println(F("[DISPLAY] Turning display OFF (dimming -1)"));
      P.displayShutdown(true);
      P.displayClear();
      displayOff = true;
      displayOffByDimming = dimActive;
      displayOffByBrightness = !dimActive;
    }
  } else {
    if (displayOff && ((dimActive && displayOffByBrightness) || (!dimActive && displayOffByDimming))) {
      P.displayShutdown(false);
      displayOff = false;
      displayOffByDimming = false;
      displayOffByBrightness = false;
    }
    P.setIntensity(targetBrightness);
  }

  // Enforce "Clock only during dimming" if enabled
  if (clockOnlyDuringDimming && dimActive) {
    if (displayMode != 0) {
      prevDisplayMode = displayMode;
      displayMode = 0;
      lastSwitch = millis();
      Serial.println(F("[DISPLAY] Forcing CLOCK because 'Clock only during dimming' is enabled and dimming is active."));
    }
  }

  // --- IMMEDIATE COUNTDOWN FINISH TRIGGER ---
  if (countdownEnabled && !countdownFinished && ntpSyncSuccessful && countdownTargetTimestamp > 0 && now_time >= countdownTargetTimestamp) {
    countdownFinished = true;
    displayMode = 3;  // Let main loop handle animation + TIMES UP
    countdownShowFinishedMessage = true;
    hourglassPlayed = false;
    countdownFinishedMessageStartTime = millis();

    Serial.println("[SYSTEM] Countdown target reached! Switching to Mode 3 to display finish sequence.");
    yield();
  }


  // --- IP Display ---
  if (showingIp) {
    if (P.displayAnimate()) {
      ipDisplayCount++;
      if (ipDisplayCount < ipDisplayMax) {
        textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
        P.displayScroll(pendingIpToShow.c_str(), PA_CENTER, actualScrollDirection, 120);
      } else {
        showingIp = false;
        P.displayClear();
        delay(500);  // Blocking delay as in working copy
        displayMode = 0;
        lastSwitch = millis();
      }
    }
    yield();
    return;  // Exit loop early if showing IP
  }


  // --- BRIGHTNESS/OFF CHECK ---
  if (brightness == -1) {
    if (!displayOff) {
      Serial.println(F("[DISPLAY] Turning display OFF"));
      P.displayShutdown(true);  // fully off
      P.displayClear();
      displayOff = true;
    }
    yield();
  }


  // --- NTP State Machine ---
  switch (ntpState) {
    case NTP_IDLE: break;
    case NTP_SYNCING:
      {
        time_t now = time(nullptr);
        if (now > 1000) {  // NTP sync successful
          Serial.println(F("[TIME] NTP sync successful."));
          ntpSyncSuccessful = true;
          ntpState = NTP_SUCCESS;
        } else if (millis() - ntpStartTime > ntpTimeout || ntpRetryCount >= maxNtpRetries) {
          Serial.println(F("[TIME] NTP sync failed."));
          ntpSyncSuccessful = false;
          ntpState = NTP_FAILED;
        } else {
          // Periodically print a more descriptive status message
          if (millis() - lastNtpStatusPrintTime >= ntpStatusPrintInterval) {
            Serial.printf("[TIME] NTP sync in progress (attempt %d of %d)...\n", ntpRetryCount + 1, maxNtpRetries);
            lastNtpStatusPrintTime = millis();
          }
          // Still increment ntpRetryCount based on your original timing for the timeout logic
          // (even if you don't print a dot for every increment)
          if (millis() - ntpStartTime > ((unsigned long)(ntpRetryCount + 1) * 1000UL)) {
            ntpRetryCount++;
          }
        }
        break;
      }
    case NTP_SUCCESS:
      if (!tzSetAfterSync) {
        const char *posixTz = ianaToPosix(timeZone);
        setenv("TZ", posixTz, 1);
        tzset();
        tzSetAfterSync = true;
      }
      ntpAnimTimer = 0;
      ntpAnimFrame = 0;
      break;

    case NTP_FAILED:
      ntpAnimTimer = 0;
      ntpAnimFrame = 0;

      static unsigned long lastNtpRetryAttempt = 0;
      static bool firstRetry = true;

      if (lastNtpRetryAttempt == 0) {
        lastNtpRetryAttempt = millis();  // set baseline on first fail
      }

      unsigned long ntpRetryInterval = firstRetry ? 30000UL : 300000UL;  // first retry after 30s, after that every 5 minutes

      if (millis() - lastNtpRetryAttempt > ntpRetryInterval) {
        lastNtpRetryAttempt = millis();
        ntpRetryCount = 0;
        ntpStartTime = millis();
        ntpState = NTP_SYNCING;
        Serial.println(F("[TIME] Retrying NTP sync..."));

        firstRetry = false;
      }
      break;
  }


  // --- OUTBOUND DATA SERVICES ---
  static WeatherRequest weatherRequest;
  const WeatherProvider configuredProvider = parseWeatherProvider(String(weatherProvider));
  if (weatherRequest.provider != configuredProvider ||
      weatherRequest.apiKey != openWeatherApiKey ||
      weatherRequest.city != openWeatherCity ||
      weatherRequest.country != openWeatherCountry ||
      weatherRequest.latitude != weatherLatitude ||
      weatherRequest.longitude != weatherLongitude ||
      weatherRequest.units != weatherUnits ||
      weatherRequest.language != language ||
      weatherRequest.timezone != timeZone) {
    weatherRequest.provider = configuredProvider;
    weatherRequest.apiKey = openWeatherApiKey;
    weatherRequest.city = openWeatherCity;
    weatherRequest.country = openWeatherCountry;
    weatherRequest.latitude = weatherLatitude;
    weatherRequest.longitude = weatherLongitude;
    weatherRequest.units = weatherUnits;
    weatherRequest.language = language;
    weatherRequest.timezone = timeZone;
  }
  networkData.configure(weatherRequest);
  networkData.update(WiFi.status() == WL_CONNECTED, lastWifiConnectTime);

  if (networkData.takeWeatherUpdate()) {
    const WeatherData &weather = networkData.weather();
    weatherAvailable = weather.available;
    weatherFetched = weather.fetched;
    if (weather.fetched) {
      currentTemp = weather.temperature;
      currentHumidity = weather.humidity;
      apparentTemperature = weather.apparentTemperature;
      currentWeatherCode = weather.weatherCode;
      mainDesc = weather.mainDescription;
      detailedDesc = weather.detailedDescription;
      weatherIcon = weather.icon;
      const String cleanedDescription = cleanTextForDisplay(detailedDesc);
      weatherDescription = weatherIcon;
      if (weatherDescription.length() > 0 && cleanedDescription.length() > 0) weatherDescription += " ";
      weatherDescription += cleanedDescription;
      sunriseHour = weather.sunriseHour;
      sunriseMinute = weather.sunriseMinute;
      sunsetHour = weather.sunsetHour;
      sunsetMinute = weather.sunsetMinute;
      Serial.printf("[WEATHER] Description used: %s\n", weatherDescription.c_str());
      if (autoDimmingEnabled && sunriseHour >= 0 && sunsetHour >= 0) {
        saveSunriseSunsetToConfig(sunriseHour, sunriseMinute, sunsetHour, sunsetMinute);
      }
    }
  }


  // Run deferred config writes before mode renderers, many of which return early.
  if (configDirty && millis() - lastBrightnessChange > saveDelay) {
    saveConfigRuntime();
    configDirty = false;
    Serial.println(F("[CONFIG] Auto-saved"));
  }

  const String formattedTime = formatClockText(
    timeinfo, language, twelveHourToggle, showDayOfWeek, colonBlinkEnabled,
    colonVisible, useCustomFont);

  const DisplayMode activeMode = displayModeFromId(displayMode);
  const unsigned long activeModeDuration = displayModeDuration(activeMode);
  if (rotationEnabled && activeModeDuration > 0 &&
      millis() - lastSwitch > activeModeDuration) {
    advanceDisplayMode();
  }


  // --- CLOCK Display Mode ---
  if (displayMode == 0) {
    if (forceMessageRestart) {
      P.displayReset();
      P.displayClear();
      forceMessageRestart = false;
      clockScrollDone = false;  // Ensure it scrolls in
    }
    if (forceMessageRestart) return;
    P.setCharSpacing(0);

    // --- NTP SYNC ---
    if (ntpState == NTP_SYNCING) {
      P.setTextAlignment(PA_CENTER);
      if (ntpSyncSuccessful || ntpRetryCount >= maxNtpRetries || millis() - ntpStartTime > ntpTimeout) {
        ntpState = NTP_FAILED;
      } else if (millis() - ntpAnimTimer > 750) {
        if (forceMessageRestart) return;
        ntpAnimTimer = millis();
        switch (ntpAnimFrame % 3) {
          case 0:
            {
              String s = F("S Y N C ");
              s += char(174);
              P.print(s.c_str());
              break;
            }
          case 1:
            {
              String s = F("S Y N C ");
              s += char(175);
              P.print(s.c_str());
              break;
            }
          case 2:
            {
              String s = F("S Y N C ");
              s += char(186);
              P.print(s.c_str());
              break;
            }
        }
        ntpAnimFrame++;
      }
    }
    // --- NTP / WEATHER ERROR ---
    else if (!ntpSyncSuccessful) {
      if (forceMessageRestart) return;
      P.setTextAlignment(PA_CENTER);
      static unsigned long errorAltTimer = 0;
      static bool showNtpError = true;

      if (!ntpSyncSuccessful && !weatherAvailable) {
        if (millis() - errorAltTimer > 2000) {
          errorAltTimer = millis();
          showNtpError = !showNtpError;
        }
        if (showNtpError) {
          P.write(2);  // NTP error glyph
        } else {
          P.write(1);  // Weather error glyph
        }

      } else if (!ntpSyncSuccessful) {
        P.write(2);
      } else if (!weatherAvailable) {
        P.write(1);
      }
    }
    // --- DISPLAY CLOCK ---
    else {
      String timeString = formattedTime;
      if (showDayOfWeek && colonBlinkEnabled && !colonVisible) {
        timeString.replace(":", " ");
      }

      // --- SCROLL IN ONLY WHEN COMING FROM SPECIFIC MODES OR FIRST BOOT ---
      bool shouldScrollIn = false;
      if (prevDisplayMode == -1 || prevDisplayMode == 3) {
        shouldScrollIn = true;  // first boot or other special modes
      } else if (prevDisplayMode == 2 && weatherDescription.length() > 8) {
        shouldScrollIn = true;  // only scroll in if weather was scrolling
      } else if (prevDisplayMode == 6) {
        shouldScrollIn = true;  // scroll in when coming from custom message
      }

      if (shouldScrollIn && !clockScrollDone) {
        textEffect_t inDir = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);

        P.displayText(
          timeString.c_str(),
          PA_CENTER,
          GENERAL_SCROLL_SPEED,
          0,
          inDir,
          PA_NO_EFFECT);
        while (!P.displayAnimate()) {
          if (displayMode != 0) {
            clockScrollDone = false;
            return;
          }
          if (forceMessageRestart) {
            clockScrollDone = false;
            return;
          }
          yield();
        }
        // Only if we finish the while loop naturally do we mark it done
        clockScrollDone = true;
      } else {
        P.setTextAlignment(PA_CENTER);
        P.print(timeString);
      }
    }

    yield();
  } else {
    // --- leaving clock mode ---
    if (prevDisplayMode == 0) {
      clockScrollDone = false;  // reset for next time we enter clock
    }
  }


  // --- WEATHER Display Mode ---
  static bool weatherWasAvailable = false;
  if (displayMode == 1) {
    if (forceMessageRestart) return;
    P.setCharSpacing(1);
    P.setTextAlignment(PA_CENTER);
    if (weatherAvailable) {
      const String weatherDisplay = formatWeatherText(
        currentTemp, currentHumidity, showHumidity, tempSymbol);
      P.print(weatherDisplay.c_str());
      weatherWasAvailable = true;
    } else {
      if (weatherWasAvailable) {
        Serial.println(F("[DISPLAY] Weather not available, showing clock..."));
        weatherWasAvailable = false;
      }
      if (ntpSyncSuccessful) {
        String timeString = formattedTime;
        if (!colonVisible) timeString.replace(":", " ");
        P.setCharSpacing(0);
        P.print(timeString);
      } else {
        P.setCharSpacing(0);
        P.setTextAlignment(PA_CENTER);
        P.write(1);
      }
    }
    yield();
    return;
  }


  // Active description may become unavailable after a weather refresh failure or
  // runtime setting change. Never leave a self-timed mode without an exit path.
  if (displayMode == displayModeId(DisplayMode::WeatherDescription) &&
      !isModeAvailable(DisplayMode::WeatherDescription)) {
    advanceDisplayMode(true);
    yield();
    return;
  }

  // --- WEATHER DESCRIPTION Display Mode ---
  if (displayMode == 2) {
    P.setCharSpacing(1);
    P.setTextAlignment(PA_CENTER);
    if (forceMessageRestart) return;
    String desc = weatherDescription;

    // Add separation only when text already needs scrolling. Short WMO labels
    // must remain unpadded so they fit on one screen.
    const bool humidityVisible = showHumidity && weatherAvailable && weatherConfigurationValid();
    if (prevDisplayMode == 1 && humidityVisible && desc.length() > 8) {
      desc = "    " + desc;
    }

    // prepare safe buffer
    static char descBuffer[128];  // large enough for OWM translations
    desc.toCharArray(descBuffer, sizeof(descBuffer));

    if (desc.length() > 8) {
      if (!descScrolling) {
        textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
        P.displayScroll(descBuffer, PA_CENTER, actualScrollDirection, GENERAL_SCROLL_SPEED);
        descScrolling = true;
        descScrollEndTime = 0;  // reset end time at start
      }
      if (displayMode != 2) return;
      if (P.displayAnimate()) {
        if (descScrollEndTime == 0) {
          descScrollEndTime = millis();  // mark the time when scroll finishes
        }
        // wait small pause after scroll stops
        if (millis() - descScrollEndTime > descriptionScrollPause) {
          if (forceMessageRestart) return;
          descScrolling = false;
          descScrollEndTime = 0;
          advanceDisplayMode();
        }
      } else {
        descScrollEndTime = 0;  // reset if not finished
      }
      yield();
      return;
    } else {
      if (descStartTime == 0) {
        P.setTextAlignment(PA_CENTER);
        P.setCharSpacing(1);
        P.print(descBuffer);
        descStartTime = millis();
      }
      if (millis() - descStartTime > descriptionDuration) {
        descStartTime = 0;
        advanceDisplayMode();
      }
      if (forceMessageRestart) return;
      yield();
      return;
    }
  }


  // --- Countdown Display Mode ---
  if (displayMode == 3 && countdownEnabled && ntpSyncSuccessful) {
    if (forceMessageRestart) return;
    const unsigned long SEGMENT_DISPLAY_DURATION = 1500;  // 1.5 seconds for each static segment

    long timeRemaining = countdownTargetTimestamp - now_time;

    // --- Countdown Finished Logic ---
    // This part of the code remains unchanged.
    if (timeRemaining <= 0 || countdownShowFinishedMessage) {
      // NEW: Only show "TIMES UP" if countdown target timestamp is valid and expired
      time_t now = time(nullptr);
      if (countdownTargetTimestamp == 0 || countdownTargetTimestamp > now) {
        // Target invalid or in the future, don't show "TIMES UP" yet, advance display instead
        countdownShowFinishedMessage = false;
        countdownFinished = false;
        countdownFinishedMessageStartTime = 0;
        hourglassPlayed = false;  // Reset if we decide not to show it
        Serial.println("[COUNTDOWN-FINISH] Countdown target invalid or not reached yet, skipping 'TIMES UP'. Advancing display.");
        advanceDisplayMode();
        yield();
        return;
      }

      // Define these static variables here if they are not global (or already defined in your loop())
      static const char *flashFrames[] = { "\x08", "\x09" };
      static unsigned long lastFlashingSwitch = 0;
      static int flashingMessageFrame = 0;

      // --- Initial Combined Sequence: Play Hourglass THEN start Flashing ---
      // This 'if' runs ONLY ONCE when the "finished" sequence begins.
      if (!hourglassPlayed) {                          // <-- This is the single entry point for the combined sequence
        countdownFinished = true;                      // Mark as finished overall
        countdownShowFinishedMessage = true;           // Confirm we are in the finished sequence
        countdownFinishedMessageStartTime = millis();  // Start the 15-second timer for the flashing duration

        // 1. Play Hourglass Animation (Blocking)
        for (int repeat = 0; repeat < 3; repeat++) {
          for (int i = 0; i < 4; i++) {
            if (displayMode != 3) return;
            if (forceMessageRestart) return;
            P.setTextAlignment(PA_CENTER);
            P.setCharSpacing(0);
            P.write(161 + i);
            delay(350);
          }
        }
        Serial.println("[COUNTDOWN-FINISH] Played hourglass animation.");
        P.displayClear();  // Clear display after hourglass animation

        // 2. Initialize Flashing "TIMES UP" for its very first frame
        flashingMessageFrame = 0;
        lastFlashingSwitch = millis();  // Set initial time for first flash frame
        P.setTextAlignment(PA_CENTER);
        P.setCharSpacing(0);
        P.print(flashFrames[flashingMessageFrame]);             // Display the first frame immediately
        flashingMessageFrame = (flashingMessageFrame + 1) % 2;  // Prepare for the next frame

        hourglassPlayed = true;  // <-- Mark that this initial combined sequence has completed!
        countdownSegment = 0;    // Reset segment counter after finished sequence initiation
        segmentStartTime = 0;    // Reset segment timer after finished sequence initiation
      }

      // --- Continue Flashing "TIMES UP" for its duration (after initial combined sequence) ---
      // This part runs in subsequent loop iterations after the hourglass has played.
      if (millis() - countdownFinishedMessageStartTime < 15000) {  // Flashing duration
        if (displayMode != 3) return;
        if (forceMessageRestart) return;
        if (millis() - lastFlashingSwitch >= 500) {  // Check for flashing interval
          lastFlashingSwitch = millis();
          P.displayClear();
          P.setTextAlignment(PA_CENTER);
          P.setCharSpacing(0);
          P.print(flashFrames[flashingMessageFrame]);
          flashingMessageFrame = (flashingMessageFrame + 1) % 2;
        }
        P.displayAnimate();  // Ensure display updates
        yield();
        return;  // Stay in this mode until the 15 seconds are over
      } else {
        // 15 seconds are over, clean up and advance
        Serial.println("[COUNTDOWN-FINISH] Flashing duration over. Advancing to Clock.");
        countdownShowFinishedMessage = false;
        countdownFinishedMessageStartTime = 0;
        hourglassPlayed = false;  // <-- RESET this flag for the next countdown cycle!

        // Final cleanup (persisted)
        countdownEnabled = false;
        countdownTargetTimestamp = 0;
        countdownLabel[0] = '\0';
        saveCountdownConfig(false, 0, "");

        P.displayClear();
        P.setInvert(false);
        advanceDisplayMode();
        yield();
        return;  // Exit loop after processing
      }
    }  // END of 'if (timeRemaining <= 0 || countdownShowFinishedMessage)'


    // --- NORMAL COUNTDOWN LOGIC ---
    // This 'else' block will only run if `timeRemaining > 0` and `!countdownShowFinishedMessage`
    else {

      // The new variable `isDramaticCountdown` toggles between the two modes
      if (isDramaticCountdown) {
        // --- EXISTING DRAMATIC COUNTDOWN LOGIC ---
        long days = timeRemaining / (24 * 3600);
        long hours = (timeRemaining % (24 * 3600)) / 3600;
        long minutes = (timeRemaining % 3600) / 60;
        long seconds = timeRemaining % 60;
        String currentSegmentText = "";

        if (segmentStartTime == 0 || (millis() - segmentStartTime > SEGMENT_DISPLAY_DURATION)) {
          segmentStartTime = millis();
          P.displayClear();

          switch (countdownSegment) {
            case 0:  // Days
              if (days > 0) {
                currentSegmentText = String(days) + " " + (days == 1 ? "DAY" : "DAYS");
                Serial.printf("[COUNTDOWN-STATIC] Displaying segment %d: %s\n", countdownSegment, currentSegmentText.c_str());
                countdownSegment++;
              } else {
                // Skip days if zero
                countdownSegment++;
                segmentStartTime = 0;
              }
              break;
            case 1:
              {  // Hours
                char buf[10];
                sprintf(buf, "%02ld HRS", hours);  // pad hours with 0
                currentSegmentText = String(buf);
                Serial.printf("[COUNTDOWN-STATIC] Displaying segment %d: %s\n", countdownSegment, currentSegmentText.c_str());
                countdownSegment++;
                break;
              }
            case 2:
              {  // Minutes
                char buf[10];
                sprintf(buf, "%02ld MINS", minutes);  // pad minutes with 0
                currentSegmentText = String(buf);
                Serial.printf("[COUNTDOWN-STATIC] Displaying segment %d: %s\n", countdownSegment, currentSegmentText.c_str());
                countdownSegment++;
                break;
              }
            case 3:
              {  // Seconds & Label Scroll
                time_t segmentNow = time(nullptr);
                unsigned long segmentStartMillis = millis();

                long nowRemaining = countdownTargetTimestamp - segmentNow;
                long currentSecond = nowRemaining % 60;
                char secondsBuf[10];
                sprintf(secondsBuf, "%02ld %s", currentSecond, currentSecond == 1 ? "SEC" : "SECS");
                String secondsText = String(secondsBuf);
                Serial.printf("[COUNTDOWN-STATIC] Displaying segment 3: %s\n", secondsText.c_str());
                P.displayClear();
                P.setTextAlignment(PA_CENTER);
                P.setCharSpacing(1);
                P.print(secondsText.c_str());
                delay(SEGMENT_DISPLAY_DURATION - 400);

                unsigned long elapsed = millis() - segmentStartMillis;
                long adjustedSecond = (countdownTargetTimestamp - segmentNow - (elapsed / 1000)) % 60;
                sprintf(secondsBuf, "%02ld %s", adjustedSecond, adjustedSecond == 1 ? "SEC" : "SECS");
                secondsText = String(secondsBuf);
                P.displayClear();
                P.setTextAlignment(PA_CENTER);
                P.setCharSpacing(1);
                P.print(secondsText.c_str());
                delay(400);

                String label;
                if (strlen(countdownLabel) > 0) {
                  label = String(countdownLabel);
                  label.trim();
                  if (!label.startsWith("TO:") && !label.startsWith("to:")) {
                    label = "TO: " + label;
                  }
                  label.replace('.', ',');
                } else {
                  static const char *fallbackLabels[] = {
                    "TO: PARTY TIME!", "TO: SHOWTIME!", "TO: CLOCKOUT!", "TO: BLASTOFF!",
                    "TO: GO TIME!", "TO: LIFTOFF!", "TO: THE BIG REVEAL!",
                    "TO: ZERO HOUR!", "TO: THE FINAL COUNT!", "TO: MISSION COMPLETE"
                  };
                  int randomIndex = random(0, 10);
                  label = fallbackLabels[randomIndex];
                }

                P.setTextAlignment(PA_LEFT);
                P.setCharSpacing(1);
                textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
                P.displayScroll(label.c_str(), PA_LEFT, actualScrollDirection, GENERAL_SCROLL_SPEED);

                while (!P.displayAnimate()) {
                  if (displayMode != 3) return;
                  if (forceMessageRestart) return;
                  yield();
                }
                countdownSegment++;
                segmentStartTime = millis();
                break;
              }
            case 4:  // Exit countdown
              Serial.println("[COUNTDOWN-STATIC] All segments and label displayed. Advancing to Clock.");
              countdownSegment = 0;
              segmentStartTime = 0;
              P.setTextAlignment(PA_CENTER);
              P.setCharSpacing(1);
              advanceDisplayMode();
              yield();
              return;

            default:
              Serial.println("[COUNTDOWN-ERROR] Invalid countdownSegment, resetting.");
              countdownSegment = 0;
              segmentStartTime = 0;
              break;
          }

          if (currentSegmentText.length() > 0) {
            P.setTextAlignment(PA_CENTER);
            P.setCharSpacing(1);
            P.print(currentSegmentText.c_str());
          }
        }
        P.displayAnimate();
      }

      // --- NEW: SINGLE-LINE COUNTDOWN LOGIC ---
      else {
        long days = timeRemaining / (24 * 3600);
        long hours = (timeRemaining % (24 * 3600)) / 3600;
        long minutes = (timeRemaining % 3600) / 60;
        long seconds = timeRemaining % 60;

        String label;
        // Check if countdownLabel is empty and grab a random one if needed
        if (strlen(countdownLabel) > 0) {
          label = String(countdownLabel);
          label.trim();

          // Replace standard digits 0–9 with your custom font character codes
          for (int i = 0; i < label.length(); i++) {
            if (isDigit(label[i])) {
              int num = label[i] - '0';           // 0–9
              label[i] = 145 + ((num + 9) % 10);  // Maps 0→154, 1→145, ... 9→153
            }
          }

        } else {
          static const char *fallbackLabels[] = {
            "PARTY TIME", "SHOWTIME", "CLOCKOUT", "BLASTOFF",
            "GO TIME", "LIFTOFF", "THE BIG REVEAL",
            "ZERO HOUR", "THE FINAL COUNT", "MISSION COMPLETE"
          };
          int randomIndex = random(0, 10);
          label = fallbackLabels[randomIndex];
        }

        // Format the full string
        char buf[50];
        // Only show days if there are any, otherwise start with hours
        if (days > 0) {
          sprintf(buf, "%s IN: %ldD %02ldH %02ldM %02ldS", label.c_str(), days, hours, minutes, seconds);
        } else {
          sprintf(buf, "%s IN: %02ldH %02ldM %02ldS", label.c_str(), hours, minutes, seconds);
        }

        String fullString = String(buf);
        bool addPadding = false;
        bool humidityVisible = showHumidity && weatherAvailable && weatherConfigurationValid();

        // Padding logic
        if (prevDisplayMode == 0 && (showDayOfWeek || colonBlinkEnabled)) {
          addPadding = true;
        } else if (prevDisplayMode == 1 && humidityVisible) {
          addPadding = true;
        }
        if (addPadding) {
          fullString = "    " + fullString;  // 4 spaces
        }

        // Display the full string and scroll it
        P.setTextAlignment(PA_LEFT);
        P.setCharSpacing(1);
        textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
        P.displayScroll(fullString.c_str(), PA_LEFT, actualScrollDirection, GENERAL_SCROLL_SPEED);

        // Blocking loop to ensure the full message scrolls
        while (!P.displayAnimate()) {
          if (displayMode != 3) return;
          if (forceMessageRestart) break;
          yield();
        }

        // After scrolling is complete, we're done with this display mode
        // Move to the next mode and exit the function.
        P.setTextAlignment(PA_CENTER);
        advanceDisplayMode();
        yield();
        return;
      }
    }

    // Keep alignment reset just in case
    P.setTextAlignment(PA_CENTER);
    P.setCharSpacing(1);
    yield();
    return;
  }  // End of if (displayMode == 3 && ...)


  // DATE Display Mode
  if (displayMode == 5 && showDate) {
    if (forceMessageRestart) return;

    if (!validDisplayDate(timeinfo)) {
      advanceDisplayMode(true);
      return;
    }

    const String dateString = formatDateText(timeinfo, language, useCustomFont);

    P.setTextAlignment(PA_CENTER);
    P.setCharSpacing(0);
    P.print(dateString.c_str());
  }


  // --- Custom Message Display Mode (displayMode == 6) ---
  if (displayMode == 6) {
    int totalPixelWidth = 0;

    if (forceMessageRestart) {
      P.displayReset();
      P.displayClear();

      // RESET TIMERS/COUNTERS so new messages start fresh
      messageStartTime = millis();
      currentScrollCount = 0;
      currentDisplayCycleCount = 0;

      forceMessageRestart = false;
    }

    if (strlen(customMessage) == 0) {
      advanceDisplayMode();
      yield();
      return;
    }

    String msg = String(customMessage);

    // --- Strip brackets around numeric tokens ONLY ---
    if (messageBigNumbers) {
      while (true) {
        int start = msg.indexOf('[');
        int end = msg.indexOf(']', start);

        if (start == -1 || end == -1) break;

        String inside = msg.substring(start + 1, end);

        bool isNumber = true;
        for (char c : inside) {
          if (!isdigit(c)) {
            isNumber = false;
            break;
          }
        }

        if (isNumber) {
          msg.remove(end, 1);
          msg.remove(start, 1);
        } else {
          break;  // leave icon tokens like [MOON]
        }
      }
    }

    replaceIconTokens(msg, totalPixelWidth);

    if (!messageBigNumbers) {
      for (int i = 0; i < msg.length(); i++) {
        if (isDigit(msg[i])) {
          int num = msg[i] - '0';
          msg[i] = 145 + ((num + 9) % 10);
        }
      }
    }

    // --- TIMEOUT & LIMIT CHECKS ---
    bool timedOut = (messageDisplaySeconds > 0 && (millis() - messageStartTime) >= (messageDisplaySeconds * 1000UL));
    bool scrollsComplete = (messageScrollTimes > 0 && currentScrollCount >= messageScrollTimes);
    bool cyclesComplete = (messageScrollTimes > 0 && currentDisplayCycleCount >= messageScrollTimes);

    if (timedOut || scrollsComplete || cyclesComplete) {
      allowInterrupt = true;
      if (strlen(lastPersistentMessage) > 0) {
        strncpy(customMessage, lastPersistentMessage, sizeof(customMessage));
        messageScrollSpeed = GENERAL_SCROLL_SPEED;
      } else {
        customMessage[0] = '\0';
      }
      currentScrollCount = 0;
      messageStartTime = 0;
      currentDisplayCycleCount = 0;
      messageDisplaySeconds = 0;
      messageScrollTimes = 0;
      prevDisplayMode = 6;  // Set for Clock scroll-in
      advanceDisplayMode();
      yield();
      return;
    }

    // --- BRANCH A: STATIC (0-32 pixels) ---
    if (totalPixelWidth <= 32) {
      unsigned long durationMs = (messageDisplaySeconds > 0) ? (messageDisplaySeconds * 1000UL) : weatherDuration;

      // 1. Initial Centered Display
      P.setTextAlignment(PA_CENTER);
      P.setCharSpacing(1);
      P.print(msg.c_str());

      unsigned long displayUntil = millis() + durationMs;
      while (millis() < displayUntil) {
        if (displayMode != 6) return;
        if (forceMessageRestart) return;
        yield();
      }

      // 2. THE MANUAL SHIFT (Create 4-5px of pure black)
      // We only want to "push" the text off-screen if this is the VERY LAST cycle
      bool isLastCycle = (messageScrollTimes > 0 && (currentDisplayCycleCount + 1 >= messageScrollTimes))
                         || (messageScrollTimes == 0);

      if (totalPixelWidth >= 27 && isLastCycle) {
        // Shift the internal pixel buffer 5 times
        for (uint8_t i = 0; i < 5; i++) {
          if (displayMode != 6) return;
          if (flipDisplay) {
            P.getGraphicObject()->transform(MD_MAX72XX::TSR);  // shift right
          } else {
            P.getGraphicObject()->transform(MD_MAX72XX::TSL);  // shift left
          }
          delay(messageScrollSpeed);
        }
      }

      // 3. Handover to Clock
      if (messageScrollTimes > 0) {
        currentDisplayCycleCount++;
      } else {
        advanceDisplayMode();
        prevDisplayMode = 6;
        clockScrollDone = false;
      }
      yield();
      return;
    }

    // --- BRANCH B: SCROLLING ---
    bool addPadding = false;
    bool humidityVisible = showHumidity && weatherAvailable && weatherConfigurationValid();
    if (prevDisplayMode == 0 && (showDayOfWeek || colonBlinkEnabled)) addPadding = true;
    else if (prevDisplayMode == 1 && humidityVisible) addPadding = true;

    if (addPadding) msg = "    " + msg;

    P.setTextAlignment(PA_LEFT);
    P.setCharSpacing(1);
    textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);

    P.displayScroll(msg.c_str(), PA_LEFT, actualScrollDirection, messageScrollSpeed);

    while (!P.displayAnimate()) {
      if (displayMode != 6) return;
      if (forceMessageRestart) return;  // Exit immediately to top level
      yield();
    }

    currentScrollCount++;

    if (messageDisplaySeconds == 0 && messageScrollTimes == 0) {
      prevDisplayMode = 6;
      advanceDisplayMode();
    }
    yield();
    return;
  }

  if (displayMode == 7) {
    showTimerMode7();
  }

  unsigned long currentMillis = millis();
  unsigned long currentTotal = getTotalRuntimeSeconds();

  // --- Log and save uptime every 10 minutes ---
  const unsigned long uptimeLogInterval = 600000UL;  // 10 minutes in ms


  if (currentMillis - lastUptimeLog >= uptimeLogInterval) {
    lastUptimeLog = currentMillis;
    Serial.printf("[UPTIME] Runtime: %s (total %.2f hours)\n",
                  formatUptime(currentTotal).c_str(), currentTotal / 3600.0);
    saveUptime();  // Save accumulated uptime every 10 minutes
  }
  yield();
}

char getPomodoroWorkIcon() {
  switch (pomodoroSession) {
    case 1: return '\xBC';  // quarter
    case 2: return '\xBD';  // half
    case 3: return '\xBE';  // three quarters
    case 4: return '\x83';  // full
    default: return '\xBC';
  }
}

void showTimerMode7() {
  unsigned long now = millis();
  P.setCharSpacing(1);

  // --- 1. INTERRUPT LOGIC ---
  if (allowInterrupt == false) {
    unsigned long waitTime = (unsigned long)clockDuration;
    if (now - lastSwitch >= waitTime) {
      Serial.println(F("[TIMER] clockDuration reached. Switching to Mode 6 (Infinite)"));
      displayMode = 6;
      lastSwitch = now;
      return;
    }
  }

  // --- 2. STOPWATCH / POMODORO BREAK ---
  if (isStopwatch) {
    unsigned long elapsed = timerPaused ? timerRemainingAtPause : (millis() - timerEndTime);

    if (isPomodoroActive) {
      unsigned long currentBreakMs = (pomodoroSession == 4) ? pomodoroLongBreakMs : pomodoroBreakMs;
      if (!timerPaused && elapsed >= currentBreakMs) {
        pomodoroSession++;
        if (pomodoroSession > 4) pomodoroSession = 1;
        pomodoroInBreak = false;
        isStopwatch = false;
        timerOriginalDuration = pomodoroWorkMs;
        timerEndTime = millis() + pomodoroWorkMs;
        timerFinished = false;
        timerPaused = false;
        Serial.printf("[POMODORO] Break over. Starting session %d.\n", pomodoroSession);
        return;
      }
      unsigned long remaining = (currentBreakMs > elapsed) ? (currentBreakMs - elapsed) : 0;
      unsigned long totalSec = remaining / 1000;
      int m = totalSec / 60;
      int s = totalSec % 60;
      char buf[10];
      sprintf(buf, "%c %02d:%02d", '\xBF', m, s);
      P.setTextAlignment(PA_CENTER);
      P.print(buf);
      return;
    }

    // Normal stopwatch with centiseconds
    unsigned long totalSec = elapsed / 1000;
    int cs = (elapsed % 1000) / 10;
    int m = totalSec / 60;
    int s = totalSec % 60;
    char buf[12];
    if (m > 59) {
      int h = m / 60;
      m = m % 60;
      sprintf(buf, "%02d:%02d:%02d", h, m, s);
    } else {
      sprintf(buf, "%02d:%02d.%02d", m, s, cs);
    }
    P.setTextAlignment(PA_CENTER);
    P.print(buf);
    return;
  }

  // --- 3. COUNTDOWN TIMER ---
  if (!timerFinished) {
    long remaining = 0;
    if (timerPaused) {
      remaining = (long)(timerRemainingAtPause / 1000);
    } else {
      if (now >= timerEndTime) {
        timerFinished = true;
        timerFinishStartTime = now;
      } else {
        remaining = (long)((timerEndTime - now) / 1000);
        if (remaining < 0) remaining = 0;

        int h = remaining / 3600;
        int m = (remaining % 3600) / 60;
        int s = remaining % 60;

        char buf[12];
        if (isPomodoroActive) {
          char icon = getPomodoroWorkIcon();
          if (h > 0) sprintf(buf, "%c %02d:%02d:%02d", icon, h, m, s);
          else sprintf(buf, "%c %02d:%02d", icon, m, s);
        } else {
          if (h > 0) sprintf(buf, "%02d:%02d:%02d", h, m, s);
          else sprintf(buf, "%02d:%02d", m, s);
        }

        P.setTextAlignment(PA_CENTER);
        P.print(buf);
        return;
      }
    }

    if (timerPaused) {
      int h = remaining / 3600;
      int m = (remaining % 3600) / 60;
      int s = remaining % 60;
      char buf[12];
      if (isPomodoroActive) {
        char icon = getPomodoroWorkIcon();
        if (h > 0) sprintf(buf, "%c %02d:%02d:%02d", icon, h, m, s);
        else sprintf(buf, "%c %02d:%02d", icon, m, s);
      } else {
        if (h > 0) sprintf(buf, "%02d:%02d:%02d", h, m, s);
        else sprintf(buf, "%02d:%02d", m, s);
      }
      P.setTextAlignment(PA_CENTER);
      P.print(buf);
      return;
    }
  }

  // --- 4. FINISHED STATE ---
  if (timerFinished) {
    if (isPomodoroActive) {
      pomodoroInBreak = true;
      isStopwatch = true;
      // Session 4 gets the long break
      timerEndTime = millis();
      timerActive = true;
      timerPaused = false;
      timerFinished = false;
      Serial.printf("[POMODORO] Session %d done. Starting %s break.\n",
                    pomodoroSession, pomodoroSession == 4 ? "long" : "short");
      return;
    }

    // Normal timer: alarm animation for 5 seconds
    if (now - timerFinishStartTime > 5000) {
      timerActive = false;
      timerFinished = false;
      displayMode = 0;
      clockScrollDone = false;
      lastSwitch = now;
      return;
    }
    if ((now / 500) % 2 == 0) P.print("\x08");
    else P.print("\x09");
  }
}