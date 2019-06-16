#pragma once

#include <FS.h>
#include <vector>
#include "FtpClient.h"

class FTPUploader {

  public:
  
    FTPUploader(String hostOrIP, String user, String password, int port = 21, String localFilePrefix = "/sdcard/");
    void begin();
    bool makeDir(const String& dir);
    // Upload a single file
    void uploadFile(String localFile, String remoteFile = "", bool overwrite = true);
    // Upload all files in localDir (must be a dir). Include files: *endingWith
    //void uploadFiles(String localDir, String remoteDir, String endingWith = "", bool deleteOnSuccess = false, int maxFiles = 100, int maxSeconds = 600, bool *continueFlag);
    bool testConnection();

    friend class TimeLapseCamera;
    
  private:

    String _host;
    String _user;
    int _port;
    String _password;
    // Arudino FS mounts SD_MCC at /,while FILE* sees files at /sdcard/
    String _localFilePrefix;
    NetBuf_t* _ftpClientNetBuf;
    FtpClient* _ftpClient;
   
    void ensureConnected();         // Throw if fail
    void throwError(String suffix);
  
};
