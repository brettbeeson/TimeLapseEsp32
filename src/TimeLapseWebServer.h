#pragma once

#include <freertos/FreeRTOS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

class TimeLapseCamera;

class TimeLapseWebServer : public AsyncWebServer {

  public:
    TimeLapseWebServer(int port,TimeLapseCamera& tlc);
    void begin(); // override : todo "does NOT override!"

  private:

    TimeLapseCamera& _tlc;
};
