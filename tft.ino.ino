#include <TJpg_Decoder.h>
#define FS_NO_GLOBALS
#include <FS.h>
#ifdef ESP32
  #include "SPIFFS.h" // ESP32 only
#endif
#include "SPI.h"
#include <TFT_eSPI.h>              // Hardware-specific library


#include <Wire.h>
#include <Arduino.h>
#include "Adafruit_SHT31.h"
#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include "time.h"

const char* ssid = "mikesnet";
const char* password = "springchicken";

#define AA_FONT_10 "YuGothicUI-Regular-10"
#define AA_FONT_12 "YuGothicUI-Regular-12"
#define AA_FONT_14 "YuGothicUI-Regular-14"
#define AA_FONT_16 "YuGothicUI-Regular-16"
#define AA_FONT_18 "YuGothicUI-Regular-18"
#define AA_FONT_20 "YuGothicUI-Regular-20"

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;  //Replace with your GMT offset (secs)
const int daylightOffset_sec = 3600;   //Replace with your daylight offset (secs)
int hours, mins, secs;

char auth[] = "qS5PQ8pvrbYzXdiA4I6uLEWYfeQrOcM4";

AsyncWebServer server(80);


WidgetTerminal terminal(V10);

TFT_eSPI tft = TFT_eSPI();         // Invoke custom library
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite img2 = TFT_eSprite(&tft);
//TFT_eSprite img3 = TFT_eSprite(&tft);
#define LED_PIN 32


#define every(interval) \
    static uint32_t __every__##interval = millis(); \
    if (millis() - __every__##interval >= interval && (__every__##interval = millis()))

bool enableHeater = false;
uint8_t loopCnt = 0;

Adafruit_SHT31 sht31 = Adafruit_SHT31();

#define CALIBRATION_FILE "/TouchCalData1"

// Set REPEAT_CAL to true instead of false to run calibration
// again, otherwise it will only be done once.
// Repeat calibration if you change the screen rotation.
#define REPEAT_CAL false



// Using two fonts since numbers are nice when bold
#define LABEL1_FONT &FreeSansOblique12pt7b // Key label font 1
#define LABEL2_FONT &FreeSansBold12pt7b    // Key label font 2


// We have a status line for messages
#define STATUS_X 120 // Centred on this
#define STATUS_Y 65

// Create 15 keys for the keypad
char keyLabel[15][5] = {"New", "Del", "Send", "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "#" };
uint16_t keyColor[15] = {TFT_RED, TFT_DARKGREY, TFT_DARKGREEN,
                         TFT_BLUE, TFT_BLUE, TFT_BLUE,
                         TFT_BLUE, TFT_BLUE, TFT_BLUE,
                         TFT_BLUE, TFT_BLUE, TFT_BLUE,
                         TFT_BLUE, TFT_BLUE, TFT_BLUE
                        };

// Invoke the TFT_eSPI button class and create all the button objects
TFT_eSPI_Button key[15];

// This next function will be called during decoding of the jpeg file to
// render each block to the TFT.  If you use a different TFT library
// you will need to adapt this function to suit.
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
   // Stop further decoding as image is running off bottom of screen
  if ( y >= tft.height() ) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);

  // This might work instead if you adapt the sketch to use the Adafruit_GFX library
  // tft.drawRGBBitmap(x, y, bitmap, w, h);

  // Return 1 to decode next block
  return 1;
}



//------------------------------------------------------------------------------------------

void touch_calibrate()
{
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // check file system exists
  if (!SPIFFS.begin()) {
    Serial.println("formatting file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL)
    {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    }
    else
    {
      File f = SPIFFS.open(CALIBRATION_FILE, "r");
      if (f) {
        if (f.readBytes((char *)calData, 14) == 14)
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
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calData, 14);
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

int brightness = 127;

BLYNK_WRITE(V1) {
  brightness = param.asInt();
  analogWrite(LED_PIN, brightness);
}

float temppool, pm25in, pm25out, bridgetemp, bridgehum, windspeed, winddir, windchill, windgust, humidex, bridgeco2, bridgeIrms, watts, kw, tempSHT, humSHT, co2SCD, presBME;

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
BLYNK_WRITE(V68) {
  windspeed = param.asFloat();
}
BLYNK_WRITE(V69) {
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

String windDirection(int temp_wind_deg)   //Source http://snowfence.umn.edu/Components/winddirectionanddegreeswithouttable3.htm
{
  switch(temp_wind_deg){
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

void prepDisplay(); {
  tft.fillScreen(TFT_BLACK);
  TJpgDec.drawFsJpg(0, 0, "/ui.jpg");
}

void doDisplay()
{
  //float dewpoint = bridgetemp - ((100 - bridgehum)/5); //calculate dewpoint
  //float pm25in, pm25out, bridgetemp, bridgehum, windspeed, winddir, windchill, windgust, humidex, bridgeco2, bridgeIrms, watts, kw, tempSHT, humSHT, co2SCD;

  String tempstring = String(tempSHT) + "°C";
  String humstring = String(humSHT) + "%";
  String windstring = String(windspeed, 0) + "kph";
  String pm25instring = String(pm25in,0) + "g";
  String upco2string = String(co2SCD,0) + "ppm";
  String presstring = String(presBME,0) + "mb";
  String poolstring = String(temppool) + "°C";

  String outtempstring = String(bridgetemp) + "°C";
  String outdewstring = String(bridgehum) + "°C";
  String winddirstring = windDirection(winddir);
  String pm25outstring = String(pm25out,0) + "g";
  String downco2string = String(bridgeco2,0) + "ppm";
  String powerstring = String(kw) + "KW";

  //String touchstring = String(t_x) + "," + String(t_y);

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

void setup()
{

  pinMode(LED_PIN, OUTPUT);
  analogWrite(LED_PIN, 127);

  Serial.begin(115200);
  Serial.println("\n\n Testing TJpg_Decoder library");
  Wire.begin(26,25);
  // Initialise SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield(); // Stay here twiddling thumbs waiting
  }
  Serial.println("\r\nInitialisation done.");

  // Initialise the TFT
  tft.begin();
  touch_calibrate();
  tft.setTextColor(0xFFFF, 0x0000);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
  tft.setTextWrap(true); // Wrap on width
  tft.print("Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        tft.print(".");
      } 

  tft.setTextWrap(false); // Wrap on width
  img.setColorDepth(16); 
  img2.setColorDepth(16); 
// ESP32 will crash if any of the fonts are missing
  bool font_missing = false;
  if (SPIFFS.exists("/YuGothicUI-Regular-10.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-12.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-14.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-16.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-18.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-20.vlw")    == false) font_missing = true;
  if (font_missing)
  {
    Serial.println("\r\nFont missing in SPIFFS, did you upload it?");
    tft.print("ERROR Fonts missing.");
    while(1) yield();
  }
  tft.setSwapBytes(true); // We need to swap the colour bytes (endianess)
  // The jpeg image can be scaled by a factor of 1, 2, 4, or 8
  TJpgDec.setJpgScale(1);
  // The decoder must be given the exact name of the rendering function above
  TJpgDec.setCallback(tft_output);

  prepDisplay();

  Serial.println("SHT31 test");
  if (! sht31.begin(0x44)) {   // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
  }
  else {
    Serial.println("Found SHT31");
  }
  temp = sht31.readTemperature();
  hum = sht31.readHumidity();

  uint16_t t_x = 0, t_y = 0; // To store the touch coordinates


  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hi! I am Indiana.");
  });

  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();
  Serial.println("HTTP server started");
  delay(250);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Blynk.config(auth, IPAddress(192, 168, 50, 197), 8080);
  Blynk.connect();
  delay(250);
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  hours = timeinfo.tm_hour;
  mins = timeinfo.tm_min;
  secs = timeinfo.tm_sec;
  terminal.println("***SERVER STARTED***");
  terminal.print("Connected to ");
  terminal.println(ssid);
  terminal.print("IP address: ");
  terminal.println(WiFi.localIP());
  printLocalTime();
  terminal.flush();
  
  img.loadFont(AA_FONT_16);
  img.createSprite(73, 320);
  img.setTextDatum(TR_DATUM);     
  img.setTextColor(TFT_WHITE, TFT_BLACK, true); 

  img2.loadFont(AA_FONT_16);
  img2.createSprite(73, 263);
  img2.setTextDatum(TR_DATUM);     
  img2.setTextColor(TFT_WHITE, TFT_BLACK, true); 
}

void loop()
{
  Blynk.run();
uint16_t t_x = 0, t_y = 0; // To store the touch coordinates

  // Pressed will be set true is there is a valid touch on the screen
  bool pressed = tft.getTouch(&t_x, &t_y);
  if (pressed){
    every(250){
      temp = sht31.readTemperature();
     hum = sht31.readHumidity();
    }
  }

  every(3000){
        temp = sht31.readTemperature();
     hum = sht31.readHumidity();  
     doDisplay();
  }



      delay(10); // UI debouncing

}



