#include "BBEsp32Lib.h"
#include <rom/rtc.h>
#include <dirent.h>
#include <ArduinoLogRemoteDebug.h>
//#include <stdio.h>
//#include <stdlib.h>
#include <ezTime.h>

using namespace std;

/**
   Return "startWith + from + endWith" without any repeated substrings
*/
String slash(const String& startWith, const String&from , const String& endWith) {

  String r = from;

  if (r.startsWith(ps)) {
    r = r.substring(ps.length());

  }
  if (r.endsWith(ps)) {
    r = r.substring(0, r.length() - ps.length());
  }
  return startWith + r + endWith;
}

/**

*/

String  slash(char startWith, const String&from) {
  String r = from;

  if (r.startsWith(ps)) {
    r = r.substring(ps.length());
  }

  return String(startWith)  + r;
}


String slash(const String&from, char endWith) {
  String r = from;

  if (r.endsWith(ps)) {
    r = r.substring(0, r.length() - ps.length());
  }
  return r + String(endWith);
}



/**
     Make just a misc function
    Use posix functions - much faster than Arduino File class for listDir
*/
vector <String> ls(const String& localDir, int maxFiles, const String& endingWith, const String& localFilePrefix) {

  vector <String> r;
  const int isFile = 1;   // Not portable!
  DIR *folder;
  struct dirent *entry;
  int files = 0;
  int niles = 0;
  String localDirPosix;
  String filename;
  String localDirSlashed ;

  localDirSlashed = slash("", localDir, "/");
  localDirPosix = localFilePrefix + slash("", localDir, "");
  folder = opendir(localDirPosix.c_str());

  if (!folder) {
    debugE("ls: couldn't open directory: %s", localDirPosix.c_str());
    return r;
  }

  while  ((entry = readdir(folder)) && files < maxFiles)   {
    if ( entry->d_type == isFile && entry->d_name) {
      filename =  String(entry->d_name);
      if (filename != "" && filename.endsWith(endingWith)) {
        files++;
        r.push_back(filename);
      } else {
        niles++;
      }
    }
  }
  //debugV("Files: %d Niles: %d", files, niles);
  closedir(folder);
  return r;
}

// function to round the number
unsigned long mround_ul(unsigned long  n, int m) {
  // Smaller multiple
  int a = (n / m) * m;
  // Larger multiple
  int b = a + m;
  // Return of closest of two
  return (n - a > b - n) ? b : a;
}


LogFile::LogFile(const char* _filename):
  filename(_filename) {
}

void LogFile::print(const String& message) {
  FILE *f ;
  String t;

  if (defaultTZ->year() > 2000) {
    t = defaultTZ->dateTime("d-M-y H:i:s");
  } else {
    t = String(millis()) + String ("ms");
  }
  if (f = fopen(filename.c_str(), "a")) {
    fprintf(f, "%s: ", t.c_str());
    fputs(message.c_str(), f);
    fputs("\n", f);
    fclose(f); f = NULL;
  } else {
    Serial.printf("LogToFile: couldn't open %s\n", filename.c_str());
  }
}

void LogFile::read() {

  FILE *fp;
  const int MAXCHAR = 256;
  char str[MAXCHAR];

  fp = fopen(filename.c_str(), "r");
  if (fp == NULL) {
    Serial.printf("Could not open log: %s\n", filename);
    return;
  }
  while (fgets(str, MAXCHAR, fp) != NULL) {
    Serial.printf("%s", str);
  }
  fclose(fp);
  if (ferror(fp)) {
    Serial.printf("Error reading log: %s\n", filename);
    perror(filename.c_str());
  }
}

void LogFile::tail(int lines) {
  int count = 0;  // To count '\n' characters
  FILE* f;
  if (!(f = fopen(filename.c_str(), "r"))) {
    return;
  }
  const int MAXLINE =   256;
  unsigned long long pos;
  char str[2 * MAXLINE];

  // Go to End of file
  if (fseek(f, 0, SEEK_END)) {
    perror("fseek() failed");
  } else  {
    // pos will contain no. of chars in    // input file.
    pos = ftell(f);
    // search for '\n' characters
    while (pos)  {
      // Move 'pos' away from end of file.
      if (!fseek(f, --pos, SEEK_SET))      {
        if (fgetc(f) == '\n') {
          // stop reading when n newlinesis found
          if (count++ == lines)
            break;
        }
      } else {
        perror("fseek() failed");
      }
    }
    while (fgets(str, sizeof(str), f))
      printf("%s", str);
  }
  printf("\n\n");
}

void LogFile::head(int lines) {
}

String resetReason(int core)
{
  assert(core == 1 || core == 0);
  switch ( rtc_get_reset_reason(core))
  {
    case 1 : return String("POWERON_RESET");        /**<1, Vbat power on reset*/
    case 3 : return String("SW_RESET");              /**<3, Software reset digital core*/
    case 4 : return String ("OWDT_RESET");           /**<4, Legacy watch dog reset digital core*/
    case 5 : return String ("DEEPSLEEP_RESET");        /**<5, Deep Sleep reset digital core*/
    case 6 : return String ("SDIO_RESET");           /**<6, Reset by SLC module, reset digital core*/
    case 7 : return String ("TG0WDT_SYS_RESET");       /**<7, Timer Group0 Watch dog reset digital core*/
    case 8 : return String ("TG1WDT_SYS_RESET"); ;      /**<8, Timer Group1 Watch dog reset digital core*/
    case 9 : return String ("RTCWDT_SYS_RESET");       /**<9, RTC Watch dog Reset digital core*/
    case 10 : return String ("INTRUSION_RESET");       /**<10, Instrusion tested to reset CPU*/
    case 11 : return String ("TGWDT_CPU_RESET");       /**<11, Time Group reset CPU*/
    case 12 : return String ("SW_CPU_RESET");          /**<12, Software reset CPU*/
    case 13 : return String ("RTCWDT_CPU_RESET");      /**<13, RTC Watch dog Reset CPU*/
    case 14 : return String ("EXT_CPU_RESET");         /**<14, for APP CPU, reseted by PRO CPU*/
    case 15 : return String ("RTCWDT_BROWN_OUT_RESET");  /**<15, Reset when the vdd voltage is not stable*/
    case 16 : return String ("RTCWDT_RTC_RESET");      /**<16, RTC Watch dog reset digital core and rtc module*/
    default : return String ("NO_MEAN");
  }
}
