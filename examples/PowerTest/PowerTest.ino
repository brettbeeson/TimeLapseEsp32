#include <soc/rtc.h>
#include "TimeLapseCamera.h"
#include <WiFiManager.h>          // https://github.com/Brunez3BD/WIFIMANAGER-ESP32
#include "FS.h"
#include "SD_MMC.h"
#include "BBEsp32Lib.h"
// Blobs
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

#define FORCE_UPLOAD true
#define FORCE_AWAKE true

#define WIFI_CHECK_PERIOD 60000
#define BATTERY_CHECK_PERIOD 60000

#define CHG_PIN 3
#define PHOTO_PERIOD_S 5
#define CHECK_FOR_UPLOAD_PERIOD_S 60
// Todo: calculate runrise/set based on location and time
#define SLEEP_HOUR 18
#define WAKE_HOUR 5
#define WIFI true

const char* SAVE_FOLDER = "/tl";

TimeLapseCamera cam(SD_MMC, SAVE_FOLDER, PHOTO_PERIOD_S, CHECK_FOR_UPLOAD_PERIOD_S);

Timezone localTime;

LogFile sdlog("/sdcard/timelapse.log");

// Use TimeKeeper?

void setup() {
  Serial.println("START"); delay(5000);
  localTime.setPosix("AEST-10");
  localTime.setDefault();
  uint32_t s;
  Serial.begin(115200); while (!Serial) {};
  Debug.setSerialEnabled(true);   // Print to serial too
  debugI("TimeLapseBlob!");

  try {
    // Storage
    //
    if (SD_MMC.begin()) {

    } else {
      throw std::runtime_error("Failed to start SD_MMC filesystem");
    }
    Serial.println("Log file:");
    sdlog.tail(20); // display logfile
    sdlog.print(String("Starting. Reset reason: ") + resetReason(0));

    Serial.println("SD  ON"); delay(5000);

    // Wifi
    //
    // On boot, try for 10 minutes to get a connection (show config portal if necessary)
    //
    if (WIFI) {
      WiFiManager wifiManager;
      wifiManager.setConfigPortalTimeout(10 * 60 * 1000);
      s = millis();
      if (wifiManager.autoConnect("TimeLapseBlob")) {
        Serial.printf("WIFI TAKES %lu\n", millis() - s);
    //    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
        sdlog.print("WiFi started");
        Serial.println("WiFi ON"); delay(5000);

        Debug.begin(HOST_NAME, RemoteDebug::VERBOSE);
        Debug.setResetCmdEnabled(true);
        Serial.println("Debug ON"); delay(5000);
        // Debug.showProfiler(true); // (Good to measure times, to optimize codes)
        //Debug.showColors(true);
        sdlog.print("Debug started");

        // Clock
        //
        ezt::setTime(compileTime());  // for development only
        ezt::updateNTP();
        ezt::waitForSync();
        debugI("LocalTime: %s", defaultTZ->dateTime().c_str());

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
        Serial.println("FTP ON"); delay(5000);
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

    s = millis();
    cam.begin();
    Serial.printf("CAM TAKES %lu\n", millis() - s);
    Serial.println("CAM ON"); delay(20000);
    cam.takePhotoTaskify(3);
    Serial.println("PHOTOS ON"); delay(20000);
    cam.setDeleteOnUpload(false);
    cam.uploadPhotosTaskify(3);
    cam.setUploading(true);
    Serial.println("UPLOAD ON"); delay(20000);


    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    
    Serial.println("WIFI OFF"); delay(20000);
    ESP_ERROR_CHECK(esp_camera_deinit());
    Serial.println("CAM OFF"); delay(20000);


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

  // don't spin
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  yield();
}
