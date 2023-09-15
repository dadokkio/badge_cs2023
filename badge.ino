/**********************************************************************************************************************/
// DISPLAY
/**********************************************************************************************************************/
#define ENABLE_GxEPD2_GFX 1
#include <GxEPD2_4G.h> // needs be first include
#include <GxEPD2_BW.h>
GxEPD2_4G<GxEPD2_290_T5, GxEPD2_290_T5::HEIGHT> display(GxEPD2_290_T5(/*CS=D8*/ SS, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));

/**********************************************************************************************************************/
// FILE SYSTEM
/**********************************************************************************************************************/
#include <FS.h>
#include <StreamUtils.h>
LoggingStream loggingStream(Serial, Serial);

/**********************************************************************************************************************/
// IMAGES & LOGO
/**********************************************************************************************************************/
#include "logo.h"
#include <Fonts/FreeSerifBoldItalic18pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include "YolksEmoticons25pt7b.h"

/**********************************************************************************************************************/
// WIFI
/**********************************************************************************************************************/
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <coredecls.h>
#include <PolledTimeout.h>
#include <include/WiFiState.h>

#ifndef RTC_USER_DATA_SLOT_WIFI_STATE
#define RTC_USER_DATA_SLOT_WIFI_STATE 33u
#endif

/* WIFI INFO */
WiFiState state;
WiFiClient wifiClient;
const char *AP_SSID = "LDO-cybershield";
const char *AP_PASS = "MouseBuffaloRabbit";
String OLD_ADDRESS;

uint32_t timeout = 30E3;
esp8266::polledTimeout::oneShotMs wifiTimeout(timeout);

/**********************************************************************************************************************/
// UTILS
/**********************************************************************************************************************/
#include <ArduinoJson.h>

#define DEBUG 0
#if DEBUG == 1
#define debug(x) Serial.print(F(x))
#define debugln(x) Serial.println(x)
#else
#define debug(x)
#define debugln(x)
#endif

/**********************************************************************************************************************/
// MENU & BUTTONS
/**********************************************************************************************************************/
const int MAX_ROW = 8;
const int MAX_COL[MAX_ROW] = {0, 1, 5, 2, 4, 2, 0, 0};
const char *MENU_ITEMS[MAX_ROW] = {"logo", "myself", "score", "stats", "news", "info", "snake", "game"};
const int N_PLAYER_PER_PAGE = 6;

const char *GRAPHQL_MAIN_URL = "http://cybershield.leonardocompany.com:7007/graphql";
const char *GRAPHQL_GAME_URL = "http://garanews.servegame.com:7007/graphql";
const char *FIRMWARE_REL = "1.0";

bool CONNECTED;
bool INGAME;
bool INTEXTGAME;
String positions[4] = {"<", "^", ">", "v"};
String TEXT_GAME_IDS[4] = {"", "", "", ""};
String LAST_CHOICE = "";

String TEAMNAME;

int menuRow = 0;
int menuCol = 0;
byte buttonState = 0;
byte lastButtonState = 0; // the previous reading from the input pin

/**********************************************************************************************************************/
// SNAKE
/**********************************************************************************************************************/
const int kGameWidth = 32, kGameHeight = 16, kMaxLength = 464, kStartLength = 4;
const int xOffset = 30, yOffset = 30;

struct Position
{
  int x, y;
};

void draw_square(Position pos, int color = GxEPD_WHITE)
{
  display.fillRect((pos.x * 4) + xOffset, (pos.y * 4) + yOffset, 4, 4, color);
}

const Position kDirPos[4] = {
    {0, -1}, {1, 0}, {0, 1}, {-1, 0}};

Position item;

struct Player
{
  Player() { reset(); }
  Position snake[kMaxLength];
  Position head;
  unsigned char direction;
  int size;
  void reset()
  {
    snake[0] = {16, 8};
    snake[1] = {15, 8};
    snake[2] = {14, 8};
    snake[3] = {13, 8};
    head = snake[0];
    direction = 1;
    size = kStartLength;
  }
  void go_up()
  {
    if (direction != 2)
      direction = 0;
  }
  void turn_left()
  {
    if (direction != 1)
      direction = 3;
  }
  void turn_right()
  {
    if (direction != 3)
      direction = 1;
  }
  void go_down()
  {
    if (direction != 0)
      direction = 2;
  }
  void update()
  {
    Position new_head = {head.x + kDirPos[direction].x, head.y + kDirPos[direction].y};

    // Move the snake's body
    for (int i = size - 1; i > 0; i--)
    {
      snake[i] = snake[i - 1];
    }
    snake[0] = new_head;
    head = snake[0];
  }
  void render(Position item) const
  {
    display.fillScreen(GxEPD_WHITE);
    draw_rectangle();
    for (int i = 0; i < size; ++i)
    {
      draw_square(snake[i], GxEPD_BLACK);
    }
    draw_square(item, GxEPD_BLACK);
  }
  bool snake_hit()
  {
    // Check if the snake hits the game border
    if (head.x <= 0 || head.x >= kGameWidth || head.y <= 0 || head.y >= kGameHeight)
    {
      return true;
    }

    // Check if the snake hits its own tail
    for (int i = 1; i < size; i++)
    {
      if (snake[i].x == head.x && snake[i].y == head.y)
      {
        return true;
      }
    }
    return false;
  }
} player;

void play_gameover()
{
  do
  {
    int score = player.size - kStartLength;
    display.setCursor(186, 44);
    display.setTextColor(GxEPD_DARKGREY);
    display.print(F("Score: "));
    display.setTextColor(GxEPD_BLACK);
    display.print(score);

    int hiscore;
    File file = SPIFFS.open("/hiscore.txt", "r");
    if (file)
    {
      hiscore = file.parseInt();
      file.close();
    }
    else
    {
      hiscore = 0;
    }
    if (score > hiscore)
    {
      file = SPIFFS.open("/hiscore.txt", "w");
      if (file)
      {
        file.print(score);
        file.close();
        hiscore = score;
        display.setCursor(164, 54);
        display.print(F("NEW"));
      }
    }
    display.setCursor(186, 54);
    display.setTextColor(GxEPD_DARKGREY);
    display.print(F("Hi-Score: "));
    display.setTextColor(GxEPD_BLACK);
    display.print(hiscore);
    INGAME = false;
  } while (display.nextPage());
}

void draw_rectangle()
{
  for (char x = 0; x < kGameWidth; ++x)
  {
    draw_square({x, 0}, GxEPD_BLACK);
    draw_square({x, kGameHeight - 1}, GxEPD_BLACK);
  }
  for (char y = 0; y < kGameHeight; ++y)
  {
    draw_square({0, y}, GxEPD_BLACK);
    draw_square({kGameWidth - 1, y}, GxEPD_BLACK);
  }
}

void item_spawn()
{
  bool ok;
  do
  {
    ok = true;
    item.x = random(kGameWidth - 1) + 1;
    item.y = random(kGameHeight - 1) + 1;
    for (int i = 1; i < player.size; i++)
    {
      if (player.snake[i].x == item.x && player.snake[i].y == item.y)
      {
        ok = false;
        break;
      }
    }
  } while (!ok);
}

void reset_game()
{
  draw_rectangle();
  player.reset();
  item_spawn();
}

void update_game()
{
  player.update();
  bool result = player.snake_hit();
  if (result)
  {
    play_gameover();
  }
  else if (player.head.x == item.x && player.head.y == item.y)
  {
    player.size++;
    item_spawn();
  }
}

/**********************************************************************************************************************/
// WIFI CONNECTION
/**********************************************************************************************************************/

void connect()
{
  // connects to default wifi network, wait till ip, set autoreconnect
  ESP.rtcUserMemoryRead(RTC_USER_DATA_SLOT_WIFI_STATE, reinterpret_cast<uint32_t *>(&state), sizeof(state));
  if (!WiFi.resumeFromShutdown(state) || (WiFi.waitForConnectResult(10000) != WL_CONNECTED))
  {
    debugln(F("Cannot resume WiFi connection"));
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA) || !WiFi.begin(AP_SSID, AP_PASS) || (WiFi.waitForConnectResult(10000) != WL_CONNECTED))
    {
      WiFi.mode(WIFI_OFF);
      debugln(F("Cannot connect!"));
      Serial.flush();
      ESP.deepSleep(10e6, RF_DISABLED);
      CONNECTED = false;
      return;
    }
  }
  wifiTimeout.reset(timeout);
  while (((!WiFi.localIP()) || (WiFi.status() != WL_CONNECTED)) && (!wifiTimeout))
  {
    yield();
  }
  if ((WiFi.status() == WL_CONNECTED) && WiFi.localIP())
  {
    CONNECTED = true;
    debugln("Connected to: " + String(AP_SSID));
    OLD_ADDRESS = WiFi.localIP().toString();
  }
  WiFi.setAutoReconnect(true);
}

bool reconnect()
{
  // reconnect from saved wifi session
  if (!CONNECTED)
  {
    return false;
  }
  WiFi.forceSleepWake();                  // reconnect with previous STA mode and connection settings
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP, 3); // Automatic Light Sleep, DTIM listen interval = 3
  wifiTimeout.reset(timeout);
  while (((!WiFi.localIP()) || (WiFi.status() != WL_CONNECTED)) && (!wifiTimeout))
  {
    yield();
  }
  if ((WiFi.status() == WL_CONNECTED) && WiFi.localIP())
  {
    debugln(F("Reconnect ok"));
    return true;
  }
  debugln(F("Reconnect failed"));
  return false;
}

void clean()
{
  // Clean local data
  debugln(F("Deleting local json files"));
  SPIFFS.remove("/myself.json");
  SPIFFS.remove("/scoreboard.json");
  SPIFFS.remove("/stats.json");
  SPIFFS.remove("/news.json");
}

/**********************************************************************************************************************/
// INITIALIZE DISPLAY, PIN, JSON & WIFI
/**********************************************************************************************************************/
void setup()
{

  Serial.begin(9600);

  /* BOOL set */
  CONNECTED = false;
  INGAME = false;
  INTEXTGAME = false;

  /* FS init */
  SPIFFS.begin();

  /* LOADING picture */
  display.init(115200);
  display.setFullWindow();
  display.setRotation(3);
  display.fillScreen(GxEPD_WHITE);
  display.firstPage();
  do
  {
    display.setCursor(193, 115);
    display.setTextColor(GxEPD_BLACK);
    display.println("... initializing");
  } while (display.nextPage());

  /* WIFI UP & DOWN */
  connect();
  WiFi.shutdown(state);

  /* PIN enable */
  pinMode(1, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);

  /* SHOW LOGO */
  display.drawImage_4G(logo_image, 4, 0, 0, 128, 296);

  display.powerOff();
}

/**********************************************************************************************************************/
// MAIN BUTTON CHECK LOOP + GAME UPDATE
/**********************************************************************************************************************/
void loop()
{

  buttonState = (digitalRead(1) == 0 ? 0 : (1 << 0)) |  // down
                (digitalRead(3) == 0 ? 0 : (1 << 1)) |  // left
                (digitalRead(5) == 0 ? 0 : (1 << 2)) |  // center
                (digitalRead(12) == 0 ? 0 : (1 << 3)) | // right
                (digitalRead(10) == 0 ? 0 : (1 << 4));  // up
  if (buttonState != lastButtonState)
  {
    for (int i = 0; i < 5; i++)
    {
      if (bitRead(buttonState, i) == 0)
      {
        if (INGAME == true)
        {
          switch (i)
          {
          case 0:
            player.go_down();
            break;
          case 1:
            player.turn_left();
            break;
          case 2:
            INGAME = false;
            showIndex(false);
            break;
          case 3:
            player.turn_right();
            break;
          case 4:
            player.go_up();
            break;
          }
        }
        else if (INTEXTGAME == true)
        {
          switch (i)
          {
          case 0:
            if (TEXT_GAME_IDS[3] != "")
            {
              LAST_CHOICE = TEXT_GAME_IDS[3];
              showIndex(false);
            }
            break;
          case 1:
            if (TEXT_GAME_IDS[0] != "")
            {
              LAST_CHOICE = TEXT_GAME_IDS[0];
              showIndex(false);
            }
            break;
          case 2:
            // INTEXTGAME = false;
            LAST_CHOICE = "";
            TEXT_GAME_IDS[0] = "";
            TEXT_GAME_IDS[1] = "";
            TEXT_GAME_IDS[2] = "";
            TEXT_GAME_IDS[3] = "";
            showIndex(true);
            break;
          case 3:
            if (TEXT_GAME_IDS[2] != 0)
            {
              LAST_CHOICE = TEXT_GAME_IDS[2];
              showIndex(false);
            }
            break;
          case 4:
            if (TEXT_GAME_IDS[1] != 0)
            {
              LAST_CHOICE = TEXT_GAME_IDS[1];
              showIndex(false);
            }
            break;
          }
        }
        else
        {
          switch (i)
          {
          case 0:
            menuRow = (menuRow < MAX_ROW - 1) ? menuRow + 1 : 0;
            menuCol = 0;
            showIndex(false);
            break;
          case 1:
            menuCol = (menuCol > 0) ? menuCol - 1 : MAX_COL[menuRow];
            showIndex(false);
            break;
          case 2:
            showIndex(true);
            break;
          case 3:
            menuCol = (menuCol < MAX_COL[menuRow]) ? menuCol + 1 : 0;
            showIndex(false);
            break;
          case 4:
            menuRow = (menuRow > 0) ? menuRow - 1 : MAX_ROW - 1;
            menuCol = 0;
            showIndex(false);
            break;
          }
        }
      }
    }
  }
  lastButtonState = buttonState;

  // update game loop
  if (INGAME)
  {
    update_game();
    // if gameover in update_game game ended
    if (INGAME)
    {
      do
      {
        player.render(item);
      } while (display.nextPage());
      delay(250);
    }
  }
}

/**********************************************************************************************************************/
// BASED ON MENU POSITION DO THINGS
/**********************************************************************************************************************/
void showIndex(bool refresh)
{
  // 0 show logo
  if (menuRow == 0)
  {
    display.drawImage_4G(logo_image, 4, 0, 0, 128, 296);
  }
  else if (menuRow == 7)
  {
    game(refresh);
  }
  // show graphics and fetch content
  else
  {
    display.setFullWindow();
    display.setRotation(3);
    display.firstPage();
    do
    {
      display.fillScreen(GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);

      /* cornicetta carina a 3 righe */
      for (int i = 0; i < 4; i += 2)
      {
        display.drawRect(i, i + 9, 296 - (i * 2), 128 - (i * 2) - 9, GxEPD_BLACK);
      }

      /* rettangolo per il titolo */
      display.drawRect(8, 0, 150, 17, GxEPD_BLACK);
      display.fillRect(9, 1, 148, 15, GxEPD_WHITE);

      /* main switch */
      switch (menuRow)
      {
      case 1:
        myself(refresh);
        break;
      case 2:
        scoreboard(refresh);
        break;
      case 3:
        stats(refresh);
        break;
      case 4:
        news(refresh);
        break;
      case 5:
        showInfo(refresh);
        break;
      case 6:
        snake(refresh);
      }

      /* titolo con pagina nel sottomenu */
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(12, 6);
      display.print(MENU_ITEMS[menuRow]);
      display.print(" ");
      display.print(menuCol + 1);
      display.setTextColor(GxEPD_DARKGREY);
      display.print("/");
      display.print(MAX_COL[menuRow] + 1);

      /* posizione del menu a cerchietti */
      for (int i = 0; i < MAX_ROW; i++)
      {
        display.drawCircle(287, 22 + 11 * i, 3, GxEPD_DARKGREY);
      }
      display.fillCircle(287, 22 + 11 * menuRow, 2, GxEPD_BLACK);
    } while (display.nextPage());
  }
}

/**********************************************************************************************************************/
// GRAPHQL DOWNLOAD OR READ OLD
/**********************************************************************************************************************/
DynamicJsonDocument get_query(int type, bool refresh)
{
  /*
   type:
    0 myself
    2 scoreboard
    3 stats
    4 news
    7 game
  */

  String query = "";
  String path = "";

  debugln("Getting " + String(MENU_ITEMS[menuRow]));

  if (type == 0)
  {
    path = "/myself.json";
    query = String("{\"query\": \"{me(where:{note:\\\"") + String(ESP.getChipId()) + String("\\\"}){Avatar,Name}, myteam(where:{note:\\\"") + String(ESP.getChipId()) + String("\\\"}){Name,Motto,user{edges{node{Name}}}}}\"}");
  }
  else if (type == 2)
  {
    path = "/scoreboard.json";
    query = "{\"query\": \"{scoreboard{timestamp,records{Name,Score,Flags}}}\"}";
  }
  else if (type == 3)
  {
    path = "/stats.json";
    query = "{\"query\": \"{stats{timestamp,flagErrors{Name,Total},flagHints{Name,Total},teamErrors{Name,Total},teamHints{Name,Total}}}\"}";
  }
  else if (type == 4)
  {
    path = "/news.json";
    query = "{\"query\": \"{news(limit: 5, orderBy: \\\"created\\\"){records{title,message,created,iconUrl}}}\"}";
  }
  else if (type == 7)
  {
    if (LAST_CHOICE == "")
    {
      query = "{\"query\": \"{scenario{scenario{Description,Name,option{edges{node{Description,uuid}}}},timestamp}}\"}";
    }
    else
    {
      query = String("{\"query\": \"{scenario(optionUuid: \\\"") + String(LAST_CHOICE) + String("\\\") {scenario{Description,Name,option{edges{node{Description,uuid}}}},timestamp}}\"}");
    }
  }

  DynamicJsonDocument doc(7168);
  // Old | Offline | Not implemented -- must exists local file
  if (!refresh or !CONNECTED or query == "" and SPIFFS.exists(path))
  {
    File file = SPIFFS.open(path, "r");
    deserializeJson(doc, file);
    file.close();
    debugln(F("    READING FROM LOCAL STORAGE"));
    return doc;
  }

  if (reconnect())
  {
    HTTPClient http;
    if (type == 7)
    {
      http.begin(wifiClient, GRAPHQL_GAME_URL);
    }
    else
    {
      http.begin(wifiClient, GRAPHQL_MAIN_URL);
    }
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(query);
    if (httpCode == 200)
    {
      Stream &response = http.getStream();
      DeserializationError err = deserializeJson(doc, response);
      JsonObject obj = doc.as<JsonObject>();
      if (path != "")
      {
        File file = SPIFFS.open(path, "w");
        serializeJson(doc, file);
        file.close();
      }
      debugln(F("[UPDATE - SAVED] done"));
    }
    http.end();
    WiFi.shutdown(state);
  }
  return doc;
}

/**********************************************************************************************************************/
// STRINGS UTILS
/**********************************************************************************************************************/
uint16_t center_print(String text)
{
  int16_t tbx, tby;
  uint16_t tbw, tbh; // boundary box window
  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
  uint16_t x = ((display.width() - tbw) / 2) - tbx;
  return x;
}

String pad_str(String team_name, int size, bool right = true)
{
  if (team_name.length() < size)
  {
    String pad;
    for (int i = 0; i < size - team_name.length(); i++)
    {
      pad += ' ';
    }
    if (right)
    {
      return team_name + pad;
    }
    else
    {
      return pad + team_name;
    }
  }
  return team_name;
}

/**********************************************************************************************************************/
// PLOT DOWNLOADED PICT
/**********************************************************************************************************************/
void plotPict(String avatar_list)
{
  int y = 35; // PIXEL POSITION
  int x = 12;

  int w = 0; // CHUNK ID X ROW 0-14

  int bits = 28;
  int img_w = 140;
  int img_h = 70;
  int chunks_row = img_w / bits * 2;

  long number;
  String str;
  display.fillRect(x - 2, y - 2, img_w + 4, img_h + 4, GxEPD_BLACK);
  display.fillRect(x - 1, y - 1, img_w + 2, img_h + 2, GxEPD_WHITE);

  for (int i = 0; i < avatar_list.length(); i += 7)
  {
    str = avatar_list.substring(i, i + 7);
    number = strtoul(str.c_str(), NULL, 16);
    for (int z = 0; z < bits; z += 2)
    {
      if (bitRead(number, bits - 1 - z) == 1 && bitRead(number, bits - z - 2) == 1)
      {
        display.drawPixel(x + w * (bits / 2) + z / 2, y, GxEPD_BLACK);
      }
      if (bitRead(number, bits - 1 - z) == 0 && bitRead(number, bits - z - 2) == 1)
      {
        display.drawPixel(x + w * (bits / 2) + z / 2, y, GxEPD_DARKGREY);
      }
      if (bitRead(number, bits - 1 - z) == 1 && bitRead(number, bits - z - 2) == 0)
      {
        display.drawPixel(x + w * (bits / 2) + z / 2, y, GxEPD_LIGHTGREY);
      }
      if (bitRead(number, bits - 1 - z) == 0 && bitRead(number, bits - z - 2) == 0)
      {
        display.drawPixel(x + w * (bits / 2) + z / 2, y, GxEPD_WHITE);
      }
    }
    w = w + 1;
    if (w % chunks_row == 0)
    {
      y += 1;
      w = 0;
    }
  }
}

/**********************************************************************************************************************/
// PRINT MY INFO [0]
/**********************************************************************************************************************/
void myself(bool refresh)
{
  DynamicJsonDocument doc = get_query(0, refresh);
  TEAMNAME = doc["data"]["myteam"]["Name"].as<String>();
  if (menuCol == 0)
  {
    plotPict(doc["data"]["me"]["Avatar"].as<String>());

    display.setCursor(165, 35);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("Hello! I am:");

    display.setCursor(165, 55);
    display.setTextColor(GxEPD_BLACK);
    display.print(doc["data"]["me"]["Name"].as<String>());
    display.setFont();

    display.setCursor(165, 75);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("Team:");

    display.setCursor(165, 95);
    display.setTextColor(GxEPD_BLACK);
    display.println(TEAMNAME);
    display.setFont();
  }
  else
  {
    display.setCursor(35, 35);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSerifBold12pt7b);
    display.print(TEAMNAME);
    display.setFont();

    display.setCursor(165, 55);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("TEAM MEMBERS: ");

    JsonArray payload_team = doc["data"]["myteam"]["user"]["edges"];
    display.setTextColor(GxEPD_BLACK);
    int i = 0;
    for (JsonObject node : payload_team)
    {
      display.setCursor(170, 65 + 10 * i);
      display.println(node["node"]["Name"].as<String>());
      i += 1;
    }
    String text = doc["data"]["myteam"]["Motto"];
    display.setCursor(center_print(text), 110);
    display.println(text);
  }
}

/**********************************************************************************************************************/
// PRINT SCOREBOARD [2]
/**********************************************************************************************************************/
void scoreboard(bool refresh)
{
  DynamicJsonDocument doc = get_query(2, refresh);

  display.setCursor(200, 0);
  display.setTextColor(GxEPD_DARKGREY);
  display.print(doc["data"]["scoreboard"]["timestamp"].as<String>());
  JsonArray payload = doc["data"]["scoreboard"]["records"];
  display.setTextColor(GxEPD_BLACK);

  display.setCursor(20, 25);
  display.print(pad_str("TEAM NAME", 22, true));
  display.print(pad_str("FLAGS", 8, false));
  display.print(pad_str("SCORE", 8, false));

  int pos = 0;
  int idx = 0;
  for (JsonObject node : payload)
  {
    if (idx >= menuCol * N_PLAYER_PER_PAGE && (menuCol + 1) * N_PLAYER_PER_PAGE > idx)
    {
      display.setCursor(20, 35 + pos);
      if (node["Name"].as<String>() == TEAMNAME)
      {
        display.setTextColor(GxEPD_BLACK);
        display.print("> ");
      }
      else if (idx == 0)
      {
        display.setTextColor(GxEPD_BLACK);
        display.print("W ");
      }
      else
      {
        display.setTextColor(GxEPD_DARKGREY);
        display.print(idx + 1);
        display.print(" ");
      }
      display.setTextColor(GxEPD_BLACK);
      display.print(pad_str(node["Name"].as<String>(), 20, true));
      display.print(pad_str(String(node["Flags"].as<int>()), 8, false));
      display.println(pad_str(String(node["Score"].as<int>()), 8, false));
      pos += 15;
    }
    idx += 1;
  }
}

/**********************************************************************************************************************/
// PRINT STATS [3]
/**********************************************************************************************************************/
void stats(bool refresh)
{
  DynamicJsonDocument doc = get_query(3, refresh);
  JsonArray payload;
  display.setCursor(200, 0);
  display.setTextColor(GxEPD_DARKGREY);
  display.print(doc["data"]["stats"]["timestamp"].as<String>());

  display.setTextColor(GxEPD_BLACK);
  int pos = 0;
  String text;

  if (menuCol == 0)
  {
    text = "-- More errors done by team --";
    payload = doc["data"]["stats"]["teamErrors"];
  }
  else if (menuCol == 1)
  {
    text = "-- More hints requested by team --";
    payload = doc["data"]["stats"]["teamHints"];
  }
  else if (menuCol == 2)
  {
    text = "-- More errors done by flag --";
    payload = doc["data"]["stats"]["flagErrors"];
  }
  display.setCursor(center_print(text), 30);
  display.print(text);
  for (JsonObject node : payload)
  {
    display.setCursor(20, 50 + pos);
    display.print(pad_str(node["Name"].as<String>(), 20, true));
    display.setTextColor(GxEPD_DARKGREY);
    display.print(" --> ");
    display.setTextColor(GxEPD_BLACK);
    display.println(pad_str(String(node["Total"].as<int>()), 4, false));
    pos += 10;
  }
}

/**********************************************************************************************************************/
// PRINT NEWS [4]
/**********************************************************************************************************************/
void news(bool refresh)
{
  DynamicJsonDocument doc = get_query(4, refresh);
  JsonArray payload = doc["data"]["news"]["records"];
  display.setTextColor(GxEPD_BLACK);

  if (payload.isNull())
  {
    display.setCursor(100, 50);
    display.setFont(&YolksEmoticons25pt7b);
    display.write(0x58);
    display.setFont();
    display.setCursor(20, 90);
    display.println("Click center button to refresh!");
  }
  else if (menuCol >= payload.size())
  {
    display.setCursor(20, 35);
    display.print("---");
    display.setTextColor(GxEPD_DARKGREY);
    display.setCursor(20, 50);
    display.println("---");
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 65);
    display.print("---");
  }
  else
  {
    JsonObject node = payload[menuCol];
    String icon = node["iconUrl"].as<String>();

    display.setCursor(230, 45);
    display.setFont(&YolksEmoticons25pt7b);
    if (icon == "SUCCESS")
    {
      display.write(0x42); // hand-ok
    }
    else if (icon == "INFO")
    {
      display.write(0x51); // information-circle-1
    }
    else if (icon == "WARNING")
    {
      display.write(0x3B); // alert-triangle
    }
    else if (icon == "ERROR")
    {
      display.write(0x4C); // skull
    }
    display.setFont();
    display.setCursor(20, 35);
    display.print(node["title"].as<String>());
    display.setTextColor(GxEPD_DARKGREY);
    display.setCursor(20, 50);
    display.println(node["created"].as<String>());
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 65);
    display.print(node["message"].as<String>());
  }
}

/**********************************************************************************************************************/
// PRINT BOARD INFO + REFRESH WIFI/MY INFO [5]
/**********************************************************************************************************************/
void showInfo(bool refresh)
{
  if (menuCol == 0)
  {
    if (refresh)
    {
      connect();
      WiFi.shutdown(state);
    }
    display.setCursor(20, 40);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("UID: ");
    display.setTextColor(GxEPD_BLACK);
    display.println(ESP.getChipId());

    display.setCursor(20, 50);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("WIFI SETTINGS: ");
    display.setTextColor(GxEPD_BLACK);
    display.print(AP_SSID);

    display.setCursor(20, 60);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("WIFI STATUS: ");
    display.setTextColor(GxEPD_BLACK);
    if (!CONNECTED)
    {
      display.println("offline");
    }
    else
    {
      display.println("connected");
    }

    display.setCursor(20, 70);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("IP ADDRESS*: ");
    display.setTextColor(GxEPD_BLACK);
    display.println(OLD_ADDRESS);

    String text = "Click center button to refresh!";
    display.setCursor(center_print(text), 90);
    display.println(text);
  }
  else if (menuCol == 1)
  {
    DynamicJsonDocument doc = get_query(0, refresh);

    display.setCursor(20, 40);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("UID: ");
    display.setTextColor(GxEPD_BLACK);
    display.println(ESP.getChipId());

    TEAMNAME = doc["data"]["myteam"]["Name"].as<String>();

    display.setCursor(20, 50);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("TEAM NAME: ");
    display.setTextColor(GxEPD_BLACK);
    display.print(TEAMNAME);

    display.setCursor(20, 60);
    display.setTextColor(GxEPD_DARKGREY);
    display.print("USER NAME: ");
    display.setTextColor(GxEPD_BLACK);
    display.print(doc["data"]["me"]["Name"].as<String>());

    String text = "Click center button to refresh!";
    display.setCursor(center_print(text), 90);
    display.println(text);
  }
  else if (menuCol == 2)
  {
    char text1[] = "MADE BY:";
    display.setCursor(center_print(text1), 25);
    display.setTextColor(GxEPD_DARKGREY);
    display.print(text1);

    display.setTextColor(GxEPD_DARKGREY);
    char text2[] = "with partecipation of:";
    display.setCursor(center_print(text2), 75);
    display.print(text2);

    display.setTextColor(GxEPD_BLACK);
    char text3[] = "Andrea Garavaglia - Davide Arcuri";
    display.setCursor(center_print(text3), 85);
    display.print(text3);

    char text4[] = "Nino Verde - Antonio Rossi";
    display.setCursor(center_print(text4), 95);
    display.print(text4);

    char text5[] = "Andrea Minigozzi - Luca Memini";
    display.setCursor(center_print(text5), 105);
    display.print(text5);

    char text6[] = "Damiano Olgiati";
    display.setCursor(center_print(text6), 115);
    display.print(text6);

    display.setFont(&FreeSerifBoldItalic18pt7b);
    display.setTextColor(GxEPD_BLACK);
    char text7[] = "Cyber&Security";
    display.setCursor(center_print(text7), 65);
    display.print(text7);
    display.setFont();
  }
}

/**********************************************************************************************************************/
// SNAKE GAME [6]
/**********************************************************************************************************************/
void snake(bool refresh)
{
  if (refresh)
  {
    INGAME = !INGAME;
    if (INGAME)
    {
      reset_game();
    }
  }
  else
  {
    char text1[] = "Press center button to ";
    display.setCursor(center_print(text1), 30);
    display.setTextColor(GxEPD_DARKGREY);
    display.print(text1);

    char text2[] = "START GAME";
    display.setCursor(center_print(text2), 80);
    display.setTextColor(GxEPD_BLACK);
    display.print(text2);
  }
}

/**********************************************************************************************************************/
// PRINT GAME [7]
/**********************************************************************************************************************/
void game(bool refresh)
{
  if (refresh)
  {
    INTEXTGAME = !INTEXTGAME;
  }

  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    if (!INTEXTGAME)
    {
      String text;
      display.setFont();
      text = "Use corresponding key to pick choice";
      display.setCursor(center_print(text), 50);
      display.println(text);
      text = "Use central button to exit";
      display.setCursor(center_print(text), 60);
      display.println(text);
      text = "Click center button to start the game!";
      display.setCursor(center_print(text), 90);
      display.println(text);
    }
    else
    {
      DynamicJsonDocument doc = get_query(7, true);
      JsonObject scenario = doc["data"]["scenario"].as<JsonObject>();
      JsonArray payload = scenario["scenario"]["option"]["edges"];

      display.setFont();

      if (!scenario["scenario"]["Name"].isNull())
      {

        display.setCursor(200, 0);
        display.setTextColor(GxEPD_DARKGREY);
        display.print(scenario["timestamp"].as<String>());
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(0, 10);
        display.print(scenario["scenario"]["Name"].as<String>());
        display.setCursor(0, 20);
        display.print(scenario["scenario"]["Description"].as<String>());

        int idx = 0;

        for (JsonObject node : payload)
        {
          display.setCursor(20, 80 + 10 * idx);
          display.print(positions[idx]);
          display.print(" ");
          display.print(node["node"]["Description"].as<String>());
          TEXT_GAME_IDS[idx] = node["node"]["uuid"].as<String>();
          idx += 1;
        }
      }
      else
      {
        String text;
        display.setFont();
        text = "No Game Available";
        display.setCursor(center_print(text), 60);
        display.println(text);
        text = "Click center button to exit!";
        display.setCursor(center_print(text), 90);
        display.println(text);
      }
    }
  } while (display.nextPage());
}
