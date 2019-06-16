#include <soc/rtc.h>
#include "TimeLapseCamera.h"
#include <WiFiManager.h>          // https://github.com/Brunez3BD/WIFIMANAGER-ESP32
#include "FS.h"
#include "SD_MMC.h"
#include "BBEsp32Lib.h"
#include "TimeKeeper.h"

#define USE_MDNS true
#define HOST_NAME "timelapse"
#include <DNSServer.h>
#include "ESPmDNS.h"
#include <rom/rtc.h>

#define MINUTE_IN_MS 60000
#undef MAX_TIME_INACTIVE // prevent compiler warning as already defined in RemoteDebug.h
#define MAX_TIME_INACTIVE 6000000000
#include "RemoteDebug.h"        //https://github.com/JoaoLopesF/RemoteDebug

#define FORCE_UPLOAD false
#define FORCE_AWAKE false

#define WIFI_CHECK_PERIOD 60000
#define BATTERY_CHECK_PERIOD 60000

#define CHG_PIN 3

#define PHOTO_PERIOD_S 3
#define CHECK_FOR_UPLOAD_PERIOD_S 60
// Todo: calculate runrise/set based on location and time
#define SLEEP_HOUR 18
#define WAKE_HOUR 5
#define WIFI true

const char* SAVE_FOLDER = "/tl";

TimeLapseCamera cam(SD_MMC, SAVE_FOLDER, PHOTO_PERIOD_S, CHECK_FOR_UPLOAD_PERIOD_S);

LogFile sdlog("/sdcard/timelapse.log");

void setup() {
  // Check if multiple rapid wakeups?
  if (!FORCE_AWAKE && rtc_get_reset_reason(0) == 12 /*SW_CPU_RESET*/) {
    delay(10 * 60000); // wait 10 minutes maybe deepsleep?
  }
  ezt::setInterval(60 * 60 /* in s */);
  TimeKeeper.setPosix("AEST-10");
  TimeKeeper.setDefault();
  TimeKeeper.restoreTime();

  Serial.begin(115200); while (!Serial) {};
  Debug.setSerialEnabled(true);   // Print to serial too

  debugI("TimeLapseBlob!");

  try {
    // Storage
    //
    if (SD_MMC.begin()) {
      //printSDCardDetails();
    } else {
      throw std::runtime_error("Failed to start SD_MMC filesystem");
    }
    Serial.println("Log file:");
    sdlog.tail(20); // display logfile
    
    sdlog.print(String("Starting. Reset reason: ") + resetReason(0));
    sdlog.print(TimeKeeper.toString());

    // Wifi
    //
    // On boot, try for 10 minutes to get a connection (show config portal if necessary)
    //
    if (WIFI) {
      WiFiManager wifiManager;
      wifiManager.setConfigPortalTimeout(10 * 60 * 1000);

      if (wifiManager.autoConnect("TimeLapseBlob")) {
        //ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
        sdlog.print("WiFi started");
        // Remote debug
        //
        /*
          if (MDNS.begin(HOST_NAME)) {
          debugI("MDNS responder started. Hostname: %s", HOST_NAME);
          } else {
          debugW("MDNS responder couldn't start");
          }
          MDNS.addService("telnet", "tcp", 23);
        */
        Debug.begin(HOST_NAME, RemoteDebug::VERBOSE); // Initialize the WiFi server
        Debug.setResetCmdEnabled(true);
        // Debug.showProfiler(true); // (Good to measure times, to optimize codes)
        //Debug.showColors(true);
        sdlog.print("Debug started");

        // Clock
        //
        
        ezt::setTime(compileTime());  // for development only
        debugI("Before updateFromNTP: LocalTime: %s", defaultTZ->dateTime().c_str());
        TimeKeeper.updateFromNTP();
        debugI("After updateFromNTP: LocalTime: %s", defaultTZ->dateTime().c_str());

        // Upload
        //
        cam.configFTP("monitor.phisaver.com", "timelapse", "U88magine!");
        //cam.configFTP("10.1.1.15", "bbeeson", "imagine");
        if (cam.testFTPConnection())   {
          debugE("Connected to FTP");
          sdlog.print("Wifi connected. FTP connected.");
          cam.setUploading(true);
        } else {
          debugI("Failed to connect to FTP.");
          cam.setUploading(false);
          sdlog.print("Wifi connected. FTP not connected.");
          // Could fail-over to another?
        }
      } else {
        // No wifi available
        sdlog.print("Wifi not connected.");
        debugE("Failed to connect to WiFi and hit timeout. Continuing without WiFi");
      }
    } else {
      debugI("WIFI is not defined. Not started.");
    }

    // Solar charger
    //
    pinMode(CHG_PIN, INPUT_PULLUP);

    // Camera
    //
    cam.begin();

    cam.takePhotoTaskify(4);
    cam.setDeleteOnUpload(true);

    cam.uploadPhotosTaskify(3);
    cam.setUploading(batteryCharged() || FORCE_UPLOAD);
    debugV("Setup complete.");
    sdlog.print("Setup complete.");

  } catch (std:: exception &e) {
    debugE("%s", e.what());
    sdlog.print(String("Setup error: ") + String(e.what()));
    ESP.deepSleep(60 * uS_TO_S_FACTOR);
    ESP.restart();
  }
  return;

}

/**

*/
void loop() {
  static uint32_t lastWifiCheck = 0;
  static uint32_t lastBatteryCheck = 0;

  if (WIFI && (millis() - lastWifiCheck > WIFI_CHECK_PERIOD)) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFiManager wifiManager;
      // Return even if not connected
      wifiManager.setBreakAfterConfig(true);
      // Run an AP for a minute, in case we moved and want to connect and adjust the SSID/Password
      wifiManager.setConfigPortalTimeout(60 * 1000);
      if (!wifiManager.autoConnect("TimeLapseBlob")) {
        debugE("Failed to re-connect Wifi");
      } else {
        debugI("Reconnected Wifi");
        sdlog.print("Wifi re-connected.");
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    // update NTP if necessary
    ezt::events();

    // handle remotedebug
    Debug.handle();

    // Sleepy head
    // Fix to sleep exactly the right time or MAXINT
    //
    if (!FORCE_AWAKE && TimeKeeper.year() > 2000 && TimeKeeper.hour() > SLEEP_HOUR) {
      int hoursToSleep = 24 - TimeKeeper.hour() + WAKE_HOUR;
      debugW("Sleeping for %d hours", hoursToSleep);
      sdlog.print("Sleeping for " + String(hoursToSleep) + "h");
      // Max sleep is 1.2h 0xFFFFFFFF = 4294967295us === 1.193 h
      ESP.deepSleep((uint32_t)3600 * (uint32_t)uS_TO_S_FACTOR);
      //ESP.deepSleep(hoursToSleep * 3600 * uS_TO_S_FACTOR);
    }
  }

  // upload if battery charged/charging
  //
  if (millis() - lastBatteryCheck > BATTERY_CHECK_PERIOD) {
    bool bcAtm = batteryCharged();
    static uint32_t chargingForMs = 0;
    static bool bcStatic = false;

    if (bcAtm) {
      chargingForMs += millis() - lastBatteryCheck;
    } else {
      chargingForMs = 0;
    }
    lastBatteryCheck = millis();

    if (bcStatic != batteryCharged()) {
      bcStatic = batteryCharged();
      sdlog.print("batteryCharged() changed to " + String(bcStatic));
    }
    cam.setUploading(chargingForMs > (60 * MINUTE_IN_MS) || FORCE_UPLOAD);
    debugI("chargingForMs: %juminutes, Now:batteryCharged: %d, FORCE_UPLOAD: %d", (uintmax_t) chargingForMs/1000/60, batteryCharged(), FORCE_UPLOAD);
  }

  // don't spin
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  yield();
}

/*
   Open drain pin, so "active" is LOW
*/
bool batteryCharged() {
  return digitalRead(CHG_PIN) == LOW;
}

void printSDCardDetails() {

  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD_MMC card attached");
    return;
  }

  Serial.print("SD_MMC Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);
  Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));
}
