#include <ArduinoJson.h>

#include <FS.h>
#include <SD_MMC.h>
#include <AsyncTCP.h>
#define DEBUGF(...) Serial.printf(__VA_ARGS__)
#include <ESPAsyncWebServer.h>
#define DEBUGF(...) Serial.printf(__VA_ARGS__)
#include "FSEditor.h"
#include "TimeLapseWS.h" 
AsyncWebServer server(80);
TimeLapseWS ws("/ws"); // derive from this and then "onevent" will pass you "this"

void dirJSON(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  String path = "/";

  if (path != "/" && !SD_MMC.exists((char *)path.c_str())) {
    response->print("BAD PATH");
    return;
  }
  File dir = SD_MMC.open((char *)path.c_str());
  path = String();
  if (!dir.isDirectory()) {
    dir.close();
    response->print("NOT DIR");
    return;
  }
  dir.rewindDirectory();
  //  setContentLength(CONTENT_LENGTH_UNKNOWN);

  response->print("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    String output;
    if (cnt > 0) {
      output = ',';
    }

    // String pn = slash("", entry.name(), ""); // path ("/asdf/sdf/asdf")
    String pn = entry.name();
    String fn;

    if (pn.lastIndexOf('/') != -1) {
      fn = pn.substring(pn.lastIndexOf('/'));
    } else {
      fn = pn;
    }
    //debugV("%s %s", pn.c_str(), fn.c_str());
    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    //    output += entry.name();

    output += fn;//slash("", fn, "");
    output += "\"";
    output += "}";
    response->print(output);
    Serial.println(output);
    entry.close();
  }
  response->print("]");
  dir.close();
  request->send(response);

}

/*
   Message from the client/browser, probably in event/data format
*/
void handleWSMessage(AsyncWebSocketClient * client, String& msg) {

  Serial.println("Right, gunna go something with:" + msg);
  const char* json = "{\"event\":\"status\",\"data\":{\"wifiMode\":\"wifiOn\",\"uploadMode\":\"noupload\",\"sleepfrom\":12,\"sleepto\":6,\"photoIntervalS\":11}}";
  client->text(json);
}


void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    TimeLapseWS *tlws = (TimeLapseWS *) server;
    Serial.printf("ws[%s][%u] connect. Secret: %s\n", server->url(), client->id(),tlws->secret.c_str());
    //client->printf("Hello Client %u :)", client->id());
    //client->ping();
    
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if (type == WS_EVT_ERROR) {
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if (type == WS_EVT_PONG) {
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char*)data : "");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if (info->final && info->index == 0 && info->len == len) {
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

      if (info->opcode == WS_TEXT) {
        for (size_t i = 0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for (size_t i = 0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n", msg.c_str());
      handleWSMessage(client, msg);
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0) {
        if (info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }
      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);

      if (info->opcode == WS_TEXT) {
        for (size_t i = 0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for (size_t i = 0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n", msg.c_str());

      if ((info->index + len) == info->len) {
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if (info->final) {
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
          handleWSMessage(client, msg);
        }
      }
    }
  }
}


const char* ssid = "NetComm 0405";
const char* password = "wimepuderi";
const char * hostName = "esp-async";
const char* http_username = "admin";
const char* http_password = "admin";

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }
  if (SD_MMC.begin()) {
    Serial.println("SD Ready");
  } else {
    throw std::runtime_error("Failed to start SD_MMC filesystem");
  }

  //  MDNS.addService("http","tcp",80);
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
//  server.addHandler(new FSEditor(SD_MMC));  // /edit

//  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) {
//    request->send(200, "text/plain", String(ESP.getFreeHeap()));
//  });

  //server.on("/dir", HTTP_GET, dirJSON);

  server.serveStatic("/", SD_MMC, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest * request) {
     Serial.printf("NOT_FOUND: ");
    if (request->method() == HTTP_GET)
      Serial.printf("GET");
    else if (request->method() == HTTP_POST)
      Serial.printf("POST");
    else if (request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if (request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if (request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if (request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if (request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if (request->contentLength()) {
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for (i = 0; i < headers; i++) {
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for (i = 0; i < params; i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p->isFile()) {
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if (p->isPost()) {
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    Serial.printf("Sent 404\n");
    request->send(404);
  });
  
  server.onFileUpload([](AsyncWebServerRequest * request, const String & filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index)
      Serial.printf("UploadStart: %s\n", filename.c_str());
    Serial.printf("%s", (const char*)data);
    if (final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index + len);
  });
  server.onRequestBody([](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if (index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });
  server.begin();
}

void loop() {

}
