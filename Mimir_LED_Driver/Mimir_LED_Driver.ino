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
IPAddress   wifi_AP_local_ip(192,168,1,1);
IPAddress   wifi_AP_gateway(192,168,1,1);
IPAddress   wifi_AP_subnet(255,255,255,0);

// Read these from the flash
bool        has_wifi_credentials = false;
bool        has_dirty_credentials = false;
String      wifi_ST_SSID;
String      wifi_ST_Pass;
String		wifi_AP_SSID = "Mimir";
String		wifi_AP_Pass = "0123456789";
const int   kEEPROM_ST_ssid_size = 20;  // 20 chars max
const int   kEEPROM_ST_pass_size = 60;  // 60 chars max
const int   kEEPROM_AP_ssid_size = 20;  // 20 chars max
const int   kEEPROM_AP_pass_size = 60;  // 60 chars max
const int   kEEPROM_ST_ssid_addr = 1;
const int   kEEPROM_ST_pass_addr = kEEPROM_ST_ssid_addr + kEEPROM_ST_ssid_size;
const int   kEEPROM_AP_ssid_addr = kEEPROM_ST_pass_addr + kEEPROM_ST_pass_size;
const int   kEEPROM_AP_pass_addr = kEEPROM_AP_ssid_addr + kEEPROM_AP_ssid_size;
const int   kEEPROM_total_size = 1 + kEEPROM_ST_ssid_size + kEEPROM_ST_pass_size + kEEPROM_AP_ssid_size + kEEPROM_AP_pass_size;  // first byte is a key == 0

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
    Serial.println(wifi_ST_SSID);

    WiFi.begin(wifi_ST_SSID, wifi_ST_Pass);
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
    Serial.println(wifi_ST_SSID);
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
    Serial.println(wifi_ST_SSID);
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
           "td { padding: 2px; padding-left: 10px; padding-right: 10px; }\r\n"
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
  reply += "<b>Usage:</b><br/>\r\n"
           "TODO<br/>";

  reply += "<hr/>\r\n"
           "<h3>Set wifi network credentials:</h3>\r\n"
           "This will allow the device to connect to your local wifi, and access this page through your wifi network without the need to connect to the access-point.<br/><br/>\r\n"
           "<form action=\"/set_credentials\">\r\n"
           "  <table>\r\n"
           "  <tr>\r\n"
           "    <td><label for=\"ssid\">Access-point SSID:</label></td>\r\n"
           "    <td><input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"" + wifi_ST_SSID + "\"></td>\r\n"
           "  </tr><tr>\r\n"
           "    <td><label for=\"passwd\">Access-point Password:</label></td>\r\n"
           "    <td><input type=\"text\" id=\"passwd\" name=\"passwd\" value=\"" + wifi_ST_Pass + "\"></td>\r\n"
           "  </tr><tr>\r\n"
           "    <td colspan=2><input type=\"submit\" value=\"Submit\"></td>\r\n"
           "  </tr>\r\n"
           "  </table>\r\n"
           "</form>\r\n"
		   "<hr/>\r\n"
		   "<a href=\"/set_credentials_ap\">Change access-point settings</a><br/>\r\n";

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

  if (ssid != wifi_ST_SSID || pass != wifi_ST_Pass)
  {
    wifi_ST_SSID = ssid;
    wifi_ST_Pass = pass;
    ESP32Flash_WriteServerInfo();
    has_wifi_credentials = wifi_ST_SSID.length() > 0;
    // Reconnect to wifi on next loop:
    has_dirty_credentials = true;
    Serial.println("Got new Wifi network credentials");
  }

  // Auto-redirect immediately to the root
  server.send(200, "text/html", "<html><head><meta http-equiv=\"refresh\" content=\"3; URL=/\" /></head><body></body></html>");
}

//----------------------------------------------------------------------------

static void _HandleSetCredentialsAP()
{
  // Expected format:
  // /set_credentials_ap?ssid=WifiName&passwd=WifiPassword

  String    ssid;
  String    pass;
  for (uint8_t i = 0; i < server.args(); i++)
  {
    if (server.argName(i) == "ssid")
       ssid = server.arg(i);
    else if (server.argName(i) == "passwd")
       pass = server.arg(i);
  }
  
  String	reply;
  
  const bool	hasSSID = ssid.length() != 0;
  const bool	hasPass = pass.length() != 0;
  if (!hasSSID && !hasPass)
  {
      // No args to the page: Display regular page with the form to change it
      reply += "<h3>Set access-point credentials</h3>\r\n"
               "This allows to configure the access-point settings of the device:<br/>\r\n"
               "It will change how the device appears in the list of wireless networks, and the password needed to connect to it.<br/>\r\n"
               "<br/>\r\n"
               "These will only be taken into account after a reboot of the device.<br/>\r\n"
               "To reboot the device, simply power it off, then on again.<br/>\r\n"
               "<hr/>\r\n"
			   "<font color=red><b>!!!! DANGER ZONE !!!</b><br/>\r\n"
			   "If you do not remember the access-point password, and you did not setup a connection to your local wifi network, there will be no way to recover this.\r\n"
			   "The device will be \"bricked\" as you won't be able to connect to it from anywhere, and only re-flashing the firmware from the USB connection will be able to fix this.</font><br/>\r\n"
			   "<br/>\r\n"
			   "<form action=\"/set_credentials_ap\">\r\n"
               "  <table>\r\n"
               "  <tr>\r\n"
               "    <td><label for=\"ssid\">Access-point SSID:</label></td>\r\n"
               "    <td><input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"" + wifi_AP_SSID + "\"></td>\r\n"
               "  </tr><tr>\r\n"
               "    <td><label for=\"passwd\">Access-point Password:</label></td>\r\n"
               "    <td><input type=\"text\" id=\"passwd\" name=\"passwd\" value=\"" + wifi_AP_Pass + "\"></td>\r\n"
               "  </tr><tr>\r\n"
               "    <td colspan=2><input type=\"submit\" value=\"Submit\"></td>\r\n"
               "  </tr>\r\n"
               "  </table>\r\n"
			   "</form>\r\n";

	  reply = _BuildStandardResponsePage(reply);
  }
  else if (hasSSID != hasPass)
  {
	  // One of them is empty but not the other: Don't allow.
	  // We must have a non-empty password and a non-empty access-point name.
      reply += "<h3>Set access-point credentials</h3>\r\n"
               "Invalid credentials: You must specify both an SSID (the name the device as it will appear in the wifi networks list), as well as a password (the password you will need to enter to connect to the device).<br/>\r\n"
               "<br/>\r\n"
			   "</form>\r\n";

	  reply = _BuildStandardResponsePage(reply);
  }
  else
  {
	  // Both are non-empty: OK
	  if (ssid != wifi_ST_SSID || pass != wifi_ST_Pass)
	  {
		wifi_AP_SSID = ssid;
		wifi_AP_Pass = pass;
		ESP32Flash_WriteServerInfo();
		// New AP name will be taken into account on next device boot: That's OK
		
		// If we want to do it without reboot, set a flag here, and in 'loop()', if the flag is set, call 'SetupServer()'.
		// However it will disconnect the current connection, and might be balls-breaking. IE: maybe you made a typo in the password,
		// and you have no way to re-check it once you clicked 'Submit', and.. you'll be fucked ! you'll have to re-upload a new firmware
		// if you had not connected to the local wifi before. So this is dangerous.
	  }
	  // Auto-redirect immediately back to root
	  reply = "<html><head><meta http-equiv=\"refresh\" content=\"3; URL=/\" /></head><body></body></html>";
  }

  // Redirect immediately to the root
  server.send(200, "text/html", reply);
}

//----------------------------------------------------------------------------

void  SetupServer()
{
  server.stop();
  server.close(); // just in case we're reconnecting

  WiFi.mode(WIFI_AP_STA);
  Serial.print("Setting up access-point");
  WiFi.softAP(wifi_AP_SSID, wifi_AP_Pass);
  WiFi.softAPConfig(wifi_AP_local_ip, wifi_AP_gateway, wifi_AP_subnet);

  // Setup server callbacks/handlers
  server.on("/", _HandleRoot);
  server.on("/set_credentials", _HandleSetCredentials);
  server.on("/set_credentials_ap", _HandleSetCredentialsAP);
  server.onNotFound(_HandleNotFound);

  // Start the server
  server.begin();
  Serial.println("Server started");
}

//----------------------------------------------------------------------------
//
//  EEPROM helper for ESP32
//  Will not work on regular arduino, EEPROM interface is slightly different.
//  EEPROM emulates through flash memory on ESP32
//
//----------------------------------------------------------------------------

void  ESP32Flash_Init()
{
  EEPROM.begin(kEEPROM_total_size);

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
  char      eeprom_ST_ssid[kEEPROM_ST_ssid_size] = {};
  char      eeprom_ST_pass[kEEPROM_ST_pass_size] = {};
  char      eeprom_AP_ssid[kEEPROM_AP_ssid_size] = {};
  char      eeprom_AP_pass[kEEPROM_AP_pass_size] = {};
  static_assert(sizeof(eeprom_ST_ssid) == kEEPROM_ST_ssid_size);
  static_assert(sizeof(eeprom_ST_pass) == kEEPROM_ST_pass_size);
  static_assert(sizeof(eeprom_AP_ssid) == kEEPROM_AP_ssid_size);
  static_assert(sizeof(eeprom_AP_pass) == kEEPROM_AP_pass_size);

  for (int i = 0; i < wifi_ST_SSID.length() && i < sizeof(eeprom_ST_ssid)-1 - 1; i++)
    eeprom_ST_ssid[i] = wifi_ST_SSID[i];
  eeprom_ST_ssid[sizeof(eeprom_ST_ssid)-1] = '\0';

  for (int i = 0; i < wifi_ST_Pass.length() && i < sizeof(eeprom_ST_pass)-1 - 1; i++)
    eeprom_ST_pass[i] = wifi_ST_Pass[i];
  eeprom_ST_pass[sizeof(eeprom_ST_pass)-1] = '\0';

  for (int i = 0; i < wifi_AP_SSID.length() && i < sizeof(eeprom_AP_ssid)-1 - 1; i++)
    eeprom_AP_ssid[i] = wifi_AP_SSID[i];
  eeprom_AP_ssid[sizeof(eeprom_AP_ssid)-1] = '\0';

  for (int i = 0; i < wifi_AP_Pass.length() && i < sizeof(eeprom_AP_pass)-1 - 1; i++)
    eeprom_AP_pass[i] = wifi_AP_Pass[i];
  eeprom_AP_pass[sizeof(eeprom_AP_pass)-1] = '\0';

  EEPROM.write(0, 0);
  EEPROM.put(kEEPROM_ST_ssid_addr, eeprom_ST_ssid);
  EEPROM.put(kEEPROM_ST_pass_addr, eeprom_ST_pass);
  EEPROM.put(kEEPROM_AP_ssid_addr, eeprom_AP_ssid);
  EEPROM.put(kEEPROM_AP_pass_addr, eeprom_AP_pass);
  EEPROM.commit();
}

//----------------------------------------------------------------------------

void  ESP32Flash_ReadServerInfo()
{
  char      eeprom_ST_ssid[kEEPROM_ST_ssid_size] = {};
  char      eeprom_ST_pass[kEEPROM_ST_pass_size] = {};
  char      eeprom_AP_ssid[kEEPROM_AP_ssid_size] = {};
  char      eeprom_AP_pass[kEEPROM_AP_pass_size] = {};
  static_assert(sizeof(eeprom_ST_ssid) == kEEPROM_ST_ssid_size);
  static_assert(sizeof(eeprom_ST_pass) == kEEPROM_ST_pass_size);
  static_assert(sizeof(eeprom_AP_ssid) == kEEPROM_AP_ssid_size);
  static_assert(sizeof(eeprom_AP_pass) == kEEPROM_AP_pass_size);

  EEPROM.get(kEEPROM_ST_ssid_addr, eeprom_ST_ssid);
  EEPROM.get(kEEPROM_ST_pass_addr, eeprom_ST_pass);
  EEPROM.get(kEEPROM_AP_ssid_addr, eeprom_AP_ssid);
  EEPROM.get(kEEPROM_AP_pass_addr, eeprom_AP_pass);
  eeprom_ST_ssid[sizeof(eeprom_ST_ssid)-1] = '\0';
  eeprom_ST_pass[sizeof(eeprom_ST_pass)-1] = '\0';
  eeprom_AP_ssid[sizeof(eeprom_AP_ssid)-1] = '\0';
  eeprom_AP_pass[sizeof(eeprom_AP_pass)-1] = '\0';

  wifi_ST_SSID = eeprom_ST_ssid;
  wifi_ST_Pass = eeprom_ST_pass;
  wifi_AP_SSID = eeprom_AP_ssid;
  wifi_AP_Pass = eeprom_AP_pass;
  if (eeprom_ST_ssid[0] != '\0') // don't check pass: allow empty password
    has_wifi_credentials = true;
}
