#pragma once

#include <FreeRTOS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

class TimeLapseCamera;

// todo make a static member
void onWebSocketEvent(AsyncWebSocket * serverBase, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);

class TimeLapseWebSocket : public AsyncWebSocket {

  public:

    TimeLapseWebSocket(const String& url, TimeLapseCamera& tlc, int port = 80);
    void handleWebSocketMessage(AsyncWebSocketClient * client, String& msg);
  private:


    TimeLapseCamera & _tlc;
};

class FancyWebSocket {

  public:
  
  FancyWebSocket(const String& json) {
  }
 

  String event() {
    
  }
  String data() {
  }
};
