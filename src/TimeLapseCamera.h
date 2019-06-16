#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "esp_camera.h"
#include <FS.h>
//#include "DropboxManager.h"
#include <RemoteDebug.h>
#include <ArduinoJson.h>

class FTPUploader;
class TimeLapseWebServer;
class TimeLapseWebSocket;
/*
    QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
            |640|800|1024|1400|1600
            |480|600|768 |960 |1200

*/
// typedef struct {
//  uint8_t * buf;              /*!< Pointer to the pixel data */
// size_t len;                 /*!< Length of the buffer in bytes */
// size_t width;               /*!< Width of the buffer in pixels */
// size_t height;              /*!< Height of the buffer in pixels */
// pixformat_t format;         /*!< Format of the pixel data */
//} camera_fb_t;

// #define __SIZE_TYPE__ long unsigned int

extern RTC_DATA_ATTR int TimeLapseCameraFileNumber;

class TimeLapseCamera {

  enum OffOnAuto {
      OFF = 0,
      ON = 1,
      AUTO = 2
    };

  public:
  
    TimeLapseCamera(fs::FS &filesys, String saveFolder = "/", int takePhotoPeriodS = 10, int uploadPhotoPeriodS = 60);
    void config(const DynamicJsonDocument& doc); // write
    DynamicJsonDocument config(); // read
    DynamicJsonDocument status(); // read
    void begin(framesize_t frameSize = FRAMESIZE_XGA, int quality = 8);
    void end();
    uint32_t sleepy(); // seconds to sleep for (0 if not required)

    void takePhotoTaskify(int priority);
    void uploadPhotosTaskify(int priority);
    void configFTP(const String& host, const String& user, const String& password);
    bool testFTPConnection();
    void takePhotoAndDiscard();
    void takePhoto();
    void uploadPhotos(int maxFiles = 100, int maxSeconds = 600);

    friend void TakePhotoTask (void *timeLapseCamera);
    friend void UploadPhotosTask (void *timeLapseCamera);
    friend TimeLapseWebServer;
    friend TimeLapseWebSocket;

  private:


    String nextSeries();

    fs::FS &_filesys;
    int _takePhotoPeriodS;
    int _uploadPhotoPeriodS;
    OffOnAuto _uploadMode;
    OffOnAuto _wifiMode;  
    String _saveFolder;
    bool _deleteOnUpload;
    FTPUploader *_ftp;
    String _lastImage;
    String _lastImageUploaded;
    int _quality = 8;
    int _sleepAt = 0;
    int _wakeAt = 0;
    bool _sleepEnabled = false;
    TaskHandle_t _uploadPhotosTaskHandle = NULL;
    TaskHandle_t _takePhotoTaskHandle = NULL;
    bool _camReady = false;
    framesize_t _frameSize = FRAMESIZE_XGA;

    String _series;

};
