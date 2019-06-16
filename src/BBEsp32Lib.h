#pragma once

#include <Arduino.h>

#include <vector>

//namespace bblib {

const String ps = "/";
String slash(const String& stripFromStart, const String&from , const String& stripFromEnd);
String slash(char withStart, const String&from);
String slash(const String&from, char withEnd);
std::vector <String> ls(const String& localDir, int maxFiles=100, const String& endingWith = "", const String& localFilePrefix = "/sdcard/");
unsigned long mround_ul(unsigned long  n, int m);
String resetReason(int core=0);

class LogFile {
  public:
    LogFile(const char* filename);
    //void printf(const char* message, ...);
    void print(const String& message);
    void read();
    void tail(int lines=10);
    void head(int lines=10);
    String filename;
};
//}
