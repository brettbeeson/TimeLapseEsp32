#include <dirent.h>     // POSIX directory functions
#include <stdexcept>
#include <algorithm>    // sort

#include "FTPUploader.h"
#include "BBEsp32Lib.h"
#include "ArduinoLogRemoteDebug.h"

using namespace std;


FTPUploader::FTPUploader(String host, String user, String password, int port, String localFilePrefix) :
  _host(host),
  _user(user),
  _password(password),
  _port(port),
  _ftpClientNetBuf(NULL),
  _ftpClient(NULL)
{
  _localFilePrefix = slash("/", localFilePrefix, "/");
}

bool FTPUploader::makeDir(const String& dir) {
  String dirSlashed = slash("", dir, "");
  ensureConnected();
  return _ftpClient->ftpClientMakeDir(dirSlashed.c_str(), _ftpClientNetBuf);
}
/**
   Upload a single file. Binary mode.
*/
void FTPUploader::uploadFile(String localFile, String remoteFile, bool overwrite) {

  ensureConnected();

  if (remoteFile == "") {
    remoteFile = localFile;
  }

  localFile = _localFilePrefix + slash("", localFile, "");
  remoteFile = slash("", remoteFile, "");

  if (overwrite) {
    _ftpClient->ftpClientDelete(remoteFile.c_str(), _ftpClientNetBuf);
  }
  debugV("ftpClientPut: %s to %s", localFile.c_str(), remoteFile.c_str());

  if (!_ftpClient->ftpClientPut(localFile.c_str(), remoteFile.c_str(), FTP_CLIENT_BINARY, _ftpClientNetBuf)) {
    throwError("ftpClientPut");
  }
}


void FTPUploader::ensureConnected () {
  const int maxy = 128;
  char buf[maxy];

  // todo: handle ftpserver reset connection

  // If connected already... return
  //
  if (_ftpClient && _ftpClientNetBuf) {
    if (_ftpClient->ftpClientGetSysType(buf, maxy, _ftpClientNetBuf)) {
      // OK
      return;
    } else {
      //_ftpClient->closeFtpClient(_ftpClientNetBuf);
      debugE("disconnected? TODO");
    }
  }
  if (!_ftpClient) {
    _ftpClient = getFtpClient();
  }

  // Try to connect
  //
  // Must manual lookup IP as ftpClient.c library doesn't work with hostname, only IP
  IPAddress hostIP;
  if (!WiFi.hostByName(_host.c_str(), hostIP) ) {
    String e = "Couldn't lookup host: " + _host;
    throw runtime_error(e.c_str());
  }
  if (!_ftpClient->ftpClientConnect(hostIP.toString().c_str(), 21, &_ftpClientNetBuf)) {
    throwError("ftpClientConnect error:");
  }
  if (!_ftpClient->ftpClientLogin(_user.c_str(), _password.c_str(), _ftpClientNetBuf)) {
    throwError("ftpClientLogin error:");
  }
}

bool FTPUploader::testConnection() {
  try {
    ensureConnected();
    return true;
  } catch (const exception & e) {
    debugW("testFTPConnection: %s", e.what());
    return false;
  }

}
void FTPUploader::throwError(String suffix) {

  char* err;
  String errStr;

  err = _ftpClient->ftpClientGetLastResponse(_ftpClientNetBuf);
  errStr = suffix + ":" + (err ? String(err) : "unknown");
  throw runtime_error(errStr.c_str());
}
