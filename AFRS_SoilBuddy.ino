// MIT License

// Copyright (c) 2024 Andrew Bierer

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//-----Device---------------------------------------------------------------------------
/*

~Soil Buddy~

Purpose: Easy to build solution for monitoring capacitance based soil sensors (Adafruit STEMMA Soil Sensor I2C, product 4026).
The ESP32-S3 feather unit incorporates a wifi module which is used for crude device control and monitoring via WifiManager. 

If uploading new code and serial port is not discoverable, you need to place the esp32 into boot mode...

NOTE: As of 07/15/2024 the ESP32 core 3.0+ is resulting in uploading problems on the ESP32-S3 board. Reverting to the 2.0.17 core fixes the issue.

  Version History:
2024.05.xx -> In development

*/

//-----Pre processor directive---------------------------- //linked to the S3 device not accepting uploads etc... https://github.com/espressif/arduino-esp32/issues/9580
// #if (!defined(CONFIG_IDF_TARGET_ESP32S3)) && (!defined(CONFIG_IDF_TARGET_ESP32C3))
//     deinit(NULL);
//     delay(10);  // USB Host has to enumerate it again
// #endif

//-----Libraries------------------------------------------------------------------------------------------------- ""local <>standard
#include <SPI.h>              //For SPI communication
#include <Wire.h>             //For I2C communication
 s
#include "Adafruit_seesaw.h"  //For Adafruit Capacitance Sensor
#include <FS.h>               //For SPIFFS, include before SPIFFS
#include <SPIFFS.h>           //For SPIFFS functionality
#include "time_zones.h"       //For timeSync
#include "time.h"             //for time elements
#include "sntp.h"             //For timeSync call back
#include <ArduinoJson.h>      //Json file format use
#include <ArduinoJson.hpp>    //Json file format use

//-----Assign Pins-----------------------------------------------------------------------------------------------
#define FORMAT_SPIFFS_IF_FAILED true     //For SPIFFS formatting, should only need setup once...
#define microsecondsToSeconds 1000000LL  //Conversion factor for micro seconds to seconds
//-----Declare Global Variables----------------------------------------------------------------------------------

bool timeSync = true;
uint16_t timezone = 154;  //From time_zones.h
char daysOfTheWeek[7][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

//precede declaration with RTC_DATA_ATTR to save in RTC fast memory
RTC_DATA_ATTR unsigned long anticipatedWakeUnix;  //to track internal esp32 rtc oscillations, stored in RTC
RTC_DATA_ATTR int bootCount = 0;
long waitingLoopCount = 0;  //temp to count loops through waiting state.
int defaultSleep = 30;      //default seconds between wake/sleep cycles.

enum states { IDLE,
              CLIENT,
              MEASURE,
              SYNC,
              WAITING };  //Add states here


bool stateBool[5]{ false, false, false, false, false };  //element 0 = IDLE, 1 = CLIENT, 2 = MEASURE, 3 = SYNC, 4 = WAITING
states currentState = IDLE;                              //The starting state

ArduinoJson::StaticJsonDocument<1024> jsonBuffer;  // Hopefully declaring once here is OK

struct deviceConfig {
  uint16_t timezone = 154;                //default to Eastern Time New York, USA
  uint16_t deviceID = 1;                  //defaults
  uint16_t capacitanceLowerLimit = 200;   //defaults
  uint16_t capacitanceUpperLimit = 2000;  //defaults
  uint16_t measureInterval = 60;          //default to 60 minute measure interval

  char databaseServer[255];
  char ntpServer1[255];  //e.g., "pool.ntp.org", "time.nist.gov"
  char ntpServer2[255];
};

deviceConfig configObject;  //Declare an object with this structure

//-----Initialize------------------------------------------------------------------------------------------------
Adafruit_seesaw ss;  //The Adafruit Soil Sensor
tm timeinfo, lastSync, eventTime, nextSync;


WiFiManager wm;  //global instance
//configuration related



/*WORKS
const char* ggg = "154";
WiFiManagerParameter customParameterTimezone("my_text", "Select Timezone Integer (0 to 461)", ggg, 50);
*/
// new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\"");

//define your default values here, if there are different values in config.json, they are overwritten.
//length should be max size + 1
char globalTimezone[50] = "154";  // Eastern US time
char globalDeviceID[50] = "1";
char globalCapacitanceLowerLimit[50] = "200";
char globalCapacitanceUpperLimit[50] = "2000";
char globalMeasureInterval[50] = "60";
char globalDatabaseServer[255] = "Your_Database_Server";
char globalNtpServer1[255] = "pool.ntp.org";
char globalNtpServer2[255] = "time.nist.gov";
char soilTempCGlobal[50];
char soilMoistureGlobal[50];
char measureTimestamp[50];

WiFiManagerParameter customParameterTimezone("my_text", "Select Timezone Integer, 0 to 461", globalTimezone, 50);
WiFiManagerParameter customParameterDeviceID("my_text", "SoilBuddy DeviceID, 0 to 65535", globalDeviceID, 50);
WiFiManagerParameter customParameterCapacitanceLowerLimit("my_text", "Sensor Capacitance Lower Limit, dry default = 200)", globalCapacitanceLowerLimit, 50);
WiFiManagerParameter customParameterCapacitanceUpperLimit("my_text", "Sensor Capacitance Upper Limit, wet default = 2000)", globalCapacitanceUpperLimit, 50);
WiFiManagerParameter customParameterMeasureInterval("my_text", "Measurement Interval, minutes 1 to 60)", globalMeasureInterval, 50);
WiFiManagerParameter customParameterDatabaseServer("my_text", "Database Server Webaddress", globalDatabaseServer, 255);
WiFiManagerParameter customParameterNtpServer1("my_text", "Time Sync. Server 1", globalNtpServer1, 255);
WiFiManagerParameter customParameterNtpServer2("my_text", "Time Sync. Server 2", globalNtpServer2, 255);


//std::new (&customParameterTimezone) WiFiManagerParameter("customParameterTimezone", "Select Timezone Integer, 0 to 461", jsonBuffer["jsonOut"]["timezone"], 50); no
//WiFiManagerParameter customParameterTimezone("my_text", "Select Timezone Integer, 0 to 461", dynamicWiFiManagerParam(jsonBuffer,"jsonOut","timezone"), 50); no
//WiFiManagerParameter customParameterTimezone("my_text", "Select Timezone Integer, 0 to 461", jsonBuffer["jsonOut"]["timezone"], 50); no

// WiFiManagerParameter customParameterTimezone("my_text", "Select Timezone Integer, 0 to 461", jsonBuffer["jsonOut"]["timezone"], 50);
// WiFiManagerParameter customParameterDeviceID("my_text", "SoilBuddy DeviceID, 0 to 65535", jsonBuffer["jsonOut"]["deviceID"], 50);
// WiFiManagerParameter customParameterCapacitanceLowerLimit("my_text", "Sensor Capacitance Lower Limit, dry default = 200)", jsonBuffer["jsonOut"]["capacitanceLowerLimit"], 50);
// WiFiManagerParameter customParameterCapacitanceUpperLimit("my_text", "Sensor Capacitance Upper Limit, wet default = 2000)", jsonBuffer["jsonOut"]["capacitanceUpperLimit"], 50);
// WiFiManagerParameter customParameterMeasureInterval("my_text", "Measurement Interval, minutes 1 to 60)", jsonBuffer["jsonOut"]["measureInterval"], 50);
// WiFiManagerParameter customParameterDatabaseServer("my_text", "Database Server Webaddress", jsonBuffer["jsonOut"]["databaseServer"], 50);
// WiFiManagerParameter customParameterNtpServer1("my_text", "Time Sync. Server 1", jsonBuffer["jsonOut"]["ntpServer1"], 50);
// WiFiManagerParameter customParameterNtpServer2("my_text", "Time Sync. Server 2", jsonBuffer["jsonOut"]["ntpServer2"], 50);

//data related -> not needed in eeprom configObject
WiFiManagerParameter customParameterMeasureTimestamp("my_text", "Last Measurement Timestamp", measureTimestamp, 50);
WiFiManagerParameter customParameterSoilTempCGlobal("my_text", "Soil Temperature, Degrees Celcius", soilTempCGlobal, 50);  //These work as global char arrays
WiFiManagerParameter customParameterSoilMoistureGlobal("my_text", "Volumetric Soil Moisture Content, Percent", soilMoistureGlobal, 50);

//______________________________________________________________________________________________________________________________________________________________________________________
void setup() {
  // put your setup code here, to run once:
  // setup code runs on power cycle and after deep sleep.
  hardware_init();
}
//______________________________________________________________________________________________________________________________________________________________________________________
void loop() {
  // put your main code here, to run repeatedly:
  Demo_loop();
  //Normal_loop();
}
//_____________________________________________________________________________________________________________________________________________________________________________
//-----Other Defined Functions------
//_____________________________________________________________________________________________________________________________________________________________________________
void hardware_init() {

  Serial.begin(115200);  //Not needed when USB CDC on boot is enabled.
  delay(3000);           //temp for troubleshooting init...

  Wire.begin();
  Serial.println(F("SoilBuddy"));

  //SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println(F("SPIFFS Mount Failed"));
  }
  importConfig(configObject);

  // int customFieldLength = 50;
  // new (&customParameterTimezone) customParameterTimezone("my_text", "Select Timezone Integer, 0 to 461", jsonBuffer["jsonOut"]["timezone"], 50);
  // wm.addParameter(&customParameterTimezone);
  // //updateWifiManagerParameters();  //Define the WiFiManager Parameters once in setup from global instance

  //Attempt to locate sensor
  if (!ss.begin(0x36)) {
    Serial.println(F("Soil Sensor Not Found..."));
  } else {
    Serial.print(F("Soil Sensor Version:  "));
    Serial.println(ss.getVersion(), HEX);
    Serial.println(F("Let's begin..."));
  }

  //Check and set time / timezone
  setenv("TZ", timezones[(timezone * 2) - 2], 1);
  tzset();
  Serial.print(F("anticipatedWakeUnix: "));
  Serial.println(anticipatedWakeUnix);
  if (anticipatedWakeUnix > 0) {
    time_t resumeTime = anticipatedWakeUnix;
    time(&resumeTime);
    localtime_r(&resumeTime, &timeinfo);
  } else {
    Serial.println(F("It looks like this is the first time this SoilBuddy device was powered on."));  //first cycle through
    measureLoop();                                                                                    //force a measurement on first cycle through
    configResetCallback();                                                                    //Define the WiFiManager Parameters once in setup from global instance
    timeSync = true;
    syncLoop();  //force time sync on first cycle through
  }
  Serial.println(F("Initialization Complete."));
  wakeupReason();  //print wakeup reason from sleep
};



void Demo_loop() {
  float tempC = ss.getTemp();
  uint16_t capread = ss.touchRead(0);
  Serial.print(F("Temperature: "));
  Serial.print(tempC);
  Serial.println(F(" Celcius"));

  capread = map(capread, 200, 2000, 0, 100);  //Map range of sensor from 0 to 100

  Serial.print(F("Volumetric Water Content: "));
  Serial.println(capread);
  Serial.println();
  Serial.println();
  Serial.println(F("Connect Wifi to the device's wireless access point to configure Soil Buddy."));
  Serial.println();
  delay(500);
}

void Normal_loop() {

  //Check events
  checkEvents();

  //Evaluate state
  soilBuddyState();
 
  // //Reset waiting bools
  // setWaitBoolsByState();

  //If in Access Point mode, check for client
  
  
  //array of difftime() results to evaluate whether to enter WAITING state
  double eventDifftime[61];       //static size of 60 is the max size of measureInterval (60 minutes = 1once per minute) + 1 entry for time sync
  for (int i = 0; i < 61; i++) {  // populate a default
    eventDifftime[i] = 9999.99;
  }

  //Print current state
  Serial.println(F("top of Normal_loop"));
  for (int i = 0; i < 4; i++) {
    Serial.print(F("stateBool["));
    Serial.print(i);
    Serial.print(F("]:  "));
    Serial.println(stateBool[i]);
  }

  //Check time
  char strftime_buf[64];
  time_t now;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  Serial.println(strftime_buf);
  //nowRoundSecondsDown.tm_sec = timeinfo;
  //nowRoundSecondsDown.tm_sec = 0;

  //Has the time been set before?
  if (timeinfo.tm_year == 70) {  //1970 is default, tm_year= years since 1900
    Serial.println(F("Time appears to have never been set..."));
    //no = change state to SYNC
    Serial.println(F("SYNC1"));
    if (currentState == IDLE || currentState == CLIENT) {
      currentState = SYNC;
    }
  };

  //Does the current time indicate the need for time synchronization?
  //to prevent lots of same timed requests concider deviceID (uint16_t or max of 65535) as seconds,
  //convert to minutes and hours, and have that be the scheduled time sync offset from 00:00:00.
  nextSync = timeinfo;  //populate
  float nextSyncHoursByDeviceid, nextSyncHoursByDeviceidFloat, nextSyncRemainderMin;
  nextSyncHoursByDeviceidFloat = ((configObject.deviceID / 60) / 60);                        //convert deviceID (as seconds) to hours
  nextSyncRemainderMin = std::modf(nextSyncHoursByDeviceidFloat, &nextSyncHoursByDeviceid);  //modf splits float into whole & fraction
  nextSyncRemainderMin = std::floor(nextSyncRemainderMin * 60);                              //convert the hour decimal to minutes
  nextSync.tm_hour = 0 + std::floor(nextSyncHoursByDeviceidFloat);                           //floor rounds a float down
  nextSync.tm_min = 0 + nextSyncRemainderMin;
  if (timeinfo.tm_hour == nextSync.tm_hour && timeinfo.tm_min == nextSync.tm_min) {  //midnight is standard, then augmented by deviceID
    //yes, this is the time to sync time with ntp server
    Serial.println(F("Scheduled time synchronization will be attempted."));
    Serial.print("tm_hour: ");
    Serial.println(timeinfo.tm_hour);
    Serial.print("tm_min: ");
    Serial.println(timeinfo.tm_min);
    if (currentState == IDLE || currentState == CLIENT) {
      currentState = SYNC;
      Serial.println(F("SYNC2"));
    }
  };

  //Calculate seconds between now and beginning of the next day as presentDaySecondsRemaining
  tm nextDay = timeinfo;                   //take present time
                                           // nextDay.tm_year = now.tm_year;
                                           // nextDay.tm_mon = now.tm_mon;
  nextDay.tm_mday = timeinfo.tm_mday + 1;  //increment a day
  nextDay.tm_hour = 0;
  nextDay.tm_min = 0;
  nextDay.tm_sec = 0;
  time_t nextDayNormalized = mktime(&nextDay);  //normalize the result to deal with overflows
  double presentDaySecondsRemaining = difftime(nextDayNormalized, now);
  Serial.print(F("presentDaySecondsRemaining: "));
  Serial.println(presentDaySecondsRemaining);
  //position 0 on the eventDifftime array is always the time of the next sync, to potentially enter WAITING state for scheduled time sync
  eventDifftime[0] = std::floor(presentDaySecondsRemaining);  //remove float
  //Is the the measureInterval a multiple of the current tm_min? I.e. a 15 minute measureInterval will have 0, 15, 30, and 45 ->(60 never appears in counting min, 59 rolls to 0)
  float minuteMultiple;
  int maxMultiple = 60 / configObject.measureInterval;  //e.g. 60/15 = 4, 60/30 = 2
  minuteMultiple = (float)timeinfo.tm_min / (float)configObject.measureInterval;
  int multipleMatch[maxMultiple];
  Serial.print(F("minuteMultiple: "));
  Serial.println(minuteMultiple);
  Serial.print(F("maxMultiple: "));
  Serial.println(maxMultiple);

  for (int i = 0; i <= maxMultiple; i++) {
    eventTime = timeinfo;
    //60 should never be used (even on measureInterval of 60) as it never appears in tm_min... for once per hour, need to look at tm_min = 0
    if (i == 0) {
      if (configObject.measureInterval == 60) {
        multipleMatch[i] = 0;
      }
    } else {
      multipleMatch[i] = configObject.measureInterval * i;
    }
    eventTime.tm_min = multipleMatch[i];

    //After changing the tm_min, if the difference in time is 0 or negative (as in the change to eventTime resulted in a time in the past)
    //need to convert the tm struct to time since epoch as time_t
    if (difftime(mktime(&eventTime), now) <= 0.0) {
      Serial.println(F("Handle time overflows..."));
      long tempUnix = mktime(&eventTime);
      tempUnix = tempUnix + 3600;          //add 1 hour to the current time so eventTime can now be scheduled properly
      localtime_r(&tempUnix, &eventTime);  //convert tempUnix and store in eventTime
      Serial.print(F("New eventTime:  "));
      Serial.println(asctime(&eventTime));
    }

    if (i > 0) {
      eventDifftime[i] = difftime(mktime(&eventTime), now);  //eventDifftime[0] is always the next time sync, eventDifftime[1] starts the time till measure
    }
    Serial.print(F("multipleMatch["));
    Serial.print(i);
    Serial.print(F("]:  "));
    Serial.println(multipleMatch[i]);
    Serial.print(F("eventDifftime["));
    Serial.print(i);
    Serial.print(F("]:  "));
    Serial.println(eventDifftime[i]);
    if (multipleMatch[i] == timeinfo.tm_min) {  //mins match
      //yes = change state to MEASURE
      if (currentState == IDLE || currentState == CLIENT) {
        currentState = MEASURE;
        Serial.println(F("Measure1"));
      }
    }
  };

  //Offer AP access periodically
  //This will be for 1 minute per hour to satisfy lowpower requirements
  //10 seconds at the beginning of each sixth hour
  // 00, 10, 20, 30, 40, 50
  int apAccess[6]{ 0, 10, 20, 30, 40, 50 };
  for (int i = 0; i < 6; i++) {
    if (timeinfo.tm_min == apAccess[i]) {
      //tm_min match
      //if (timeinfo.tm_sec <= 10) { //would need to be in eventDifftime
      //AP access
      if (currentState == IDLE) {
        Serial.println(F("Scheduled AP access time."));
        currentState = CLIENT;
      }
      //}
    } else {
      //No AP access
    }
  }

  //Determine how long until the next measureInterval or time sync (always eventDifftime[0]) or AP access
  for (int i = 0; i < 61; i++) {
    if (eventDifftime[i] != 9999.99) {  //check for valid events
      //Serial.println(F("pass 9999.99 test"));
      if (eventDifftime[i] < (((float)defaultSleep) + 5)) {  //30+5 = 35
        //If the difference in time (in seconds) is < than the defaultSleep length + buffer (in seconds), we need to wait for that event
        Serial.print(F("sleep length + buffer:  "));
        Serial.println((((float)defaultSleep) + 5));
        Serial.println(F("We should wait for the next event"));
        Serial.print(F("eventDifftime["));
        Serial.print(i);
        Serial.print(F("]:  "));
        Serial.print(eventDifftime[i]);
        Serial.println(F(" seconds from now."));
        if (currentState == IDLE) {
          currentState = WAITING;
          Serial.println(F("WAITING1"));
        }
      }
    } else {
      //No need to wait
      if (currentState != CLIENT && currentState != SYNC) {
        Serial.println(F("Do not wait for next event."));
        currentState = IDLE;
        break;  //should only need to evaluate the passing difftimes from above
      }
    }
  };

  //call the state machine with every loop
  //default to IDLE
  // if (currentState != MEASURE && currentState != SYNC && currentState != CLIENT && currentState != WAITING) {
  //   currentState = IDLE;
  // }
  soilBuddyState();
  //EVERYTHING ELSE SHOULD BE HANDLED IN THE SETUP.


}

//-----read updated WiFiManager parameters-----
void readUpdatedParametersFromAP() {

  Serial.println(F("in readUpdatedParametersFromAP. getValue()")); //not correct values returned
  Serial.println(customParameterTimezone.getValue());
  Serial.println(customParameterDeviceID.getValue());
  Serial.println(customParameterCapacitanceLowerLimit.getValue());
  Serial.println(customParameterCapacitanceUpperLimit.getValue());
  Serial.println(customParameterMeasureInterval.getValue());
  Serial.println(customParameterDatabaseServer.getValue());
  Serial.println(customParameterNtpServer1.getValue());
  Serial.println(customParameterNtpServer2.getValue());
  Serial.println(customParameterMeasureTimestamp.getValue());
  Serial.println(customParameterSoilTempCGlobal.getValue());
  Serial.println(customParameterSoilMoistureGlobal.getValue());

  strcpy(globalTimezone, customParameterTimezone.getValue());
  strcpy(globalDeviceID, customParameterDeviceID.getValue());
  strcpy(globalCapacitanceLowerLimit, customParameterCapacitanceLowerLimit.getValue());
  strcpy(globalCapacitanceUpperLimit, customParameterCapacitanceUpperLimit.getValue());
  strcpy(globalMeasureInterval, customParameterMeasureInterval.getValue());
  strcpy(globalDatabaseServer, customParameterDatabaseServer.getValue());
  strcpy(globalNtpServer1, customParameterNtpServer1.getValue());
  strcpy(globalNtpServer2, customParameterNtpServer2.getValue());

  strcpy(measureTimestamp, customParameterMeasureTimestamp.getValue());
  strcpy(soilTempCGlobal, customParameterSoilTempCGlobal.getValue());
  strcpy(soilMoistureGlobal, customParameterSoilMoistureGlobal.getValue());

  Serial.println(F("in readUpdatedParametersFromAP. globals after strcpy.")); // not correct values returned
  Serial.println(globalTimezone);
  Serial.println(globalDeviceID);
  Serial.println(globalCapacitanceLowerLimit);
  Serial.println(globalCapacitanceUpperLimit);
  Serial.println(globalMeasureInterval);
  Serial.println(globalDatabaseServer);
  Serial.println(globalNtpServer1);
  Serial.println(globalNtpServer2);
  Serial.println(measureTimestamp);
  Serial.println(soilTempCGlobal);
  Serial.println(soilMoistureGlobal);

  //Try it all in a single callback function - maybe scope was wrong?
  jsonBuffer.clear();
  JsonObject jsonOut = jsonBuffer.createNestedObject("jsonOut");

  jsonOut["timezone"] = globalTimezone;
  jsonOut["deviceID"] = globalDeviceID;
  jsonOut["capacitanceLowerLimit"] = globalCapacitanceLowerLimit;
  jsonOut["capacitanceUpperLimit"] = globalCapacitanceUpperLimit;
  jsonOut["measureInterval"] = globalMeasureInterval;
  jsonOut["databaseServer"] = globalDatabaseServer;
  jsonOut["ntpServer1"] = globalNtpServer1;
  jsonOut["ntpServer2"] = globalNtpServer2;

  jsonOut["measureTimestamp"] = measureTimestamp;
  jsonOut["soilTempCGlobal"] = soilTempCGlobal;
  jsonOut["soilMoistureGlobal"] = soilMoistureGlobal;

  Serial.print(F("jsonBuffer at the end of readUpdatedWifiManagerParams:  "));
  serializeJson(jsonBuffer, Serial);  //check

}

//-----updateGlobalMeasuresFromJson----------------------------------------------------------------------------------
void updateGlobalMeasuresFromJson() {

  //added 9/12/2024
  strcpy(globalTimezone, jsonBuffer["jsonOut"]["timezone"]);
  strcpy(globalDeviceID, jsonBuffer["jsonOut"]["deviceID"]);
  strcpy(globalCapacitanceLowerLimit, jsonBuffer["jsonOut"]["capacitanceLowerLimit"]);
  strcpy(globalCapacitanceUpperLimit, jsonBuffer["jsonOut"]["capacitanceUpperLimit"]);
  strcpy(globalMeasureInterval, jsonBuffer["jsonOut"]["measureInterval"]);
  strcpy(globalDatabaseServer, jsonBuffer["jsonOut"]["databaseServer"]);
  strcpy(globalNtpServer1, jsonBuffer["jsonOut"]["ntpServer1"]);
  strcpy(globalNtpServer2, jsonBuffer["jsonOut"]["ntpServer2"]);
  //

  strcpy(measureTimestamp, jsonBuffer["jsonOut"]["measureTimestamp"]);
  strcpy(soilTempCGlobal, jsonBuffer["jsonOut"]["soilTempCGlobal"]);
  strcpy(soilMoistureGlobal, jsonBuffer["jsonOut"]["soilMoistureGlobal"]);
}

//-----importConfig & last measurement details------------------------------------------------------------------------------
//Imports /config.json and passes to configObject, last measurement details saved to globals.
void importConfig(deviceConfig &trgt) {
  Serial.println(F("Importing configuration..."));
  readFile(SPIFFS, "/config.json");
  File file = SPIFFS.open("/config.json", "r+");
  jsonBuffer.clear();
  DeserializationError error = deserializeJson(jsonBuffer, file);
  Serial.print(F("Deserialized /config.json:  "));
  serializeJson(jsonBuffer, Serial);  //print to Serial port to view import
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  } else {
    Serial.println();
    Serial.println(F("deserializeJson() success."));
  }
  file.close();  //close file

  trgt.timezone = jsonBuffer["jsonOut"]["timezone"];
  trgt.deviceID = jsonBuffer["jsonOut"]["deviceID"];
  trgt.capacitanceLowerLimit = jsonBuffer["jsonOut"]["capacitanceLowerLimit"];
  trgt.capacitanceUpperLimit = jsonBuffer["jsonOut"]["capacitanceUpperLimit"];
  trgt.measureInterval = jsonBuffer["jsonOut"]["measureInterval"];
  // configObject.timezone = jsonBuffer["timezone"];
  // configObject.deviceID = jsonBuffer["deviceID"];
  // configObject.capacitanceLowerLimit = jsonBuffer["capacitanceLowerLimit"];
  // configObject.capacitanceUpperLimit = jsonBuffer["capacitanceUpperLimit"];
  // configObject.measureInterval = jsonBuffer["measureInterval"];

  const char *dataServer = jsonBuffer["jsonOut"]["databaseServer"];
  const char *ntp1 = jsonBuffer["jsonOut"]["ntpServer1"];
  const char *ntp2 = jsonBuffer["jsonOut"]["ntpServer2"];
  for (int i = 0; i < 255; i++) {
    // configObject.databaseServer[i] = dataServer[i];
    // configObject.ntpServer1[i] = ntp1[i];
    // configObject.ntpServer2[i] = ntp2[i];
    trgt.databaseServer[i] = dataServer[i];
    trgt.ntpServer1[i] = ntp1[i];
    trgt.ntpServer2[i] = ntp2[i];
  }
  saveConfig();
  Serial.println(F("inside importConfig: "));
  Serial.print(F("trgt.timezone: "));
  Serial.println(trgt.timezone);
  Serial.print(F("deviceID:  "));
  Serial.println(trgt.deviceID);
  Serial.print(F("ntpServer1:  "));
  Serial.println(trgt.ntpServer1);

  Serial.println(F("Import of configuration complete."));
}

//------saveConfig-------------------------------------------------------------------------------
//Saves the jsonBuffer to flash memory at /config.json
void saveConfig() {
  Serial.println(F("in saveConfig..."));
  // Now serialize and save
  int bytes_req = jsonBuffer.memoryUsage();  //returns number of bytes in the Buffer
  int extra_space = 100;
  int total_space = bytes_req + extra_space;
  Serial.print(F("Actual memory requirement:  "));
  Serial.print(total_space);
  Serial.println(F(" bytes"));
  char json_array[1048];  // char array large enough
  Serial.print(F("Saving Device Configuration:  "));
  serializeJson(jsonBuffer, Serial);  //print to Serial port to view what you are writing. This works fine.
  Serial.println();
  serializeJson(jsonBuffer, json_array);          //copy the info in the buffer to the array to use writeFile below
  writeFile(SPIFFS, "/config.json", json_array);  //write character array to SPIFFS with the specified name, note it will overwrite the file there.
}

//-----POST Request to databaseServer------------------------------------------------------------
bool postRequest(const char *serverName) {
  //Attempts to send whatever is in the jsonBuffer to the serverName
}

//-----downloadWifiManagerParams-------------------------------------------------------------------------
void downloadWifiManagerParams() {
  //Pull from XXXX to populate the jsonBuffer with new entries in the AP...
  //No need to download the measurement data.
  Serial.println(F("Inside downloadWifiManagerParams..."));

  int parameterCount = wm.getParametersCount();
  Serial.print(F("There are "));
  Serial.print(parameterCount);
  Serial.println(F("  parameters in WiFiManager"));
  Serial.println();

  //try pulling from globals
  Serial.println(F("Global Params while inside download function."));
  Serial.println(globalTimezone);
  Serial.println(globalDeviceID);
  Serial.println(globalCapacitanceLowerLimit);
  Serial.println(globalCapacitanceUpperLimit);
  Serial.println(globalMeasureInterval);
  Serial.println(globalDatabaseServer);
  Serial.println(globalNtpServer1);
  Serial.println(globalNtpServer2);
  Serial.println(measureTimestamp);
  Serial.println(soilTempCGlobal);
  Serial.println(soilMoistureGlobal);


  jsonBuffer.clear();
  JsonObject jsonOut = jsonBuffer.createNestedObject("jsonOut");

  jsonOut["timezone"] = globalTimezone;
  jsonOut["deviceID"] = globalDeviceID;
  jsonOut["capacitanceLowerLimit"] = globalCapacitanceLowerLimit;
  jsonOut["capacitanceUpperLimit"] = globalCapacitanceUpperLimit;
  jsonOut["measureInterval"] = globalMeasureInterval;
  jsonOut["databaseServer"] = globalDatabaseServer;
  jsonOut["ntpServer1"] = globalNtpServer1;
  jsonOut["ntpServer2"] = globalNtpServer2;

  jsonOut["measureTimestamp"] = measureTimestamp;
  jsonOut["soilTempCGlobal"] = soilTempCGlobal;
  jsonOut["soilMoistureGlobal"] = soilMoistureGlobal;


  //try pulling from AP
  // jsonBuffer.clear();
  // JsonObject jsonOut = jsonBuffer.createNestedObject("jsonOut");

  // jsonOut["timezone"] = customParameterTimezone.getValue();
  // jsonOut["deviceID"] = customParameterDeviceID.getValue();
  // jsonOut["capacitanceLowerLimit"] = customParameterCapacitanceLowerLimit.getValue();
  // jsonOut["capacitanceUpperLimit"] = customParameterCapacitanceUpperLimit.getValue();
  // jsonOut["measureInterval"] = customParameterMeasureInterval.getValue();
  // jsonOut["databaseServer"] = customParameterDatabaseServer.getValue();
  // jsonOut["ntpServer1"] = customParameterNtpServer1.getValue();
  // jsonOut["ntpServer2"] = customParameterNtpServer2.getValue();

  // jsonOut["measureTimestamp"] = measureTimestamp;
  // jsonOut["soilTempCGlobal"] = soilTempCGlobal;
  // jsonOut["soilMoistureGlobal"] = soilMoistureGlobal;

  Serial.print(F("jsonBuffer during downloadWifiManagerParams:  "));
  serializeJson(jsonBuffer, Serial);  //check


  //arrayParams[wm.getParametersCount()] potential for array handling
  // strcpy(globalTimezone, customParameterTimezone.getValue());
  // strcpy(globalDeviceID, customParameterDeviceID.getValue());
  // strcpy(globalCapacitanceLowerLimit, customParameterCapacitanceLowerLimit.getValue());
  // strcpy(globalCapacitanceUpperLimit, customParameterCapacitanceUpperLimit.getValue());
  // strcpy(globalMeasureInterval, customParameterMeasureInterval.getValue());

  // strcpy(globalDatabaseServer, customParameterDatabaseServer.getValue());  // didnt work
  // strcpy(globalNtpServer1, customParameterNtpServer1.getValue());          // didnt work
  // strcpy(globalNtpServer2, customParameterNtpServer2.getValue());          // didnt work

  // int funTimezone, funDeviceID;

  // funTimezone = atoi(customParameterTimezone.getValue());
  // funDeviceID = atoi(customParameterDeviceID.getValue());

  // const char *funNtpServer1 = (customParameterNtpServer1.getValue());

  ////No
  // char funTimezone[50];
  // char funDeviceID[50];
  // char funNtpServer1[255];
  // strcpy(funTimezone, customParameterTimezone.getValue());
  // strcpy(funDeviceID, customParameterDeviceID.getValue());
  //strcpy(funNtpServer1, customParameterNtpServer1.getValue());

  // Serial.println(F("Function Parameters Test"));
  // Serial.print(F("funDeviceID:  "));
  // Serial.println(funDeviceID);
  // Serial.print(F("funNtpServer1:  "));
  // Serial.println(funNtpServer1);

  // Serial.println(F("Global Parameters after strcpy in downloadWiFiManagerParams"));
  // Serial.print(F("globalDeviceID: "));
  // Serial.println(globalDeviceID);

  // Serial.println(F("globalNtpServer1: "));
  // Serial.println(globalNtpServer1);

  // const char *jsonOut_timezone = customParameterTimezone.getValue();
  // const char *jsonOut_deviceID = customParameterDeviceID.getValue();
  // const char *jsonOut_capacitanceLowerLimit = customParameterCapacitanceLowerLimit.getValue();
  // const char *jsonOut_capacitanceUpperLimit = customParameterCapacitanceUpperLimit.getValue();
  // const char *jsonOut_measureInterval = customParameterMeasureInterval.getValue();
  // const char *jsonOut_databaseServer = customParameterNtpServer1.getValue();
  // const char *jsonOut_ntpServer1 = customParameterNtpServer1.getValue();
  // const char *jsonOut_ntpServer2 = customParameterNtpServer2.getValue();


  // Serial.print(F("jsonBuffer during downloadWifiManagerParams:  "));
  // serializeJson(jsonBuffer, Serial);  //check
  // Serial.println();

  // configObject.timezone = atoi(globalTimezone);
  // configObject.deviceID = atoi(globalDeviceID);
  // configObject.capacitanceLowerLimit = atoi(globalCapacitanceLowerLimit);
  // configObject.capacitanceUpperLimit = atoi(globalCapacitanceUpperLimit);
  // configObject.measureInterval = atoi(globalMeasureInterval);

  // for (int i = 0; i < 255; i++) {
  //   configObject.databaseServer[i] = globalDatabaseServer[i];
  //   configObject.ntpServer1[i] = globalNtpServer1[i];
  //   configObject.ntpServer2[i] = globalNtpServer2[i];
  // }

  //   // strcpy(configObject.databaseServer, customParameterDatabaseServer.getValue());
  //   // strcpy(configObject.ntpServer1, customParameterNtpServer1.getValue());
  //   // strcpy(configObject.ntpServer2, customParameterNtpServer2.getValue());

  //   //9/5/2024 need to confirm this is working or not....
  //   //->>>>> THEY ARE NOT
  // Serial.println(F("end downloadWifiManagerParams... here are the configObject elements..."));
  // Serial.print(F("configObject.timezone: "));
  // Serial.println(configObject.timezone);
  // Serial.print(F("configObject.deviceID:  "));
  // Serial.println(configObject.timezone);
  // Serial.print(F("configObject.ntpServer1:  "));
  // Serial.println(configObject.ntpServer1);
}

void globalCustomParamsToConfigObject() {  //copy globalParams to configObject
  Serial.println(F("inside globalCustomParamsToConfigObject..."));

  configObject.timezone = atoi(globalTimezone);
  configObject.deviceID = atoi(globalDeviceID);
  configObject.capacitanceLowerLimit = atoi(globalCapacitanceLowerLimit);
  configObject.capacitanceUpperLimit = atoi(globalCapacitanceUpperLimit);
  configObject.measureInterval = atoi(globalMeasureInterval);

  for (int i = 0; i < 255; i++) {
    configObject.databaseServer[i] = globalDatabaseServer[i];
    configObject.ntpServer1[i] = globalNtpServer1[i];
    configObject.ntpServer2[i] = globalNtpServer2[i];
  }
}

//-----readFile------------------------------------------------------------------------------
void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println(F("- failed to open file for reading"));
    return;
  }
  Serial.print(F("- read from file:"));
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println();
  file.close();
}

//-----writeFile------------------------------------------------------------------------
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);  //"w" FILE_WRITE
  if (!file) {
    if (file.isDirectory()) {
      Serial.println(F("file is a directory"));
    }
    Serial.println(F("- failed to open file for writing"));
    return;
  }
  if (file.print(message)) {
    Serial.println(F("- file written"));
  } else {
    Serial.println(F("- write failed"));
  }
  file.close();
}

//-----appendFile-------------------------------------------------------------------------------
void appendFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);  //"a" FILE_APPEND
  if (!file) {
    Serial.println(F("- failed to open file for appending"));
    return;
  }
  if (file.print(message)) {
    Serial.println(F("- message appended"));
  } else {
    Serial.println(F("- append failed"));
  }
  file.close();
}

//-----renameFile-----------------------------------------------------------
void renameFile(fs::FS &fs, const char *path1, const char *path2) {
  Serial.printf("Renaming file %s to %s\r\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println(F("- file renamed"));
  } else {
    Serial.println(F("- rename failed"));
  }
}

//-----deleteFile------------------------------------------------------------
void deleteFile(fs::FS &fs, const char *path) {
  Serial.printf("Deleting file: %s\r\n", path);
  if (fs.remove(path)) {
    Serial.println(F("- file deleted"));
  } else {
    Serial.println(F("- delete failed"));
  }
}

//-----Sleep functionality-------------------------------------------------------------------
void sleepNow() {
  //Sleep and wake more frequently than measureInterval
  //This is so that we do not "miss" a user trying to access the AP
  //And so that other events (SYNC etc.) can occur in between measurements
  uint64_t totalSleepUs = defaultSleep * microsecondsToSeconds;

  esp_sleep_enable_timer_wakeup(defaultSleep * microsecondsToSeconds);  //set the sleep time in micro seconds
  anticipatedWakeUnix = time(NULL) + defaultSleep;                      //unix time we are going to sleep... will be recalled to re-set the time.

  Serial.print(F("Going to Sleep for "));
  Serial.print(defaultSleep);
  Serial.println(F(" Second(s)..."));
  delay(10);
  Serial.end();  //otherwise blocks on this call as serial port was dropped after first sleep?
  esp_deep_sleep_start();
  Serial.println(F("This should never be printed."));
}

void wakeupReason() {
  esp_sleep_wakeup_cause_t wakeupReason;
  wakeupReason = esp_sleep_get_wakeup_cause();
  delay(10);
  switch (wakeupReason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println(F("Wakeup caused by external signal using RTC_IO")); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println(F("Wakeup caused by external signal using RTC_CNTL")); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println(F("Wakeup caused by timer")); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println(F("Wakeup caused by touchpad")); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println(F("Wakeup caused by ULP program")); break;
    default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeupReason); break;
  }
}

//----- server time sync-------------------------------------------------------
void syncTime() {
  // struct tm timeinfo;
  long startSyncMillis, endSyncMillis, differenceSyncSeconds;  //for non-blocking...
  Serial.println(F("Entered syncTime"));
  if (timeSync) {
    //configTzTime(timezones[(timezone * 2) - 1], configObject.ntpServer1, configObject.ntpServer2);  // sets the time in the internal ESP32 RTC
    configTzTime(timezones[(timezone * 2) - 1], "pool.ntp.org", "time.nist.gov");
    Serial.print(F("Time Sync requested from: "));
    Serial.print(configObject.ntpServer1);
    Serial.print(F(" at timezone: "));
    Serial.print(timezones[(timezone * 2) - 2]);
    Serial.print(F(", "));
    Serial.println(timezones[(timezone * 2) - 1]);
    timeSync = false;
  }
}

//----- callback for time sync--------------------------------------------------
void cbSyncTime(struct timeval *tv) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println(F("No time available yet."));
    return;
  } else {
    Serial.print(F("Time returned from timeSync!: "));
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");  //print the time in the interal ESP32 clock
    lastSync = timeinfo;                                 //update the time of last sync
  }
}
//-----printLocalTime, this prints the info from the internal ESP32 clock--------
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("No time available (yet)");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

//getParam from advanced wifimanager example
// String getParam(String name) {
//   //read parameter from server, for customhmtl input
//   String value;
//   if (wm.server->hasArg(name)) {
//     value = wm.server->arg(name);
//   }
//   return value;
// }


//-----soilBuddyState------------------------------------------------------------------------------
//Add in our "State Machine" to do various tasks according to state
//Add these to global
//enum states{IDLE, CLIENT}; //Add states here
//states currentState = IDLE; //The starting state
void soilBuddyState() {
  //default should be idle..
  switch (currentState) {
    case IDLE:
      //tasks for IDLE state
      stateBool[0] = true;
      stateBool[1, 2, 3, 4] = false;
      idleLoop();
      break;

    case CLIENT:
      //tasks for CLIENT state
      stateBool[1] = true;
      stateBool[0, 2, 3, 4] = false;
      clientLoop();
      break;

    case MEASURE:
      //tasks for MEASURE state
      stateBool[2] = true;
      stateBool[0, 1, 3, 4] = false;
      measureLoop();
      break;

    case SYNC:
      //tasks for SYNC state
      stateBool[3] = true;
      stateBool[0, 1, 2, 4] = false;
      syncLoop();
      break;

    case WAITING:
      //tasks for WAITING state
      //Write code for waitingLoop(); which exists for the case that the controller wakes, and an event is scheduled before the next wake would occur naturally.
      //So, we need to wait until after it occurs to return to normal sleep/wake cycle.
      stateBool[4] = true;
      stateBool[0, 1, 2, 3] = false;
      waitingLoop();
      break;
      //other states added below...
  }
}



void idleLoop() {
  //Should be simple wake/sleep
  Serial.println(F("idleLoop"));
  sleepNow();  //go to sleep
}

//Should check if there is already a client connected
void clientLoop() {  //may need to add feature to keep portal open if a client is connected...  8/27/2024
  Serial.println(F("clientLoop"));
  bool launchedAP = false;

  if (WiFi.softAPgetStationNum() > 0) {
    //have a client
    if (launchedAP) {
      //update WiFiManager Params (if different from present?)
      Serial.println(F("in client loop updating WM params..."));
      updateWifiManagerParameters();
    } else {
      //wait for client to finish
      Serial.println(F("waiting for client to finish."));
    }
  } else {
    //no client
    if (!launchedAP) {
      //AP launch
      bool res;
      char deviceIDChar = '0' + configObject.deviceID;
      char nameAP[25];
      strcpy(nameAP, "SoilBuddy_");
      appendChar(nameAP, deviceIDChar);
      const char *nameAPConstChar = nameAP;
      res = wm.autoConnect(nameAPConstChar, "password");
      if (!res) {
        Serial.println(F("Failed to connect."));
        Serial.println(F("Wifi disconnected."));
      } else {
        Serial.println(F("WiFi connection complete."));
        launchedAP = true;
      }
    } else {
      //no client and already launched AP
    }
  }
};


void measureLoop() {
  Serial.println("measureLoop");
  //take one measurement and save the data
  float tempC = ss.getTemp();
  uint16_t capread = ss.touchRead(0);
  capread = map(capread, configObject.capacitanceLowerLimit, configObject.capacitanceUpperLimit, 0, 100);  //Map range of sensor from 0 to 100
  //Construct the json file for posting
  jsonBuffer.clear();
  JsonObject jsonOut = jsonBuffer.createNestedObject("jsonOut");

  jsonOut["timezone"] = configObject.timezone;
  jsonOut["deviceID"] = configObject.deviceID;
  jsonOut["capacitanceLowerLimit"] = configObject.capacitanceLowerLimit;
  jsonOut["capacitanceUpperLimit"] = configObject.capacitanceUpperLimit;
  jsonOut["measureInterval"] = configObject.measureInterval;

  jsonOut["databaseServer"] = configObject.databaseServer;
  jsonOut["ntpServer1"] = configObject.ntpServer1;
  jsonOut["ntpServer2"] = configObject.ntpServer2;

  jsonOut["measureTimestamp"] = mktime(&timeinfo);
  jsonOut["soilTempCGlobal"] = tempC;
  jsonOut["soilMoistureGlobal"] = capread;

  //update globals
  snprintf(measureTimestamp, sizeof(measureTimestamp), "%d", mktime(&timeinfo));  //int
  snprintf(soilTempCGlobal, sizeof(soilTempCGlobal), "%f", tempC);                //float
  snprintf(soilMoistureGlobal, sizeof(soilMoistureGlobal), "%d", capread);        //int

  int bytes_req = jsonBuffer.memoryUsage();  //returns number of bytes in the Buffer
  int extra_space = 100;
  int total_space = bytes_req + extra_space;
  // Serial.print(F("Actual memory requirement:  "));
  // Serial.print(total_space);
  // Serial.println(F(" bytes"));
  char json_array[1048];  // char array large enough
  Serial.print(F("Saving to dat.json: "));
  serializeJson(jsonBuffer, Serial);  //print to Serial port to view what you are writing. This works fine.
  Serial.println();
  serializeJson(jsonBuffer, json_array);  //copy the info in the buffer to the array to use writeFile below
  appendFile(SPIFFS, "/dat.json", json_array);
}

void syncLoop() {
  Serial.println("syncLoop");
  //Determine if wifi access is provided
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi connection needs established to complete time sync..."));
    //no, try to reconnect to WiFi
    bool res;
    char deviceIDChar = '0' + configObject.deviceID;
    char nameAP[25];
    strcpy(nameAP, "SoilBuddy_");
    appendChar(nameAP, deviceIDChar);
    const char *nameAPConstChar = nameAP;
    res = wm.autoConnect(nameAPConstChar, "password");
    if (res) {
      Serial.println(F("WiFi connection Success!"));
      //Reconnect success,
      //put ntp time sync code after this evaluation.
      if (currentState == SYNC) {
        Serial.println(F("WiFi connection permited time sync!"));
        timeSync = true;
        syncTime();
      } else {
        currentState = SYNC;
      }
    } else {
      Serial.println(F("WiFi connection failed."));
      //Reconnect failed, return to IDLE state
      currentState = IDLE;
    }
  } else {
    Serial.println(F("WiFi connection permited time sync!"));
    //Yes, a connection is established
    //send sync request, sync ESP32 clock, update tm lastSync for display in AP
    sntp_set_time_sync_notification_cb(cbSyncTime);  //sets callback
    timeSync = true;                                 //sets flag
    syncTime();                                      //polls configObject.ntpServer1 OR configObject.ntpServer2 for time
  }
}

void waitingLoop() {
  Serial.println(F("waitingLoop..."));
  checkEvents();
  soilBuddyState();
}

//

//add arduinojson support for tm
bool convertToJson(const tm &src, JsonVariant dst) {
  char buf[32];
  strftime(buf, sizeof(buf), "%FT%TZ", &src);
  return dst.set(buf);
}

void convertFromJson(JsonVariantConst src, tm &dst) {
  strptime(src.as<const char *>(), "%FT%TZ", &dst);
}
//

//from the jsonBuffer(configuration related) and global (data related)
void updateWifiManagerParameters() {
  //populate global parameters with json file contents?


  //configuration related
  wm.addParameter(&customParameterTimezone);
  wm.addParameter(&customParameterDeviceID);
  wm.addParameter(&customParameterCapacitanceLowerLimit);
  wm.addParameter(&customParameterCapacitanceUpperLimit);
  wm.addParameter(&customParameterMeasureInterval);
  wm.addParameter(&customParameterDatabaseServer);
  wm.addParameter(&customParameterNtpServer1);
  wm.addParameter(&customParameterNtpServer2);

  //data related
  //add to custom parameters, populate from globals
  wm.addParameter(&customParameterMeasureTimestamp);
  wm.addParameter(&customParameterSoilTempCGlobal);
  wm.addParameter(&customParameterSoilMoistureGlobal);

  //theme
  wm.setClass("invert");  //set dark
  //wm.setParamsPage(true); //puts customParameters on its own page <192.168.4.1/param>

  //callbacks
  wm.setPreSaveParamsCallback(preSaveParamsCallback);
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setConfigResetCallback(configResetCallback);

  //timeouts/checks
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(60);  //
  wm.setConfigPortalTimeoutCallback(configPortalTimeoutCallback);
  //wm.setConfigPortalBlocking(false);//?? not sure yet....
  wm.setWebPortalClientCheck(true);

  //breaks
  wm.setBreakAfterConfig(true); //this has to be true to be non-blocking. //9/18/2024 ->

  //
}



void appendChar(char *s, char c) {
  int len = strlen(s);
  s[len] = c;
  s[len + 1] = '\0';
}

void configPortalTimeoutCallback() {
  Serial.println(F("The configuration portal has timed out..."));
}

//callback for things to do IMMEDIATELY BEFORE saveParams
void preSaveParamsCallback() {
  Serial.println(F("preSaveParamsCallback"));
  readUpdatedParametersFromAP();  //copies AP values to globals?
  downloadWifiManagerParams();  //Passes globalParameters to jsonBuffer... ->9/16/2024 jsonbuffer in here does not contain the updated values.... what are the global?
  //globalCustomParamsToConfigObject();  //send globals to configObject
}

//callback for user selection of SAVE on WiFiManager
void saveParamsCallback() {
  Serial.println();
  Serial.println(F("saveParamsCallback"));
  saveConfig();                //Saves current jsonBuffer contents as /config.json
  importConfig(configObject);  //imports config.json, populates jsonBuffer & global measures
}

void configResetCallback() {
  wm.resetSettings();  // erase WiFi credentials
  //wm.reboot(); //reboot?
  deviceConfig defaultConfig;     //new default instance
  configObject = defaultConfig;   //pass to configObject
  saveConfig();                   //save configObject to /config.json
  updateWifiManagerParameters();  //update WifiManager from jsonObject
}



//-----checkEvents--------------------------------------------------
//Intended to check for scheduled events (sync, measure, client)
//Calculate the difftime (seconds) between rtc now and the event
//Append the eventdifftime array

//If the difftime of the event is less than the seconds before the device will wake next, enter WAITING state and handle the event ->>>> Will be handled by a sep. function to
//handle all eventdifftimes....

void checkEvents() {

  //array of eventDifftime() stores seconds until some event to remain in waiting state if something is scheduled before next wake
  double eventDifftime[62];       //static size of 60 is the max size of measureInterval (60 minutes = 1once per minute) + 1 entry for time sync and 1 entry for client access...
  for (int i = 0; i < 62; i++) {  // populate a default
    eventDifftime[i] = 9999.99;
  }

  //------1------
  //Get the current time from the RTC
  char strftime_buf[64];
  time_t now, nextSleep;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  Serial.println(strftime_buf);  // print current time

  //Calculate seconds between now and beginning of the next day
  tm nextDay = timeinfo;
  nextDay.tm_mday = timeinfo.tm_mday + 1;  //increment a day and set hour minute and second to 0
  nextDay.tm_hour = 0;
  nextDay.tm_min = 0;
  nextDay.tm_sec = 0;
  time_t nextDayNormalized = mktime(&nextDay);  //normalize the result to deal with overflows
  double presentDaySecondsRemaining = difftime(nextDayNormalized, now);

  //-----2-----
  //Has the RTC been set from the default?
  if (timeinfo.tm_year == 70) {  //1970 is default, tm_year= years since 1900
    Serial.println(F("Time appears to have never been set..."));
    eventDifftime[0] = (now + defaultSleep - 10.0);  //append the eventDifftime with now (unix) plus the default sleep interval
  } else {
    //Is it time for a scheduled time sync?
    //to prevent lots of identical timed requests use deviceID (uint16_t or max of 65535) as seconds to offset the scheduled time sync from 00:00:00.
    nextSync = timeinfo;  //populate
    float nextSyncHoursByDeviceid, nextSyncHoursByDeviceidFloat, nextSyncRemainderMin;
    nextSyncHoursByDeviceidFloat = ((configObject.deviceID / 60) / 60);                        //convert deviceID (as seconds) to hours
    nextSyncRemainderMin = std::modf(nextSyncHoursByDeviceidFloat, &nextSyncHoursByDeviceid);  //modf splits float into whole & fraction
    nextSyncRemainderMin = std::floor(nextSyncRemainderMin * 60);                              //convert the hour decimal to minutes
    nextSync.tm_hour = 0 + std::floor(nextSyncHoursByDeviceidFloat);                           //floor rounds a float down
    nextSync.tm_min = 0 + nextSyncRemainderMin;
    if (timeinfo.tm_hour == nextSync.tm_hour && timeinfo.tm_min == nextSync.tm_min) {  //midnight is standard, then augmented by deviceID
      //yes, this is the time to sync time with ntp server
      Serial.println(F("Scheduled time synchronization will be attempted."));
      eventDifftime[0] = difftime(mktime(&nextSync), now);
    } else {
      eventDifftime[0] == 9999.99;  //RTC has been set, it is not sync time, set to 9999.99
    }
  };


  //-----3-----
  //Is it measurement time?
  //Is the the measureInterval a multiple of the current tm_min?
  //I.e. a 15 minute measureInterval will have 0, 15, 30, and 45 ->(60 never appears in counting min, 59 rolls to 0)
  float minuteMultiple;
  int maxMultiple = 60 / configObject.measureInterval;  //e.g. 60/15 = 4, 60/30 = 2
  minuteMultiple = (float)timeinfo.tm_min / (float)configObject.measureInterval;
  int multipleMatch[maxMultiple];
  for (int i = 0; i <= maxMultiple; i++) {
    eventTime = timeinfo;
    //60 should never be used (even on measureInterval of 60) as it never appears in tm_min... for once per hour, need to look at tm_min = 0
    if (i == 0) {
      if (configObject.measureInterval == 60) {
        multipleMatch[i] = 0;
      }
    } else {
      multipleMatch[i] = configObject.measureInterval * i;
    }
    eventTime.tm_min = multipleMatch[i];
    //After changing the tm_min, if the difference in time is 0 or negative (as in the change to eventTime resulted in a time in the past)
    //need to convert the tm struct to time since epoch as time_t
    if (difftime(mktime(&eventTime), now) <= 0.0) {
      Serial.println(F("Handle time overflows..."));
      long tempUnix = mktime(&eventTime);
      tempUnix = tempUnix + 3600;          //add 1 hour to the current time so eventTime can now be scheduled properly
      localtime_r(&tempUnix, &eventTime);  //convert tempUnix and store in eventTime
    }
    Serial.print(F("Measurement scheduled at: "));
    Serial.println(asctime(&eventTime));
    eventDifftime[i] = difftime(mktime(&eventTime), now);  //eventDifftime[0] is always the next time sync, so i > 0
  };


  //-----4-----
  //Offer AP access periodically, 6 minutes per hour (1/10th), to satisfy lowpower requirements?
  //60 seconds at the beginning of each 1/6 hour
  int apAccess[6]{ 0, 10, 20, 30, 40, 50 };
  time_t apAccessTime[6]{ time(&now) };
  // Serial.println(F("Beginning scheduled client AP access."));
  localtime_r(&now, &timeinfo);
  for (int i = 0; i < 6; i++) {
    timeinfo.tm_min = apAccess[i];                 //set access minutes
    if (difftime(mktime(&timeinfo), now) < 0.0) {  //if the difference between apAccess time and now is 0.0 or negative
      apAccessTime[i] + 3600;                      //handle time overflows by adding one hour (3600 secs) to unix time
    }
  }
  //Find the closest in time to now...
  time_t nextAccessPoint = 0;
  for (int i = 0; i < 6; i++) {
    if (apAccessTime[i] < apAccessTime[i + 1]) {
      nextAccessPoint = apAccess[i];
    } else {
      nextAccessPoint = apAccess[i + 1];
    }
  }
  //Calc difftime and add to eventDifftime
  for (int i = 0; i < 61; i++) {
    if (eventDifftime[i + 1] == 9999.99) {
      eventDifftime[i + 1] = difftime(nextAccessPoint, now);
      break;
    }
  };


  //-----5-----
  //If seconds until eventDifftime is < default sleep, enter waiting state... else IDLE
  for (int i = 0; i < 61; i++) {
    if (eventDifftime[i] != 9999.99) {
      if (i == 0) {
        if (eventDifftime[i] < 1.0 && eventDifftime[i] > 0.0) {  //within the second....
          //it is syncTime
          currentState = SYNC;
          break;
        }
      } else if (i < maxMultiple && i != 0) {
        //measurements
        if (eventDifftime[i] < 1.0 && eventDifftime[i] > 0.0) {
          currentState = MEASURE;
          break;
        }
      } else if (i == maxMultiple && i != 0) {
        //client
        if (eventDifftime[i] < 1.0 && eventDifftime[i] > 0.0) {
          currentState = CLIENT;
          break;
        }
      } else {
        if (eventDifftime[i] > defaultSleep) {
          currentState = WAITING;
          break;
        } else {
          currentState = IDLE;
        }
      }
    }
  };
};

//compiled 9/10/2024 but cannot use because out of scope...
const char *dynamicWiFiManagerParam(JsonDocument &trgt, String a, String b) {
  const char *dynamicParam = trgt[a][b];
  return (dynamicParam);
}