#include <TJpg_Decoder.h>
#define USE_LINE_BUFFER  // Enable for faster rendering
#define FS_NO_GLOBALS
#include <FS.h>
#ifdef ESP32
#include "SPIFFS.h"  // ESP32 only
#endif
#include "SPI.h"
#include <TFT_eSPI.h>  // Hardware-specific library

 #define MATRIX_WIDTH                192   // Single panel of 64 pixel width

 #define MATRIX_HEIGHT               96
 #define FACTOR                       3

 #define CHAIN_LENGTH                3   // Number of modules chained together, i.e. 4 panels chained result in virtualmatrix 64x4=256 px long


#include <Wire.h>
#include <Arduino.h>
#include "Adafruit_SHT31.h"
#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include "time.h"


#include "constants.h"
#include "ApiHandler.h"
#include "./util.hpp"


unsigned long time_counter = 0;
ApiHandler* apiHandler;

//Define a score struct
struct Score {
  int home;
  int away;
};

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library

Score currentScore;

HTTPClient http;
DynamicJsonDocument doc(JSON_BUFFER_SIZE);

class Game {
private:
  String homeTeam;
  int homeTeamId;
  String awayTeam;
  int awayTeamId;
  int homeTeamScore;
  int awayTeamScore;
  int currentInning;
  bool topOfInning;
  bool firstBaseOccupied;
  bool secondBaseOccupied;
  bool thirdBaseOccupied;

  /* data */
public:
  Game(JsonObject game);
  ~Game();
  String getHomeTeam() {
    return this->homeTeam;
  };
  int getHomeTeamId() {
    return this->homeTeamId;
  };
  String getAwayTeam() {
    return this->awayTeam;
  };
  int getAwayTeamId() {
    return this->awayTeamId;
  };
  int getHomeTeamScore() {
    return this->homeTeamScore;
  };
  int getAwayTeamScore() {
    return this->awayTeamScore;
  };
  int getCurrentInning() {
    return this->currentInning;
  };
  bool isTopOfInning() {
    return this->topOfInning;
  };
  bool isFirstBaseOccupied() {
    return this->firstBaseOccupied;
  };
  bool isSecondBaseOccupied() {
    return this->secondBaseOccupied;
  };
  bool isThirdBaseOccupied() {
    return this->thirdBaseOccupied;
  };
};

Game::Game(JsonObject game) {
  this->homeTeam = game["teams"]["home"]["team"]["abbreviation"].as<String>();
  this->homeTeamId = game["teams"]["home"]["team"]["id"].as<int>();
  this->awayTeam = game["teams"]["away"]["team"]["abbreviation"].as<String>();
  this->awayTeamId = game["teams"]["away"]["team"]["id"].as<int>();
  this->homeTeamScore = game["linescore"]["teams"]["home"]["runs"].as<int>();
  this->awayTeamScore = game["linescore"]["teams"]["away"]["runs"].as<int>();
  this->currentInning = game["linescore"]["currentInning"].as<int>();
  this->topOfInning = game["linescore"]["isTopInning"].as<bool>();
  this->firstBaseOccupied = game["linescore"]["offense"]["first"].as<bool>();
  this->secondBaseOccupied = game["linescore"]["offense"]["second"].as<bool>();
  this->thirdBaseOccupied = game["linescore"]["offense"]["third"].as<bool>();

  Serial.println(game["linescore"]["currentInning"].as<int>());
}

Game::~Game() {
}




class GameDrawer {
private:
  void drawLayout(Game* game);
  void drawScores(Game* game);
  void drawBox(int16_t x, int16_t y, String text, uint16_t bg_color, uint16_t fg_color);
  void drawInning(Game* game);
  void topInningTriangle();
  void bottomInningTriangle();
  void drawBases(Game* game);
  void drawBase(int16_t x, int16_t y, bool occupied);
public:
  GameDrawer();
  ~GameDrawer();
  void drawGame(Game* game);
  void drawLoading();
};

GameDrawer gameDrawer;


GameDrawer::GameDrawer(/* args */) {
}

GameDrawer::~GameDrawer() {
}

void GameDrawer::drawGame(Game* game) {
  drawLayout(game);
  drawScores(game);
  drawBases(game);
}

void GameDrawer::drawLayout(Game* game) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(tft.color565(255, 255, 255));

  int16_t upperLeftX, upperLeftY;
  uint16_t width, height;

  drawBox(15, 15, game->getAwayTeam(), TEAM_BG_COLORS.find(game->getAwayTeamId())->second, TEAMS_TEXT_COLORS.find(game->getAwayTeamId())->second);
  drawInning(game);
  drawBox(MATRIX_WIDTH - 21, 15, game->getHomeTeam(), TEAM_BG_COLORS.find(game->getHomeTeamId())->second, TEAMS_TEXT_COLORS.find(game->getHomeTeamId())->second);
}

void GameDrawer::topInningTriangle() {
  tft.fillTriangle(24*FACTOR, 7*FACTOR, 32*FACTOR, 7*FACTOR, 28*FACTOR, 3*FACTOR, tft.color565(255, 255, 255));
}

void GameDrawer::bottomInningTriangle() {
  tft.fillTriangle(24*FACTOR, 3*FACTOR, 32*FACTOR, 3*FACTOR, 28*FACTOR, 7*FACTOR, tft.color565(255, 255, 255));
}

void GameDrawer::drawInning(Game* game) {
  if (game->isTopOfInning()) {
    topInningTriangle();
  } else {
    bottomInningTriangle();
  }
  tft.setCursor(35*FACTOR, 2*FACTOR);
  tft.print(game->getCurrentInning());
}

void GameDrawer::drawBox(int16_t x, int16_t y, String text, uint16_t bg_color, uint16_t fg_color) {
  int16_t upperLeftX, upperLeftY;
  uint16_t width, height;

  //tft.getTextBounds(text.c_str(), x + BOX_PADDING, y + BOX_PADDING, &upperLeftX, &upperLeftY, &width, &height);
  upperLeftX = x+2;
  upperLeftY = y+2;
  width = tft.textWidth(text.c_str());
  height = 17;
  tft.setCursor(upperLeftX, upperLeftY);

  // Draw Away team box - should always start at 0,0
  tft.fillRect(x, y, width + 2 * BOX_PADDING - 1, height + 2 * BOX_PADDING - 1, bg_color);
  tft.drawRect(x, y, width + 2 * BOX_PADDING - 1, height + 2 * BOX_PADDING - 1, fg_color);
  tft.setTextColor(fg_color);
  tft.print(text);
  tft.setTextColor(COLORS::WHITE);
}

void GameDrawer::drawScores(Game* game) {
  drawBox(6*FACTOR, 13*FACTOR, String(game->getAwayTeamScore()), COLORS::BLACK, COLORS::WHITE);

  drawBox(MATRIX_WIDTH - 15, 13*FACTOR, String(game->getHomeTeamScore()), COLORS::BLACK, COLORS::WHITE);
}

void GameDrawer::drawLoading() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(tft.color565(255, 255, 255));
  tft.setCursor(0, 0);
  tft.print("Loading...");
}

/**
 * @brief Draws a diamond base using two triangles
 * @param x X coordinate of the top verex of the base
 * @param y Y coordinate of the top verex of the base
 * @param occupied if the base is occupied
*/
void GameDrawer::drawBase(int16_t x, int16_t y, bool occupied) {
  if (occupied) {
    tft.fillTriangle(x, y, x - BASE_SIDE_LENGTH + 1, y + BASE_SIDE_LENGTH - 1, x, y + (2 * (BASE_SIDE_LENGTH - 1)), COLORS::YELLOW);
    tft.fillTriangle(x, y, x + BASE_SIDE_LENGTH - 1, y + BASE_SIDE_LENGTH - 1, x, y + (2 * (BASE_SIDE_LENGTH - 1)), COLORS::YELLOW);
  } else {
    tft.drawTriangle(x, y, x - BASE_SIDE_LENGTH + 1, y + BASE_SIDE_LENGTH - 1, x, y + (2 * (BASE_SIDE_LENGTH - 1)), COLORS::YELLOW);
    tft.drawTriangle(x, y, x + BASE_SIDE_LENGTH - 1, y + BASE_SIDE_LENGTH - 1, x, y + (2 * (BASE_SIDE_LENGTH - 1)), COLORS::YELLOW);

    // Remove line in the middle of the base
    tft.drawLine(x, y + 1, x, y + (2 * (BASE_SIDE_LENGTH - 1)) - 1, COLORS::BLACK);
  }
}

void GameDrawer::drawBases(Game* game) {
  drawBase(BASES_TOP_X + BASE_SIDE_LENGTH + 1, BASES_TOP_Y + BASE_SIDE_LENGTH + 1, game->isFirstBaseOccupied());
  drawBase(BASES_TOP_X, BASES_TOP_Y, game->isSecondBaseOccupied());
  drawBase(BASES_TOP_X - BASE_SIDE_LENGTH - 1, BASES_TOP_Y + BASE_SIDE_LENGTH + 1, game->isThirdBaseOccupied());
}






TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite img2 = TFT_eSprite(&tft);
TFT_eSprite imgOrr = TFT_eSprite(&tft);  // Sprite class

#include <HTTPClient.h>

#include "support_functions.h"

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

AsyncWebServer server(80);


WidgetTerminal terminal(V10);



#define sunX tft.width() / 2
#define sunY tft.height() / 2

uint16_t orb_inc;
uint16_t planet_r;

#include <stdio.h>
#include "astronomy.h"
#define TIME_TEXT_BYTES 25

astro_time_t astro_time;

uint16_t grey;

static const astro_body_t body[] = {
  BODY_SUN, BODY_MERCURY, BODY_VENUS, BODY_EARTH, BODY_MARS,
  BODY_JUPITER, BODY_SATURN, BODY_URANUS, BODY_NEPTUNE
};

static const uint16_t bodyColour[] = {
  TFT_YELLOW, TFT_DARKGREY, TFT_ORANGE, TFT_BLUE, TFT_RED,
  TFT_GOLD, TFT_BROWN, TFT_DARKCYAN, TFT_CYAN
};

//TFT_eSprite img3 = TFT_eSprite(&tft);
#define LED_PIN 32

int page = 1;
uint16_t t_x = 0, t_y = 0;        // To store the touch coordinates
uint16_t oldt_x = 0, oldt_y = 0;  // To store the touch coordinates


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
#define LABEL1_FONT &FreeSansOblique12pt7b  // Key label font 1
#define LABEL2_FONT &FreeSansBold12pt7b     // Key label font 2


// We have a status line for messages
#define STATUS_X 120  // Centred on this
#define STATUS_Y 65

// Create 15 keys for the keypad
char keyLabel[15][5] = { "New", "Del", "Send", "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "#" };
uint16_t keyColor[15] = { TFT_RED, TFT_DARKGREY, TFT_DARKGREEN,
                          TFT_BLUE, TFT_BLUE, TFT_BLUE,
                          TFT_BLUE, TFT_BLUE, TFT_BLUE,
                          TFT_BLUE, TFT_BLUE, TFT_BLUE,
                          TFT_BLUE, TFT_BLUE, TFT_BLUE };

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
  if (!SPIFFS.begin()) {
    Serial.println("formatting file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL) {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    } else {
      File f = SPIFFS.open(CALIBRATION_FILE, "r");
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
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
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

float temppool, pm25in, pm25out, bridgetemp, bridgehum, windspeed, winddir, windchill, windgust, humidex, bridgeco2, bridgeIrms, watts, kw, tempSHT, humSHT, co2SCD, presBME, neotemp;

BLYNK_WRITE(V41) {
  neotemp = param.asFloat();
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

// =========================================================================
// Get coordinates of end of a vector, pivot at x,y, length r, angle a
// =========================================================================
// Coordinates are returned to caller via the xp and yp pointers
#define DEG2RAD 0.0174532925
void getCoord(int x, int y, int* xp, int* yp, int r, float a) {
  float sx1 = cos(-a * DEG2RAD);
  float sy1 = sin(-a * DEG2RAD);
  *xp = sx1 * r + x;
  *yp = sy1 * r + y;
}

// =========================================================================
// Convert astronomical time to UTC and display
// =========================================================================
void showTime(astro_time_t time) {
  astro_status_t status;
  char text[TIME_TEXT_BYTES];

  status = Astronomy_FormatTime(time, TIME_FORMAT_SECOND, text, sizeof(text));
  if (status != ASTRO_SUCCESS) {
    fprintf(stderr, "\nFATAL(PrintTime): status %d\n", status);
    exit(1);
  }
  tft.setTextDatum(TL_DATUM);
  tft.drawString(text, 15, 10, 2);
}

// =========================================================================
// Plot planet positions as an Orrery
// =========================================================================
int plot_planets(void) {
  astro_angle_result_t ang;

  int i;
  int num_bodies = sizeof(body) / sizeof(body[0]);

  // i initialised to 1 so Sun is skipped
  for (i = 1; i < num_bodies; ++i) {
    ang = Astronomy_EclipticLongitude(body[i], astro_time);

    int x1 = 0;  // getCoord() will update these
    int y1 = 0;

    getCoord(0, 0, &x1, &y1, i * 14, ang.angle);  // Get x1 ,y1

    imgOrr.fillSprite(TFT_TRANSPARENT);
    imgOrr.fillCircle(9, 9, 5, TFT_BLACK);
    imgOrr.drawCircle(9 - x1, 9 - y1, i * 14, grey);
    imgOrr.fillCircle(9, 9, 3, bodyColour[i]);
    imgOrr.pushSprite(sunX + x1 - 9, sunY + y1 - 9, TFT_TRANSPARENT);

    if (body[i] == BODY_EARTH) {
      astro_angle_result_t mang = Astronomy_LongitudeFromSun(BODY_MOON, astro_time);

      int xm = 0;
      int ym = 0;

      getCoord(x1, y1, &xm, &ym, 7, 180 + ang.angle + mang.angle);  // Get x1 ,y1

      imgOrr.fillSprite(TFT_TRANSPARENT);
      imgOrr.fillCircle(9, 9, 4, TFT_BLACK);
      imgOrr.drawCircle(9 - xm, 9 - ym, i * 14, grey);
      imgOrr.fillCircle(9, 9, 1, TFT_WHITE);
      imgOrr.pushSprite(sunX + xm - 9, sunY + ym - 9, TFT_TRANSPARENT);
    }
  }

  return 0;
}

void prepOrrery() {
  tft.fillScreen(TFT_BLACK);
  astro_time = Astronomy_MakeTime(2020, 10, 16, 19, 31, 0);
  tft.fillCircle(sunX, sunY, 5, TFT_YELLOW);  //10

  // i initialised to 1 so Sun is skipped
  for (int i = 1; i < sizeof(body) / sizeof(body[0]); ++i) {
    tft.drawCircle(sunX, sunY, i * 14, grey);
  }
}

void prepMLB() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(0xFFFF, 0x0000);
    JsonObject schedule = apiHandler->getTeamScheduleToday(TEAM_ID::TORONTO_BLUE_JAYS);
    Game* game = new Game(schedule["dates"][0]["games"][0].as<JsonObject>());
  gameDrawer.drawGame(game);
}

void prepDisplay() {
  tft.fillScreen(TFT_BLACK);
  TJpgDec.drawFsJpg(0, 0, "/ui.jpg");
}

void prepDisplay2() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  TJpgDec.drawFsJpg(0, 0, "/pg2.jpg");
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

  if ((bridgetemp > neotemp) && (neotemp != 0))
  {outtempstring = String(neotemp, 1) + "°C";}
  else {outtempstring = String(bridgetemp, 1) + "°C";}

   
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



void doOrrery() {
  plot_planets();
  showTime(astro_time);

  // Add time increment (more than 0.6 days will lead to stray pixel on screen
  // due to the way previous object images are erased)
  astro_time = Astronomy_AddDays(astro_time, 0.25);  // 0.25 day (6 hour) increment
}

void doMLB() {
  every(30000) {
    JsonObject schedule = apiHandler->getTeamScheduleToday(TEAM_ID::TORONTO_BLUE_JAYS);
    Game* game = new Game(schedule["dates"][0]["games"][0].as<JsonObject>());
    gameDrawer.drawGame(game);
  }
}

bool isSleeping = false;

void setup() {

  pinMode(LED_PIN, OUTPUT);
  analogWrite(LED_PIN, brightness);

  Serial.begin(115200);
  Serial.println("\n\n Testing TJpg_Decoder library");
  Wire.begin(26, 25);
  // Initialise SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield();  // Stay here twiddling thumbs waiting
  }
  Serial.println("\r\nInitialisation done.");

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
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Hi! I am Indiana.");
  });

  AsyncElegantOTA.begin(&server);  // Start ElegantOTA
  server.begin();
  tft.print("OTA at /update.");
  
  tft.setCursor(15, 110);
  tft.print("Touch to continue.");

  //setPngPosition(0, 0);
  //load_png("https://i.imgur.com/EeCUlxr.png");

  while(!tft.getTouch(&t_x, &t_y)){}
  touch_calibrate();  
  tft.setTextWrap(false);  // Wrap on width
  img.setColorDepth(16);
  img2.setColorDepth(16);
  // ESP32 will crash if any of the fonts are missing
  bool font_missing = false;
  /*if (SPIFFS.exists("/YuGothicUI-Regular-10.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-12.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-14.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-16.vlw")    == false) font_missing = true;
  if (SPIFFS.exists("/YuGothicUI-Regular-18.vlw")    == false) font_missing = true;*/
  if (SPIFFS.exists("/YuGothicUI-Regular-20.vlw") == false) font_missing = true;
  if (SPIFFS.exists("/NotoSans-Condensed-22.vlw") == false) font_missing = true;
  if (SPIFFS.exists("/NotoSans-Condensed-24.vlw") == false) font_missing = true;
  if (SPIFFS.exists("/NotoSans-Condensed-26.vlw") == false) font_missing = true;
  if (font_missing) {
    Serial.println("\r\nFont missing in SPIFFS, did you upload it?");
    tft.print("ERROR Fonts missing.");
    while (1) yield();
  }
  tft.setSwapBytes(true);  // We need to swap the colour bytes (endianess)
  // The jpeg image can be scaled by a factor of 1, 2, 4, or 8
  TJpgDec.setJpgScale(1);
  // The decoder must be given the exact name of the rendering function above
  TJpgDec.setCallback(tft_output);



  Serial.println("SHT31 test");
  if (!sht31.begin(0x44)) {  // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
  } else {
    Serial.println("Found SHT31");
  }
  temp = sht31.readTemperature();
  hum = sht31.readHumidity();

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

  imgOrr.createSprite(19, 19);
  grey = tft.color565(30, 30, 30);

  prepDisplay();
  doDisplay();
  tft.setTextFont(1);
  apiHandler = new ApiHandler(&http, &doc);
}

void loop() {
  Blynk.run();
  bool pressed = tft.getTouch(&t_x, &t_y);
  if (pressed) {
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
        page = 3;
        prepOrrery();
        doOrrery();
        delay(300);
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


  every(3000) {
    if (page == 1) { doDisplay(); }
    if (page == 2) { doDisplay2(); }
  }
  if (page == 3) { doOrrery(); }

  every(60000) {
    temp = sht31.readTemperature();
    hum = sht31.readHumidity();
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
