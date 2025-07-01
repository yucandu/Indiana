#include <TJpg_Decoder.h>
#define USE_LINE_BUFFER  // Enable for faster rendering
#define FS_NO_GLOBALS
#include <FS.h>
#include <LittleFS.h>
#include "SPI.h"
#include <LovyanGFX.hpp>  // Hardware-specific library
#include "NotoSans-Condensed-26.h"
#include <ArduinoOTA.h>
#include <WiFiClientSecure.h>
#include <ArduinoStreamParser.h>
#include <Wire.h>
#include <Arduino.h>
#include "Adafruit_SHT31.h"
#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "time.h"
#include "esp_sntp.h"
#include <HTTPClient.h>

unsigned long time_counter = 0;

struct tm timeinfo;
uint32_t localTimeUnix = 0;
bool isSetNtp = false;
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;

public:
  LGFX(void)
  {
    { // Configure SPI bus
      auto cfg = _bus_instance.config();
      
      cfg.spi_host = SPI2_HOST;     // Select SPI to use (VSPI_HOST or HSPI_HOST)
      cfg.spi_mode = 3;             // Set SPI communication mode (0 ~ 3)
      cfg.freq_write = 27000000;    // SPI clock when transmitting (max 80MHz, rounded to 80MHz/n)
      cfg.freq_read = 20000000;     // SPI clock when receiving
      cfg.spi_3wire = true;         // Set true when receiving on MOSI pin
      cfg.use_lock = true;          // Set true when using transaction lock
      cfg.dma_channel = SPI_DMA_CH_AUTO; // Set DMA channel to use (0=not use DMA / 1=1ch / 2=ch / SPI_DMA_CH_AUTO=auto setting)
      
      cfg.pin_sclk = 4;             // Set SPI SCLK pin number
      cfg.pin_mosi = 6;             // Set SPI MOSI pin number
      cfg.pin_miso = 5;             // Set SPI MISO pin number (-1 = disable)
      cfg.pin_dc = 20;              // Set SPI D/C pin number (-1 = disable)
      
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Configure display panel
      auto cfg = _panel_instance.config();

      cfg.pin_cs = 7;               // Pin number to which CS is connected (-1 = disable)
      cfg.pin_rst = 21;             // Pin number to which RST is connected (-1 = disable)
      cfg.pin_busy = -1;            // Pin number to which BUSY is connected (-1 = disable)

      // The following setting values ​​are set to general default values ​​for each panel,
      // so please try commenting out any unknown items.

      cfg.panel_width = 240;        // Actual displayable width
      cfg.panel_height = 320;       // Actual displayable height
      cfg.offset_x = 0;             // Panel offset amount in X direction
      cfg.offset_y = 0;             // Panel offset amount in Y direction
      cfg.offset_rotation = 0;      // Rotation direction value offset 0~7 (4~7 is upside down)
      cfg.dummy_read_pixel = 8;     // Number of dummy read bits before pixel readout
      cfg.dummy_read_bits = 1;      // Number of dummy read bits before non-pixel data read
      cfg.readable = true;          // Set to true if data can be read
      cfg.invert = false;           // Set to true if panel light and dark are inverted
      cfg.rgb_order = false;        // Set to true if panel red and blue are swapped (BGR order)
      cfg.dlen_16bit = false;       // Set to true for panels that transmit data length in 16-bit units
      cfg.bus_shared = true;        // Set to true when sharing the bus with SD card (drawJpgFile etc.)

      _panel_instance.config(cfg);
    }

    { // Configure backlight control (delete if not necessary)
      auto cfg = _light_instance.config();

      cfg.pin_bl = -1;              // Pin number to which the backlight is connected
      cfg.invert = false;           // True to invert backlight brightness
      cfg.freq = 44100;             // Backlight PWM frequency
      cfg.pwm_channel = 7;          // PWM channel number to use

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    { // Configure touch screen control (delete if not necessary)
      auto cfg = _touch_instance.config();

      cfg.x_min = 0;                // Minimum X value (raw value) obtained from touchscreen
      cfg.x_max = 4095;             // Maximum X value (raw value) obtained from touchscreen
      cfg.y_min = 0;                // Minimum Y value (raw value) obtained from touchscreen
      cfg.y_max = 4095;             // Maximum Y value (raw value) obtained from touchscreen
      cfg.pin_int = -1;             // Pin number to which INT is connected
      cfg.bus_shared = true;        // Set to true when using the same bus as the screen
      cfg.offset_rotation = 0;      // Adjustment when display and touch orientation do not match Set with a value from 0 to 7

      // For SPI connection
      cfg.spi_host = SPI2_HOST;     // Select SPI to use (HSPI_HOST or VSPI_HOST)
      cfg.freq = 2500000;           // Set SPI clock
      cfg.pin_sclk = 4;             // Set SCLK pin number
      cfg.pin_mosi = 6;             // Set MOSI pin number
      cfg.pin_miso = 5;             // Set MISO pin number
      cfg.pin_cs = 3;               // Set CS pin number

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

// Create an instance to use
static LGFX tft;



LGFX_Sprite img(&tft);
LGFX_Sprite img2(&tft);
LGFX_Sprite imgOrr(&tft);

bool autobright = true;         // Flag to enable/disable auto brightness
int ldr_pin = 0;              // Analog pin for light sensor
int ldr_read = 0;              // Raw light sensor reading
int newldr = 0;                // Processed brightness value
int previous_brightness = -1;   // Previous brightness value for hysteresis
int hysteresis_threshold = 5;   // Minimum change required to update brightness
float indewp, inhumidex;
#define VERSION 1.04

String titleLine = "***INDIANA v" + String(VERSION) + "***";
unsigned long reconnectTime;
const char* ssid = "mikesnet";
const char* password = "springchicken";
String powerstring = "0W";

/*#define AA_FONT_10 "YuGothicUI-Regular-10"
#define AA_FONT_12 "YuGothicUI-Regular-12"
#define AA_FONT_14 "YuGothicUI-Regular-14"
#define AA_FONT_16 "YuGothicUI-Regular-16"
#define AA_FONT_18 "YuGothicUI-Regular-18"*/
#define AA_FONT_20 "YuGothicUI-Regular-20"
#define AA_FONT_22 "NotoSans-Condensed-22"
#define AA_FONT_24 "NotoSans-Condensed-24"
#define AA_FONT_26 "NotoSans-Condensed-26"

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;    //Replace with your GMT offset (secs)
const int daylightOffset_sec = 3600;  //Replace with your daylight offset (secs)
int hours, mins, secs;

char auth[] = "qS5PQ8pvrbYzXdiA4I6uLEWYfeQrOcM4";



WidgetTerminal terminal(V10);

//TFT_eSprite img3 = TFT_eSprite(&tft);
#define LED_PIN 8

int page = 1;
uint16_t t_x = 0, t_y = 0;        // To store the touch coordinates
uint16_t oldt_x = 0, oldt_y = 0;  // To store the touch coordinates


#define every(interval) \
  static uint32_t __every__##interval = millis(); \
  if (millis() - __every__##interval >= interval && (__every__##interval = millis()))

#define CALIBRATION_FILE "/TouchCalData1"

// Set REPEAT_CAL to true instead of false to run calibration
// again, otherwise it will only be done once.
// Repeat calibration if you change the screen rotation.
#define REPEAT_CAL false



// Using two fonts since numbers are nice when bold
#define LABEL1_FONT &FreeSansOblique12pt7b  // Key label font 1
#define LABEL2_FONT &FreeSansBold12pt7b     // Key label font 2


// We have a status line for messages
#define STATUS_X 120  // Centred on this
#define STATUS_Y 65





void cbSyncTime(struct timeval *tv) { // callback function to show when NTP was synchronized
  Serial.println("NTP time synched");
  Serial.println("getlocaltime");
  getLocalTime(&timeinfo);

  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);

  Serial.println(asctime(timeinfo));
  time_t now = time(nullptr); // local-adjusted time
  localTimeUnix = static_cast<uint32_t>(now); // 32-bit to send via ESP-NOW
  isSetNtp = true;
}


void initSNTP() {  
  sntp_set_sync_interval(10 * 60 * 1000UL);  // 1 hour
  sntp_set_time_sync_notification_cb(cbSyncTime);
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "192.168.50.197");
  esp_sntp_init();
  wait4SNTP();
  setTimezone();
}

void wait4SNTP() {
  Serial.print("Waiting for time...");
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    delay(1000);
    Serial.print(".");
  }
}

void setTimezone() {  
  setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1);
  tzset();
}

// This next function will be called during decoding of the jpeg file to
// render each block to the TFT.  If you use a different TFT library
// you will need to adapt this function to suit.
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Stop further decoding as image is running off bottom of screen
  if (y >= tft.height()) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);

  // This might work instead if you adapt the sketch to use the Adafruit_GFX library
  // tft.drawRGBBitmap(x, y, bitmap, w, h);

  // Return 1 to decode next block
  return 1;
}

void drawMeasurementGrid() {
  tft.fillScreen(TFT_BLACK);
  
  // Draw 5-pixel markers along edges
  for(int x = 0; x < tft.width(); x += 5) {
    // Top and bottom edges
    if(x < 50 || x > tft.width()-50) {  // Only first and last 50 pixels
      // Top edge
      tft.drawLine(x, 0, x, 3, TFT_WHITE);
      // Bottom edge  
      tft.drawLine(x, tft.height()-3, x, tft.height()-1, TFT_WHITE);
      
      // Add numbers every 10 pixels
      if(x % 10 == 0) {
        tft.setTextColor(TFT_GREEN);
        tft.setTextSize(1);
        // Top numbers
        tft.setCursor(x-3, 5);
        tft.print(x);
        // Bottom numbers
        tft.setCursor(x-3, tft.height()-12);
        tft.print(x);
      }
    }
  }
  
  // Left and right edges
  for(int y = 0; y < tft.height(); y += 5) {
    if(y < 50 || y > tft.height()-50) {  // Only first and last 50 pixels
      // Left edge
      tft.drawLine(0, y, 3, y, TFT_WHITE);
      // Right edge
      tft.drawLine(tft.width()-3, y, tft.width()-1, y, TFT_WHITE);
      
      // Add numbers every 10 pixels
      if(y % 10 == 0) {
        tft.setTextColor(TFT_GREEN);
        tft.setTextSize(1);
        // Left side numbers
        tft.setCursor(5, y-3);
        tft.print(y);
        // Right side numbers
        tft.setCursor(tft.width()-25, y-3);
        tft.print(y);
      }
    }
  }
  
  // Draw thin red border at absolute edges
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_RED);
}

//------------------------------------------------------------------------------------------

void touch_calibrate() {
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  if (!LittleFS.begin()) {
    Serial.println("formatting file system");
    LittleFS.format();
    LittleFS.begin();
  }

  if (LittleFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL) {
      LittleFS.remove(CALIBRATION_FILE);
    } else {
      File f = LittleFS.open(CALIBRATION_FILE, "r");
      if (f) {
        if (f.readBytes((char*)calData, 14) == 14)
          calDataOK = 1;
        f.close();
      }
    }
  }

  if (calDataOK && !REPEAT_CAL) {
    tft.setTouchCalibrate(calData);
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Touch corners as indicated");
    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 35);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    File f = LittleFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char*)calData, 14);
      f.close();
    }
  }
}

//------------------------------------------------------------------------------------------

float temp;
float hum;

void printLocalTime() {
  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  terminal.println(asctime(timeinfo));
  terminal.flush();
}

int brightness = 32;

BLYNK_WRITE(V1) {
  brightness = param.asInt();
  analogWrite(LED_PIN, brightness);
}

float temppool, pm25in, pm25out, bridgetemp, bridgehum, windspeed, winddir, windchill, windgust, humidex, bridgeco2, bridgeIrms, watts, kw, tempSHT, humSHT, co2SCD, presBME, neotemp, jojutemp, temptodraw;

BLYNK_WRITE(V41) {
  neotemp = param.asFloat();
}

BLYNK_WRITE(V42) {
  jojutemp = param.asFloat();
}

BLYNK_WRITE(V71) {
  pm25in = param.asFloat();
}

BLYNK_WRITE(V61) {
  temppool = param.asFloat();
}


BLYNK_WRITE(V62) {
  bridgetemp = param.asFloat();
}
BLYNK_WRITE(V63) {
  bridgehum = param.asFloat();
}
BLYNK_WRITE(V64) {
  windchill = param.asFloat();
}
BLYNK_WRITE(V65) {
  humidex = param.asFloat();
}
BLYNK_WRITE(V66) {
  windgust = param.asFloat();
}
BLYNK_WRITE(V67) {
  pm25out = param.asFloat();
}
BLYNK_WRITE(V78) {
  windspeed = param.asFloat();
}
BLYNK_WRITE(V79) {
  winddir = param.asFloat();
}




BLYNK_WRITE(V77) {
  bridgeco2 = param.asFloat();
}

BLYNK_WRITE(V81) {
  bridgeIrms = param.asFloat();
  watts = bridgeIrms;
  kw = watts / 1000.0;
}

BLYNK_WRITE(V91) {
  tempSHT = param.asFloat();
}
BLYNK_WRITE(V92) {
  humSHT = param.asFloat();
}
BLYNK_WRITE(V93) {
  co2SCD = param.asFloat();
}

BLYNK_WRITE(V94) {
  presBME = param.asFloat();
}


BLYNK_WRITE(V10) {
  if (String("help") == param.asStr()) {
    terminal.println("==List of available commands:==");
    terminal.println("wifi");
    terminal.println("==End of list.==");
  }
  if (String("wifi") == param.asStr()) {
    terminal.print("Connected to: ");
    terminal.println(ssid);
    terminal.print("IP address:");
    terminal.println(WiFi.localIP());
    terminal.print("Signal strength: ");
    terminal.println(WiFi.RSSI());
    printLocalTime();
  }
}

String windDirection(int temp_wind_deg)  //Source http://snowfence.umn.edu/Components/winddirectionanddegreeswithouttable3.htm
{
  switch (temp_wind_deg) {
    case 0 ... 11:
      return "N";
      break;
    case 12 ... 33:
      return "NNE";
      break;
    case 34 ... 56:
      return "NE";
      break;
    case 57 ... 78:
      return "ENE";
      break;
    case 79 ... 101:
      return "E";
      break;
    case 102 ... 123:
      return "ESE";
      break;
    case 124 ... 146:
      return "SE";
      break;
    case 147 ... 168:
      return "SSE";
      break;
    case 169 ... 191:
      return "S";
      break;
    case 192 ... 213:
      return "SSW";
      break;
    case 214 ... 236:
      return "SW";
      break;
    case 237 ... 258:
      return "WSW";
      break;
    case 259 ... 281:
      return "W";
      break;
    case 282 ... 303:
      return "WNW";
      break;
    case 304 ... 326:
      return "NW";
      break;
    case 327 ... 348:
      return "NNW";
      break;
    case 349 ... 360:
      return "N";
      break;
    default:
      return "error";
      break;
  }
}

// MLB-related global variables
const char* scheduleUrlTemplate = "http://statsapi.mlb.com/api/v1/schedule?sportId=1&startDate=%s&endDate=%s&teamId=141";
const char* atBatUrlTemplate = "https://statsapi.mlb.com/api/v1/game/%s/playByPlay";
char url[200];
String currentGameId = "";
int currentAtBat = -1;
struct PitchData {
  float x;
  float y;
  float speed;
  String type;
};
PitchData pitches[10];  // Store up to 10 pitches
int numPitches = 0;
String homeTeam = "";
String awayTeam = "";
int homeScore = 0;
int awayScore = 0;
String inningState = "";
int inningNum = 0;

// Helper functions for MLB data
String getCurrentDate() {
  struct tm timeinfo;
  char dateStr[11];  
  if(!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "";
  }
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
  return String(dateStr);
}

String getDateBefore(int daysAgo) {
  struct tm timeinfo;
  char dateStr[11];  
  if(!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "";
  }
  timeinfo.tm_mday -= daysAgo;
  mktime(&timeinfo);
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
  return String(dateStr);
}

void getLastGoodAtBat(String gameId, int startingAtBat) {
    // Try current at-bat first
    getPitchDataForAtBat(gameId, startingAtBat);
    
    // If no pitches found, try previous at-bat
    if(numPitches == 0 && startingAtBat > 0) {
        Serial.println("No pitches in current at-bat, trying previous...");
        getPitchDataForAtBat(gameId, startingAtBat - 1);
        
        // If still no pitches, something's wrong
        if(numPitches == 0) {
            Serial.println("ERROR: No pitch data found in current or previous at-bat");
        }
    }
}

String getLatestGameId() {
  String endDate = getCurrentDate();
  String startDate = getDateBefore(0);  // Today's games
  sprintf(url, scheduleUrlTemplate, startDate.c_str(), endDate.c_str());
  
  HTTPClient http;
  http.begin(url);
  http.useHTTP10(true);
  int httpCode = http.GET();
  String gameId = "";
  
  if(httpCode == HTTP_CODE_OK) {
    DynamicJsonDocument filter(512);
    filter["dates"][0]["games"][0]["gamePk"] = true;
    filter["dates"][0]["games"][0]["status"]["abstractGameState"] = true;
    filter["dates"][0]["games"][0]["status"]["detailedState"] = true;
    filter["dates"][0]["games"][0]["teams"]["away"]["team"]["name"] = true;
    filter["dates"][0]["games"][0]["teams"]["home"]["team"]["name"] = true;
    filter["dates"][0]["games"][0]["teams"]["away"]["score"] = true;
    filter["dates"][0]["games"][0]["teams"]["home"]["score"] = true;
    filter["dates"][0]["games"][0]["linescore"]["currentInning"] = true;
    filter["dates"][0]["games"][0]["linescore"]["inningHalf"] = true;

    DynamicJsonDocument doc(2048);  // Increased size for full team names
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    
    if(!err && doc.containsKey("dates") && doc["dates"].size() > 0) {
      JsonArray games = doc["dates"][0]["games"].as<JsonArray>();
      if(games.size() > 0) {
        JsonObject game = games[0];
        // Get team names and scores
        homeTeam = game["teams"]["home"]["team"]["name"].as<String>();
        awayTeam = game["teams"]["away"]["team"]["name"].as<String>();
        // Shorten team names to 3 letters if needed
        if(homeTeam.length() > 3) homeTeam = homeTeam.substring(0,3);
        if(awayTeam.length() > 3) awayTeam = awayTeam.substring(0,3);
        
        homeScore = game["teams"]["home"]["score"] | 0;
        awayScore = game["teams"]["away"]["score"] | 0;
        inningNum = game["linescore"]["currentInning"] | 0;
        inningState = game["linescore"]["inningHalf"].as<String>();
        
        String state = game["status"]["abstractGameState"].as<String>();
        String detailed = game["status"]["detailedState"].as<String>();
        if(state == "Live" || detailed.indexOf("In Progress") >= 0) {
          gameId = game["gamePk"].as<String>();
          Serial.println("Found live game: " + gameId);
          http.end();
          return gameId;
        }
      }
    }
  }
  http.end();

  // If no live game found, look for most recent final game
  if(gameId.isEmpty()) {
    startDate = getDateBefore(2); // Check last 2 days for completed games
    sprintf(url, scheduleUrlTemplate, startDate.c_str(), endDate.c_str());
    Serial.println("No live game found, checking recent completed games...");
    
    http.begin(url);
    httpCode = http.GET();
    
    if(httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument filter(128);
      filter["dates"][0]["games"][0]["gamePk"] = true;
      filter["dates"][0]["games"][0]["status"]["abstractGameState"] = true;

      DynamicJsonDocument doc(768);
      DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      
      if(!err && doc.containsKey("dates") && doc["dates"].size() > 0) {
        // Find most recent final game
        for(JsonVariant date : doc["dates"].as<JsonArray>()) {
          for(JsonVariant game : date["games"].as<JsonArray>()) {
            String state = game["status"]["abstractGameState"].as<String>();
            if(state == "Final") {
              gameId = game["gamePk"].as<String>();
              Serial.println("Found completed game: " + gameId);
              http.end();
              return gameId;
            }
          }
        }
      }
    }
    http.end();
  }
  
  return gameId;
}

int getLastAtBatIndex(String gameId) {
  if(gameId.isEmpty()) {
    Serial.println("No game ID provided");
    return -1;
  }
  
  sprintf(url, atBatUrlTemplate, gameId.c_str());
  Serial.print("Getting last at-bat for game: ");
  Serial.println(gameId);
  
  HTTPClient http;
  http.begin(url);
  http.useHTTP10(true);
  int httpCode = http.GET();
  
  if(httpCode == HTTP_CODE_OK) {
    int lastIndex = -1;
    bool inQuotes = false;
    bool foundCurrentPlay = false;
    char searchStr[] = "\"atBatIndex\":";
    char currentPlayStr[] = "\"currentPlay\":";
    int searchLen = strlen(searchStr);
    int currentPlayLen = strlen(currentPlayStr);
    int matchPos = 0;
    int curlyBraceLevel = 0;
    
    size_t len = http.getSize();
    uint8_t buff[128] = {0};
    WiFiClient * stream = http.getStreamPtr();
    
    Serial.println("Parsing response...");
    String numStr = "";
    bool readingNumber = false;
    
    while(http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available();
      if(size > 0) {
        int readBytes = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        
        for(int i = 0; i < readBytes; i++) {
          char c = (char)buff[i];
          
          if(c == '{') {
            curlyBraceLevel++;
          } else if(c == '}') {
            curlyBraceLevel--;
            if(curlyBraceLevel == 1) { // Exit currentPlay object if we found index
              if(foundCurrentPlay && lastIndex >= 0) {
                Serial.println("Using currentPlay index");
                http.end();
                return lastIndex;
              }
            }
          }
          
          // Look for currentPlay first
          if(!foundCurrentPlay && c == currentPlayStr[matchPos]) {
            matchPos++;
            if(matchPos == currentPlayLen) {
              foundCurrentPlay = true;
              matchPos = 0;
            }
          } 
          // Then look for atBatIndex
          else if(c == searchStr[matchPos]) {
            matchPos++;
            if(matchPos == searchLen) {
              readingNumber = true;
              numStr = "";
              matchPos = 0;
            }
          } else {
            matchPos = (c == currentPlayStr[0]) ? 1 : 
                      (c == searchStr[0]) ? 1 : 0;
          }
          
          // Read number after match
          if(readingNumber) {
            if(isDigit(c)) {
              numStr += c;
            } else if(numStr.length() > 0) {
              int newIndex = numStr.toInt();
              lastIndex = newIndex;
              readingNumber = false;
              if(foundCurrentPlay) {
                Serial.println("Using currentPlay index");
                http.end();
                return lastIndex;
              }
            }
          }
        }
        
        if(len > 0) {
          len -= readBytes;
        }
      }
    }
    
    http.end();
    if(lastIndex >= 0) {
      Serial.print("Last at-bat index: ");
      Serial.println(lastIndex);
      return lastIndex;
    }
  }
  
  http.end();
  return -1;
}

void getPitchDataForAtBat(String gameId, int atBatIndex) {
  if(gameId.isEmpty() || atBatIndex < 0) {
    Serial.println("Invalid game ID or at-bat index");
    return;
  }
  
  sprintf(url, atBatUrlTemplate, gameId.c_str());
  Serial.print("Getting pitch data from: ");
  Serial.println(url);
  
  HTTPClient http;
  http.begin(url);
  http.useHTTP10(true);
  int httpCode = http.GET();
  
  if(httpCode == HTTP_CODE_OK) {
    bool foundTargetAtBat = false;
    bool inPitchData = false;
    bool inAllPlay = false;
    bool processedAtBat = false;
    numPitches = 0;
    
    size_t len = http.getSize();
    const size_t BUFFER_SIZE = 64;
    uint8_t buff[BUFFER_SIZE] = {0};
    WiFiClient * stream = http.getStreamPtr();
    
    String keyName = "";
    String valueStr = "";
    bool inKey = false;
    bool inValue = false;
    bool inQuotes = false;
    bool inDetails = false;
    bool inType = false;
    int curlyBraceLevel = 0;
    
    float startSpeed = 0;
    String pitchType = "";
    float xCoord = 0;
    float yCoord = 0;
    bool hasStartSpeed = false;
    bool hasType = false;
    bool hasXCoord = false;
    bool hasYCoord = false;
    
    while(http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available();
      if(size > 0) {
        int readBytes = stream->readBytes(buff, ((size > BUFFER_SIZE) ? BUFFER_SIZE : size));
        
        for(int i = 0; i < readBytes; i++) {
          char c = (char)buff[i];
          
          if(c == '{') {
            curlyBraceLevel++;
            if(curlyBraceLevel == 2) {
              inAllPlay = true;
            }
          } else if(c == '}') {
            if(inPitchData && curlyBraceLevel == 4) {
              if(foundTargetAtBat && hasStartSpeed && hasXCoord && hasYCoord && hasType) {
                if(numPitches < 10) {
                  pitches[numPitches].speed = startSpeed;
                  pitches[numPitches].x = xCoord;
                  pitches[numPitches].y = yCoord;
                  pitches[numPitches].type = pitchType;
                  numPitches++;
                  Serial.printf("Stored pitch #%d: %s %.1f mph (%.2f, %.2f)\n", 
                              numPitches, pitchType.c_str(), startSpeed, xCoord, yCoord);
                }
              }
              inPitchData = false;
              hasStartSpeed = hasXCoord = hasYCoord = hasType = false;
            }
            curlyBraceLevel--;
            
            if(curlyBraceLevel == 1) {
              inAllPlay = false;
            } else if(curlyBraceLevel == 3) {
              inDetails = false;
            }
          } else if(c == '\"') {
            inQuotes = !inQuotes;
            if(inQuotes) {
              inKey = true;
              keyName = "";
            } else if(inKey) {
              inKey = false;
              if(keyName == "atBatIndex") {
                inValue = true;
                valueStr = "";
              } else if(keyName == "pitchData") {
                inPitchData = true;
              } else if(keyName == "details") {
                inDetails = true;
              } else if(keyName == "type") {
                inType = true;
              } else if(inPitchData && (keyName == "startSpeed" || keyName == "x" || keyName == "y")) {
                inValue = true;
                valueStr = "";
              } else if(keyName == "description" && inType) {
                inValue = true;
                valueStr = "";
              }
            }
          } else if(inKey && inQuotes) {
            keyName += c;
          } else if(inValue) {
            if((isDigit(c) || c == '.' || c == '-' || (inType && isAlpha(c))) && valueStr.length() < 20) {
              valueStr += c;
            } else if(c == ',' || c == '}') {
              if(keyName == "atBatIndex" && inAllPlay) {
                if(valueStr.toInt() == atBatIndex && !foundTargetAtBat) {
                  foundTargetAtBat = true;
                  Serial.print("Found target at-bat #");
                  Serial.println(atBatIndex);
                }
              } else if(foundTargetAtBat) {
                if(inPitchData) {
                  if(keyName == "startSpeed") {
                    startSpeed = valueStr.toFloat();
                    hasStartSpeed = true;
                  } else if(keyName == "x") {
                    xCoord = valueStr.toFloat();
                    hasXCoord = true;
                  } else if(keyName == "y") {
                    yCoord = valueStr.toFloat();
                    hasYCoord = true;
                  }
                } else if(inType && keyName == "description") {
                  pitchType = valueStr;
                  hasType = true;
                  inType = false;
                }
              }
              inValue = false;
            }
          }
        }
        
        if(len > 0) {
          len -= readBytes;
        }
      }
    }
    
    Serial.printf("Found %d pitches for at-bat #%d\n", numPitches, atBatIndex);
  }
  
  http.end();
}

void prepMLB() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(0xFFFF, 0x0000);
}

// Modified doMLB function
void doMLB() {
    currentGameId = getLatestGameId();
    if(!currentGameId.isEmpty()) {
        currentAtBat = getLastAtBatIndex(currentGameId);
        if(currentAtBat >= 0) {
            numPitches = 0;  // Reset pitch count
            getLastGoodAtBat(currentGameId, currentAtBat);  // Try current and previous at-bat
            
            // Draw game info
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(2);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            
            // Display teams and score
            String scoreText = awayTeam + " " + String(awayScore) + " @ " + 
                             homeTeam + " " + String(homeScore);
            tft.setCursor(5, 5);
            tft.println(scoreText);
            
            // Display inning
            String inningText = inningState + " " + String(inningNum);
            tft.setCursor(5, 25);
            tft.println(inningText);
            
            // Draw strike zone grid (230x230)
            int gridX = 5;
            int gridY = 45;
            int gridSize = 230;
            int cellSize = gridSize / 3;
            
            // Draw grid lines
            tft.drawRect(gridX, gridY, gridSize, gridSize, TFT_WHITE);
            for(int i = 1; i < 3; i++) {
                tft.drawFastHLine(gridX, gridY + (cellSize * i), gridSize, TFT_WHITE);
                tft.drawFastVLine(gridX + (cellSize * i), gridY, gridSize, TFT_WHITE);
            }
            
            // Plot pitches
            for(int i = 0; i < numPitches; i++) {
                // Convert pitch coordinates to screen coordinates
                // MLB coordinates: x is -2.5 to 2.5 feet, y is 0 to 5 feet typically
                int screenX = gridX + (pitches[i].x + 2.5) * (gridSize / 5);
                int screenY = gridY + gridSize - (pitches[i].y * (gridSize / 5));
                
                // Draw pitch location
                uint16_t color = TFT_RED;  // Different colors for different pitch types?
                tft.fillCircle(screenX, screenY, 5, color);
                
                // Draw pitch info below grid
                int textY = gridY + gridSize + 10 + (i * 20);
                if(textY < 320) {  // Make sure we don't go off screen
                    tft.setCursor(5, textY);
                    tft.setTextSize(1);
                    tft.printf("%s - %.1f mph", pitches[i].type.c_str(), pitches[i].speed);
                }
            }
        }
    }
}

void prepDisplay() {
  tft.fillScreen(TFT_BLACK);
  tft.drawJpgFile(LittleFS, "/ui.jpg", 0, -10);
}

void prepDisplay2() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  TJpgDec.drawFsJpg(0, -10, "/pg2.jpg", LittleFS);
}

void doDisplay() {

  //float pm25in, pm25out, bridgetemp, bridgehum, windspeed, winddir, windchill, windgust, humidex, bridgeco2, bridgeIrms, watts, kw, tempSHT, humSHT, co2SCD;
  indewp = tempSHT - ((100 - humSHT)/5); //calculate dewpoint
  inhumidex = tempSHT + 0.5555 * (6.11 * pow(2.71828, 5417.7530*( (1/273.16) - (1/(273.15 + indewp)) ) ) - 10); //calculate humidex using Environment Canada formula
   
  String tempstring = String(tempSHT, 1) + "°C";
  String humstring = String(inhumidex, 1) + "°C";
  String windstring = String(windspeed, 0) + "kph";
  String pm25instring = String(pm25in, 0) + "g";
  String upco2string = String(co2SCD, 0) + "p";
  String presstring = String(presBME, 0) + "m";
  String poolstring = String(temppool, 1) + "°C";

  String outtempstring;
  temptodraw = min(bridgetemp, min(neotemp, jojutemp));
  outtempstring = String(temptodraw, 1) + "°C";

   
  String outdewstring = String(humidex, 1) + "°C";
  String winddirstring = windDirection(winddir);
  String pm25outstring = String(pm25out, 0) + "g";
  String downco2string = String(bridgeco2, 0) + "p";
  // if (watts < 1000) {String powerstring = String(watts,0) + "W";}

  String powerstring = String(kw, 1) + "KW";


  //String touchstring = String(t_x) + "," + String(t_y);
  tft.setTextDatum(TR_DATUM);
  img.fillSprite(TFT_BLACK);
  img.drawString(tempstring, 73, 11);
  img.drawString(humstring, 73, 52);
  img.drawString(windstring, 73, 94); 
  img.drawString(pm25instring, 73, 136);
  img.drawString(upco2string, 73, 182);
  img.drawString(presstring, 73, 221);
  img.drawString(poolstring, 73, 267);
  img.pushSprite(46, 0);

  img2.fillSprite(TFT_BLACK);
  img2.drawString(outtempstring, 73, 11);
  img2.drawString(outdewstring, 73, 52);
  img2.drawString(winddirstring, 73, 94);
  img2.drawString(pm25outstring, 73, 136);
  img2.drawString(downco2string, 73, 182);
  img2.drawString(powerstring, 73, 221);
  img2.pushSprite(155, 0);
}

void doDisplay2() {
  tft.setTextDatum(TR_DATUM);
  tft.setTextFont(1);
  tft.setCursor(115, 237);
  tft.print(titleLine);
  tft.setCursor(115, 247);
  tft.print(ssid);
  tft.setCursor(115, 257);
  tft.print(WiFi.localIP());
  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  tft.setCursor(115, 267);
  tft.print(asctime(timeinfo));
  tft.setCursor(115, 277);
  tft.print("My Temp: ");
  tft.print(temp);
  tft.print(" C");
  tft.setCursor(115, 287);
  tft.print("My Hum: ");
  tft.print(hum);
  tft.println("%");
}


bool isSleeping = false;

void setup() {

  pinMode(LED_PIN, OUTPUT);
  analogWrite(LED_PIN, brightness);

  Serial.begin(115200);
  Serial.println("\n\n Testing TJpg_Decoder library");

  LittleFS.begin();


  // Initialise the TFT
  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.setRotation(0);
  //tft.setViewport(8, 0, tft.width()-16, tft.height() - 26, true);
  
  tft.setTextColor(0xFFFF, 0x0000);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextWrap(true);  // Wrap on width
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.print("Connecting...");
  tft.setCursor(15, 25);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    tft.print(".");
  }
  
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(15, 10);
  tft.print("Connected!");
  tft.setCursor(15, 25);
  tft.print(titleLine);
  tft.setCursor(15, 40);
  tft.print(ssid);
  tft.setCursor(15, 65);
  tft.print(WiFi.localIP());
  initSNTP();
  //testMLBFeed();
  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  tft.setCursor(15, 80);
  tft.print(asctime(timeinfo));
  tft.setCursor(15, 95);
  ArduinoOTA.setHostname("Indiana");
  ArduinoOTA.begin();
  tft.print("OTA at /update.");
  
  tft.setCursor(15, 110);
  tft.print("Touch to continue.");

  //setPngPosition(0, 0);
  //load_png("https://i.imgur.com/EeCUlxr.png");
  //drawMeasurementGrid();
  while((!tft.getTouch(&t_x, &t_y)) && (millis() < 15000)){delay(1);}
  touch_calibrate();  
  tft.setTextWrap(false);  // Wrap on width
  img.setColorDepth(16);
  img2.setColorDepth(16);
  // ESP32 will crash if any of the fonts are missing
  bool font_missing = false;
  /*if (LittleFS.exists("/YuGothicUI-Regular-10.vlw")    == false) font_missing = true;
  if (LittleFS.exists("/YuGothicUI-Regular-12.vlw")    == false) font_missing = true;
  if (LittleFS.exists("/YuGothicUI-Regular-14.vlw")    == false) font_missing = true;
  if (LittleFS.exists("/YuGothicUI-Regular-16.vlw")    == false) font_missing = true;
  if (LittleFS.exists("/YuGothicUI-Regular-18.vlw")    == false) font_missing = true;*/
  if (LittleFS.exists("/NotoSans-Condensed-26.vlw") == false) font_missing = true;
  if (font_missing) {
    Serial.println("\r\nFont missing in LittleFS, did you upload it?");
    tft.print("ERROR Fonts missing.");
  }
  tft.setSwapBytes(true);  // We need to swap the colour bytes (endianess)
  // The jpeg image can be scaled by a factor of 1, 2, 4, or 8
  TJpgDec.setJpgScale(1);
  // The decoder must be given the exact name of the rendering function above
  TJpgDec.setCallback(tft_output);


  //uint16_t t_x = 0, t_y = 0; // To store the touch coordinates


  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Blynk.config(auth, IPAddress(192, 168, 50, 197), 8080);
  Blynk.connect();

  terminal.println(titleLine);
  terminal.print("Connected to ");
  terminal.println(ssid);
  terminal.print("IP address: ");
  terminal.println(WiFi.localIP());
  printLocalTime();
  terminal.flush();


  img.createSprite(73, 320);
  img.setTextDatum(TR_DATUM);
  img.setTextColor(TFT_WHITE, TFT_BLACK);
img.loadFont(NotoSansCondensed26);

  img2.createSprite(73, 253);
  img2.setTextDatum(TR_DATUM);
  img2.setTextColor(TFT_WHITE, TFT_BLACK);
img2.loadFont(NotoSansCondensed26);


  prepDisplay();
  doDisplay();
  tft.setTextFont(1);
  pinMode(ldr_pin, INPUT);
}

void loop() {
      if (WiFi.status() == WL_CONNECTED) {Blynk.run();   ArduinoOTA.handle();}  //don't do Blynk unless wifi
      else { //if no wifi, try to reconnect
        if (millis() - reconnectTime > 30000) {
              WiFi.disconnect();
              WiFi.reconnect();
              reconnectTime = millis();
        }

      } 

  bool pressed = tft.getTouch(&t_x, &t_y);
  if (pressed) {
    tft.fillSmoothCircle(t_x, t_y, 4, TFT_YELLOW);
    every(250) {
      Serial.print(t_x);
      Serial.print(",");
      Serial.println(t_y);
    }


    if (page == 3) {
      page = 1;
      prepDisplay();
      doDisplay();
    }
    if (page == 4) {
      page = 1;
      prepDisplay();
      doDisplay();
    }
    if (page == 2) {
      if ((t_x > 31) && (t_y > 227) && (t_x < 100) && (t_y < 285)) {  //BACK button
        delay(100);
        page = 1;
        prepDisplay();
        doDisplay();
      }
      if ((t_x > 30) && (t_y > 30) && (t_x < 100) && (t_y < 87)) {  //BRIGHTNESS DOWN button
        delay(250);
        brightness -= 16;
        if (brightness < 1) { brightness = 1; }
        if (brightness > 255) { brightness = 255; }
        analogWrite(LED_PIN, brightness);
        Blynk.virtualWrite(V1, brightness);
      }
      if ((t_x > 137) && (t_y > 30) && (t_x < 205) && (t_y < 87)) {  //BRIGHTNESS UP button
        delay(250);
        brightness += 16;
        if (brightness < 1) { brightness = 1; }
        if (brightness > 255) { brightness = 255; }
        analogWrite(LED_PIN, brightness);
        Blynk.virtualWrite(V1, brightness);
      }
      if ((t_x > 31) && (t_y > 121) && (t_x < 102) && (t_y < 182)) {  //ORRERY  button

      }
      if ((t_x > 137) && (t_y > 121) && (t_x < 205) && (t_y < 182)) {  //MLB button
        page = 4;
        prepMLB();
        doMLB();
        delay(300);
      }
    }
    if (page == 1) {                                                   //MAIN display
      if ((t_x > 130) && (t_y > 268) && (t_x < 240) && (t_y < 320)) {  //SETTINGS button
        delay(100);
        page = 2;
        prepDisplay2();
        doDisplay2();
      }
    }
  }

  // Pressed will be set true is there is a valid touch on the screen


  every(20000) {
    if (page == 1) { doDisplay(); }
    if (page == 2) { doDisplay2(); }
    if (page == 4) { doMLB(); }
  }



  every(5000){
    if (autobright) {

      ldr_read = analogRead(ldr_pin);
      newldr = map(ldr_read, 0, 4096, 0, 255);
      // Apply non-linear scaling for indoor brightness adjustment
      float scale = newldr / 255.0;
      newldr = (int)(pow(scale, 0.45) * 255);  // 0.75 gamma boosts mid-range brightness

      // Apply hysteresis to prevent flickering
      if (previous_brightness == -1 || abs(newldr - previous_brightness) > hysteresis_threshold) {
          previous_brightness = newldr;
          newldr += brightness;
          if (newldr < 1) { newldr = 1; }
          if (newldr > 255) { newldr = 255; }
          analogWrite(LED_PIN, newldr);
      }
    }
  }

  every(60000) {

        struct tm timeinfo;
  getLocalTime(&timeinfo);
  hours = timeinfo.tm_hour;
  mins = timeinfo.tm_min;
  secs = timeinfo.tm_sec;
    /*if ((hours == 17) && (!isSleeping)){
      for(int i=brightness; i<255; i++)
      {
        analogWrite(LED_PIN, i);
        delay(40);
      }
      analogWrite(LED_PIN, 1);
      isSleeping = true;
    }
      if ((hours == 8) && (isSleeping)){
      for (int i=255; i>brightness; i--)
        {
          analogWrite(LED_PIN, i);
          delay(40);
        }
        analogWrite(LED_PIN, brightness);
      isSleeping = false;
    }*/
  }
  delay(10);  // UI debouncing
}
