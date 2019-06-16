#include "TimeLapseWebSocket.h"
#include "TimeLapseCamera.h"

#include <ArduinoJson.h>
#include <ezTime.h>
#include <ArduinoLogRemoteDebug.h>

TimeLapseWebSocket::TimeLapseWebSocket(const String& url, TimeLapseCamera& tlc, int port):
  AsyncWebSocket(url),
  _tlc(tlc)
{
  this->onEvent(onWebSocketEvent);
}

void merge(JsonObject& dest, const JsonObject& src) {
  for (JsonPair kvp : src) {
    dest[kvp.key()] = kvp.value();
  }
}


void onWebSocketEvent(AsyncWebSocket * serverBase, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {

  TimeLapseWebSocket* server = (TimeLapseWebSocket*) serverBase;

  if (type == WS_EVT_CONNECT) {
    Serial.printf("ws[%s][%u] connect.\n", server->url(), client->id());
    client->text("{\"event\":\"message\",\"data\":{\"text\":\"Connected to Espy!\"}}");
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
      server->handleWebSocketMessage(client, msg);
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
          server->handleWebSocketMessage(client, msg);
        }
      }
    }
  }
}


/*
   Message from the client/browser, probably in event/data format
*/
void TimeLapseWebSocket::handleWebSocketMessage(AsyncWebSocketClient * client, String& msg) {
  String errorMessage;

  // Parse the message
  const size_t capacity = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(10) + 500;
  DynamicJsonDocument msgDoc(capacity);
  DeserializationError msgError = deserializeJson(msgDoc, msg);
  if (msgError) {
    Serial.printf("deserializeJson() failed: %s", msgError.c_str());
    return;
  }

  // Distribute the message
  // Pretty hacky - could use a bind system / classes
  // but this is the only place it is required
  //
  const char* event = msgDoc["event"]; // "status"
  const JsonObject& msgData = msgDoc["data"];

  debugV("Event received: %s", event);

  if (strcmp(event, "status") == 0) {

    // Return the status. Ignore msgData - status is read-only
    // eg. {"event":"status","data":{"wifiMode":"wifiOn","uploadMode":"noupload","sleepfrom":12,"sleepto":6,"photoIntervalS":11}}";
    String json;
    DynamicJsonDocument doc = _tlc.status();
    serializeJson(doc, json);
    client->text(json);

  } else if (strcmp(event, "ftpsettings") == 0) {
    assert(0);
    if (msgData.size() == 0) {
      // request for ftpsettings
      DynamicJsonDocument doc(JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(10));
      String json;
      DynamicJsonDocument data = _tlc.config(); // read all, to report back as a copy
      doc["event"] = "ftpsettings";
      JsonObject dataObj = doc.createNestedObject("data");
      merge(dataObj, data.as<JsonObject>());
      serializeJson(doc, json);
      client->text(json);
      //errorMessage = "ftpsettings updated";
    } else if (msgData.containsKey("ftpserver")
               && msgData.containsKey("ftpusername")
               && msgData.containsKey("ftppassword")) {
      _tlc.configFTP(msgData["ftpserver"], msgData["ftpusername"], msgData["ftppassword"]);
      //errorMessage = "ftpsettings set";
    } else {
      errorMessage = "ftpsettings not set - needs server, user and password";
    }

    debugV("Done ftpsettings");

  } else if (strcmp(event, "message") == 0) {

    debugI("Message from client: %s", msgData["text"].as<char*>());

  } else if (strcmp(event, "settings") == 0) {

    DynamicJsonDocument doc(JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(25));
    String json;
    if (msgData.size() > 0) {
      _tlc.config(msgData); // write settings
      errorMessage = "Updated settings";
    }
    DynamicJsonDocument data = _tlc.config(); // read all, to report back as a copy
    doc["event"] = "settings";
    JsonObject dataObj = doc.createNestedObject("data");
    merge(dataObj, data.as<JsonObject>());
    serializeJson(doc, json);
    Serial.println("Sending:");
    serializeJson(doc, Serial);
    client->text(json);

  } else if (strcmp(event, "lastphotourl") == 0) {

  } else {

    errorMessage = String("Unknown event of type:") + String(event);
  }

  // Send make an error message if necessary
  debugV("Done message event. MessageBack: %s", errorMessage.c_str());
  if (errorMessage != "" ) {
    String json;
    const size_t capacity = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2);
    DynamicJsonDocument doc(capacity);
    doc["event"] = "message";
    const JsonObject& nestedObject = doc.createNestedObject("data");
    nestedObject["text"] = errorMessage.c_str();
    serializeJson(doc, json);
    Serial.println(json);
    client->text(json);
  }
}
