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

#define NUM_LEDS_NECK       3*3
#define NUM_LEDS_MANDIBLE_B 8
#define NUM_LEDS_MANDIBLE_S 10
#define NUM_LEDS_TEMPLE_R   2
#define NUM_LEDS_TEMPLE_L   2
#define NUM_LEDS_EYE_R      1
#define NUM_LEDS_EYE_L      1
#define NUM_LEDS_NOSE       2

#define NUM_LEDS          (NUM_LEDS_NECK + NUM_LEDS_MANDIBLE_B + NUM_LEDS_MANDIBLE_S + NUM_LEDS_TEMPLE_R + NUM_LEDS_TEMPLE_L + NUM_LEDS_EYE_R + NUM_LEDS_EYE_L + NUM_LEDS_NOSE)

#define LED_ID_NECK       0
#define LED_ID_MANDIBLE_B LED_ID_NECK + NUM_LEDS_NECK
#define LED_ID_MANDIBLE_S LED_ID_MANDIBLE_B + NUM_LEDS_MANDIBLE_B
#define LED_ID_TEMPLE_R   LED_ID_MANDIBLE_S + NUM_LEDS_MANDIBLE_S
#define LED_ID_EYE_R      LED_ID_TEMPLE_R + NUM_LEDS_TEMPLE_R
#define LED_ID_NOSE       LED_ID_EYE_R + NUM_LEDS_EYE_R
#define LED_ID_TEMPLE_L   LED_ID_NOSE + NUM_LEDS_NOSE
#define LED_ID_EYE_L      LED_ID_TEMPLE_L + NUM_LEDS_TEMPLE_L

//----------------------------------------------------------------------------
// LED propagation cursor lookup used for wave animation

static const float  kLEDPropagationCursors[] =
{
  // Neck
  0.30f, 0.15f, 0.00f,
  0.00f, 0.15f, 0.30f,
  0.30f, 0.15f, 0.00f,

  // Mandible: Bottom
  0.35f, 0.425f, 0.5f, 0.575f,  0.575f, 0.5f, 0.425f, 0.35f,

  // Mandible: Sides
  0.4f, 0.45f, 0.5f, 0.55f, 0.6f,  0.6f, 0.55f, 0.5f, 0.45f, 0.4f,

  // Right temple
  0.7f, 0.8f,
  // Right eye
  1.0f + 0.1f,  // slightly offset compared to right eye so they're not perfectly synced

  // Nose
  0.7f, 0.7f,

  // Left temple
  0.7f, 0.8f,
  // Left eye
  1.0f,
};
static_assert(sizeof(kLEDPropagationCursors) / sizeof(kLEDPropagationCursors[0]) == NUM_LEDS);

//----------------------------------------------------------------------------

static const float  kLEDBrightnessFactors[] =
{
  // Neck
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,

  // Mandible: Bottom
  0.5f, 1, 1, 1,  1, 1, 1, 0.5f,

  // Mandible: Sides
  0.1f, 0.8f, 1, 1, 1,  1, 1, 1, 0.8f, 0.1f,

  // Right temple
  1, 0.8f,
  // Right eye
  1,

  // Nose
  1, 1,

  // Left temple
  1, 0.8f,
  // Left eye
  1,
};
static_assert(sizeof(kLEDBrightnessFactors) / sizeof(kLEDBrightnessFactors[0]) == NUM_LEDS);

//----------------------------------------------------------------------------

CRGB        leds[NUM_LEDS];

WebServer   server(80);
LoopTimer   loopTimer;
uint32_t    chipId = 0;
uint32_t    MsSinceConnected = 0;
IPAddress   wifi_AP_local_ip(192,168,1,1);
IPAddress   wifi_AP_gateway(192,168,1,1);
IPAddress   wifi_AP_subnet(255,255,255,0);

//----------------------------------------------------------------------------
// Read these from the flash

bool        has_wifi_credentials = false;
bool        has_dirty_credentials = false;
String      wifi_ST_SSID;
String      wifi_ST_Pass;
String		  wifi_AP_SSID;
String		  wifi_AP_Pass;
int32_t     state_mode = 0;
int32_t     state_brightness = 255;   // [0, 255]
int32_t     state_wave_speed = 50;    // [0, 100]
int32_t     state_wave_min = 25;      // [0, 100]
int32_t     state_wave_contrast = 1;  // [0, 3]
int32_t     state_wave_tiling = 50;   // [0, 100]
int32_t     state_base_speed = 50;    // [0, 100]
int32_t     state_base_min = 80;      // [0, 100]
int32_t     state_hue_shift = 0;      // [0, 255]
int32_t     state_sat_shift = 0;      // [-255, 255]
CRGB        state_color_eye_l = CRGB(255, 255, 255);
CRGB        state_color_eye_r = CRGB(255, 255, 255);
CRGB        state_color_neck = CRGB(255, 255, 255);
CRGB        state_color_nose = CRGB(255, 255, 255);
CRGB        state_color_temples = CRGB(255, 255, 255);
CRGB        state_color_mandible = CRGB(255, 255, 255);
CRGB        state_color_throat = CRGB(255, 255, 255);

// Element sizes & addresses in the flash storage:
const int   kEEPROM_ST_ssid_size = 20;  // 20 chars max
const int   kEEPROM_ST_pass_size = 60;  // 60 chars max
const int   kEEPROM_AP_ssid_size = 20;  // 20 chars max
const int   kEEPROM_AP_pass_size = 60;  // 60 chars max
const int   kEEPROM_int32_t_size = 4;
const int   kEEPROM_col_rgb_size = 3;

const int   kEEPROM_ST_ssid_addr = 1;
const int   kEEPROM_ST_pass_addr = kEEPROM_ST_ssid_addr + kEEPROM_ST_ssid_size;
const int   kEEPROM_AP_ssid_addr = kEEPROM_ST_pass_addr + kEEPROM_ST_pass_size;
const int   kEEPROM_AP_pass_addr = kEEPROM_AP_ssid_addr + kEEPROM_AP_ssid_size;
const int   kEEPROM_mode_addr = kEEPROM_AP_pass_addr + kEEPROM_AP_pass_size;
const int   kEEPROM_bright_addr = kEEPROM_mode_addr + kEEPROM_int32_t_size;
const int   kEEPROM_wave_speed_addr = kEEPROM_bright_addr + kEEPROM_int32_t_size;
const int   kEEPROM_wave_min_addr = kEEPROM_wave_speed_addr + kEEPROM_int32_t_size;
const int   kEEPROM_wave_contrast_addr = kEEPROM_wave_min_addr + kEEPROM_int32_t_size;
const int   kEEPROM_wave_tiling_addr = kEEPROM_wave_contrast_addr + kEEPROM_int32_t_size;
const int   kEEPROM_base_speed_addr = kEEPROM_wave_tiling_addr + kEEPROM_int32_t_size;
const int   kEEPROM_base_min_addr = kEEPROM_base_speed_addr + kEEPROM_int32_t_size;
const int   kEEPROM_hue_shift_addr = kEEPROM_base_min_addr + kEEPROM_int32_t_size;
const int   kEEPROM_sat_shift_addr = kEEPROM_hue_shift_addr + kEEPROM_int32_t_size;
const int   kEEPROM_col_eye_l_addr = kEEPROM_sat_shift_addr + kEEPROM_int32_t_size;
const int   kEEPROM_col_eye_r_addr = kEEPROM_col_eye_l_addr + kEEPROM_col_rgb_size;
const int   kEEPROM_col_neck_addr = kEEPROM_col_eye_r_addr + kEEPROM_col_rgb_size;
const int   kEEPROM_col_nose_addr = kEEPROM_col_neck_addr + kEEPROM_col_rgb_size;
const int   kEEPROM_col_temples_addr = kEEPROM_col_nose_addr + kEEPROM_col_rgb_size;
const int   kEEPROM_col_mandible_addr = kEEPROM_col_temples_addr + kEEPROM_col_rgb_size;
const int   kEEPROM_col_throat_addr = kEEPROM_col_mandible_addr + kEEPROM_col_rgb_size;
const int   kEEPROM_col_FIRST_addr = kEEPROM_col_eye_l_addr;

const int   kEEPROM_total_size = 1 + // first byte is a key == 0
                                 kEEPROM_ST_ssid_size + kEEPROM_ST_pass_size + kEEPROM_AP_ssid_size + kEEPROM_AP_pass_size +
                                 kEEPROM_int32_t_size * 10 +
                                 kEEPROM_col_rgb_size * 7;

static_assert(kEEPROM_total_size == kEEPROM_col_throat_addr + kEEPROM_col_rgb_size);

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

//  delay(15000); // DEBUG

  // Init baselines
  chipId = 0;
  for(int i=0; i<17; i=i+8)
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  Serial.printf("ESP32 Chip model = %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("This chip has %d cores\n", ESP.getChipCores());
  Serial.printf("Chip ID: %08X\n", chipId);

  // Dump MAC address to serial. Useful for dev during initial network setup.
  // Just send the mac address to @vksiezak to pin this module to a specific IP address
  {
    uint8_t macAddr[6];
    WiFi.macAddress(macAddr);
    Serial.printf("Mac address: %02x:%02x:%02x:%02x:%02x:%02x\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
  }

  // Init default AP name before reading from flash
  wifi_AP_SSID = "Mimir_" + String(chipId, HEX);
  wifi_AP_Pass = "0123456789";

  // Read the AP name & pass + server's IP & port we recorded from the previous runs to instantly connect to
  // what will likely be the correct one if we reboot.
  ESP32Flash_Init();
  ESP32Flash_ReadServerInfo();

  // Setup wifi in both access-point & station mode:
  SetupServer();

  // Try to connect to the wifi network
  ConnectToWiFi();

  WaitForWiFi(2000);  // Wait at most 2 seconds for wifi
}

//----------------------------------------------------------------------------

static CRGB  ShiftHS(const CRGB &color, int32_t hue_shift, int32_t sat_shift)
{
  if (hue_shift == 0) // Nothing to shift
    return color;

  // Convert from RGB to HSV
  int32_t       h = 0;
  int32_t       s = 0;
  const int32_t v = max(max(color.r, color.g), color.b);
  const int32_t delta = v - min(min(color.r, color.g), color.b);
  if (delta > 0)
  {
    const float invDelta = 1.0f / delta;
    float   hvalue;
    if (v == color.r)
      hvalue = (color.g - color.b) * invDelta + 0.0f;
    else if (v == color.g)
      hvalue = (color.b - color.r) * invDelta + 2.0f;
    else
      hvalue = (color.r - color.g) * invDelta + 4.0f;
    if (hvalue < 0.0f)
      hvalue += 6.0f;

    h = clamp(int32_t(hvalue * 255.0f / 6.0f), 0, 255);
    s = clamp(int32_t(delta * 255.0f / v), 0, 255);
  }

  // Shift the hue & sat
  h = (h + hue_shift) & 0xFF;         // wrap hue
  s = clamp(s + sat_shift, 0, 0xFF);  // clamp sat

  // Convert back from HSV to RGB
  const float delta2 = (s * v) / 255.0f;
  const float h6 = h * 6.0f / 255.0f;
  const float r0n = 2.0f - fabsf(h6 - 3.0f);
  const float g0n = fabsf(h6 - 2.0f) - 1.0f;
  const float b0n = fabsf(h6 - 4.0f) - 1.0f;
  return CRGB(v - int32_t(delta2 * clamp(r0n, 0.0f, 1.0f)),
              v - int32_t(delta2 * clamp(g0n, 0.0f, 1.0f)),
              v - int32_t(delta2 * clamp(b0n, 0.0f, 1.0f)));
}

//----------------------------------------------------------------------------
// Returns a -1, 1 noise

static float  Noise(float t)
{
  return sinf(t *  1.000f) * 0.400f +
         sinf(t *  3.275f) * 0.325f +
         sinf(t *  7.241f) * 0.200f +
         sinf(t * 18.800f) * 0.075f;
}

//----------------------------------------------------------------------------

static CRGB ApplyPropagationBrightness(int ledIndex, float et, float p0, const CRGB &base)
{
  const float p1 = 4.0f * (state_wave_tiling / 100.0f);  // period of cursor offset, increase this to make the noise "tile" more across space
  const float cursor = kLEDPropagationCursors[ledIndex];
  const float ledScale = kLEDBrightnessFactors[ledIndex];
  const float bMin = state_wave_min / 100.0f;
  const float bMax = 1.0f;
  const float noise01 = Noise((et + cursor * p1) * p0) * 0.5f + 0.5f;           // remap from [-1, 1] to [0, 1]
  const float intensity_01 = clamp(noise01 * (bMax - bMin) + bMin, 0.0f, 1.0f); // remap from [0, 1] to [bMin, bMax]
#if 1
  const float intensity = ledScale * powf(intensity_01, state_wave_contrast + 1.0f);
#else
  float       intensity = intensity_01;
  // Avoid using expensive powf()
  switch (state_wave_contrast)
  {
    case  0:
      break;
    case  1:
      intensity = intensity * intensity;
      break;
    case  2:
      intensity = intensity * intensity * intensity;
      break;
    case  3:
      intensity = intensity * intensity;
      intensity = intensity * intensity;
      break;
  }
  intensity *= ledScale;
#endif
  return CRGB(int32_t(base.r * intensity),
              int32_t(base.g * intensity),
              int32_t(base.b * intensity));
}

//----------------------------------------------------------------------------

void loop()
{
  loopTimer.tick();

  // Update wifi & web server
  WifiServerLoop();

  const float   dt = loopTimer.dt();
  const float   et = loopTimer.elapsedTime();

  // What to configure:
  // - Mode: 0=normal, 1= color debug, 3=ID cycle, 2=RGB cycle
  // - overall hue shift
  // - 5 inner colors
  // - 2 eye colors
  // - animation speed (main)
  // - animation speed (eyes)

  FastLED.setBrightness(state_brightness);

  if (state_mode == 0 || state_mode == 1)
  {
    const CRGB  colorNeck       = ShiftHS(state_color_neck, state_hue_shift, state_sat_shift);
    const CRGB  colorMandibleS  = ShiftHS(state_color_mandible, state_hue_shift, state_sat_shift);
    const CRGB  colorMandibleB  = ShiftHS(state_color_throat, state_hue_shift, state_sat_shift);
    const CRGB  colorTempleR    = ShiftHS(state_color_temples, state_hue_shift, state_sat_shift);
    const CRGB  colorTempleL    = ShiftHS(state_color_temples, state_hue_shift, state_sat_shift);
    const CRGB  colorEyeR       = ShiftHS(state_color_eye_r, state_hue_shift, state_sat_shift);
    const CRGB  colorEyeL       = ShiftHS(state_color_eye_l, state_hue_shift, state_sat_shift);
    const CRGB  colorBottom     = ShiftHS(state_color_nose, state_hue_shift, state_sat_shift);

    // Animation
    // Use a "propagation cursor" for each individual LED, starting at 0 at the base of the C3 vertebrae, and ending at the jaw extremity, and eyes
    // Use this cursor to lookup a noise function, and propagate intensity waves across.
    // On top of it, add a second global noise to vary intensity

    // Compute global brightness level
    const float p0 = 2.0f * state_base_speed / 100.0f;
    const float p1 = 2.0f * state_wave_speed / 100.0f;
    const float bMin = state_base_min / 100.0f;
    const float bMax = 1.0f;
    const float noise01 = Noise(et * p0) * 0.5f + 0.5f;                           // remap from [-1, 1] to [0, 1]
    const float intensity_01 = clamp(noise01 * (bMax - bMin) + bMin, 0.0f, 1.0f); // remap from [0, 1] to [bMin, bMax]
    FastLED.setBrightness(int32_t(state_brightness * intensity_01));

#define SET_LED_WITH_PROPAGATION(__i, __base) leds[i + (__i)] = ApplyPropagationBrightness(i + (__i), et, p1, __base);

    for (int i = 0; i < NUM_LEDS_NECK; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_NECK, colorNeck);
    for (int i = 0; i < NUM_LEDS_MANDIBLE_S; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_MANDIBLE_S, colorMandibleS);
    for (int i = 0; i < NUM_LEDS_MANDIBLE_B; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_MANDIBLE_B, colorMandibleB);
    for (int i = 0; i < NUM_LEDS_TEMPLE_R; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_TEMPLE_R, colorTempleR);
    for (int i = 0; i < NUM_LEDS_EYE_R; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_EYE_R, colorEyeR);
    for (int i = 0; i < NUM_LEDS_NOSE; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_NOSE, colorBottom);
    for (int i = 0; i < NUM_LEDS_TEMPLE_L; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_TEMPLE_L, colorTempleL);
    for (int i = 0; i < NUM_LEDS_EYE_L; i++)
      SET_LED_WITH_PROPAGATION(LED_ID_EYE_L, colorEyeL);

#undef SET_LED_WITH_PROPAGATION
  }
  else if (state_mode == 1) // Coloration per-area
  {
    for (int i = 0; i < NUM_LEDS_NECK; i++)
      leds[i + LED_ID_NECK] = CRGB(255, 40, 10);
    for (int i = 0; i < NUM_LEDS_MANDIBLE_S; i++)
      leds[i + LED_ID_MANDIBLE_S] = CRGB(255, 150, 10);
    for (int i = 0; i < NUM_LEDS_MANDIBLE_B; i++)
      leds[i + LED_ID_MANDIBLE_B] = CRGB(10, 150, 255);
    for (int i = 0; i < NUM_LEDS_TEMPLE_R; i++)
      leds[i + LED_ID_TEMPLE_R] = CRGB(80, 255, 10);
    for (int i = 0; i < NUM_LEDS_EYE_R; i++)
      leds[i + LED_ID_EYE_R] = CRGB(255, 0, 0);
    for (int i = 0; i < NUM_LEDS_NOSE; i++)
      leds[i + LED_ID_NOSE] = CRGB(255, 10, 150);
    for (int i = 0; i < NUM_LEDS_TEMPLE_L; i++)
      leds[i + LED_ID_TEMPLE_L] = CRGB(80, 255, 10);
    for (int i = 0; i < NUM_LEDS_EYE_L; i++)
      leds[0 + LED_ID_EYE_L] = CRGB(255, 0, 0);
  }
  else
  {
    // Increase 'x' counter every 0.2 seconds to animate the debug modes:
    static int    x = 0;
    {
      static float  t = 0;
      const float   p = 0.2f;
      t += dt;
      if (t > p)
      {
        t -= p;
        x++;
      }
    }

    if (state_mode == 2)  // Highlight each LED individually
    {
      for (int i = 0; i < NUM_LEDS; i++)
      {
        if (i == (x % NUM_LEDS))
          leds[i].setRGB(255, 255, 255);
        else
          leds[i].setRGB(255, 50, 10);
      }
    }
    else  // RGB "christmas tree" + brightness variation
    {
      const float b = sinf(et) * 0.5f + 0.5f;
      const int   c0 = clamp((int)(b * 255) + 1, 1, 255);
      const int   c1 = clamp((int)(b * 50), 0, 255);
      for (int i = 0; i < NUM_LEDS; i++)
      {
        int key = (i + x) % NUM_LEDS;
        leds[i].setRGB((key % 3) == 0 ? c0 : c1, ((key + 1) % 3) == 0 ? c0 : c1, ((key + 2) % 3) == 0 ? c0 : c1);
      }
    }
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
  if (!has_wifi_credentials)  // Nothing to connect to if we don't have any credentials
    return;

  Serial.println("Connecting to wifi...");
  Serial.println("Connecting to " + wifi_ST_SSID);

  WiFi.begin(wifi_ST_SSID, wifi_ST_Pass);
}

//----------------------------------------------------------------------------

void  WaitForWiFi(int maxMs)
{
  if (!has_wifi_credentials)  // Nothing to wait on if we don't have any credentials
    return;

  const int kMsPerRetry = 500;
  int retries = max(maxMs / kMsPerRetry, 1);
  Serial.print("Waiting for Wifi connection");
  while (WiFi.status() != WL_CONNECTED && retries-- > 0)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED)
  {
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
           "Chip ID: <font class=\"code\">";
  reply += String(chipId, HEX);
  reply += "</font>";
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
// HTML form helpers

#define FORM_INPUTBOX(__name, __title, __value) \
    "  <tr>\r\n" \
    "    <td><label for=\"" __name "\">" __title ":</label></td>\r\n" \
    "    <td><input type=\"text\" id=\"" __name "\" name=\"" __name "\" value=\"" + __value + "\"></td>\r\n" \
    "  </tr>"
#define FORM_SLIDER(__name, __title, __min, __max, __value) \
    "  <tr>\r\n" \
    "    <td><label for=\"" __name "\">" __title ":</label></td>\r\n" \
    "    <td><input type=\"range\" min=\"" __min "\" max=\"" __max "\" id=\"" __name "\" name=\"" __name "\" value=\"" + __value + "\"></td>\r\n" \
    "  </tr>"
#define FORM_RGB(__name, __title, __value_r, __value_g, __value_b) \
    "  <tr>\r\n" \
    "    <td><label for=\"" __name "_r\">" __title ":</label></td>\r\n" \
    "    <td><input type=\"text\" id=\"" __name "_r\" name=\"" __name "_r\" value=\"" + __value_r + "\" style=\"width:50px\">\r\n" \
    "        <input type=\"text\" id=\"" __name "_g\" name=\"" __name "_g\" value=\"" + __value_g + "\" style=\"width:50px\">\r\n" \
    "        <input type=\"text\" id=\"" __name "_b\" name=\"" __name "_b\" value=\"" + __value_b + "\" style=\"width:50px\"></td>\r\n" \
    "  </tr><tr>\r\n"

//----------------------------------------------------------------------------

static void _HandleRoot()
{
  String  reply;
  reply += "<h2>Configure lighting:</h2>\r\n"
           "<form action=\"/configure\">\r\n"
           "  <table>\r\n"
           "  <tr>\r\n"
           "    <td><label for=\"mode\">Mode:</label></td>\r\n"
           "    <td><select id=\"mode\" name=\"mode\">\r\n"
           "      <option value=\"0\">Normal</option>\r\n"
           "      <option value=\"1\">Debug regions</option>\r\n"
           "      <option value=\"2\">Debug ID</option>\r\n"
           "      <option value=\"3\">Debug RGB Cycle</option>\r\n"
           "    </select></td>\r\n"
           "  </tr>\r\n"
           "  <tr>\r\n"
           "    <td colspan=2 height=\"25px\"><big><b>Color control:</b></big></td>\r\n"
           "  </tr>\r\n"
           FORM_SLIDER("brightness", "Brightness", "0", "255", String(state_brightness))
           FORM_SLIDER("sat_shift", "Saturation", "-255", "255", String(state_sat_shift))
           FORM_SLIDER("hue_shift", "Hue shift", "0", "255", String(state_hue_shift))
           FORM_RGB("col_eye_l", "Left eye color", String(state_color_eye_l.r), String(state_color_eye_l.g), String(state_color_eye_l.b))
           FORM_RGB("col_eye_r", "Right eye color", String(state_color_eye_r.r), String(state_color_eye_r.g), String(state_color_eye_r.b))
           FORM_RGB("col_nose", "Nose color", String(state_color_nose.r), String(state_color_nose.g), String(state_color_nose.b))
           FORM_RGB("col_temples", "Temples color", String(state_color_temples.r), String(state_color_temples.g), String(state_color_temples.b))
           FORM_RGB("col_mandible", "Mandible color", String(state_color_mandible.r), String(state_color_mandible.g), String(state_color_mandible.b))
           FORM_RGB("col_throat", "Throat color", String(state_color_throat.r), String(state_color_throat.g), String(state_color_throat.b))
           FORM_RGB("col_neck", "Neck color", String(state_color_neck.r), String(state_color_neck.g), String(state_color_neck.b))
           "  <tr>\r\n"
           "    <td colspan=2 height=\"25px\"><big><b>Animation:</b></big></td>\r\n"
           "  </tr>\r\n"
           FORM_SLIDER("wave_speed", "Wave anim speed", "0", "100", String(state_wave_speed))
           FORM_SLIDER("wave_min", "Wave min intensity", "0", "100", String(state_wave_min))
           FORM_SLIDER("wave_contrast", "Wave contrast", "0", "3", String(state_wave_contrast))
           FORM_SLIDER("wave_tiling", "Wave tiling", "0", "100", String(state_wave_tiling))
           FORM_SLIDER("base_speed", "Base anim speed", "0", "100", String(state_base_speed))
           FORM_SLIDER("base_min", "Base min intensity", "0", "100", String(state_base_min))
           "  <tr>\r\n"
           "    <td colspan=2><input type=\"submit\" value=\"Submit\"></td>\r\n"
           "  </tr>\r\n"
           "  </table>\r\n"
           "</form>\r\n";

  reply += "<hr/>\r\n"
           "<h3>Set wifi network credentials:</h3>\r\n"
           "This will allow the device to connect to your local wifi, and access this page through your wifi network without the need to connect to the access-point.<br/><br/>\r\n"
           "<form action=\"/set_credentials\">\r\n"
           "  <table>\r\n"
           FORM_INPUTBOX("ssid", "WiFi SSID", wifi_ST_SSID)
           FORM_INPUTBOX("passwd", "WiFi Password", wifi_ST_Pass)
           "  <tr>\r\n"
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
    has_dirty_credentials = true;  // Reconnect to wifi on next loop
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
    			   "The device will be \"bricked\" as you won't be able to connect to it from anywhere, and only re-flashing the firmware from the USB connection will fix this.</font><br/>\r\n"
    			   "<br/>\r\n"
    			   "<form action=\"/set_credentials_ap\">\r\n"
             "  <table>\r\n"
             FORM_INPUTBOX("ssid", "Access-point SSID", wifi_AP_SSID)
             FORM_INPUTBOX("passwd", "Access-point Password", wifi_AP_Pass)
             "  <tr>\r\n"
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

static void _HandleConfigure()
{
  for (uint8_t i = 0; i < server.args(); i++)
  {
    const String  argName = server.argName(i);
    const int32_t argInt = server.arg(i).toInt();
    if (argName == "mode")
       state_mode = argInt;
    else if (argName == "brightness")
       state_brightness = argInt;
    else if (argName == "wave_speed")
       state_wave_speed = argInt;
    else if (argName == "wave_min")
       state_wave_min = argInt;
    else if (argName == "wave_contrast")
       state_wave_contrast = argInt;
    else if (argName == "wave_tiling")
       state_wave_tiling = argInt;
    else if (argName == "base_speed")
       state_base_speed = argInt;
    else if (argName == "base_min")
       state_base_min = argInt;
    else if (argName == "hue_shift")
       state_hue_shift = argInt;
    else if (argName == "sat_shift")
       state_sat_shift = argInt;
    else if (argName == "col_eye_l_r")  // Left eye
       state_color_eye_l.r = argInt;
    else if (argName == "col_eye_l_g")
       state_color_eye_l.g = argInt;
    else if (argName == "col_eye_l_b")
       state_color_eye_l.b = argInt;
    else if (argName == "col_eye_r_r")  // Right eye
       state_color_eye_r.r = argInt;
    else if (argName == "col_eye_r_g")
       state_color_eye_r.g = argInt;
    else if (argName == "col_eye_r_b")
       state_color_eye_r.b = argInt;
    else if (argName == "col_neck_r")   // Neck
       state_color_neck.r = argInt;
    else if (argName == "col_neck_g")
       state_color_neck.g = argInt;
    else if (argName == "col_neck_b")
       state_color_neck.b = argInt;
    else if (argName == "col_nose_r")   // Nose
       state_color_nose.r = argInt;
    else if (argName == "col_nose_g")
       state_color_nose.g = argInt;
    else if (argName == "col_nose_b")
       state_color_nose.b = argInt;
    else if (argName == "col_temples_r")// Temples
       state_color_temples.r = argInt;
    else if (argName == "col_temples_g")
       state_color_temples.g = argInt;
    else if (argName == "col_temples_b")
       state_color_temples.b = argInt;
    else if (argName == "col_mandible_r")// Mandible
       state_color_mandible.r = argInt;
    else if (argName == "col_mandible_g")
       state_color_mandible.g = argInt;
    else if (argName == "col_mandible_b")
       state_color_mandible.b = argInt;
    else if (argName == "col_throat_r")// Throat
       state_color_throat.r = argInt;
    else if (argName == "col_throat_g")
       state_color_throat.g = argInt;
    else if (argName == "col_throat_b")
       state_color_throat.b = argInt;
  }

  ESP32Flash_WriteServerInfo();

  // Auto-redirect immediately to the root
  server.send(200, "text/html", "<html><head><meta http-equiv=\"refresh\" content=\"3; URL=/\" /></head><body></body></html>");
}

//----------------------------------------------------------------------------

void  SetupServer()
{
  server.stop();
  server.close(); // just in case we're reconnecting

  WiFi.mode(WIFI_AP_STA);
  Serial.println("Setting up access-point: " + wifi_AP_SSID);
  WiFi.softAP(wifi_AP_SSID, wifi_AP_Pass);
  WiFi.softAPConfig(wifi_AP_local_ip, wifi_AP_gateway, wifi_AP_subnet);

  // Setup server callbacks/handlers
  server.on("/", _HandleRoot);
  server.on("/set_credentials", _HandleSetCredentials);
  server.on("/set_credentials_ap", _HandleSetCredentialsAP);
  server.on("/configure", _HandleConfigure);
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
    // Init all the beginning before the first colors to 0
    for (int i = 0, stop = kEEPROM_col_FIRST_addr; i < stop; i++)
      EEPROM.write(i, 0);

    // Init all colors to 0xFF (assumes colors appear last)
    for (int i = kEEPROM_col_FIRST_addr, stop = EEPROM.length(); i < stop; i++)
      EEPROM.write(i, 0xFF);

    // Hand-patch a few things that must be initialized to zero:
    EEPROM.put(kEEPROM_bright_addr, int32_t(0xFF));
    EEPROM.put(kEEPROM_wave_speed_addr, int32_t(50));
    EEPROM.put(kEEPROM_wave_min_addr, int32_t(25));
    EEPROM.put(kEEPROM_wave_contrast_addr, int32_t(1));
    EEPROM.put(kEEPROM_wave_tiling_addr, int32_t(50));
    EEPROM.put(kEEPROM_base_speed_addr, int32_t(50));
    EEPROM.put(kEEPROM_base_min_addr, int32_t(80));

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
  EEPROM.put(kEEPROM_mode_addr, state_mode);

  EEPROM.put(kEEPROM_wave_speed_addr, state_wave_speed);
  EEPROM.put(kEEPROM_wave_min_addr, state_wave_min);
  EEPROM.put(kEEPROM_wave_contrast_addr, state_wave_contrast);
  EEPROM.put(kEEPROM_wave_tiling_addr, state_wave_tiling);
  EEPROM.put(kEEPROM_base_speed_addr, state_base_speed);
  EEPROM.put(kEEPROM_base_min_addr, state_base_min);
  EEPROM.put(kEEPROM_hue_shift_addr, state_hue_shift);
  EEPROM.put(kEEPROM_sat_shift_addr, state_sat_shift);

  static_assert(sizeof(state_color_eye_l.r) == 1);
  EEPROM.put(kEEPROM_col_eye_l_addr + 0, state_color_eye_l.r);
  EEPROM.put(kEEPROM_col_eye_l_addr + 1, state_color_eye_l.g);
  EEPROM.put(kEEPROM_col_eye_l_addr + 2, state_color_eye_l.b);
  EEPROM.put(kEEPROM_col_eye_r_addr + 0, state_color_eye_r.r);
  EEPROM.put(kEEPROM_col_eye_r_addr + 1, state_color_eye_r.g);
  EEPROM.put(kEEPROM_col_eye_r_addr + 2, state_color_eye_r.b);
  EEPROM.put(kEEPROM_col_neck_addr + 0, state_color_neck.r);
  EEPROM.put(kEEPROM_col_neck_addr + 1, state_color_neck.g);
  EEPROM.put(kEEPROM_col_neck_addr + 2, state_color_neck.b);
  EEPROM.put(kEEPROM_col_nose_addr + 0, state_color_nose.r);
  EEPROM.put(kEEPROM_col_nose_addr + 1, state_color_nose.g);
  EEPROM.put(kEEPROM_col_nose_addr + 2, state_color_nose.b);
  EEPROM.put(kEEPROM_col_temples_addr + 0, state_color_temples.r);
  EEPROM.put(kEEPROM_col_temples_addr + 1, state_color_temples.g);
  EEPROM.put(kEEPROM_col_temples_addr + 2, state_color_temples.b);
  EEPROM.put(kEEPROM_col_mandible_addr + 0, state_color_mandible.r);
  EEPROM.put(kEEPROM_col_mandible_addr + 1, state_color_mandible.g);
  EEPROM.put(kEEPROM_col_mandible_addr + 2, state_color_mandible.b);
  EEPROM.put(kEEPROM_col_throat_addr + 0, state_color_throat.r);
  EEPROM.put(kEEPROM_col_throat_addr + 1, state_color_throat.g);
  EEPROM.put(kEEPROM_col_throat_addr + 2, state_color_throat.b);
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
  if (eeprom_ST_ssid[0] != '\0') // don't check password: allow empty password
    has_wifi_credentials = true;
  if (eeprom_AP_ssid[0] != '\0')
  {
    wifi_AP_SSID = eeprom_AP_ssid;
    wifi_AP_Pass = eeprom_AP_pass;
  }

  // Read state
  EEPROM.get(kEEPROM_mode_addr, state_mode);
  EEPROM.get(kEEPROM_bright_addr, state_brightness);
  EEPROM.get(kEEPROM_wave_speed_addr, state_wave_speed);
  EEPROM.get(kEEPROM_wave_min_addr, state_wave_min);
  EEPROM.get(kEEPROM_wave_contrast_addr, state_wave_contrast);
  EEPROM.get(kEEPROM_wave_tiling_addr, state_wave_tiling);
  EEPROM.get(kEEPROM_base_speed_addr, state_base_speed);
  EEPROM.get(kEEPROM_base_min_addr, state_base_min);
  EEPROM.get(kEEPROM_hue_shift_addr, state_hue_shift);
  EEPROM.get(kEEPROM_sat_shift_addr, state_sat_shift);
  static_assert(sizeof(state_color_eye_l.r) == 1);
  EEPROM.get(kEEPROM_col_eye_l_addr + 0, state_color_eye_l.r);
  EEPROM.get(kEEPROM_col_eye_l_addr + 1, state_color_eye_l.g);
  EEPROM.get(kEEPROM_col_eye_l_addr + 2, state_color_eye_l.b);
  EEPROM.get(kEEPROM_col_eye_r_addr + 0, state_color_eye_r.r);
  EEPROM.get(kEEPROM_col_eye_r_addr + 1, state_color_eye_r.g);
  EEPROM.get(kEEPROM_col_eye_r_addr + 2, state_color_eye_r.b);
  EEPROM.get(kEEPROM_col_neck_addr + 0, state_color_neck.r);
  EEPROM.get(kEEPROM_col_neck_addr + 1, state_color_neck.g);
  EEPROM.get(kEEPROM_col_neck_addr + 2, state_color_neck.b);
  EEPROM.get(kEEPROM_col_nose_addr + 0, state_color_nose.r);
  EEPROM.get(kEEPROM_col_nose_addr + 1, state_color_nose.g);
  EEPROM.get(kEEPROM_col_nose_addr + 2, state_color_nose.b);
  EEPROM.get(kEEPROM_col_temples_addr + 0, state_color_temples.r);
  EEPROM.get(kEEPROM_col_temples_addr + 1, state_color_temples.g);
  EEPROM.get(kEEPROM_col_temples_addr + 2, state_color_temples.b);
  EEPROM.get(kEEPROM_col_mandible_addr + 0, state_color_mandible.r);
  EEPROM.get(kEEPROM_col_mandible_addr + 1, state_color_mandible.g);
  EEPROM.get(kEEPROM_col_mandible_addr + 2, state_color_mandible.b);
  EEPROM.get(kEEPROM_col_throat_addr + 0, state_color_throat.r);
  EEPROM.get(kEEPROM_col_throat_addr + 1, state_color_throat.g);
  EEPROM.get(kEEPROM_col_throat_addr + 2, state_color_throat.b);
}
