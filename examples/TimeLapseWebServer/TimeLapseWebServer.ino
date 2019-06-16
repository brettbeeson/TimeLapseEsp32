#include "TimeLapseCamera.h"
#include <WiFiManager.h>          // https://github.com/Brunez3BD/WIFIMANAGER-ESP32
#include "FS.h"
#include "SD_MMC.h"
#include "BBEsp32Lib.h"
#include <ArduinoLogRemoteDebug.h>
#include "TimeLapseWebServer.h"
#include "TimeLapseWebSocket.h"

TimeLapseCamera cam(SD_MMC);
TimeLapseWebServer webServer(80,cam);
TimeLapseWebSocket socketServer("/ws",cam);

void setup() {
  Serial.begin(115200); while (!Serial) {};
  Debug.setSerialEnabled(true);   // Print to serial too
  
  try {
    // Storage
    //
    if (SD_MMC.begin()) {
      debugV("SD Ready");
    } else {
      throw std::runtime_error("Failed to start SD_MMC filesystem");
    }
    //
    // Wifi
    //
    WiFiManager wifiManager;
    if (wifiManager.autoConnect("TimeLapseBlob")) {
      debugI("WiFi started");
      Debug.begin("tls", RemoteDebug::VERBOSE); // Initialize the debug server
    } else {
      debugE("Failed to connect to WiFi and hit timeout.");
      while (1);
      
    }
//cam.configFTP("monitor.phisaver.com", "timelapse", "U88magine!");
 //cam.configFTP("10.1.1.15", "bbeeson", "imagine");
    
    cam.begin();
    cam.takePhotoTaskify(2);
    
    //
    // Webserver
    //
    webServer.addHandler(&socketServer);
    webServer.begin();
    debugV("Setup complete.");

  } catch (std:: exception &e) {
    debugE("%s", e.what());
  }
}

/**

*/
void loop() {
  Debug.handle();
  yield();
}
