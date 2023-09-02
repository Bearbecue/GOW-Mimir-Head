//----------------------------------------------------------------------------
// TOOLS>BOARD SHOULD BE SET TO EITHER "ESP32 Dev Module" OR "ESP32-WROOM-DA Module"

#define FASTLED_ALL_PINS_HARDWARE_SPI
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <uptime_formatter.h>

#include "HWPinConfig.h"
#include "Helpers.h"

//----------------------------------------------------------------------------
// LED count
// 30 LEDs total across the whole head

#define NUM_LEDS_NECK     3*4
#define NUM_LEDS_MANDIBLE 2*5
#define NUM_LEDS_TEMPLES  2*2
#define NUM_LEDS_EYES     2*1
#define NUM_LEDS_NOSE     1

#define NUM_LEDS          NUM_LEDS_NECK + NUM_LEDS_MANDIBLE + NUM_LEDS_TEMPLES + NUM_LEDS_EYES + NUM_LEDS_NOSE

#define LED_ID_NECK       0
#define LED_ID_MANDIBLE   LED_ID_NECK + NUM_LEDS_NECK
#define LED_ID_TEMPLES    LED_ID_MANDIBLE + NUM_LEDS_MANDIBLE
#define LED_ID_EYES       LED_ID_TEMPLES + NUM_LEDS_TEMPLES
#define LED_ID_NOSE       LED_ID_EYES + NUM_LEDS_EYES

//----------------------------------------------------------------------------

CRGB        leds[NUM_LEDS];

WebServer   server(80);
LoopTimer   loopTimer;
uint32_t    chipId = 0;
uint32_t    MsSinceConnected = 0;
const char  *wifi_AP_SSID = "Mimir";
const char  *wifi_AP_Password = "0123456789";
IPAddress   wifi_AP_local_ip(192,168,1,1);
IPAddress   wifi_AP_gateway(192,168,1,1);
IPAddress   wifi_AP_subnet(255,255,255,0);

// Read these from the flash
bool        has_wifi_credentials = false;
bool        has_dirty_credentials = false;
String      wifi_STA_SSID;
String      wifi_STA_Pass;
const int   kEEPROM_ssid_size = 20;  // 20 chars max
const int   kEEPROM_pass_size = 60;  // 60 chars max
const int   kEEPROM_ssid_addr = 1;
const int   kEEPROM_pass_addr = kEEPROM_ssid_addr + kEEPROM_ssid_size;
const int   kEEPROM_total_size = 1 + kEEPROM_ssid_size + kEEPROM_pass_size;  // first byte is a key == 0

//----------------------------------------------------------------------------

void setup()
{
  pinMode(PIN_OUT_WS2812, OUTPUT);

  for (int i = 0; i < NUM_LEDS; i++)
    leds[i].setRGB(0, 0, 0);

  // Setup LED strip
  FastLED.addLeds<WS2812, PIN_OUT_WS2812, GRB>(leds, NUM_LEDS);  // The WS2812 strip I got from aliexpress has red and green swapped, so GRB instead of RGB
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);  // 5V, 2A
  FastLED.setBrightness(255);
  FastLED.clear();
  FastLED.show();

  Serial.begin(115200);
  Serial.println();

  delay(15000);

  // Read the server's IP & port we recorded from the previous runs to instantly connect to
  // what will likely be the correct one if we reboot.
  ESP32Flash_Init();
  ESP32Flash_ReadServerInfo();

  // Setup wifi in both access-point & station mode:
  SetupServer();

  Serial.print("Connecting to wifi...\n");

  ConnectToWiFi();
  WaitForWiFi(2000);  // Wait at most 2 seconds for wifi

  for(int i=0; i<17; i=i+8)
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;

  Serial.printf("ESP32 Chip model = %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("This chip has %d cores\n", ESP.getChipCores());
  Serial.print("Chip ID: "); Serial.println(chipId);

  // Dump MAC address to serial. Useful for dev during initial network setup.
  // Just send the mac address to @vksiezak to pin this module to a specific IP address
  {
    uint8_t macAddr[6];
    WiFi.macAddress(macAddr);
    Serial.printf("Mac address: %02x:%02x:%02x:%02x:%02x:%02x\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
  }
}

//----------------------------------------------------------------------------

void loop()
{
  loopTimer.tick();

  // Update wifi & web server
  WifiServerLoop();

  const float   dt = loopTimer.dt();
  const float   et = loopTimer.elapsedTime();

  static float  t = 0;
  static int    x = 0;
  t += dt;
  if (t > 2.0f)
  {
    t -= 2.0f;
    x++;
  }

  const float b = sinf(et) * 0.5f + 0.5f;
  int c0 = clamp((int)(b * 255) + 1, 1, 255);
  int c1 = clamp((int)(b * 50), 0, 255);

  for (int i = 0; i < NUM_LEDS; i++)
  {
    int key = (i + x) % NUM_LEDS;
    leds[i].setRGB((key % 3) == 0 ? c0 : c1, ((key + 1) % 3) == 0 ? c0 : c1, ((key + 2) % 3) == 0 ? c0 : c1);
  }
  FastLED.show();

  delay(10);
}

//----------------------------------------------------------------------------
//
//  WiFi helpers
//
//----------------------------------------------------------------------------

void  ConnectToWiFi()
{
  if (has_wifi_credentials)
  {
    Serial.print("Connecting to ");
    Serial.println(wifi_STA_SSID);

    WiFi.begin(wifi_STA_SSID, wifi_STA_Pass);
  }
}

//----------------------------------------------------------------------------

void  WaitForWiFi(int maxMs)
{
  const int kMsPerRetry = 500;
  int retries = max(maxMs / kMsPerRetry, 1);
  Serial.print("Waiting for Wifi connection");
  while (WiFi.status() != WL_CONNECTED && retries-- > 0)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("");
    Serial.print("WiFi connected to ");
    Serial.println(wifi_STA_SSID);
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

//----------------------------------------------------------------------------

void  EnsureWiFiConnected()
{
  static long int prevMS = millis() - 10;
  const long int  curMS = millis();
  const int       dtMS = (curMS > prevMS) ? curMS - prevMS : 10;  // by default 10 ms when wrapping around
  prevMS = curMS;

  // The wifi network might have gone down, then up again.
  // We don't want to require rebooting the IOT there, so if the connection was lost, redo a connection round
  if (WiFi.status() != WL_CONNECTED)
  {
    MsSinceConnected += dtMS;
    if (MsSinceConnected > 10000) // retry every 10s
    {
      MsSinceConnected = 0;
      ConnectToWiFi();
    }
  }
  else if (MsSinceConnected != 0) // was previously not connected
  {
    MsSinceConnected = 0;
    Serial.println("");
    Serial.print("WiFi connected to ");
    Serial.println(wifi_STA_SSID);
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

//----------------------------------------------------------------------------

void  DisconnectFromWiFi()
{
  WiFi.disconnect();
}

//----------------------------------------------------------------------------

void  WifiServerLoop()
{
  server.handleClient();

  if (has_dirty_credentials)
  {
    Serial.println("Has dirty credentials");
    DisconnectFromWiFi();
    ConnectToWiFi();
    MsSinceConnected = 0;
    has_dirty_credentials = false;
  }

  if (has_wifi_credentials)
    EnsureWiFiConnected();
}

//----------------------------------------------------------------------------
//
//  Web server
//
//----------------------------------------------------------------------------

#define FONT_OTAG_CODE  "<font class=code>"

static String  _BuildStandardResponsePage(const String &contents)
{
  static uint32_t reqId = 0;
  String reply;
  reply += "<!DOCTYPE HTML>\r\n"
           "<html><head><style>\r\n"
           "body { font-size: 9pt; font-family: Roboto, Tahoma, Verdana; }\r\n"
           "table { border-collapse: collapse; font-size: 9pt; }\r\n"
           "td { padding: 2px; padding-left: 10px; padding-right: 10px; border: 1px solid #252525; }\r\n"
           "td.code { font-family: Consolas, Courier; }\r\n"
           "td.header { color: #AAA; background-color: #505050; }\r\n"
           ".code { font-family: Consolas, Courier; background:#EEE; }\r\n"
           "</style></head><body>\r\n"
          "<h1>Mimir server</h1>\r\n"
           "<hr/>\r\n";
  reply += contents;
  reply += "<hr/>\r\n"
           "Uptime: ";
  reply += uptime_formatter::getUptime();
  reply += "<br/>\r\n"
           "Requests since startup: ";
  reply += ++reqId;
  reply += "<br/>\r\n"
           "MAC Address: <font class=\"code\">";
  reply += WiFi.macAddress() + "</font>";

  if (WiFi.status() == WL_CONNECTED)
  {
    reply += "<br/>\r\n"
             "IP Address: ";
    reply += WiFi.localIP().toString();
    reply += "<br/>\r\n"
             "Signal strength: ";
    reply += WiFi.RSSI();
    reply += " dB<br/>\r\n";
  }
  
  reply += "</body></html>\r\n";
  return reply;
}

//----------------------------------------------------------------------------

static void _HandleRoot()
{
//  String  localIp = WiFi.localIP().toString();
  String  reply;
  reply += "<b>Usage:</b><br/>\r\n";

  reply += "<hr/>\r\n"
           "<h3>Set wifi network credentials:</h3>\r\n"
           "<form action=\"/set_credentials\">\r\n"
           "  <label for=\"ssid\">Wifi SSID:</label><br>\r\n"
           "  <input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"" + wifi_STA_SSID + "\"><br>\r\n"
           "  <label for=\"passwd\">Wifi Password:</label><br>\r\n"
           "  <input type=\"text\" id=\"passwd\" name=\"passwd\" value=\"" + wifi_STA_Pass + "\"><br><br>\r\n"
           "  <input type=\"submit\" value=\"Submit\">\r\n"
           "</form>\r\n";

  server.send(200, "text/html", _BuildStandardResponsePage(reply));
}

//----------------------------------------------------------------------------

static void _HandleNotFound()
{
  String reply;
  reply += "Unhandled Request\n\n";
  reply += "URI: ";
  reply += server.uri();
  reply += "\nMethod: ";
  reply += (server.method() == HTTP_GET) ? "GET" : "POST";
  reply += "\nArguments: ";
  reply += server.args();
  reply += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
    reply += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  server.send(404, "text/plain", reply);
}

//----------------------------------------------------------------------------

static void _HandleSetCredentials()
{
  // Expected format:
  // /set_credentials?ssid=WifiName&passwd=WifiPassword

  String    ssid;
  String    pass;
  for (uint8_t i = 0; i < server.args(); i++)
  {
    if (server.argName(i) == "ssid")
       ssid = server.arg(i);
    else if (server.argName(i) == "passwd")
       pass = server.arg(i);
  }

  if (ssid != wifi_STA_SSID || pass != wifi_STA_Pass)
  {
    wifi_STA_SSID = ssid;
    wifi_STA_Pass = pass;
    ESP32Flash_WriteServerInfo();
    has_wifi_credentials = wifi_STA_SSID.length() > 0;
    // Reconnect to wifi on next loop:
    has_dirty_credentials = true;
    Serial.println("Got new Wifi network credentials");
  }

  // Redirect immediately to the root
  server.send(200, "text/html", "<html><head><meta http-equiv=\"refresh\" content=\"3; URL=/\" /></head><body></body></html>");
}

//----------------------------------------------------------------------------

void  SetupServer()
{
  server.stop();
  server.close(); // just in case we're reconnecting

  WiFi.mode(WIFI_AP_STA);
  Serial.print("Setting up access-point");
  WiFi.softAP(wifi_AP_SSID, wifi_AP_Password);
  WiFi.softAPConfig(wifi_AP_local_ip, wifi_AP_gateway, wifi_AP_subnet);

  // Setup server callbacks/handlers
  server.on("/", _HandleRoot);
  server.on("/set_credentials", _HandleSetCredentials);
  server.onNotFound(_HandleNotFound);

  // Start the server
  server.begin();
  Serial.println("Server started");
}

//----------------------------------------------------------------------------
//
//  EEPROM helper for ESP8266
//  Will not work on regular arduino, EEPROM interface is slightly different.
//  EEPROM emulates through flash memory on ESP8266
//
//----------------------------------------------------------------------------

void  ESP32Flash_Init()
{
  EEPROM.begin(kEEPROM_total_size); // we actually only need 19 bytes

  // factory-initialized to 0xFF. Our first byte should always be exactly 0.
  // If it's not, wipe the entire memory.
  if (EEPROM.read(0) != 0)
  {
    for (int i = 0, stop = EEPROM.length(); i < stop; i++)
      EEPROM.write(i, 0);
    EEPROM.commit();
  }
}

//----------------------------------------------------------------------------

void  ESP32Flash_WriteServerInfo()
{
  char      eeprom_ssid[kEEPROM_ssid_size] = {};
  char      eeprom_pass[kEEPROM_pass_size] = {};
  static_assert(sizeof(eeprom_ssid) == kEEPROM_ssid_size);
  static_assert(sizeof(eeprom_pass) == kEEPROM_pass_size);

  for (int i = 0; i < wifi_STA_SSID.length() && i < sizeof(eeprom_ssid)-1 - 1; i++)
    eeprom_ssid[i] = wifi_STA_SSID[i];
  eeprom_ssid[sizeof(eeprom_ssid)-1] = '\0';

  for (int i = 0; i < wifi_STA_Pass.length() && i < sizeof(eeprom_pass)-1 - 1; i++)
    eeprom_pass[i] = wifi_STA_Pass[i];
  eeprom_pass[sizeof(eeprom_pass)-1] = '\0';

  EEPROM.write(0, 0);
  EEPROM.put(kEEPROM_ssid_addr, eeprom_ssid);
  EEPROM.put(kEEPROM_pass_addr, eeprom_pass);
  EEPROM.commit();
}

//----------------------------------------------------------------------------

void  ESP32Flash_ReadServerInfo()
{
  Serial.printf("READING FROM EEPROM\n");
  char      eeprom_ssid[kEEPROM_ssid_size] = {};
  char      eeprom_pass[kEEPROM_pass_size] = {};
  static_assert(sizeof(eeprom_ssid) == kEEPROM_ssid_size);
  static_assert(sizeof(eeprom_pass) == kEEPROM_pass_size);

  EEPROM.get(kEEPROM_ssid_addr, eeprom_ssid);
  EEPROM.get(kEEPROM_pass_addr, eeprom_pass);
  eeprom_ssid[sizeof(eeprom_ssid)-1] = '\0';
  eeprom_pass[sizeof(eeprom_pass)-1] = '\0';

  wifi_STA_SSID = eeprom_ssid;
  wifi_STA_Pass = eeprom_pass;
  if (eeprom_ssid[0] != '\0') // don't check pass: allow empty password ?
    has_wifi_credentials = true;
}
