#include "TimeLapseWebServer.h"
#include "TimeLapseCamera.h"
#include "RemoteDebug.h"
#include "BBEsp32Lib.h"
#include <WiFiClient.h>

TimeLapseWebServer::TimeLapseWebServer(int port, TimeLapseCamera& tlc):
  AsyncWebServer(port),
  _tlc(tlc)

{
  //  this->addHandler(new FSEditor(SD_MMC));  // At /edit

  this->on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  this->on("/log", HTTP_GET, [](AsyncWebServerRequest * request) {
    
    int lines = 100;
    if(request->hasParam("lines")) {
      //lines = request->getParam("lines"));
    }
    
    //tail("/sdcard/timelapse.log",lines // print to request
    request->send(200, "text/plain", "Enter log file here!");  
  });

  this->serveStatic("/", tlc._filesys, "/").setDefaultFile("index.htm");

  this->onNotFound([](AsyncWebServerRequest * request) {
    Serial.printf("Sent 404\n");
    request->send(404);
  });
}

void TimeLapseWebServer::begin() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  AsyncWebServer::begin();
}
