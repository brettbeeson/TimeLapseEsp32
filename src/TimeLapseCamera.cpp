/**
   ArduinoLog doesn't print %lld, etc.
   typedef uint32_t TickType_t;
   int BaseType_t
   time_t uint32_t
   uint32_t millis
*/

#include "BBEsp32Lib.h"
#include "TimeLapseCamera.h"
#include "TimeLapseWebServer.h"
#include "esp_camera.h"
#include <ArduinoLogRemoteDebug.h>
#include <ArduinoJson.h>
#include "FTPUploader.h"
#include "FS.h"
#include "SD_MMC.h"

#include <ezTime.h>

// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

RTC_DATA_ATTR int TimeLapseCameraFileNumber = 0;

// Local function for RTOS - must be non-class
void TakePhotoTask (void* timeLapseCamera);
void UploadPhotosTask (void *timeLapseCamera);

TimeLapseCamera::TimeLapseCamera(fs::FS &filesys, String saveFolder, int takePhotoPeriodS, int uploadPhotoPeriodS):
  _filesys(filesys),
  _takePhotoPeriodS (takePhotoPeriodS),
  _uploadPhotoPeriodS(uploadPhotoPeriodS),
  _uploadMode(OFF),
  _wifiMode(ON),
  _saveFolder(saveFolder),
  _deleteOnUpload(true),
  _ftp(NULL),
  _series("aa")
{
  _saveFolder = slash(_saveFolder, '/');
}

void TimeLapseCamera::configFTP(const String& host, const String& user, const String& password) {
  if (_ftp) {
    if (_ftp->_host == host && _ftp->_user == user && _ftp->_password == password) {
      return; // no change
    }
    delete (_ftp); _ftp = NULL;
  }
  _ftp = new FTPUploader(host, user, password);
}


uint32_t TimeLapseCamera::sleepy() {

  if (!_sleepEnabled) return false;
  if (defaultTZ->year() < 2000) return false; // bad time

  if (defaultTZ->hour() > _sleepAt && defaultTZ->hour() < _wakeAt ) {
    int hoursToSleep = 24 - defaultTZ->hour() + _wakeAt;
    return (hoursToSleep * 3600);
  } else {
    return 0;
  }
}

bool TimeLapseCamera::testFTPConnection() {
  assert(_ftp);
  return _ftp -> testConnection();
}


// quality: 0-63, lower means higher quality
void TimeLapseCamera::begin(framesize_t frameSize, int quality) {

  camera_config_t config;
  esp_err_t cam_err;
  int psramStartkB = ESP.getFreePsram() / 1024;
  _frameSize = frameSize;
  _camReady = false;
  
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = _frameSize ;
    config.jpeg_quality = quality ; // 0-63 lower means higher quality
    config.fb_count = 1;
  } else {
    // oh no, we're too littlely
    throw std::runtime_error("No psramFound. Not tested without it!");
  }

  int initTries = 0;
  while ( (cam_err = esp_camera_init(&config) != ESP_OK) && initTries++ < 3) {
    delay(3000);
  }

  if (cam_err != ESP_OK) {
    String err = "Camera init failed with error: " + String(cam_err, HEX);
    throw std::runtime_error(err.c_str());
  } else {
    debugI("esp_camera_init ok. Used %dkB Psram", psramStartkB - (ESP.getFreePsram() / 1024));
  }
  _camReady = true;
}


void TimeLapseCamera :: end() {
  esp_err_t camErr;
  if (_camReady) {
    _camReady = false;
    if ((camErr = esp_camera_deinit() ) != ESP_OK) {
      debugE("esp_err_t esp_camera_deinit() failed: %d", camErr); // esp_err_t type?
    }
  }
}

void TimeLapseCamera :: takePhotoTaskify(int priority) {


  xTaskCreatePinnedToCore(TakePhotoTask, "TakePhotoTask",
                          10000 /* stack depth */,
                          (void *)this, /* parameters */
                          priority,
                          &_takePhotoTaskHandle /* task handle */,
                          1 /* core */);
  assert(_takePhotoTaskHandle);
}

void TimeLapseCamera :: uploadPhotosTaskify(int priority) {


  xTaskCreatePinnedToCore(UploadPhotosTask, "UploadPhotosTask",
                          20000 /* stack depth */,
                          (void *)this, /* parameters */
                          priority, /* less important than taking photo */
                          &_uploadPhotosTaskHandle /* task handle */,
                          1 /* core */);
  assert(_uploadPhotosTaskHandle);
}


/*
   Take one photo and save to SD Card
*/
void TimeLapseCamera::takePhoto() {

  camera_fb_t* f = NULL;
  time_t t;

  if (!_camReady) throw std::runtime_error("Camera not ready");

  f = esp_camera_fb_get();    // 300ms
  // Round to nearest interval of photo taking
  t  = mround_ul(UTC.now(), _takePhotoPeriodS);

  if (!f) {
    throw std::runtime_error("Failed to get fb with esp_camera_fb_get()");
  }

  String filename;
  if (defaultTZ->year(t) > 2000) {
    // Timestamped
    // Careful! filesystem doesn't like weird names (fails to write)
    filename = _saveFolder + defaultTZ->dateTime(t, "Y-m-d\\TH-i-s") + ".jpg";
  } else {
    // Sequence. No time is available
    char ibuf[8 + 1]; // 9, 999, 999 + 1 for \0 is 3 years @ 10s
    snprintf(ibuf, 8, "%06d", ++TimeLapseCameraFileNumber);
    filename = _saveFolder + _series + String(ibuf) + ".jpg";
    // Prepend (aa,ab,etc) to avoid overwriting sequences
    // If we get to "zz", start overwriting
    while (_filesys.exists(filename) && _series != "zz") {
      filename = _saveFolder + nextSeries() + String(ibuf) + ".jpg";
    }
  }
  // To avoid overwriting, add a prefix (A-Z). This is easily stripped out
  // in post-processing, cos its a non-digit

  //debugV("2 Took");
  //'debugV("Saving %s (%dkB) at %s", filename.c_str(), f->len / 1024, defaultTZ->dateTime("y-M-d H:i:s.v").c_str());

  // Prevent other tasks readings a half-written file
  // without use of semaphores
  //portENTER_CRITICAL();

  // FILE*.open takes 900ms, but much better than 2500ms for fs::File.open
  FILE* file;
  int bytesWritten = 0;
  _lastImage = filename;  // lastImage will without /sdcard/ for retrival via URL
  filename = "/sdcard/" + slash("", filename, "");
  if ( (file = fopen(filename.c_str(), "w")))  {   // 900ms
    //debugV("T2.1 Opened");
    debugV("Writing: t:%s fb %d: ->buf:%d ->len:%lu ->wxh:%dx%d", ezt::dateTime(t, "Y-m-d H-i-s").c_str(), f, f->buf, f->len, f->width, f->height);
    bytesWritten = fwrite(f->buf, 1, f->len, file);
    //debugV("T2.2 Wrote");
    if (bytesWritten != f->len) {
      debugW("Only wrote: %dB of %dB to %s", (int) bytesWritten, (int) f->len, filename.c_str());
    }
  }  else  {
    esp_camera_fb_return(f);
    //   portEXIT_CRITICAL();
    throw std::runtime_error(String("Could not save file:" + filename).c_str());
  }
  fclose(file); file = NULL;

  debugV("3 Wrote");
  
  // portEXIT_CRITICAL()
  esp_camera_fb_return(f);
  //debugV("T4 Returned");
}


void TimeLapseCamera::takePhotoAndDiscard() {
  camera_fb_t *fb = NULL;

  if (!_camReady) throw std::runtime_error("Failed to get fb with esp_camera_fb_get()");

  fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }
}


void TimeLapseCamera::uploadPhotos(int maxFiles, int maxSeconds) {

  String localDirSlashed;
  String localDir;
  String remoteDir;
  std::vector<String> filesToUpload;
  int nFiles = 0;
  int endSeconds = millis() / 1000 + maxSeconds;
  uint32_t s;
  assert (_ftp);

  localDirSlashed = slash("/", _saveFolder, "/");
  localDir = slash("/", _saveFolder, "");
  remoteDir = slash("", _saveFolder, "/");
  s = millis();

  filesToUpload = ls(localDir, maxFiles);

  Serial.print(localDir); Serial.print(",");
  Serial.print(millis() - s); Serial.print(",");
  Serial.print((long) filesToUpload.size()); Serial.print(",");
  Serial.print(remoteDir); Serial.println("");
  Serial.flush();

  debugV("uploadPhotos: ls:%lums localDir:%s (%ld files) remoteDir:%s", (uint32_t) (millis() - s), localDir.c_str(), (long) filesToUpload.size(), remoteDir.c_str());

  for (size_t i = 0; i < filesToUpload.size(); ++i)  {
    //Serial.println(filesToUpload[i]);
  }

  std::sort(filesToUpload.begin(), filesToUpload.end());

  _ftp->makeDir(remoteDir);

  for (size_t i = 0; i < filesToUpload.size(); ++i)  {
    s = millis();
    _ftp->uploadFile(localDirSlashed + filesToUpload[i], remoteDir + filesToUpload[i]);
    debugV("upload took %lums", millis() - s);
    _lastImageUploaded = filesToUpload[i];
    s = millis();
    if (_deleteOnUpload) {
      String fn = "/sdcard" + localDirSlashed + filesToUpload[i];
      if ( remove(fn.c_str()) != 0 ) {
        perror( "Error deleting file" );
        debugW("Couldn't remove: %s", fn.c_str());
      }
    }
    debugV("delete took %lums", millis() - s);
    if (++nFiles > maxFiles || millis() / 1000 > endSeconds) {
      debugI("uploadPhotos: returning early _upload:%d nFiles:%d of %d", _uploadMode, nFiles, maxFiles);
      return;
    }
  }
}

/******************************************************************
   RTOS Task Functions
*******************************************************************/

/**
   Take a photo every _takePhotoPeriodS seconds.
   Try to take on a multiple of seconds (ie time_t = 0 + period * t)
   Take immediately if overrun, or wait until exact time if underrun.

*/

// time_t unsigned long
void TakePhotoTask (void *timeLapseCamera) {
  time_t      tickZero_Secs;      // time (in epoch seconds) when ticks=0
  time_t      now_Secs;           // eg. 342342344
  TickType_t  now_Tcks;
  signed long long lastWakeAgo_Tcks;  // can be -ve
  time_t      lastWakeTime_Secs;
  TickType_t  lastWakeTime_Tcks;
  TimeLapseCamera* tlc = (TimeLapseCamera*) timeLapseCamera;

  // As TickType_t is unsigned, and we have to specify the *previous* waketime,
  // we must wait until the previous waketime is positive (ie. timenow > period)
  // what a sneeky
  while (xTaskGetTickCount() < (tlc->_takePhotoPeriodS * configTICK_RATE_HZ)) {
    delay(100);
  }
  delay(500);

  now_Tcks = xTaskGetTickCount();     // 13889
  now_Secs = UTC.now();               // 1559617105
  tickZero_Secs = now_Secs - (now_Tcks / configTICK_RATE_HZ); // 1559617102
  lastWakeTime_Secs = ((time_t) now_Secs / tlc->_takePhotoPeriodS) * tlc->_takePhotoPeriodS;
  lastWakeAgo_Tcks = now_Tcks - (lastWakeTime_Secs - tickZero_Secs) * configTICK_RATE_HZ;
  lastWakeTime_Tcks = now_Tcks - lastWakeAgo_Tcks;
  /*
    Serial.print("now_Tcks:"); Serial.println(now_Tcks);
    Serial.print("now_Secs:");  Serial.println(now_Secs);                 //
    Serial.print("tickZero_Secs:"); Serial.println(tickZero_Secs);
    Serial.print("lastWakeAgo_Tcks:"); Serial.println((signed long int) lastWakeAgo_Tcks);
    Serial.print("lastWakeTime_Secs:"); Serial.println(lastWakeTime_Secs);
    Serial.print("lastWakeTime_Tcks:"); Serial.println(lastWakeTime_Tcks);
  */
  debugV("TakePhotoTask has begun!");
  uint32_t s;
  for (;;) {
    vTaskDelayUntil(&lastWakeTime_Tcks, pdMS_TO_TICKS(tlc->_takePhotoPeriodS * 1000));
    try {
      if (tlc->_camReady) {
        //debugV("Take Photo starting at %d ticks, %d S t:%s", (int) xTaskGetTickCount, (int) UTC.now(), defaultTZ->dateTime("Y-m-d H-i-s").c_str());
        tlc->takePhoto();
      }
    } catch (const std::runtime_error& e) {
      debugE("Exception: %s", e.what());
    }
  }
}


/**
   Upload photos continuously whilst the "upload" flag is set
   Poll for it
*/
void UploadPhotosTask (void *timeLapseCamera) {

  TimeLapseCamera* tlc = (TimeLapseCamera *) timeLapseCamera;
  TickType_t  lastWakeTime_Tcks = xTaskGetTickCount();
  debugV("UploadPhotosTask has begun!");

  for (;;) {
    vTaskDelayUntil(&lastWakeTime_Tcks, pdMS_TO_TICKS(tlc->_uploadPhotoPeriodS * 1000));
    try {
      debugV("Check uploading: _upload:%d WiFi:%d", tlc->_uploadMode, WiFi.status() == WL_CONNECTED);
      switch (tlc->_uploadMode) {
        case TimeLapseCamera::OffOnAuto::OFF:
          break;
        case TimeLapseCamera::OffOnAuto::AUTO:
          if (1 /* chg pin off */) {
            break;
          } else {
            // fall through

          }
        case TimeLapseCamera::OffOnAuto::ON:
          if (WiFi.status() == WL_CONNECTED && tlc->_ftp) {
            tlc->uploadPhotos();
          }
          break;
      }
    } catch (const std::runtime_error& e) {
      debugE("Exception: %s", e.what());
    }
  }
}


/**
   base 24 (a-z)
   aa, ab, ac ... zz, aa
*/
String TimeLapseCamera::nextSeries() {

  // c1 | c2
  char c1;
  char c2;
  // asc
  const char a = 97;

  assert(_series.length() == 2);
  // Convert to numbers 0-26,0-26
  c1 = char(_series[0]) - a;
  c2 = char(_series[1]) - a;

  // Base 24 increment
  c2++;
  if (c2 >= 26) {
    c2 = 0;
    c1++;
    if (c1 >= 26) {
      c1 = 0;
    }
  }
  // Back to letters
  _series[0] = char(c1 + a);
  _series[1] = char(c2 + a);
  return _series;
}

// todo error checks on ranges
void TimeLapseCamera::config(const DynamicJsonDocument& data) {
  // Must use containsKey, as (bool) data["saveFolder"] == false (why?)
  Serial.println("config with:");
  serializeJson(data, Serial);

  if (data.containsKey("saveFolder"))     _saveFolder = data["saveFolder"].as<String>();
  if (data.containsKey("wifiMode"))       _wifiMode = (OffOnAuto)  data["wifiMode"].as<int>();
  if (data.containsKey("uploadMode"))     _uploadMode = (OffOnAuto)  data["uploadMode"].as<int>();
  if (data.containsKey("sleepAt"))        _sleepAt = data["sleepAt"];
  if (data.containsKey("wakeAt"))         _wakeAt = data["wakeAt"];
  if (data.containsKey("sleepEnabled"))   _sleepEnabled = data["sleepEnabled"].as<int>();


  if (data.containsKey("frameSize"))  {
    if (_frameSize != (framesize_t)  data["frameSize"].as<int>()) {
      _frameSize = (framesize_t)  data["frameSize"].as<int>();
      
      debugV("Restarting cam with framesize %d", (int) _frameSize);
      
      // Restart required if cam is running
      if (_camReady) {
        end();
        begin(_frameSize);
      }
    }
  }
  
  if (data.containsKey("camReady"))  {
    debugV("Camready: sent:%d now:%d\n", data["camReady"].as<int>() , _camReady);
    if (data["camReady"].as<int>() && !_camReady) {
      debugV("Starting cam");
      begin(_frameSize);
    } else if (!data["camReady"].as<int>() & _camReady) {
      debugV("Stopping cam");
      end();
    } else {
      // No change
    }
  }




  if (data.containsKey("ftpServer")) {
    configFTP(data["ftpServer"].as<String>(),
              data["ftpUsername"].as<String>(),
              data["ftpPassword"].as<String>());
  }
  if (data.containsKey("takePhotoPeriodS")) _takePhotoPeriodS = data["takePhotoPeriodS"].as<int>();

  Serial.println("Save config to file with:");
  serializeJson(data, Serial);

}
// do the rest



DynamicJsonDocument TimeLapseCamera::config() {

  DynamicJsonDocument data(JSON_OBJECT_SIZE(20));

  data["saveFolder"]          = _saveFolder.c_str();
  data["wifiMode"]            = (int) _wifiMode;
  data["uploadMode"]          = (int) _uploadMode;
  data["sleepAt"]             = _sleepAt;
  data["wakeAt"]              = _wakeAt;
  data["sleepEnabled"]        = (int) _sleepEnabled;
  data["takePhotoPeriodS"]  = _takePhotoPeriodS;
  if (_ftp) {
    data["ftpServer"]   = _ftp->_host.c_str();
    data["ftpPort"]     = _ftp->_port;
    data["ftpUsername"] = _ftp->_user.c_str();
    data["ftpPassword"] = _ftp->_password.c_str(); // secure as a beach
  }
  data["camReady"]      = (int) _camReady;
  // Only if set
  if (_frameSize)  data["frameSize"]   = (int) _frameSize;
    Serial.print("Ready to returned:");
  serializeJsonPretty(data, Serial);
  return data; // copy (i hope)
}


DynamicJsonDocument TimeLapseCamera::status() {
  //                      // data             :
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(12));
  String json;
  // Use c_str for JSON to avoid copy (also, String doesn't work!)
  doc["event"] = "status";
  JsonObject data = doc.createNestedObject("data");
  data["saveFolder"]      = _saveFolder.c_str();
  data["lastImage"]      = _lastImage.c_str();
  data["lastImageUploaded"]      = _lastImageUploaded.c_str();
  data["freeheap"]        = ESP.getFreeHeap();
  data["datetime"]        = defaultTZ->dateTime(RFC3339); //"d-M-y H:i:s");
  data["wifiMode"]        = (int) _wifiMode;
  data["uploadMode"]      = (int) _uploadMode;
  data["takePhotoPeriodS"] = _takePhotoPeriodS;
  data["camReady"] = _camReady;

  return doc; // copies
}
