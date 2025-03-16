#include <TJpg_Decoder.h>
#define USE_LINE_BUFFER  // Enable for faster rendering
#define FS_NO_GLOBALS
#include <FS.h>
#include <LittleFS.h>
#include "SPI.h"
#include <TFT_eSPI.h>  // Hardware-specific library

#include <ArduinoOTA.h>


#include <Wire.h>
#include <Arduino.h>
#include "Adafruit_SHT31.h"
#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <WiFi.h>

#include "time.h"


TFT_eSPI tft = TFT_eSPI();  // Invoke custom library


TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite img2 = TFT_eSprite(&tft);
TFT_eSprite imgOrr = TFT_eSprite(&tft);  // Sprite class



#define VERSION 1.04

String titleLine = "***INDIANA v" + String(VERSION) + "***";

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


// Invoke the TFT_eSPI button class and create all the button objects
TFT_eSPI_Button key[15];

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



//------------------------------------------------------------------------------------------

void touch_calibrate() {
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // check file system exists
  if (!LittleFS.begin()) {
    Serial.println("formatting file system");
    LittleFS.format();
    LittleFS.begin();
  }

  // check if calibration file exists and size is correct
  if (LittleFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL) {
      // Delete if we want to re-calibrate
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
    // calibration data valid
    tft.setTouch(calData);
  } else {
    // data not valid so recalibrate
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

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    // store data
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


void prepDisplay() {
  tft.fillScreen(TFT_BLACK);
  TJpgDec.drawFsJpg(0, 0, "/ui.jpg", LittleFS);
}

void prepDisplay2() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  TJpgDec.drawFsJpg(0, 0, "/pg2.jpg", LittleFS);
}

void doDisplay() {

  //float pm25in, pm25out, bridgetemp, bridgehum, windspeed, winddir, windchill, windgust, humidex, bridgeco2, bridgeIrms, watts, kw, tempSHT, humSHT, co2SCD;

  String tempstring = String(tempSHT, 1) + "°C";
  String humstring = String(humSHT, 1) + "%";
  String windstring = String(windspeed, 0) + "kph";
  String pm25instring = String(pm25in, 0) + "g";
  String upco2string = String(co2SCD, 0) + "p";
  String presstring = String(presBME, 0) + "m";
  String poolstring = String(temppool, 1) + "°C";

  String outtempstring;
  temptodraw = min(bridgetemp, min(neotemp, jojutemp));
  outtempstring = String(temptodraw, 1) + "°C";

   
  String outdewstring = String(bridgehum, 1) + "°C";
  String winddirstring = windDirection(winddir);
  String pm25outstring = String(pm25out, 0) + "g";
  String downco2string = String(bridgeco2, 0) + "p";
  // if (watts < 1000) {String powerstring = String(watts,0) + "W";}

  String powerstring = String(kw, 1) + "KW";


  //String touchstring = String(t_x) + "," + String(t_y);
  tft.setTextDatum(TR_DATUM);
  img.fillSprite(TFT_BLACK);
  img.drawString(tempstring, 73, 21);
  img.drawString(humstring, 73, 62);
  img.drawString(windstring, 73, 104);
  img.drawString(pm25instring, 73, 146);
  img.drawString(upco2string, 73, 192);
  img.drawString(presstring, 73, 231);
  img.drawString(poolstring, 73, 277);
  img.pushSprite(46, 0);

  img2.fillSprite(TFT_BLACK);
  img2.drawString(outtempstring, 73, 21);
  img2.drawString(outdewstring, 73, 62);
  img2.drawString(winddirstring, 73, 104);
  img2.drawString(pm25outstring, 73, 146);
  img2.drawString(downco2string, 73, 192);
  img2.drawString(powerstring, 73, 231);
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

  tft.setTextColor(0xFFFF, 0x0000);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
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
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
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

  //while(!tft.getTouch(&t_x, &t_y)){}
  //touch_calibrate();  
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
  if (LittleFS.exists("/YuGothicUI-Regular-20.vlw") == false) font_missing = true;
  if (LittleFS.exists("/NotoSans-Condensed-22.vlw") == false) font_missing = true;
  if (LittleFS.exists("/NotoSans-Condensed-24.vlw") == false) font_missing = true;
  if (LittleFS.exists("/NotoSans-Condensed-26.vlw") == false) font_missing = true;
  if (font_missing) {
    Serial.println("\r\nFont missing in LittleFS, did you upload it?");
    tft.print("ERROR Fonts missing.");
    while (1) yield();
  }
  tft.setSwapBytes(true);  // We need to swap the colour bytes (endianess)
  // The jpeg image can be scaled by a factor of 1, 2, 4, or 8
  TJpgDec.setJpgScale(1);
  // The decoder must be given the exact name of the rendering function above
  TJpgDec.setCallback(tft_output);


  //uint16_t t_x = 0, t_y = 0; // To store the touch coordinates


  //configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Blynk.config(auth, IPAddress(192, 168, 50, 197), 8080);
  Blynk.connect();

  terminal.println(titleLine);
  terminal.print("Connected to ");
  terminal.println(ssid);
  terminal.print("IP address: ");
  terminal.println(WiFi.localIP());
  printLocalTime();
  terminal.flush();

  img.loadFont(AA_FONT_26);
  img.createSprite(73, 320);
  img.setTextDatum(TR_DATUM);
  img.setTextColor(TFT_WHITE, TFT_BLACK, true);

  img2.loadFont(AA_FONT_26);
  img2.createSprite(73, 263);
  img2.setTextDatum(TR_DATUM);
  img2.setTextColor(TFT_WHITE, TFT_BLACK, true);



  prepDisplay();
  doDisplay();
  tft.setTextFont(1);

}

void loop() {
  Blynk.run();
  ArduinoOTA.handle();
  //bool pressed = tft.getTouch(&t_x, &t_y);
  /*if (pressed) {
    tft.fillSmoothCircle(t_x, t_y, 4, TFT_YELLOW, TFT_BLACK);
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
  }*/

  // Pressed will be set true is there is a valid touch on the screen


  every(3000) {
    if (page == 1) { doDisplay(); }
    if (page == 2) { doDisplay2(); }
  }


  every(60000) {

        struct tm timeinfo;
  getLocalTime(&timeinfo);
  hours = timeinfo.tm_hour;
  mins = timeinfo.tm_min;
  secs = timeinfo.tm_sec;
    if ((hours == 17) && (!isSleeping)){
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
    }
  }
  delay(10);  // UI debouncing
}
