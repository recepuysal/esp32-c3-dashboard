#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include "logo.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "esp_netif.h"
#include <WiFiManager.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>
#if CONFIG_IDF_TARGET_ESP32S3
#include <Adafruit_NeoPixel.h>
#endif

#define VERSION_TEXT "v1.8.14"  // Menu donusu: hava paneli silinmesi

#define LEFT_INFO_W  86
#define LEFT_INFO_H  44
#define LEFT_LINE1_Y 4
#define LEFT_LINE2_Y 16
#define LEFT_LINE3_Y 28
#define CHARGE_STATUS_Y 42
#define WEATHER_HTTP_UA  "ESP32-ST7789-Dashboard/1.8.9"
#define WEATHER_BODY_MAX 8192

// Sabit konum — IP ile ilce tahmini guvenilir degil
#define WEATHER_REGION_NAME  "Umraniye"
#define WEATHER_LAT          41.0214f
#define WEATHER_LON          29.1247f

// =====================================================
// 🔹 Wi-Fi Bilgileri (WiFiManager ile yapılandırılacak)
// =====================================================
// Hard-coded WiFi bilgileri artık kullanılmıyor, 
// WiFiManager ile kullanıcı ayarlayacak
const char* ap_ssid = "ESP32-Dashboard-Setup";  // AP modu ağ adı
const char* ap_password = "1234";           // AP modu şifresi

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3 * 3600;
const int daylightOffset_sec = 0;

// ===== ESP32-S3 Super Mini — pin haritasi (ST7789 320x172) =====
// TFT:  VCC=3V3  GND  CS=10  DC=7  RST=5  SCK=4  MOSI=6  BL=2
// Pil:  LiPo+ --100k-- GPIO1 --200k-- GND
// Enc:  CLK=8  DT=9  SW=3
// TP4056: CHRG=13  STDBY=12 (active LOW)
// Buzzer: GPIO11 (+)  GND (-)
// Onboard WS2812 RGB: GPIO48 (S3 Super Mini)
// ================================================================

#if CONFIG_IDF_TARGET_ESP32S3
#define ONBOARD_RGB_PIN      48
// NeoPixel setBrightness 0-255; onceki 40 cok parlakti -> %50 = 20
#define ONBOARD_RGB_BRIGHT_PCT  50
#define ONBOARD_RGB_BRIGHT_BASE 40
#define ONBOARD_RGB_BRIGHT      ((uint8_t)((ONBOARD_RGB_BRIGHT_BASE * ONBOARD_RGB_BRIGHT_PCT) / 100))
#endif

#define TFT_CS   10
#define TFT_DC    7
#define TFT_RST   5
#define TFT_SCLK  4
#define TFT_MOSI  6

// 🔹 DHT11 (GPIO2 artik BL — sensör bagli degilse 0 birak)
#define USE_DHT11 0
#if USE_DHT11
#include <DHT.h>
#define DHT_PIN   21
#endif

// 🔹 Batarya (LiPo → R1 100k → GPIO1/A0 ← R2 200k → GND)
#define BAT_ADC_PIN       1
// Kalibrasyon: aynı anda multimetre pil ucu + ekran (tek nokta)
// Son ölçüm: multimetre 3.93V, ekran 4.17V → scale *= 3.93/4.17
#define BAT_CALIB_VBAT        3.93f
#define BAT_CALIB_ESP_ADC_V   2.871f
#define BAT_VOLT_SCALE        (BAT_CALIB_VBAT / BAT_CALIB_ESP_ADC_V)
#define BAT_FULL_V            4.20f
#define BAT_EMPTY_V           3.00f
#define BAT_DISPLAY_MAX_V     4.35f

// TP4056 (active LOW) — CHRG/STDBY kablolari takas (GPIO12=STDBY, GPIO13=CHRG)
#define PIN_CHRG   13
#define PIN_STDBY  12

// 🔹 Buzzer (aktif 3.3V) — ses yoksa BUZZER_ACTIVE_LOW 1 dene
#define BUZZER_PIN 11
#define BUZZER_ACTIVE_LOW 0
#if BUZZER_ACTIVE_LOW
#define BUZZER_ON  LOW
#define BUZZER_OFF HIGH
#else
#define BUZZER_ON  HIGH
#define BUZZER_OFF LOW
#endif

// 🔹 Encoder Pinleri
#define ENCODER_CLK   8   // GPIO8 - CLK pini
#define ENCODER_DT    9   // GPIO9 - DT pini  
#define ENCODER_SW    3   // GPIO3 - Switch/Button pini

// 🔹 Backlight Pin (PWM)
#define TFT_BACKLIGHT 2   // GPIO2 - Backlight PWM
#define LEDC_FREQ 5000    // PWM frekansı (Hz)
#define LEDC_RESOLUTION 8 // 8-bit çözünürlük (0-255)
#define USE_PWM true      // PWM kullanılsın
#define BACKLIGHT_LEDC_CHANNEL 1   // S3: kanal 0 WiFi ile cakismasin

#define TFT_WIDTH  320
#define TFT_HEIGHT 172

// Sağ üst: [pil yazısı] [WiFi 17x13] [pil ikonu 40x20]
#define TOPBAR_MARGIN   6
#define TOPBAR_GAP      4
#define BAT_ICON_W      40
#define BAT_ICON_H      20
#define WIFI_ICON_W     17
#define WIFI_ICON_H     13

static int batteryIconX() {
  return TFT_WIDTH - BAT_ICON_W - TOPBAR_MARGIN;
}

static int batteryIconY() {
  return TOPBAR_MARGIN;
}

static int wifiIconX() {
  return batteryIconX() - WIFI_ICON_W - TOPBAR_GAP;
}
static int wifiIconY() {
  return batteryIconY() + (BAT_ICON_H - WIFI_ICON_H) / 2;
}

static int topBarReservedWidth() {
  return BAT_ICON_W + WIFI_ICON_W + TOPBAR_GAP + TOPBAR_MARGIN + 8;
}

#if CONFIG_IDF_TARGET_ESP32S3
SPIClass tftSpi(HSPI);
Adafruit_ST7789 tft(&tftSpi, TFT_CS, TFT_DC, TFT_RST);
#else
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
#endif

#if USE_DHT11
DHT dht(DHT_PIN, DHT11);
#endif

static void initDisplayBacklight() {
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  if (USE_PWM) {
    ledcSetup(BACKLIGHT_LEDC_CHANNEL, LEDC_FREQ, LEDC_RESOLUTION);
    ledcAttachPin(TFT_BACKLIGHT, BACKLIGHT_LEDC_CHANNEL);
    ledcWrite(BACKLIGHT_LEDC_CHANNEL, 200);
  }
  Serial.println("Backlight acik (GPIO2)");
}

static void initDisplay() {
  initDisplayBacklight();
  delay(50);

#if CONFIG_IDF_TARGET_ESP32S3
  tftSpi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tftSpi.setFrequency(27000000);
  Serial.println("TFT SPI: HSPI 27MHz");
#else
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
#endif

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(10);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);

  tft.init(TFT_HEIGHT, TFT_WIDTH);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  Serial.printf("TFT ST7789 %dx%d init OK\n", TFT_WIDTH, TFT_HEIGHT);
  Serial.printf("  CS=%d DC=%d RST=%d SCK=%d MOSI=%d BL=%d\n",
                TFT_CS, TFT_DC, TFT_RST, TFT_SCLK, TFT_MOSI, TFT_BACKLIGHT);
}

static void printPinMap() {
  Serial.println("--- Pin ozeti (S3 Super Mini) ---");
  Serial.println("TFT: CS=10 DC=7 RST=5 SCK=4 MOSI=6 BL=2");
  Serial.println("Pil ADC=1 | Enc 8/9/3 | CHRG=13 STDBY=12 | Buzzer=11");
#if CONFIG_IDF_TARGET_ESP32S3
  Serial.println("Onboard RGB WS2812: GPIO48");
#endif
}

static void printChipInfo() {
  Serial.println("--- Chip / bellek ---");
  Serial.printf("Model: %s  rev=%d  cores=%d\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("Flash: %u byte (%.1f MB)\n",
                ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1048576.0f);
  Serial.printf("PSRAM: %u byte\n", ESP.getPsramSize());
  Serial.printf("Heap:  %u byte\n", ESP.getHeapSize());
  if (ESP.getPsramSize() == 0) {
    Serial.println("UYARI: PSRAM=0 — platformio.ini yanlis profil olabilir!");
    Serial.println("  N4R2/FH4R2 -> env esp32s3_super_mini");
    Serial.println("  N16R8      -> env esp32s3_super_mini_16mb");
  }
}

static void buzzerBeep(uint16_t durationMs = 25) {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

static void buzzerEncoderTick() {
  buzzerBeep(18);
}

static void buzzerEncoderClick() {
  buzzerBeep(35);
}

// =====================================================
unsigned long lastTimeUpdate = 0;
const unsigned long timeInterval = 1000;

String prevTime = "";
String prevDate = "";
String prevTemp = "";
String prevHum = "";       // Geriye uyumluluk: prevDetail ile ayni
String prevRegion = "";
String prevDetail = "";
String weatherRegion = "";
String weatherCondition = "";
float weatherTempC = NAN;
bool locationValid = false;
bool tempValid = false;
bool conditionValid = false;
bool astronValid = false;
int sunriseHour = 6;
int sunriseMin = 0;
int sunsetHour = 20;
int sunsetMin = 0;
float geoLat = NAN;
float geoLon = NAN;
unsigned long lastLocationFetch = 0;
unsigned long lastTempFetch = 0;
unsigned long lastWeatherAttempt = 0;
const unsigned long weatherFetchInterval = 900000UL;  // 15 dk
const unsigned long weatherRetryInterval = 10000UL; // basarisizda 10 sn
static bool otaRgbActive = false;
String prevBattery = "";
String prevChargeStatus = "";
static int lastBatteryPctForIcon = 0;
String ipAddress = "";
static String prevWifiStatusLine = "";
static bool bottomBarVersionDrawn = false;
static bool bottomBarMenuDrawn = false;

static void resetBottomStatusBarCache() {
  bottomBarVersionDrawn = false;
  bottomBarMenuDrawn = false;
  prevWifiStatusLine = "";
}

static void resetLeftInfoPanelCache() {
  prevTemp = "";
  prevRegion = "";
  prevDetail = "";
  prevHum = "";
}

static void resetMainPageCaches() {
  resetBottomStatusBarCache();
  resetLeftInfoPanelCache();
}

static void redrawHomeScreenWidgets();

unsigned long lastBatteryRead = 0;
const unsigned long batteryReadInterval = 30000;  // 30 saniyede bir ADC oku
int prevOTAPercent = -1;  // OTA progress takibi için

// 🔹 WiFi Yeniden Bağlanma
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 10000;  // 10 saniyede bir kontrol (CPU'yu yormaz)
bool wifiReconnecting = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 5000;  // 5 saniyede bir yeniden bağlanma dene

// 🔹 Encoder Değişkenleri
volatile int encoderPosition = 0;
volatile int lastEncoderPosition = 0;
int lastCLKState = HIGH;
bool encoderButtonPressed = false;
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;  // Debounce süresi artırıldı

// 🔹 Menü Sistemi
int currentMenuPage = 0;  // 0:Ana 1:Ayarlar 2:Parlaklik 3:WiFi 4:Istatistik 5:Sistem 6:WiFiSifirla 7:Dil 8:Pil
int menuItem = 0;

// 🔹 İstatistikler
float minTemp = 999.0;
float maxTemp = -999.0;
float minHum = 999.0;
float maxHum = -999.0;
float sumTemp = 0.0;
float sumHum = 0.0;
float minChipTempC = 999.0f;
float maxChipTempC = -999.0f;
unsigned long readingCount = 0;
unsigned long wifiConnectedTime = 0;  // WiFi bağlantı zamanı (millis)
bool wifiWasConnected = false;

// 🔹 Sistem Bilgileri
unsigned long systemStartTime = 0;  // Sistem başlangıç zamanı
unsigned long totalUptimeSeconds = 0;  // Kayıtlı toplam çalışma süresi (saniye)
Preferences prefs;  // Preferences API için
unsigned long lastUptimeSave = 0;  // Son kayıt zamanı
const unsigned long uptimeSaveInterval = 60000;  // 60 saniyede bir kaydet

// 🔹 WiFiManager
WiFiManager wifiManager;
bool wifiSetupMode = false;  // AP modu aktif mi?
WebServer server(80);  // Web sunucusu port 80'de
int wifiResetConfirmItem = 0;  // 0 = Evet, 1 = Hayır

// 🔹 Dil Sistemi
int currentLanguage = 0;  // 0 = Türkçe, 1 = İngilizce (varsayılan: Türkçe)
int languageSelectionItem = 0;  // Dil seçim sayfası için

// 🔹 Parlaklık Kontrolü
int brightness = 128;  // 0-255 arası (varsayılan: %50)
const int brightnessMin = 20;
const int brightnessMax = 255;

// 🔹 Ekran Koruyucu (Screen Saver)
bool screenSaverActive = false;
unsigned long lastActivityTime = 0;
unsigned long screenSaverStartTime = 0;  // Ekran koruyucunun aktif olduğu zaman
const unsigned long screenSaverTimeout = 60000;  // 1 dakika (60000 ms)
const unsigned long deepSleepTimeout = 300000;  // 5 dakika (300000 ms) - Deep Sleep için
int savedBrightness = 128;  // Normal parlaklığı saklamak için
bool needRedraw = false;  // Ekran koruyucudan çıkınca yeniden çizmek için

const char* otaName = "sp_dashboard";
const char* otaPass = "1234";

// İleri bildirimler (PlatformIO .cpp derlemesi için)
bool isCharging();
bool isChargeComplete();
void drawBatteryIcon();
void updateLogoByRSSI();
void setBrightness(int level);
void saveTotalUptime();
void loadTotalUptime();
void saveLanguage();
void loadLanguage();
String getTitleText(int index);
String getText(const char* tr, const char* en);
String getText(const char* tr, const char* en);
String getMenuText(int index);
void showBatteryHealthMenu(bool reset = false);
String getBatteryHealthLabel(int pct, float vBat, uint16_t* colorOut);
void drawWifiSetupScreen();
void drawWifiSetupStatus(const char* status);
void drawWifiStatusLine();
void startWifiSetupPortal();
void exitWifiSetupMode();
void sanitizeWifiPrefs();
bool hasSavedWifiCredentials();
void drawMainDashboard();
void drawDHT11Data(bool forceRedraw = false);
void initOnboardRgb();
void updateOnboardRgb();

static String jsonExtractString(const String& json, const char* key) {
  String q = String("\"") + key + "\":\"";
  int i = json.indexOf(q);
  if (i < 0) return "";
  i += q.length();
  int j = json.indexOf('"', i);
  if (j < 0) return "";
  return json.substring(i, j);
}

static float jsonExtractFloat(const String& json, const char* key) {
  String q = String("\"") + key + "\":";
  int pos = 0;
  while (pos < (int)json.length()) {
    int i = json.indexOf(q, pos);
    if (i < 0) {
      return NAN;
    }
    i += q.length();
    while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\t')) {
      i++;
    }
    // current_units icinde "weather_code":"wmo code" gibi metinleri atla
    if (i < (int)json.length() && json[i] == '"') {
      pos = i + 1;
      continue;
    }
    int j = i;
    while (j < (int)json.length()) {
      char c = json[j];
      if (isdigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E') {
        j++;
      } else {
        break;
      }
    }
    if (j == i) {
      pos = i + 1;
      continue;
    }
    return json.substring(i, j).toFloat();
  }
  return NAN;
}

static float jsonExtractNumber(const String& json, const char* key) {
  float v = jsonExtractFloat(json, key);
  if (!isnan(v)) {
    return v;
  }
  String s = jsonExtractString(json, key);
  if (s.length() > 0) {
    return s.toFloat();
  }
  return NAN;
}

static String jsonExtractDailyArrayFirst(const String& json, const char* key) {
  String q = String("\"") + key + "\":[\"";
  int i = json.indexOf(q);
  if (i < 0) {
    return "";
  }
  i += q.length();
  int j = json.indexOf('"', i);
  if (j < 0) {
    return "";
  }
  return json.substring(i, j);
}

static bool parseIsoLocalHm(const String& iso, int& hourOut, int& minOut) {
  int tPos = iso.indexOf('T');
  if (tPos < 0) {
    return false;
  }
  int colon = iso.indexOf(':', tPos);
  if (colon < 0) {
    return false;
  }
  hourOut = iso.substring(tPos + 1, colon).toInt();
  int colon2 = iso.indexOf(':', colon + 1);
  if (colon2 > colon) {
    minOut = iso.substring(colon + 1, colon2).toInt();
  } else {
    minOut = iso.substring(colon + 1).toInt();
  }
  return true;
}

static double julianDay(int y, int m, int d) {
  if (m <= 2) {
    y -= 1;
    m += 12;
  }
  int A = y / 100;
  int B = 2 - A + (A / 4);
  return (int)(365.25 * (y + 4716)) + (int)(30.6001 * (m + 1)) + d + B - 1524.5;
}

static float moonPhaseIndex(int y, int m, int d) {
  const double synodic = 29.530588853;
  const double refNewMoonJd = 2451550.26;
  double jd = julianDay(y, m, d) + 0.5;
  double age = fmod(jd - refNewMoonJd, synodic);
  if (age < 0.0) {
    age += synodic;
  }
  return (float)(age / synodic);
}

static const char* moonPhaseToText(float phase) {
  if (phase < 0.03f || phase > 0.97f) {
    return "Yeni ay";
  }
  if (phase < 0.22f) {
    return "Hilal";
  }
  if (phase < 0.30f) {
    return "Ilk yarim";
  }
  if (phase < 0.47f) {
    return "Sisik ay";
  }
  if (phase < 0.53f) {
    return "Dolunay";
  }
  if (phase < 0.70f) {
    return "Sisik ay";
  }
  if (phase < 0.78f) {
    return "Son yarim";
  }
  return "Hilal";
}

static bool isNightTime(const struct tm& t) {
  if (!astronValid) {
    return false;
  }
  int nowMin = t.tm_hour * 60 + t.tm_min;
  int riseMin = sunriseHour * 60 + sunriseMin;
  int setMin = sunsetHour * 60 + sunsetMin;
  return (nowMin >= setMin) || (nowMin < riseMin);
}

static float parseWttrTempLine(const String& raw) {
  String t = raw;
  t.trim();
  t.replace("\r", "");
  t.replace("\n", "");
  t.replace("°C", "");
  t.replace("C", "");
  t.replace("+", "");
  t.trim();
  if (t.length() == 0) {
    return NAN;
  }
  return t.toFloat();
}

static void configureWifiDns() {
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!netif) {
    return;
  }
  esp_netif_dns_info_t dns;
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("8.8.8.8");
  esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
  dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("1.1.1.1");
  esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
}

static bool httpRequest(const char* url, String& bodyOut, int timeoutMs = 20000) {
  bodyOut = "";
  bool useHttps = (strncmp(url, "https://", 8) == 0);

  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);

  WiFiClient plain;
  WiFiClientSecure secure;
  bool begun = false;

  if (useHttps) {
    secure.setInsecure();
    secure.setTimeout(timeoutMs / 1000);
    begun = http.begin(secure, url);
  } else {
    plain.setTimeout(timeoutMs / 1000);
    begun = http.begin(plain, url);
  }

  if (!begun) {
    Serial.printf("HTTP begin: %s\n", url);
    return false;
  }

  http.addHeader("User-Agent", WEATHER_HTTP_UA);
  http.addHeader("Accept", "*/*");
  http.addHeader("Connection", "close");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP %d (%s): %s\n", code, http.errorToString(code).c_str(), url);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream) {
    bodyOut.reserve(512);
    unsigned long t0 = millis();
    while (http.connected() && bodyOut.length() < WEATHER_BODY_MAX &&
           (millis() - t0) < (unsigned long)timeoutMs) {
      while (stream->available() && bodyOut.length() < WEATHER_BODY_MAX) {
        bodyOut += (char)stream->read();
      }
      if (!stream->available()) {
        delay(10);
      }
    }
  } else {
    bodyOut = http.getString();
    if (bodyOut.length() > WEATHER_BODY_MAX) {
      bodyOut = bodyOut.substring(0, WEATHER_BODY_MAX);
    }
  }

  http.end();
  return bodyOut.length() > 0;
}

static int clockAreaLeft() {
  return LEFT_INFO_W;
}

static int clockAreaWidth() {
  return TFT_WIDTH - topBarReservedWidth() - LEFT_INFO_W;
}

static bool fetchLocationData() {
  weatherRegion = WEATHER_REGION_NAME;
  geoLat = WEATHER_LAT;
  geoLon = WEATHER_LON;
  locationValid = true;
  lastLocationFetch = millis();
  prevHum = "";
  Serial.println("Konum: Umraniye (sabit)");
  return true;
}

// WMO hava kodu -> kisa Turkce metin (open-meteo weather_code)
static const char* wmoCodeToText(int code) {
  switch (code) {
    case 0: return "Gunesli";
    case 1: return "Az bulutlu";
    case 2: return "Parcali bulut";
    case 3: return "Kapali";
    case 45:
    case 48: return "Sisli";
    case 51:
    case 53:
    case 55: return "Cisenti";
    case 56:
    case 57: return "Donan cisenti";
    case 61: return "Hafif yagmur";
    case 63: return "Yagmurlu";
    case 65: return "Siddetli yagmur";
    case 66:
    case 67: return "Donan yagmur";
    case 71: return "Hafif kar";
    case 73: return "Karli";
    case 75: return "Siddetli kar";
    case 77: return "Kar taneleri";
    case 80: return "Sagak yagmur";
    case 81: return "Kuvvetli sagak";
    case 82: return "Cok siddetli";
    case 85:
    case 86: return "Kar yagisi";
    case 95: return "Firtina";
    case 96:
    case 99: return "Dolu";
    default: return "";
  }
}

static String wttrConditionToTurkish(const String& raw) {
  String s = raw;
  s.trim();
  s.replace("\r", "");
  s.replace("\n", "");
  s.toLowerCase();

  if (s.indexOf("thunder") >= 0) return "Firtina";
  if (s.indexOf("hail") >= 0) return "Dolu";
  if (s.indexOf("sleet") >= 0 || s.indexOf("ice pellets") >= 0) return "Karla karisik";
  if (s.indexOf("snow") >= 0) return "Karli";
  if (s.indexOf("heavy rain") >= 0) return "Siddetli yagmur";
  if (s.indexOf("light rain") >= 0 || s.indexOf("patchy rain") >= 0) return "Hafif yagmur";
  if (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0) return "Yagmurlu";
  if (s.indexOf("fog") >= 0 || s.indexOf("mist") >= 0) return "Sisli";
  if (s.indexOf("overcast") >= 0) return "Kapali";
  if (s.indexOf("partly cloudy") >= 0) return "Parcali bulut";
  if (s.indexOf("cloudy") >= 0) return "Bulutlu";
  if (s.indexOf("sunny") >= 0 || s.indexOf("clear") >= 0) return "Gunesli";

  if (raw.length() > 14) {
    return raw.substring(0, 14);
  }
  return raw;
}

// ---------- SICAKLIK + HAVA DURUMU (open-meteo, tek istek) ----------
static bool fetchOpenMeteoCurrent(float lat, float lon, float& tempOut, int& wmoCodeOut) {
  char url[300];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?"
           "latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code"
           "&daily=sunrise,sunset&forecast_days=1&timezone=auto",
           lat, lon);

  String body;
  if (!httpRequest(url, body)) {
    return false;
  }
  Serial.println(body);

  float temp = jsonExtractFloat(body, "temperature_2m");
  if (isnan(temp)) {
    return false;
  }
  tempOut = temp;

  float codeF = jsonExtractFloat(body, "weather_code");
  wmoCodeOut = isnan(codeF) ? -1 : (int)(codeF + 0.5f);

  String riseIso = jsonExtractDailyArrayFirst(body, "sunrise");
  String setIso = jsonExtractDailyArrayFirst(body, "sunset");
  int rh = 0, rm = 0, sh = 0, sm = 0;
  if (parseIsoLocalHm(riseIso, rh, rm) && parseIsoLocalHm(setIso, sh, sm)) {
    sunriseHour = rh;
    sunriseMin = rm;
    sunsetHour = sh;
    sunsetMin = sm;
    astronValid = true;
    Serial.printf("Gunes dogumu: %02d:%02d  batimi: %02d:%02d\n",
                  sunriseHour, sunriseMin, sunsetHour, sunsetMin);
  }

  return true;
}

static bool fetchConditionWttr(float lat, float lon, String& condOut) {
  char url[96];
  snprintf(url, sizeof(url), "http://wttr.in/~%.2f,%.2f?format=%%C", lat, lon);

  String body;
  if (!httpRequest(url, body, 12000)) {
    return false;
  }
  body.trim();
  Serial.print("wttr durum: ");
  Serial.println(body);
  if (body.length() == 0) {
    return false;
  }
  condOut = wttrConditionToTurkish(body);
  return condOut.length() > 0;
}

static bool fetchTempWttr(float lat, float lon, float& tempOut) {
  char url[96];
  snprintf(url, sizeof(url), "http://wttr.in/~%.2f,%.2f?format=%%t", lat, lon);

  String body;
  if (!httpRequest(url, body, 12000)) {
    return false;
  }
  Serial.print("wttr sicaklik: ");
  Serial.println(body);

  float temp = parseWttrTempLine(body);
  if (isnan(temp)) {
    return false;
  }
  tempOut = temp;
  return true;
}

static bool fetchTempWttrAuto(float& tempOut) {
  String body;
  if (!httpRequest("http://wttr.in/?format=%t", body, 12000)) {
    return false;
  }
  Serial.print("wttr IP sicaklik: ");
  Serial.println(body);

  float temp = parseWttrTempLine(body);
  if (isnan(temp)) {
    return false;
  }
  tempOut = temp;
  return true;
}

static bool fetchTemperatureData() {
  Serial.println("--- Hava API (sicaklik + durum) ---");

  float temp = NAN;
  int wmoCode = -1;
  bool ok = false;
  bool gotCondition = false;

  if (locationValid && !isnan(geoLat) && !isnan(geoLon)) {
    ok = fetchOpenMeteoCurrent(geoLat, geoLon, temp, wmoCode);
    if (ok && wmoCode >= 0) {
      const char* txt = wmoCodeToText(wmoCode);
      if (txt[0] != '\0') {
        weatherCondition = txt;
        conditionValid = true;
        gotCondition = true;
        Serial.print("Hava durumu (WMO ");
        Serial.print(wmoCode);
        Serial.print("): ");
        Serial.println(weatherCondition);
      }
    }
    if (!ok) {
      ok = fetchTempWttr(geoLat, geoLon, temp);
    }
    if (!gotCondition) {
      String wttrCond;
      if (fetchConditionWttr(geoLat, geoLon, wttrCond)) {
        weatherCondition = wttrCond;
        conditionValid = true;
        gotCondition = true;
        Serial.print("Hava durumu (wttr): ");
        Serial.println(weatherCondition);
      }
    }
  }

  if (!ok) {
    ok = fetchTempWttrAuto(temp);
  }

  if (!ok || isnan(temp)) {
    Serial.println("Sicaklik alinamadi");
    conditionValid = false;
    weatherCondition = "";
    return false;
  }

  weatherTempC = temp;
  tempValid = true;
  lastTempFetch = millis();
  prevTemp = "";
  prevHum = "";
  prevRegion = "";
  prevDetail = "";

  if (!gotCondition) {
    conditionValid = false;
    weatherCondition = "";
  }

  Serial.print("Sicaklik: ");
  Serial.print(weatherTempC, 1);
  Serial.println(" C");
  return true;
}

static bool waitForTimeSync() {
  struct tm timeinfo;
  for (int i = 0; i < 15; i++) {
    if (getLocalTime(&timeinfo)) {
      return true;
    }
    delay(400);
  }
  return false;
}

void invalidateRegionalWeather() {
  locationValid = false;
  tempValid = false;
  conditionValid = false;
  astronValid = false;
  weatherTempC = NAN;
  weatherRegion = "";
  weatherCondition = "";
  geoLat = NAN;
  geoLon = NAN;
  lastLocationFetch = 0;
  lastTempFetch = 0;
  lastWeatherAttempt = 0;
  prevTemp = "";
  prevHum = "";
  prevRegion = "";
  prevDetail = "";
}

bool fetchRegionalWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  configureWifiDns();
  delay(300);

  bool gotLoc = fetchLocationData();
  bool gotTemp = fetchTemperatureData();

  if (gotLoc || gotTemp) {
    if (currentMenuPage == 0 && !screenSaverActive) {
      drawDHT11Data();
    }
  }

  return gotLoc && gotTemp;
}

void updateRegionalWeather() {
#if USE_DHT11
  return;
#endif
  if (wifiSetupMode || WiFi.status() != WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();

  bool needLoc = !locationValid ||
                 (now - lastLocationFetch >= weatherFetchInterval);
  bool needTemp = !tempValid ||
                  (now - lastTempFetch >= weatherFetchInterval);
  bool needCondition = !conditionValid && tempValid;

  if (!needLoc && !needTemp && !needCondition) {
    return;
  }

  if (lastWeatherAttempt != 0 &&
      (now - lastWeatherAttempt < weatherRetryInterval)) {
    return;
  }

  lastWeatherAttempt = now;

  if (needLoc) {
    fetchLocationData();
  }
  if (needTemp || needCondition) {
    fetchTemperatureData();
  }

  if ((!locationValid || !tempValid || !conditionValid) &&
      currentMenuPage == 0 && !screenSaverActive) {
    drawDHT11Data();
  }
}

// =======================================================
//  🟦 OTA BAŞLATMA
// =======================================================
static bool otaInitialized = false;

void resetOTA() {
  // ArduinoOTA.begin() tekrar cagrilmamali — sadece bayrak (gecici kopma)
}

void startOTA() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (otaInitialized) {
    return;
  }

  WiFi.setSleep(WIFI_PS_NONE);

  Serial.println("OTA yukleme servisi baslatiliyor...");

  if (!MDNS.begin(otaName)) {
    Serial.println("mDNS baslatilamadi (IP ile yukleme yine calisir)");
  }

  ArduinoOTA.setHostname(otaName);
  ArduinoOTA.setPassword(otaPass);
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setTimeout(120000);
  ArduinoOTA.setRebootOnSuccess(true);

  ArduinoOTA
      .onStart([]() {
        Serial.println("OTA Basladi!");
        otaRgbActive = true;
        prevOTAPercent = -1;
        tft.fillScreen(ST77XX_BLACK);
        
        // Başlık
        tft.setCursor(10, 30);
        tft.setTextColor(ST77XX_CYAN);
        tft.setTextSize(2);
        tft.println("OTA Yukleniyor");
        
        // Progress bar çerçevesi
        tft.drawRect(10, 70, TFT_WIDTH - 20, 25, ST77XX_WHITE);
      })
      .onEnd([]() {
        Serial.println("OTA Tamamlandi! Yeniden baslatiliyor...");
        prevOTAPercent = -1;
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(10, 70);
        tft.setTextColor(ST77XX_GREEN);
        tft.setTextSize(2);
        tft.println("OTA OK");
        delay(800);
        ESP.restart();
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        yield();
        int percent = (progress * 100) / total;
        
        int barWidth = TFT_WIDTH - 22;
        int filledWidth = (progress * barWidth) / total;
        
        if (prevOTAPercent != percent) {
          int prevFilledWidth = (prevOTAPercent >= 0) ? (prevOTAPercent * barWidth) / 100 : 0;
          
          // Sadece yeni eklenen kısmı yeşil yap
          if (filledWidth > prevFilledWidth) {
            tft.fillRect(11 + prevFilledWidth, 71, filledWidth - prevFilledWidth, 23, ST77XX_GREEN);
          }
          
          // Yüzde bilgisini sadece değiştiğinde güncelle
          String percentStr = String(percent) + "% tamamlandi";
          tft.setTextSize(2);
          tft.setTextColor(ST77XX_WHITE);
          
          int16_t x1, y1;
          uint16_t w, h;
          tft.getTextBounds(percentStr, 0, 0, &x1, &y1, &w, &h);
          
          int x = (TFT_WIDTH - w) / 2;
          int y = 105;
          
          tft.fillRect(x - 2, y - 2, w + 4, h + 4, ST77XX_BLACK);
          tft.setCursor(x, y);
          tft.println(percentStr);
          
          prevOTAPercent = percent;
        }
      })
      .onError([](ota_error_t error) {
        otaRgbActive = false;
        Serial.printf("Hata[%u]\n", error);
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(10, 70);
        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(2);
        tft.print("OTA HATASI: ");
        tft.println(error);
      });

  ArduinoOTA.begin();
  otaInitialized = true;
  Serial.println("========================================");
  Serial.print("OTA hazir | IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname: ");
  Serial.print(otaName);
  Serial.println(".local");
  Serial.print("Sifre: ");
  Serial.println(otaPass);
  Serial.println("PlatformIO: -e esp32s3_super_mini_ota");
  Serial.println("upload_port = ekrandaki IP ile ayni olmali");
  Serial.println("========================================");
}

// =======================================================
// 🟦 VERSİYON YAZISI (SAĞ-ALT)
// =======================================================
void drawVersionText() {
  if (wifiSetupMode || currentMenuPage != 0 || screenSaverActive) {
    bottomBarVersionDrawn = false;
    return;
  }
  if (bottomBarVersionDrawn) {
    return;
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  int16_t x1, y1;
  uint16_t w, h;

  tft.getTextBounds(VERSION_TEXT, 0, 0, &x1, &y1, &w, &h);

  int x = TFT_WIDTH - w - 6;
  int y = TFT_HEIGHT - h - 20;

  tft.fillRect(x - 2, y - 2, w + 6, h + 4, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.println(VERSION_TEXT);
  bottomBarVersionDrawn = true;
}

// =======================================================
//  🟦 AÇILIŞ LOGOSU
// =======================================================
void showSplashScreen() {
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  int x = (tft.width() - 170) / 2;
  int y = (tft.height() - 172) / 2;

  tft.drawRGBBitmap(x, y, epd_bitmap_SP, 170, 172);
  delay(3000);

  tft.fillScreen(ST77XX_BLACK);
}

// =======================================================
//  🟦 EKRAN KORUYUCU (SCREEN SAVER)
// =======================================================
void showScreenSaver() {
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  int x = (tft.width() - 170) / 2;
  int y = (tft.height() - 172) / 2;

  tft.drawRGBBitmap(x, y, epd_bitmap_SP, 170, 172);
}

// =======================================================
//  🟦 EKRAN KORUYUCU AKTİVİTE KAYDI
// =======================================================
void updateActivity() {
  lastActivityTime = millis();
  
  // Eğer ekran koruyucu aktifse, kapat
  if (screenSaverActive) {
    screenSaverActive = false;
    screenSaverStartTime = 0;  // Ekran koruyucu zamanlayıcısını sıfırla
    // Normal parlaklığa geri dön
    brightness = savedBrightness;
    setBrightness(brightness);
    // Ekranı hemen temizle (ekran koruyucu görüntüsü kalkması için)
    tft.fillScreen(ST77XX_BLACK);
    needRedraw = true;  // Ekranı yeniden çiz
    Serial.println("Ekran koruyucu devre disi - normal moda donuldu");
  }
}

// =======================================================
//  🟦 DEEP SLEEP MODUNA GEÇ
// =======================================================
void enterDeepSleep() {
  Serial.println("5 dakika ekran koruyucuda kalindi - Deep Sleep moduna geçiliyor...");
  
  // Önce tüm önemli verileri kaydet
  saveTotalUptime();
  
  // Ekranı kapat (mesaj göster)
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(20, 60);
  tft.println("Deep Sleep");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 90);
  tft.println("Butona basiniz");
  
  delay(2000);  // Mesajı göster
  
  // Encoder butonunu (GPIO3) wake-up pini olarak ayarla (RTC GPIO)
  // Buton pull-up olduğu için basıldığında LOW olur
  
  // Encoder interrupt'ını devre dışı bırak (Deep Sleep için gerekli)
  detachInterrupt(digitalPinToInterrupt(ENCODER_CLK));
  
  // GPIO'yu INPUT olarak yapılandır (Deep Sleep için gerekli)
  // Pull-up aktif olmalı (buton basıldığında LOW olacak)
  pinMode(ENCODER_SW, INPUT_PULLUP);
  delay(200);  // Yapılandırmanın tamamlanması için bekle
  
  // Yedek olarak timer wake-up ekle (1 saat sonra otomatik uyanır)
  // Eğer GPIO wake-up çalışmazsa en azından timer ile uyanır
  esp_sleep_enable_timer_wakeup(3600000000ULL);  // 1 saat = 3600 saniye * 1000000 mikrosaniye
  
  // GPIO wake-up: C3 = gpio_wakeup, S3 = ext0 (encoder SW, active LOW)
#if CONFIG_IDF_TARGET_ESP32C3
  esp_deep_sleep_enable_gpio_wakeup((1ULL << ENCODER_SW), ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_sleep_enable_ext0_wakeup((gpio_num_t)ENCODER_SW, 0);
#endif
  
  Serial.print("Deep Sleep basladi - GPIO");
  Serial.print(ENCODER_SW);
  Serial.println(" (LOW) butonu ile uyandirilabilir");
  Serial.println("NOT: Butona basip BASILI TUTUNUZ");
  Serial.println("Yedek: 1 saat sonra otomatik uyanir");
  Serial.flush();  // Seri port verilerini gönder
  
  delay(300);  // Son kontrol için bekleme
  
  // Deep Sleep'e geç
  esp_deep_sleep_start();
  // Buradan sonra kod çalışmaz, cihaz uyanınca setup() tekrar çalışır
}

// =======================================================
//  🟦 EKRAN KORUYUCU KONTROLÜ
// =======================================================
void checkScreenSaver() {
  unsigned long now = millis();
  
  // Menü açıkken veya OTA güncellemesi sırasında ekran koruyucuyu aktif etme
  if (currentMenuPage != 0) {
    lastActivityTime = now;  // Menüdeyken aktiviteyi güncelle
    screenSaverStartTime = 0;  // Ekran koruyucu zamanlayıcısını sıfırla
    return;
  }
  
  // Ekran koruyucu aktif değilse kontrol et
  if (!screenSaverActive) {
    // Son aktiviteden bu yana geçen süre
    unsigned long inactiveTime = now - lastActivityTime;
    
    if (inactiveTime >= screenSaverTimeout) {
      // Ekran koruyucuyu aktif et
      screenSaverActive = true;
      screenSaverStartTime = now;  // Ekran koruyucunun başlangıç zamanını kaydet
      savedBrightness = brightness;  // Mevcut parlaklığı kaydet
      
      // Parlaklığı %10'a düşür (255'in %10'u = 25.5, yaklaşık 26)
      int screenSaverBrightness = (brightnessMax * 10) / 100;  // %10
      if (screenSaverBrightness < brightnessMin) screenSaverBrightness = brightnessMin;
      setBrightness(screenSaverBrightness);
      
      // Logoyu göster
      showScreenSaver();
      
      Serial.println("Ekran koruyucu aktif");
    }
  } else {
    // Ekran koruyucu aktif - Deep Sleep kontrolü yap
    if (screenSaverStartTime > 0) {
      unsigned long screenSaverActiveTime = now - screenSaverStartTime;
      
      if (screenSaverActiveTime >= deepSleepTimeout) {
        // 5 dakika ekran koruyucuda kaldı - Deep Sleep'e geç
        // ANCAK: WiFi bağlıyken Deep Sleep'e geçme (OTA güncellemesi yapılabilsin)
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("WiFi bagli - OTA guncellemesi yapilabilir, Deep Sleep geciktirildi");
          // Aktivite zamanını sıfırla (5 dakika daha bekle)
          screenSaverStartTime = now;
        } else {
          Serial.println("WiFi bagli degil - Deep Sleep'e geciliyor...");
          enterDeepSleep();
        }
      }
    }
  }
}

// =======================================================
void drawTimeAndDate(struct tm &timeinfo) {
  char bufDate[32];
  char bufTime[16];
  strftime(bufDate, sizeof(bufDate), "%d.%m.%Y", &timeinfo);
  strftime(bufTime, sizeof(bufTime), "%H:%M:%S", &timeinfo);

  String curTime = String(bufTime);
  String curDate = String(bufDate);

  int16_t x1, y1;
  uint16_t w, h;
  int areaW = clockAreaWidth();

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.getTextBounds(curDate, 0, 0, &x1, &y1, &w, &h);

  int dateX = clockAreaLeft() + (areaW - (int)w) / 2;
  int dateY = 14;

  if (curDate != prevDate) {
    tft.fillRect(dateX - 4, dateY, w + 8, h + 4, ST77XX_BLACK);
    tft.setCursor(dateX, dateY);
    tft.println(curDate);
    prevDate = curDate;
    drawBatteryIcon();
    updateLogoByRSSI();
  }

  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.getTextBounds(bufTime, 0, 0, &x1, &y1, &w, &h);

  int timeX = clockAreaLeft() + (areaW - (int)w) / 2;
  int timeY = dateY + h + 8;

  if (curTime != prevTime) {
    tft.fillRect(timeX - 4, timeY, w + 8, h + 6, ST77XX_BLACK);
    tft.setCursor(timeX, timeY);
    tft.println(curTime);
    prevTime = curTime;
    drawBatteryIcon();
    updateLogoByRSSI();
  }
}

// =======================================================
// 🟦 MENÜ BUTONU
// =======================================================
void drawMenuButton() {
  if (wifiSetupMode || currentMenuPage != 0 || screenSaverActive) {
    bottomBarMenuDrawn = false;
    return;
  }
  if (bottomBarMenuDrawn) {
    return;
  }

  tft.setTextSize(3);
  tft.setTextColor(ST77XX_CYAN);

  String menuText = "MENU";
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(menuText, 0, 0, &x1, &y1, &w, &h);

  int x = (TFT_WIDTH - w) / 2;
  int y = TFT_HEIGHT - h - 14;

  tft.fillRect(x - 2, y - 2, w + 4, h + 4, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.println(menuText);
  bottomBarMenuDrawn = true;
}

static void drawBottomStatusBar() {
  drawVersionText();
  drawMenuButton();
  drawWifiStatusLine();
}

void drawLogo(const unsigned char *bitmap, int w, int h) {
  int x = wifiIconX();
  int y = wifiIconY();
  tft.fillRect(x, y, w, h, ST77XX_BLACK);
  tft.drawBitmap(x, y, bitmap, w, h, ST77XX_WHITE);
}

void updateLogoByRSSI() {
  int x = wifiIconX();
  int y = wifiIconY();
  tft.fillRect(x, y, WIFI_ICON_W, WIFI_ICON_H, ST77XX_BLACK);

  if (WiFi.status() != WL_CONNECTED) {
    tft.drawRect(x + 2, y + 1, WIFI_ICON_W - 4, WIFI_ICON_H - 2, ST77XX_RED);
    tft.drawLine(x + 3, y + 2, x + WIFI_ICON_W - 3, y + WIFI_ICON_H - 2, ST77XX_RED);
    tft.drawLine(x + WIFI_ICON_W - 3, y + 2, x + 3, y + WIFI_ICON_H - 2, ST77XX_RED);
    return;
  }

  int rssi = WiFi.RSSI();
  if (rssi >= -60) {
    drawLogo(epd_bitmap_High, 17, 13);
  } else if (rssi >= -80) {
    drawLogo(epd_bitmap_Mid, 17, 13);
  } else {
    drawLogo(epd_bitmap_Low, 17, 13);
  }
}

// Pil ikonu: tamam → full | şarj → charge* | normal → battery* (aynı % kovaları)
const uint16_t* pickBatteryIconBitmap() {
  if (!isCharging() && isChargeComplete()) {
    return epd_bitmap_full;
  }

  int p = lastBatteryPctForIcon;
  bool charging = isCharging() && !isChargeComplete();

  if (p > 80) {
    return charging ? epd_bitmap_charge100 : epd_bitmap_battery100;
  }
  if (p > 60) {
    return charging ? epd_bitmap_charge80 : epd_bitmap_battery80;
  }
  if (p > 40) {
    return charging ? epd_bitmap_charge60 : epd_bitmap_battery60;
  }
  if (p > 20) {
    return charging ? epd_bitmap_charge40 : epd_bitmap_battery40;
  }
  return charging ? epd_bitmap_charge20 : epd_bitmap_battery20;
}

void drawBatteryIcon() {
  tft.fillRect(batteryIconX() - 2, batteryIconY() - 2, BAT_ICON_W + 4, BAT_ICON_H + 4, ST77XX_BLACK);
  tft.drawRGBBitmap(batteryIconX(), batteryIconY(), pickBatteryIconBitmap(), BAT_ICON_W, BAT_ICON_H);
}

// =======================================================
void drawIPAddress() {
  drawWifiStatusLine();
}

bool hasSavedWifiCredentials() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  prefs.end();
  ssid.trim();
  return ssid.length() > 0;
}

void drawWifiStatusLine() {
  if (wifiSetupMode || currentMenuPage != 0) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
    ipAddress = WiFi.localIP().toString();
  } else {
    if (prevWifiStatusLine.length() > 0) {
      prevWifiStatusLine = "";
      ipAddress = "";
      tft.fillRect(TFT_WIDTH - 130, TFT_HEIGHT - 18, 130, 16, ST77XX_BLACK);
    }
    return;
  }

  if (ipAddress == prevWifiStatusLine) {
    return;
  }
  prevWifiStatusLine = ipAddress;

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(ipAddress.c_str(), 0, 0, &x1, &y1, &w, &h);
  int x = TFT_WIDTH - w - 6;
  int y = TFT_HEIGHT - h - 6;

  tft.fillRect(x - 2, y - 2, w + 6, h + 4, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.println(ipAddress);
}

// =======================================================
// 🟦 BATARYA ÖLÇÜMÜ (voltaj bölücü GPIO1 / A0)
// =======================================================
static float lastBatAdcRawV = 0.0f;

float readBatteryVoltage() {
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  uint32_t sum_mV = 0;
  const int samples = 32;
  for (int i = 0; i < samples; i++) {
    sum_mV += analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }

  float v_adc = (sum_mV / (float)samples) / 1000.0f;
  lastBatAdcRawV = v_adc;
  float vBat = v_adc * BAT_VOLT_SCALE;

  if (vBat < 0.0f) {
    vBat = 0.0f;
  }
  if (vBat > BAT_DISPLAY_MAX_V) {
    vBat = BAT_DISPLAY_MAX_V;
  }

  return vBat;
}

int batteryPercent(float vBat) {
  if (vBat >= BAT_FULL_V) return 100;
  if (vBat <= BAT_EMPTY_V) return 0;
  return (int)((vBat - BAT_EMPTY_V) / (BAT_FULL_V - BAT_EMPTY_V) * 100.0f);
}

uint16_t batteryColor(int percent) {
  if (percent <= 10) return ST77XX_RED;
  if (percent <= 20) return ST77XX_YELLOW;
  return ST77XX_GREEN;
}

bool isCharging() {
  return digitalRead(PIN_CHRG) == LOW;
}

bool isChargeComplete() {
  return digitalRead(PIN_STDBY) == LOW;
}

#if CONFIG_IDF_TARGET_ESP32S3
static Adafruit_NeoPixel onboardRgb(1, ONBOARD_RGB_PIN, NEO_GRB + NEO_KHZ800);
static bool rgbReady = false;
static unsigned long lastRgbUpdate = 0;
static bool otaRgbPulse = false;

void initOnboardRgb() {
  onboardRgb.begin();
  onboardRgb.setBrightness(ONBOARD_RGB_BRIGHT);
  onboardRgb.clear();
  onboardRgb.show();
  rgbReady = true;
  Serial.println("Onboard RGB LED hazir (GPIO48)");
}

static void showOnboardRgb(uint8_t r, uint8_t g, uint8_t b) {
  if (!rgbReady) {
    return;
  }
  onboardRgb.setPixelColor(0, onboardRgb.Color(r, g, b));
  onboardRgb.show();
}

void updateOnboardRgb() {
  if (!rgbReady) {
    return;
  }

  unsigned long now = millis();
  if (otaRgbActive) {
    if (now - lastRgbUpdate >= 350) {
      lastRgbUpdate = now;
      otaRgbPulse = !otaRgbPulse;
      showOnboardRgb(0, 0, otaRgbPulse ? 180 : 30);
    }
    return;
  }

  if (now - lastRgbUpdate < 400) {
    return;
  }
  lastRgbUpdate = now;

  if (screenSaverActive) {
    showOnboardRgb(0, 0, 0);
    return;
  }

  if (isCharging() && !isChargeComplete()) {
    showOnboardRgb(255, 90, 0);
    return;
  }

  if (wifiSetupMode) {
    showOnboardRgb(255, 120, 0);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiReconnecting || hasSavedWifiCredentials()) {
      showOnboardRgb(255, 0, 0);
      return;
    }
    showOnboardRgb(0, 0, 0);
    return;
  }

  showOnboardRgb(0, 220, 0);
}
#else
void initOnboardRgb() {}
void updateOnboardRgb() {}
#endif

String getChargeStatusText() {
  bool chrg = isCharging();
  bool done = isChargeComplete();

  if (chrg && !done) {
    return getText("Sarj oluyor", "Charging");
  }
  if (!chrg && done) {
    return getText("Sarj tamam", "Charge complete");
  }
  if (!chrg && !done) {
    return getText("Sarj yok", "Not charging");
  }
  return getText("Sarj hata?", "Charge fault?");
}

uint16_t getChargeStatusColor() {
  if (isCharging()) {
    return ST77XX_YELLOW;
  }
  if (isChargeComplete()) {
    return ST77XX_GREEN;
  }
  return ST77XX_WHITE;
}

void drawChargeStatus() {
  String status = getChargeStatusText();

  if (status == prevChargeStatus) {
    return;
  }
  prevChargeStatus = status;

  tft.setTextSize(1);
  tft.setTextColor(getChargeStatusColor());

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);

  const int x = 6;
  const int y = CHARGE_STATUS_Y;

  tft.fillRect(x, y, LEFT_INFO_W - 8, h + 4, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.println(status);

  drawBatteryIcon();

  Serial.print("TP4056 CHRG=");
  Serial.print(isCharging() ? "1" : "0");
  Serial.print(" STDBY=");
  Serial.print(isChargeComplete() ? "1" : "0");
  Serial.print(" -> ");
  Serial.println(status);
}

static void clearMainPageBatteryLabels() {
  // Hava paneli (y<LEFT_INFO_H) ile cakismasin — sadece sarj satiri
  tft.fillRect(6, CHARGE_STATUS_Y - 2, LEFT_INFO_W - 8, 14, ST77XX_BLACK);
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(1);
  tft.getTextBounds("100% 4.20V", 0, 0, &x1, &y1, &w, &h);
  int textY = (TFT_HEIGHT - h) / 2;
  if (textY > LEFT_INFO_H + 4) {
    tft.fillRect(0, textY - 6, TFT_WIDTH, h + 12, ST77XX_BLACK);
  }
}

void drawBattery(bool forceRead, bool showLabel) {
  unsigned long now = millis();
  if (!forceRead && (now - lastBatteryRead < batteryReadInterval)) {
    return;
  }
  lastBatteryRead = now;

  float vBat = readBatteryVoltage();
  int pct = batteryPercent(vBat);
  lastBatteryPctForIcon = pct;
  String batStr = String(pct) + "% " + String(vBat, 2) + "V";

  bool forceDraw = forceRead;
  if (batStr == prevBattery && !forceDraw) {
    return;
  }
  prevBattery = batStr;

  drawBatteryIcon();

  if (!showLabel) {
    Serial.print("Batarya: ");
    Serial.print(vBat, 2);
    Serial.print(" V (");
    Serial.print(pct);
    Serial.println("%) [ikon only]");
    return;
  }

  tft.setTextSize(1);
  tft.setTextColor(batteryColor(pct));

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(batStr, 0, 0, &x1, &y1, &w, &h);

  int textX = (TFT_WIDTH - w) / 2;
  int textY = (TFT_HEIGHT - h) / 2;

  tft.fillRect(textX - 4, textY - 2, w + 8, h + 4, ST77XX_BLACK);
  tft.setCursor(textX, textY);
  tft.println(batStr);

  Serial.print("Batarya: ");
  Serial.print(vBat, 2);
  Serial.print(" V (");
  Serial.print(pct);
  Serial.print("%) | ADC ham: ");
  Serial.print(lastBatAdcRawV, 2);
  Serial.println(" V");
}

String getBatteryHealthLabel(int pct, float vBat, uint16_t* colorOut) {
  if (pct >= 90 || vBat >= 4.05f) {
    *colorOut = ST77XX_GREEN;
    return getText("Mukemmel", "Excellent");
  }
  if (pct >= 70 || vBat >= 3.90f) {
    *colorOut = ST77XX_GREEN;
    return getText("Iyi", "Good");
  }
  if (pct >= 40 || vBat >= 3.70f) {
    *colorOut = ST77XX_YELLOW;
    return getText("Orta", "Fair");
  }
  if (pct >= 15 || vBat >= 3.50f) {
    *colorOut = ST77XX_YELLOW;
    return getText("Dusuk", "Low");
  }
  *colorOut = ST77XX_RED;
  return getText("Kritik - Sarj edin", "Critical - Charge");
}

// Pil durum menusu: ana ekrandaki ikon adi (battery / charge / full)
static String getBatteryIconMenuLabel(int pct) {
  if (!isCharging() && isChargeComplete()) {
    return getText("Ekran ikonu: full (dolu)", "Screen icon: full");
  }
  bool charging = isCharging() && !isChargeComplete();
  int step = 20;
  if (pct > 80) step = 100;
  else if (pct > 60) step = 80;
  else if (pct > 40) step = 60;
  else if (pct > 20) step = 40;

  if (charging) {
    return getText("Ekran ikonu: charge", "Screen icon: charge") + String(step);
  }
  return getText("Ekran ikonu: battery", "Screen icon: battery") + String(step);
}

// =======================================================
// 🟦 PİL DURUMU / SAĞLIĞI SAYFASI
// =======================================================
void showBatteryHealthMenu(bool reset) {
  static bool firstDraw = true;
  const int lineHeight = 15;
  int yPos = 32;
  int16_t x1, y1;
  uint16_t w, h;

  if (reset) {
    firstDraw = true;
    return;
  }

  unsigned long now = millis();
  static unsigned long lastUpdate = 0;
  if (!firstDraw && (now - lastUpdate < 1500)) {
    return;
  }
  lastUpdate = now;

  lastBatteryRead = 0;
  float vBat = readBatteryVoltage();
  int pct = batteryPercent(vBat);
  lastBatteryPctForIcon = pct;

  bool chrg = isCharging();
  bool done = isChargeComplete();
  bool charging = chrg && !done;

  uint16_t healthColor;
  String healthLabel = getBatteryHealthLabel(pct, vBat, &healthColor);
  String chargeStatus = getChargeStatusText();
  uint16_t chargeColor = getChargeStatusColor();
  if (chrg && done) {
    chargeColor = ST77XX_RED;
  }

  if (firstDraw) {
    tft.fillScreen(ST77XX_BLACK);
    firstDraw = false;

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    String title = getTitleText(8);
    tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 6);
    tft.println(title);
  }

  tft.setTextSize(1);
  tft.fillRect(6, 28, TFT_WIDTH - 12, TFT_HEIGHT - 42, ST77XX_BLACK);

  auto drawLine = [&](const String& line, uint16_t color) {
    tft.setTextColor(color);
    tft.fillRect(8, yPos, TFT_WIDTH - 16, lineHeight, ST77XX_BLACK);
    tft.setCursor(8, yPos);
    tft.println(line);
    yPos += lineHeight;
  };

  // Sarj durumu (ana bilgi)
  drawLine(getText("Sarj: ", "Charge: ") + chargeStatus, chargeColor);

  drawLine(String(vBat, 2) + " V | " + String(pct) + "% | " + healthLabel, batteryColor(pct));
  drawLine(getBatteryIconMenuLabel(pct), ST77XX_CYAN);

  // TP4056 pinleri (L=aktif, H=pasif)
  String pins = getText("Pin ", "Pin ") +
                (chrg ? "CHRG-L " : "CHRG-H ") +
                (done ? "STDBY-L" : "STDBY-H");
  drawLine(pins, (chrg || done) ? ST77XX_YELLOW : ST77XX_WHITE);

  if (chrg && done) {
    drawLine(getText("! CHRG+STDBY: baglanti?", "! CHRG+STDBY both?"), ST77XX_RED);
  } else if (charging) {
    drawLine(getText("Sarj devam ediyor...", "Charging in progress..."), ST77XX_YELLOW);
  } else if (done) {
    drawLine(getText("Pil dolu.", "Battery full."), ST77XX_GREEN);
  } else if (pct < 20) {
    drawLine(getText("! Yakinda sarj edin", "! Charge soon"), ST77XX_RED);
  } else if (pct < 40) {
    drawLine(getText("Pil seviyesi dusuk", "Battery getting low"), ST77XX_YELLOW);
  }

  drawLine(getText("LiPo 3.7V | Canli 1.5sn", "LiPo 3.7V | Live 1.5s"), ST77XX_WHITE);

  tft.setTextColor(ST77XX_CYAN);
  String backText = getText("Buton: geri", "Button: back");
  tft.getTextBounds(backText, 0, 0, &x1, &y1, &w, &h);
  tft.fillRect((TFT_WIDTH - w) / 2 - 4, TFT_HEIGHT - 16, w + 8, h + 4, ST77XX_BLACK);
  tft.setCursor((TFT_WIDTH - w) / 2, TFT_HEIGHT - 14);
  tft.println(backText);
}

// =======================================================
// 🟦 DHT11 SICAKLIK VE NEM GÖSTERİMİ
// =======================================================
void drawDHT11Data(bool forceRedraw) {
  String tempStr;
  String regionStr;
  String detailStr;

#if USE_DHT11
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT11 okuma hatasi!");
    return;
  }

  if (temperature < minTemp) minTemp = temperature;
  if (temperature > maxTemp) maxTemp = temperature;
  if (humidity < minHum) minHum = humidity;
  if (humidity > maxHum) maxHum = humidity;
  sumTemp += temperature;
  sumHum += humidity;
  readingCount++;

  tempStr = String(temperature, 1) + " C";
  regionStr = "--";
  detailStr = String(humidity, 1) + "%";
#else
  struct tm timeinfo;
  bool haveTime = getLocalTime(&timeinfo);
  bool showMoon = haveTime && isNightTime(timeinfo);

  if (tempValid && !isnan(weatherTempC)) {
    tempStr = String(weatherTempC, 1) + " C";
  } else if (WiFi.status() == WL_CONNECTED) {
    tempStr = "... C";
  } else {
    tempStr = "-- C";
  }

  if (locationValid && weatherRegion.length() > 0) {
    regionStr = weatherRegion;
  } else if (WiFi.status() == WL_CONNECTED) {
    regionStr = getText("Konum...", "Loc...");
  } else {
    regionStr = getText("WiFi yok", "No WiFi");
  }

  if (showMoon) {
    float mp = moonPhaseIndex(timeinfo.tm_year + 1900,
                              timeinfo.tm_mon + 1,
                              timeinfo.tm_mday);
    detailStr = moonPhaseToText(mp);
  } else if (conditionValid && weatherCondition.length() > 0) {
    detailStr = weatherCondition;
  } else if (WiFi.status() == WL_CONNECTED) {
    detailStr = getText("Hava...", "Wx...");
  } else {
    detailStr = "--";
  }
#endif

  if (!forceRedraw &&
      tempStr == prevTemp && regionStr == prevRegion && detailStr == prevDetail) {
    return;
  }

  // Sol ust — 3 satir: sicaklik, bolge, hava/ay
  tft.fillRect(0, 0, LEFT_INFO_W, LEFT_INFO_H, ST77XX_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(6, LEFT_LINE1_Y);
  tft.println(tempStr);

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(6, LEFT_LINE2_Y);
  tft.println(regionStr);

#if !USE_DHT11
  if (!showMoon && conditionValid && weatherCondition.length() > 0 &&
      detailStr == weatherCondition) {
    tft.setTextColor(ST77XX_YELLOW);
  } else {
    tft.setTextColor(ST77XX_WHITE);
  }
#else
  tft.setTextColor(ST77XX_WHITE);
#endif
  tft.setCursor(6, LEFT_LINE3_Y);
  tft.println(detailStr);

  prevTemp = tempStr;
  prevRegion = regionStr;
  prevDetail = detailStr;
  prevHum = detailStr;
}

static void redrawHomeScreenWidgets() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    prevTime = "";
    prevDate = "";
    drawTimeAndDate(timeinfo);
  } else {
    prevTime = "";
    prevDate = "";
  }
  prevBattery = "";
  prevChargeStatus = "";
  clearMainPageBatteryLabels();
  drawBattery(true, false);
  updateLogoByRSSI();
  drawBottomStatusBar();
  drawDHT11Data(true);
}

// =======================================================
// 🟦 İSTATİSTİKLER SAYFASI
// =======================================================
static float readChipTempC() {
  return temperatureRead();
}

static uint16_t chipTempColor(float chipC) {
  if (chipC >= 70.0f) return ST77XX_RED;
  if (chipC >= 55.0f) return ST77XX_YELLOW;
  return ST77XX_WHITE;
}

void showStatisticsMenu(bool reset = false) {
  static bool firstDraw = true;
  static String prevUptimeStr = "";
  static String prevWifiUptimeStr = "";
  static String prevChipTempStr = "";
  static String prevChipRangeStr = "";
  
  // Değişkenleri fonksiyonun başında tanımla
  int lineHeight = 18;
  int yPos = 35;
  
  if (reset) {
    firstDraw = true;
    prevUptimeStr = "";
    prevWifiUptimeStr = "";
    prevChipTempStr = "";
    prevChipRangeStr = "";
    return;
  }

  float chipC = readChipTempC();
  if (chipC < minChipTempC) minChipTempC = chipC;
  if (chipC > maxChipTempC) maxChipTempC = chipC;
  String chipTempStr = "Cip Sicakligi: " + String(chipC, 1) + " C";
  String chipRangeStr = "Cip min/max: " + String(minChipTempC, 1) + " / " + String(maxChipTempC, 1) + " C";
  
  if (firstDraw) {
    tft.fillScreen(ST77XX_BLACK);
    firstDraw = false;
    
    // Başlık (ortalanmış)
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    String title = getTitleText(4);  // "ISTATISTIKLER" / "STATISTICS"
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int titleX = (TFT_WIDTH - w) / 2;
    tft.setCursor(titleX, 10);
    tft.println(title);
    
    // İlk çizimde tüm verileri göster
    tft.setTextSize(1);
    
    yPos = 35;  // Başlangıç pozisyonu

    tft.setTextColor(chipTempColor(chipC));
    tft.setCursor(10, yPos);
    tft.println(chipTempStr);
    yPos += lineHeight;

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, yPos);
    tft.println(chipRangeStr);
    yPos += lineHeight;
    
    // DHT istatistikleri (varsa)
    if (readingCount > 0) {
      float avgTemp = sumTemp / readingCount;
      String avgTempStr = "Ort. Sicaklik: " + String(avgTemp, 1) + " C";
      tft.setCursor(10, yPos);
      tft.println(avgTempStr);
      yPos += lineHeight;
      
      String tempRangeStr = "Sicaklik: " + String(minTemp, 1) + " / " + String(maxTemp, 1) + " C";
      tft.setCursor(10, yPos);
      tft.println(tempRangeStr);
      yPos += lineHeight;
      
      float avgHum = sumHum / readingCount;
      String avgHumStr = "Ort. Nem: " + String(avgHum, 1) + " %";
      tft.setCursor(10, yPos);
      tft.println(avgHumStr);
      yPos += lineHeight;
      
      String humRangeStr = "Nem: " + String(minHum, 1) + " / " + String(maxHum, 1) + " %";
      tft.setCursor(10, yPos);
      tft.println(humRangeStr);
      yPos += lineHeight;
    }
    
    // Talimat (ortalanmış)
    tft.setTextColor(ST77XX_CYAN);
    String instructionText = "Buton ile geri don";
    tft.getTextBounds(instructionText, 0, 0, &x1, &y1, &w, &h);
    int instX = (TFT_WIDTH - w) / 2;
    tft.setCursor(instX, 155);
    tft.println(instructionText);
    
    prevUptimeStr = "";
    prevWifiUptimeStr = "";
    prevChipTempStr = chipTempStr;
    prevChipRangeStr = chipRangeStr;
  }

  // Cip sicakligi - CANLI GUNCELLEME
  yPos = 35;
  if (chipTempStr != prevChipTempStr) {
    tft.setTextSize(1);
    tft.setTextColor(chipTempColor(chipC));
    tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
    tft.setCursor(10, yPos);
    tft.println(chipTempStr);
    prevChipTempStr = chipTempStr;
  }
  yPos += lineHeight;

  if (chipRangeStr != prevChipRangeStr) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
    tft.setCursor(10, yPos);
    tft.println(chipRangeStr);
    prevChipRangeStr = chipRangeStr;
  }
  yPos += lineHeight;
  
  // Sadece süreleri güncelle (canlı)
  if (readingCount > 0) {
    yPos += lineHeight * 4;  // 4 satir DHT istatistik
  }
  
  // Toplam Çalışma Süresi (Uptime) - CANLI GÜNCELLEME
  unsigned long currentUptimeSeconds = (millis() - systemStartTime) / 1000;
  unsigned long totalSeconds = totalUptimeSeconds + currentUptimeSeconds;
  unsigned long days = totalSeconds / 86400;
  unsigned long hours = (totalSeconds % 86400) / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  String uptimeStr = "Toplam Calisma Suresi: " + String(days) + "g " + String(hours) + "s " + String(minutes) + "d";
  
  if (uptimeStr != prevUptimeStr) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
    tft.setCursor(10, yPos);
    tft.println(uptimeStr);
    prevUptimeStr = uptimeStr;
  }
  yPos += lineHeight;
  
  // WiFi Bağlantı Süresi - CANLI GÜNCELLEME
  String wifiUptimeStr;
  if (WiFi.status() == WL_CONNECTED && wifiWasConnected) {
    unsigned long wifiUptimeSeconds = (millis() - wifiConnectedTime) / 1000;
    unsigned long wifiDays = wifiUptimeSeconds / 86400;
    unsigned long wifiHours = (wifiUptimeSeconds % 86400) / 3600;
    unsigned long wifiMinutes = (wifiUptimeSeconds % 3600) / 60;
    wifiUptimeStr = "WiFi Baglanti: " + String(wifiDays) + "g " + String(wifiHours) + "s " + String(wifiMinutes) + "d";
    
    if (wifiUptimeStr != prevWifiUptimeStr) {
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_WHITE);
      tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
      tft.setCursor(10, yPos);
      tft.println(wifiUptimeStr);
      prevWifiUptimeStr = wifiUptimeStr;
    }
  } else {
    wifiUptimeStr = "WiFi Baglanti: BAGLI DEGIL";
    if (wifiUptimeStr != prevWifiUptimeStr) {
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_RED);
      tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
      tft.setCursor(10, yPos);
      tft.println(wifiUptimeStr);
      prevWifiUptimeStr = wifiUptimeStr;
    }
  }
}

// =======================================================
// 🟦 SİSTEM BİLGİLERİ SAYFASI
// =======================================================
void showSystemInfoMenu(bool reset = false) {
  static bool firstDraw = true;
  static unsigned long lastUpdate = 0;
  
  if (reset) {
    firstDraw = true;
    lastUpdate = 0;
    return;
  }
  
  // 2 saniyede bir güncelle
  unsigned long now = millis();
  if (!firstDraw && (now - lastUpdate < 2000)) {
    return;
  }
  lastUpdate = now;
  
  if (firstDraw) {
    tft.fillScreen(ST77XX_BLACK);
    firstDraw = false;
  }
  
  // Başlık (ortalanmış)
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  String title = getTitleText(5);  // "SISTEM BILGILERI" / "SYSTEM INFO"
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int titleX = (TFT_WIDTH - w) / 2;
  tft.fillRect(titleX - 5, 8, w + 10, h + 4, ST77XX_BLACK);
  tft.setCursor(titleX, 10);
  tft.println(title);
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  
  int lineHeight = 18;
  int yPos = 35;
  
  // CPU Frekansı
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  String cpuStr = "CPU Frekans: " + String(cpuFreq) + " MHz";
  tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.println(cpuStr);
  yPos += lineHeight;
  
  // Bellek Kullanımı
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t usedHeap = totalHeap - freeHeap;
  float heapPercent = (float)usedHeap / totalHeap * 100.0;
  String heapStr = "Bellek: " + String(usedHeap / 1024) + "/" + String(totalHeap / 1024) + " KB (" + String(heapPercent, 1) + "%)";
  tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.println(heapStr);
  yPos += lineHeight;
  
  // Chip ID
  uint64_t chipid = ESP.getEfuseMac();
  String chipIdStr = "Chip ID: " + String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  chipIdStr.toUpperCase();
  tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.println(chipIdStr);
  yPos += lineHeight;
  
  // Firmware Versiyonu
  String versionStr = "Firmware: " + String(VERSION_TEXT);
  tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.println(versionStr);
  yPos += lineHeight;
  
  // Uptime (Çalışma Süresi)
  unsigned long uptimeSeconds = (millis() - systemStartTime) / 1000;
  unsigned long days = uptimeSeconds / 86400;
  unsigned long hours = (uptimeSeconds % 86400) / 3600;
  unsigned long minutes = (uptimeSeconds % 3600) / 60;
  String uptimeStr = "Uptime: " + String(days) + "g " + String(hours) + "s " + String(minutes) + "d";
  tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.println(uptimeStr);
  yPos += lineHeight;

  // Batarya
  float vBat = readBatteryVoltage();
  int batPct = batteryPercent(vBat);
  String batStr = "Batarya: " + String(batPct) + "% (" + String(vBat, 2) + " V)";
  tft.setTextColor(batteryColor(batPct));
  tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.println(batStr);
  tft.setTextColor(ST77XX_WHITE);
  yPos += lineHeight;
  
  // WiFi Durumu
  String wifiStatus = "WiFi: ";
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus += "BAGLI";
    tft.setTextColor(ST77XX_GREEN);
  } else {
    wifiStatus += "BAGLI DEGIL";
    tft.setTextColor(ST77XX_RED);
  }
  tft.fillRect(10, yPos, TFT_WIDTH - 20, lineHeight, ST77XX_BLACK);
  tft.setCursor(10, yPos);
  tft.println(wifiStatus);
  tft.setTextColor(ST77XX_WHITE);
  
  // Talimat (ortalanmış)
  tft.setTextColor(ST77XX_CYAN);
  String instructionText = "Buton ile geri don";
  tft.getTextBounds(instructionText, 0, 0, &x1, &y1, &w, &h);
  int instX = (TFT_WIDTH - w) / 2;
  tft.fillRect(instX - 5, 155, w + 10, h + 4, ST77XX_BLACK);
  tft.setCursor(instX, 155);
  tft.println(instructionText);
}

// =======================================================
// 🟦 ENCODER INTERRUPT HANDLER
// =======================================================
void IRAM_ATTR encoderISR() {
  int CLKState = digitalRead(ENCODER_CLK);
  int DTState = digitalRead(ENCODER_DT);
  
  if (CLKState != lastCLKState) {
    if (DTState != CLKState) {
      encoderPosition++;  // Saat yönünde
    } else {
      encoderPosition--;  // Saat yönünün tersi
    }
  }
  lastCLKState = CLKState;
}

// =======================================================
// 🟦 ENCODER BAŞLATMA
// =======================================================
void initEncoder() {
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  
  lastCLKState = digitalRead(ENCODER_CLK);
  
  // Interrupt ayarları
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderISR, CHANGE);
  
  Serial.println("Encoder baslatildi!");
}

// =======================================================
// 🟦 ENCODER BUTON KONTROLÜ
// =======================================================
void checkEncoderButton() {
  int buttonState = digitalRead(ENCODER_SW);
  
  if (buttonState == LOW && (millis() - lastButtonPress) > debounceDelay) {
    encoderButtonPressed = true;
    lastButtonPress = millis();
    Serial.println("Encoder buton basildi!");
  }
}

// =======================================================
// 🟦 PARLAKLIK AYARLAMA
// =======================================================
void setBrightness(int level) {
  if (level < brightnessMin) level = brightnessMin;
  if (level > brightnessMax) level = brightnessMax;
  brightness = level;
  
  if (USE_PWM) {
    ledcWrite(BACKLIGHT_LEDC_CHANNEL, brightness);
  } else {
    // Basit açık/kapalı kontrolü
    // %50'nin üzerinde açık, altında kapalı
    if (brightness > (brightnessMin + brightnessMax) / 2) {
      digitalWrite(TFT_BACKLIGHT, HIGH);  // Açık
    } else {
      digitalWrite(TFT_BACKLIGHT, LOW);   // Kapalı
    }
  }
  
  // Debug bilgisi (sadece değiştiğinde)
  static int lastReportedBrightness = -1;
  if (brightness != lastReportedBrightness) {
    Serial.print("Parlaklik: ");
    Serial.print(brightness);
    Serial.print(" (");
    Serial.print(map(brightness, brightnessMin, brightnessMax, 0, 100));
    Serial.print("%), Pin=GPIO");
    Serial.print(TFT_BACKLIGHT);
    if (USE_PWM) {
      Serial.print(", PWM=");
      Serial.print(brightness);
    }
    Serial.println();
    lastReportedBrightness = brightness;
  }
}

// =======================================================
// 🟦 PARLAKLIK AYAR MENÜSÜ
// =======================================================
void showBrightnessMenu(bool reset = false) {
  static int lastDrawnBrightness = -1;
  static bool firstDraw = true;
  
  if (reset) {
    firstDraw = true;
    lastDrawnBrightness = -1;
    return;
  }
  
  if (firstDraw) {
    tft.fillScreen(ST77XX_BLACK);
    
    // Başlık (ortalanmış)
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    String title = getTitleText(1);  // "PARLAKLIK" / "BRIGHTNESS"
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int titleX = (TFT_WIDTH - w) / 2;
    tft.setCursor(titleX, 10);
    tft.println(title);
    
    // Progress bar çerçevesi
    tft.drawRect(20, 60, TFT_WIDTH - 40, 20, ST77XX_WHITE);
    
    // Talimat (ortalanmış) - aynı değişkenleri kullan
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    String instructionText = "Encoder ile ayarla, buton ile geri don";
    tft.getTextBounds(instructionText, 0, 0, &x1, &y1, &w, &h);
    int instX = (TFT_WIDTH - w) / 2;
    tft.setCursor(instX, 140);
    tft.println(instructionText);
    
    firstDraw = false;
  }
  
  // Sadece parlaklık değiştiğinde güncelle
  if (brightness != lastDrawnBrightness) {
    // Progress bar doldurma
    int barWidth = TFT_WIDTH - 42;
    int filledWidth = map(brightness, brightnessMin, brightnessMax, 0, barWidth);
    
    // Önceki bar'ı tamamen temizle (her zaman)
    if (lastDrawnBrightness >= 0) {
      int prevFilledWidth = map(lastDrawnBrightness, brightnessMin, brightnessMax, 0, barWidth);
      // Tüm bar alanını temizle (artma veya azalma fark etmez)
      if (prevFilledWidth > 0) {
        tft.fillRect(21, 61, prevFilledWidth, 18, ST77XX_BLACK);
      }
    }
    
    // Yeni bar'ı çiz
    if (filledWidth > 0) {
      tft.fillRect(21, 61, filledWidth, 18, ST77XX_YELLOW);
    }
    
    // Yüzde gösterimi
    int percent = map(brightness, brightnessMin, brightnessMax, 0, 100);
    String percentStr = String(percent) + "%";
    
    tft.setTextSize(3);
    tft.setTextColor(ST77XX_WHITE);
    
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(percentStr, 0, 0, &x1, &y1, &w, &h);
    
    // Maksimum genişliği hesapla (%100 en geniş)
    String maxPercentStr = "100%";
    int16_t mx1, my1;
    uint16_t mw, mh;
    tft.getTextBounds(maxPercentStr, 0, 0, &mx1, &my1, &mw, &mh);
    
    int x = (TFT_WIDTH - w) / 2;
    int y = 100;
    
    // Önceki yüzdeyi temizle - maksimum genişlik kullan (iz kalmaması için)
    // Yüzde alanının tamamını temizle
    tft.fillRect((TFT_WIDTH - mw) / 2 - 5, y - 3, mw + 10, mh + 6, ST77XX_BLACK);
    
    // Yeni yüzdeyi çiz
    tft.setCursor(x, y);
    tft.println(percentStr);
    
    lastDrawnBrightness = brightness;
  }
}

// =======================================================
// 🟦 DİL SEÇİM MENÜSÜ
// =======================================================
void showLanguageMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 10);
  tft.println(getTitleText(2));  // "DIL" / "LANGUAGE"
  
  // Dil seçenekleri
  String languages[] = {
    getText("Turkce", "Turkish"),
    getText("Ingilizce", "English")
  };
  
  for (int i = 0; i < 2; i++) {
    if (i == languageSelectionItem) {
      // Seçili item
      tft.fillRect(15, 50 + (i * 40), TFT_WIDTH - 30, 35, ST77XX_CYAN);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setTextSize(2);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(languages[i], 0, 0, &x1, &y1, &w, &h);
    int x = (TFT_WIDTH - w) / 2;
    tft.setCursor(x, 60 + (i * 40));
    tft.println(languages[i]);
  }
  
  // Geri dön talimatı
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  String backText = getText("Buton ile geri don", "Press button to go back");
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(backText, 0, 0, &x1, &y1, &w, &h);
  int backX = (TFT_WIDTH - w) / 2;
  tft.setCursor(backX, TFT_HEIGHT - 15);
  tft.println(backText);
}

// =======================================================
// 🟦 WIFI SIFIRLA ONAY EKRANI
// =======================================================
void showWiFiResetConfirm() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(40, 30);
  tft.println(getTitleText(6));  // "EMIN MISINIZ?" / "ARE YOU SURE?"
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 65);
  tft.println(getText("Tum WiFi bilgileri", "All WiFi settings"));
  tft.setCursor(20, 80);
  tft.println(getText("silinecek!", "will be deleted!"));
  
  // Evet/Hayır seçenekleri
  String options[] = {
    getText("EVET", "YES"),
    getText("HAYIR", "NO")
  };
  for (int i = 0; i < 2; i++) {
    if (i == wifiResetConfirmItem) {
      // Seçili item
      tft.fillRect(15, 110 + (i * 30), TFT_WIDTH - 30, 28, ST77XX_CYAN);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setTextSize(2);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(options[i], 0, 0, &x1, &y1, &w, &h);
    int x = (TFT_WIDTH - w) / 2;
    tft.setCursor(x, 118 + (i * 30));
    tft.println(options[i]);
  }
}

// =======================================================
// 🟦 WIFI AYARLARINI SIFIRLA
// =======================================================
void resetWiFiConfig() {
  // WiFi ayarlarını sıfırla
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  
  wifiManager.resetSettings();
  
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(30, 70);
  tft.println("SIFIRLANDI");
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 100);
  tft.println("Yeniden baslatiliyor...");
  
  delay(2000);
  ESP.restart();
}

// =======================================================
// 🟦 AYARLAR MENÜSÜ
// =======================================================
void showSettingsMenu() {
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 10);
  tft.println(getTitleText(0));  // "AYARLAR" / "SETTINGS"
  
  const int totalItems = 8;
  int startIndex = 0;
  if (menuItem >= 4) {
    startIndex = menuItem - 3;
    if (startIndex > totalItems - 4) startIndex = totalItems - 4;
  }
  
  int visibleItems = 4;
  int endIndex = startIndex + visibleItems;
  if (endIndex > totalItems) endIndex = totalItems;
  
  for (int i = startIndex; i < endIndex; i++) {
    int displayIndex = i - startIndex;
    if (i == menuItem) {
      tft.fillRect(15, 38 + (displayIndex * 25), TFT_WIDTH - 30, 22, ST77XX_CYAN);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setCursor(20, 40 + (displayIndex * 25));
    tft.println(getMenuText(i));
  }
  
  if (startIndex > 0 || endIndex < totalItems) {
    tft.fillCircle(TFT_WIDTH - 10, 15, 3, ST77XX_WHITE);
  }
}

// =======================================================
// 🟦 WIFI BİLGİLERİ SAYFASI
// =======================================================
void showWiFiInfoMenu(bool reset = false) {
  static bool firstDraw = true;
  static String prevSSID = "";
  static String prevIP = "";
  static int prevRSSI = -999;
  
  if (reset) {
    firstDraw = true;
    prevSSID = "";
    prevIP = "";
    prevRSSI = -999;
    return;
  }
  
  if (firstDraw) {
    tft.fillScreen(ST77XX_BLACK);
    
    // Başlık (ortalanmış)
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    String title = getTitleText(3);  // "WIFI BILGILERI" / "WIFI INFO"
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int titleX = (TFT_WIDTH - w) / 2;
    tft.setCursor(titleX, 10);
    tft.println(title);
    
    // Alt kısım - Geri dön talimatı (ortalanmış)
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    String backText = getText("Buton ile geri don", "Press button to go back");
    tft.getTextBounds(backText, 0, 0, &x1, &y1, &w, &h);
    int backX = (TFT_WIDTH - w) / 2;
    tft.setCursor(backX, TFT_HEIGHT - 15);
    tft.println(backText);
    
    firstDraw = false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    tft.fillRect(10, 32, TFT_WIDTH - 20, 118, ST77XX_BLACK);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(70, 36);
    tft.println(getText("BAGLI DEGIL", "NOT CONNECTED"));

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 58);
    if (hasSavedWifiCredentials()) {
      prefs.begin("wifi", true);
      String ssid = prefs.getString("ssid", "");
      prefs.end();
      tft.print(getText("Kayitli ag: ", "Saved network: "));
      tft.setTextColor(ST77XX_YELLOW);
      tft.println(ssid);
      tft.setTextColor(ST77XX_CYAN);
      tft.setCursor(10, 72);
      tft.println(getText("Arka planda yeniden deneniyor", "Retrying in background"));
    } else {
      tft.println(getText("Henuz WiFi kaydi yok", "No WiFi saved yet"));
    }

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 90);
    tft.println(getText("Kurulum icin:", "To configure:"));
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(10, 102);
    tft.println(getText("Menu > WiFi Ayarlari", "Menu > WiFi Settings"));

    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 118);
    tft.println(getText("AP: ", "AP: ") + String(ap_ssid));
    tft.setCursor(10, 130);
    tft.print(getText("Sifre: ", "Pass: "));
    tft.println(ap_password);
    tft.setCursor(10, 142);
    tft.setTextColor(ST77XX_YELLOW);
    tft.println("http://192.168.4.1");

    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, TFT_HEIGHT - 14);
    tft.println(getText("Buton: geri don", "Button: go back"));

    prevRSSI = -999;
    return;
  }
  
  tft.setTextSize(1);
  int yPos = 45;
  int lineHeight = 22;  // Satır aralığı optimize edildi
  
  // SSID - Tam yazılsın (kısaltma kaldırıldı)
  String ssidStr = WiFi.SSID();
  if (ssidStr != prevSSID) {
    // Önceki SSID'yi temizle (daha geniş alan)
    tft.fillRect(10, yPos, TFT_WIDTH - 20, 18, ST77XX_BLACK);
    
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, yPos);
    tft.print("SSID: ");
    
    // SSID uzunluğunu kontrol et ve gerekirse alt satıra geç
    tft.setTextColor(ST77XX_YELLOW);
    int16_t x1, y1;
    uint16_t labelW, labelH;
    tft.getTextBounds("SSID: ", 0, 0, &x1, &y1, &labelW, &labelH);
    
    int ssidX = 10 + labelW;
    int maxWidth = TFT_WIDTH - ssidX - 10;
    
    // SSID'nin genişliğini kontrol et
    uint16_t ssidW, ssidH;
    tft.getTextBounds(ssidStr, 0, 0, &x1, &y1, &ssidW, &ssidH);
    
    if (ssidW > maxWidth) {
      // Uzun SSID - alt satıra geç
      tft.setCursor(10, yPos);
      tft.println("SSID:");
      tft.setCursor(10, yPos + lineHeight);
      tft.println(ssidStr);
      yPos += lineHeight;  // Ekstra satır eklendi
    } else {
      // Normal SSID - aynı satırda
      tft.setCursor(ssidX, yPos);
      tft.println(ssidStr);
    }
    prevSSID = ssidStr;
  }
  yPos += lineHeight;
  
  // IP Adresi
  String ipStr = WiFi.localIP().toString();
  if (ipStr != prevIP) {
    tft.fillRect(10, yPos, TFT_WIDTH - 20, 18, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, yPos);
    tft.print("IP: ");
    tft.setTextColor(ST77XX_CYAN);
    tft.println(ipStr);
    prevIP = ipStr;
  }
  yPos += lineHeight;
  
  // RSSI (Sinyal Gücü) - Sürekli güncellenir
  int rssi = WiFi.RSSI();
  if (rssi != prevRSSI) {
    tft.fillRect(10, yPos, TFT_WIDTH - 20, 18, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, yPos);
    tft.print("Sinyal: ");
    tft.setTextColor(ST77XX_YELLOW);
    String rssiStr = String(rssi) + " dBm";
    tft.print(rssiStr);
    
    // Sinyal seviyesi gösterimi
    if (rssi >= -60) {
      tft.setTextColor(ST77XX_GREEN);
      tft.println(" (Iyi)");
    } else if (rssi >= -80) {
      tft.setTextColor(ST77XX_YELLOW);
      tft.println(" (Orta)");
    } else {
      tft.setTextColor(ST77XX_RED);
      tft.println(" (Zayif)");
    }
    prevRSSI = rssi;
  }
}

// =======================================================
// 🟦 ENCODER İLE MENÜ KONTROLÜ
// =======================================================
void handleEncoderNavigation() {
  // Encoder pozisyon değişikliği kontrolü
  if (encoderPosition != lastEncoderPosition) {
    int diff = encoderPosition - lastEncoderPosition;
    
    // Encoder hareketi = aktivite (ekran koruyucuyu kapat)
    updateActivity();
    
    if (currentMenuPage == 0) {
      // Ana sayfada encoder ile menü item seçimi yapılabilir
      // Şimdilik sadece log
      Serial.print("Encoder pozisyon: ");
      Serial.println(encoderPosition);
    } else if (currentMenuPage == 1) {
      menuItem += diff;
      if (menuItem < 0) menuItem = 0;
      if (menuItem > 7) menuItem = 7;
      showSettingsMenu();
    } else if (currentMenuPage == 6) {
      // WiFi Sıfırla onay ekranı
      wifiResetConfirmItem += diff;
      if (wifiResetConfirmItem < 0) wifiResetConfirmItem = 0;
      if (wifiResetConfirmItem > 1) wifiResetConfirmItem = 1;
      showWiFiResetConfirm();
    } else if (currentMenuPage == 7) {
      // Dil seçim menüsü
      languageSelectionItem += diff;
      if (languageSelectionItem < 0) languageSelectionItem = 0;
      if (languageSelectionItem > 1) languageSelectionItem = 1;
      showLanguageMenu();
    } else if (currentMenuPage == 2) {
      // Parlaklık ayarı menüsünde
      brightness += diff * 5;  // Her adımda 5 artır/azalt
      if (brightness < brightnessMin) brightness = brightnessMin;
      if (brightness > brightnessMax) brightness = brightnessMax;
      setBrightness(brightness);
      showBrightnessMenu();
    }
    
    lastEncoderPosition = encoderPosition;
    buzzerEncoderTick();
  }
  
  // Buton kontrolü
  checkEncoderButton();
  
  if (encoderButtonPressed) {
    encoderButtonPressed = false;
    buzzerEncoderClick();

    // Buton basımı = aktivite (ekran koruyucuyu kapat)
    updateActivity();
    
    if (currentMenuPage == 0) {
      // Ana sayfadan ayarlar menüsüne geç
      resetBottomStatusBarCache();
      currentMenuPage = 1;
      menuItem = 0;
      showSettingsMenu();
    } else if (currentMenuPage == 1) {
      // Ayarlar menüsünde buton basıldı
      if (menuItem == 0) {
        // "Parlaklik" seçildi - parlaklık menüsüne geç
        currentMenuPage = 2;
        showBrightnessMenu(true);  // Reset
        showBrightnessMenu();      // İlk çizim
      } else if (menuItem == 1) {
        if (WiFi.status() == WL_CONNECTED) {
          currentMenuPage = 3;
          showWiFiInfoMenu(true);
          showWiFiInfoMenu();
        } else {
          currentMenuPage = 0;
          startWifiSetupPortal();
        }
      } else if (menuItem == 2) {
        // "Dil" seçildi - Dil seçim sayfasına geç
        currentMenuPage = 7;  // Dil seçim sayfası
        languageSelectionItem = currentLanguage;  // Mevcut dili seçili göster
        showLanguageMenu();
      } else if (menuItem == 3) {
        currentMenuPage = 4;
        showStatisticsMenu(true);
        showStatisticsMenu();
      } else if (menuItem == 4) {
        currentMenuPage = 8;
        showBatteryHealthMenu(true);
        showBatteryHealthMenu();
      } else if (menuItem == 5) {
        currentMenuPage = 5;
        showSystemInfoMenu(true);
        showSystemInfoMenu();
      } else if (menuItem == 6) {
        currentMenuPage = 6;
        showWiFiResetConfirm();
      } else if (menuItem == 7) {
        // "Geri Don" seçildi - ana sayfaya dön
        resetMainPageCaches();
        currentMenuPage = 0;
        tft.fillScreen(ST77XX_BLACK);
        lastTimeUpdate = 0;
        lastBatteryRead = 0;
        redrawHomeScreenWidgets();
      } else {
        // Diğer menü item'ları için (ileride fonksiyonellik eklenebilir)
        Serial.print("Menu item secildi: ");
        Serial.println(menuItem);
      }
    } else if (currentMenuPage == 2) {
      // Parlaklık menüsünden ayarlar menüsüne geri dön
      currentMenuPage = 1;
      menuItem = 0;
      // showBrightnessMenu() içindeki static değişkeni sıfırlamak için
      // fonksiyonu bir kez daha çağırmadan önce ekranı temizleyelim
      showSettingsMenu();
    } else if (currentMenuPage == 3) {
      // WiFi bilgileri sayfasından ayarlar menüsüne geri dön
      currentMenuPage = 1;
      menuItem = 1;  // WiFi Ayarlari seçili kalsın
      showWiFiInfoMenu(true);  // Reset
      showSettingsMenu();
    } else if (currentMenuPage == 4) {
      currentMenuPage = 1;
      menuItem = 3;
      showStatisticsMenu(true);
      showSettingsMenu();
    } else if (currentMenuPage == 5) {
      currentMenuPage = 1;
      menuItem = 5;
      showSystemInfoMenu(true);
      showSettingsMenu();
    } else if (currentMenuPage == 8) {
      currentMenuPage = 1;
      menuItem = 4;
      showBatteryHealthMenu(true);
      showSettingsMenu();
    } else if (currentMenuPage == 6) {
      // WiFi Sıfırla onay ekranında buton basıldı
      if (wifiResetConfirmItem == 0) {
        // EVET seçildi - WiFi'yi sıfırla
        resetWiFiConfig();
      } else {
        // HAYIR seçildi - menüye dön
        currentMenuPage = 1;
        menuItem = 7;
        wifiResetConfirmItem = 0;
        showSettingsMenu();
      }
    } else if (currentMenuPage == 7) {
      // Dil seçim menüsünde buton basıldı
      // Dil seçiminde butona basıldığında: seçili dili kaydet ve ayarlar menüsüne dön
      if (languageSelectionItem == 0 || languageSelectionItem == 1) {
        currentLanguage = languageSelectionItem;
        saveLanguage();  // Dil tercihini kaydet
      }
      
      // Ayarlar menüsüne dön (dil değişti, menü yeniden çizilecek)
      currentMenuPage = 1;
      menuItem = 2;  // Dil seçili kalsın
      languageSelectionItem = currentLanguage;  // Seçili dili göster
      showSettingsMenu();
    }
  }
}

// =======================================================
void updateTimeIfNeeded() {
  unsigned long now = millis();
  
  // Ekran koruyucudan çıkınca ekranı yeniden çiz
  if (needRedraw && !screenSaverActive) {
    needRedraw = false;
    if (currentMenuPage == 0) {
      // Ana sayfa çizimlerini yeniden yap
      resetMainPageCaches();
      redrawHomeScreenWidgets();
    }
  }

  if (now - lastTimeUpdate >= timeInterval) {
    lastTimeUpdate = now;
    
    // Ekran koruyucu aktifse zaman güncelleme yapma
    if (screenSaverActive) {
      return;
    }

    if (currentMenuPage == 0) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        drawTimeAndDate(timeinfo);
      }
      drawDHT11Data();
      drawBattery(false, false);
      updateLogoByRSSI();
      drawBottomStatusBar();
    } else if (currentMenuPage == 3) {
      // WiFi bilgileri sayfası - sinyal gücü güncellensin
      showWiFiInfoMenu();
    } else if (currentMenuPage == 4) {
      // İstatistikler sayfası - güncelle
      showStatisticsMenu();
    } else if (currentMenuPage == 5) {
      showSystemInfoMenu();
    } else if (currentMenuPage == 8) {
      showBatteryHealthMenu();
    }
  }
}

// =======================================================
// 🟦 WIFI DURUM KONTROLÜ VE YENİDEN BAĞLANMA (Non-blocking)
// =======================================================
void checkWiFiConnection() {
  // WiFiManager AP modunda çalışıyorsa kontrol etme
  if (wifiSetupMode) {
    return;
  }
  
  unsigned long now = millis();
  
  // Her 10 saniyede bir kontrol et (CPU'yu yormaz)
  if (now - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = now;
    
    // WiFi durumunu kontrol et
    if (WiFi.status() != WL_CONNECTED) {
      if (!wifiReconnecting) {
        wifiReconnecting = true;
        lastReconnectAttempt = 0;
        wifiWasConnected = false;
        invalidateRegionalWeather();
        Serial.println("WiFi baglantisi koptu! Yeniden baglanma baslatiliyor...");
      }
      
      // 5 saniyede bir yeniden bağlanmayı dene (non-blocking)
      if (now - lastReconnectAttempt >= reconnectInterval) {
        lastReconnectAttempt = now;
        
        Serial.print("WiFi yeniden baglanma denemesi... ");
        
        // Preferences'dan WiFi bilgilerini oku
        prefs.begin("wifi", true);
        String savedSSID = prefs.getString("ssid", "");
        String savedPass = prefs.getString("pass", "");
        prefs.end();
        
        if (savedSSID.length() > 0) {
          savedSSID.trim();
          // WiFi'yi durdur ve yeniden başlat (hızlı, blocking değil)
          WiFi.disconnect();
          delay(100);  // Kısa delay, blocking değil
          WiFi.mode(WIFI_STA);
          WiFi.begin(savedSSID.c_str(), savedPass.c_str());
          
          Serial.println("baslatildi (non-blocking)");
        }
      }
    } else {
      // Bağlantı var
      if (!wifiWasConnected) {
        // İlk bağlantı zamanını kaydet
        wifiConnectedTime = millis();
        wifiWasConnected = true;
      }
      if (wifiReconnecting) {
        wifiReconnecting = false;
        prevWifiStatusLine = "";
        Serial.println("WiFi YENİDEN BAGLANDI!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        
        ipAddress = WiFi.localIP().toString();
        
        // WiFi bağlantı zamanını güncelle (yeniden bağlandı)
        wifiConnectedTime = millis();
        wifiWasConnected = true;
        
        // NTP'yi yeniden yapılandır (hızlı, blocking değil)
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        WiFi.setSleep(WIFI_PS_NONE);
        configureWifiDns();
        invalidateRegionalWeather();
        startOTA();
        
        // Önceki değerleri sıfırla ki ekran yeniden çizilsin
        prevTime = "";
        prevDate = "";
      }
    }
  }
}

// =======================================================
// 🟦 WIFI BİLGİLERİNİ PREFERENCES'DAN YÜKLE
// =======================================================
bool loadWiFiCredentials() {
  prefs.begin("wifi", true);  // Read-only mode
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();
  
  if (savedSSID.length() > 0) {
    Serial.print("Kayitli WiFi bilgisi bulundu: ");
    Serial.println(savedSSID);
    return true;
  }
  return false;
}

// =======================================================
// 🟦 WEB SUNUCUSU HANDLER FONKSİYONLARI
// =======================================================
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32 WiFi Ayarları</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; background: #f5f5f5; }";
  html += "h1 { color: #333; text-align: center; }";
  html += ".form-group { margin: 20px 0; }";
  html += "label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }";
  html += "input[type='text'], input[type='password'] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; font-size: 16px; }";
  html += "button { width: 100%; padding: 12px; background: #4CAF50; color: white; border: none; border-radius: 4px; font-size: 16px; cursor: pointer; margin-top: 10px; }";
  html += "button:hover { background: #45a049; }";
  html += ".info { background: #e3f2fd; padding: 15px; border-radius: 4px; margin-bottom: 20px; }";
  html += "</style></head><body>";
  html += "<h1>📶 ESP32 WiFi Yapılandırma</h1>";
  html += "<div class='info'>";
  html += "<strong>Talimatlar:</strong><br>";
  html += "1. Aşağıdaki listeden WiFi ağınızı seçin<br>";
  html += "2. WiFi şifrenizi girin<br>";
  html += "3. 'Bağlan' butonuna tıklayın<br>";
  html += "ESP32 otomatik olarak yeniden başlayacak ve WiFi'ye bağlanacak.";
  html += "</div>";
  html += "<form action='/save' method='POST'>";
  html += "<div class='form-group'>";
  html += "<label for='ssid'>WiFi Ağ Adı (SSID):</label>";
  html += "<input type='text' id='ssid' name='ssid' required placeholder='WiFi ag adi (zorunlu)'>";
  html += "</div>";
  html += "<div class='form-group'>";
  html += "<label for='pass'>WiFi Şifresi:</label>";
  html += "<input type='password' id='pass' name='pass' placeholder='Acik ag ise bos birakin'>";
  html += "</div>";
  html += "<button type='submit'>🔗 WiFi'ye Bağlan</button>";
  html += "</form>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSave() {
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    String pass = server.hasArg("pass") ? server.arg("pass") : "";
    ssid.trim();
    pass.trim();

    if (ssid.length() == 0) {
      server.send(400, "text/html; charset=utf-8",
                  "<html><body><h1>Hata</h1><p>WiFi adi (SSID) bos olamaz.</p>"
                  "<a href='/'>Geri</a></body></html>");
      return;
    }

    Serial.println("WiFi bilgileri alindi:");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.println(pass);
    
    // WiFi bilgilerini Preferences'a kaydet
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>WiFi Ayarları Kaydedildi</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; text-align: center; }";
    html += "h1 { color: #4CAF50; }";
    html += ".success { background: #d4edda; padding: 20px; border-radius: 4px; margin: 20px 0; }";
    html += "</style></head><body>";
    html += "<h1>✅ WiFi Ayarları Kaydedildi!</h1>";
    html += "<div class='success'>";
    html += "<p><strong>SSID:</strong> " + ssid + "</p>";
    html += "<p>ESP32 şimdi yeniden başlatılıyor ve WiFi'ye bağlanıyor...</p>";
    html += "<p>Bu sayfayı kapatabilirsiniz.</p>";
    html += "</div>";
    html += "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
    
    delay(2000);  // Kullanıcıya mesajı görmesi için zaman ver
    
    // WiFi'ye bağlanmayı dene
    Serial.println("WiFi'ye baglaniliyor...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi BAGLANDI!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      
      // NTP yapılandırması
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      
      // ESP32'yi yeniden başlat (normal moda geçmek için)
      delay(1000);
      ESP.restart();
    } else {
      Serial.println("\nBaglanamadi! ESP32 yeniden baslatiliyor...");
      delay(2000);
      ESP.restart();
    }
  } else {
    server.send(400, "text/plain", "Hata: SSID ve şifre gerekli!");
  }
}

// =======================================================
// 🟦 WiFi kurulum AP (tek web sunucusu — port 80 cakismasi yok)
// =======================================================
void drawWifiSetupStatus(const char* status) {
  tft.fillRect(0, 154, TFT_WIDTH, 14, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(4, 156);
  tft.print(getText("Durum: ", "Status: "));
  tft.setTextColor(ST77XX_YELLOW);
  tft.println(status);
}

void drawWifiSetupScreen() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(72, 4);
  tft.println("WiFi");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 26);
  tft.println(getText("1) Telefonda WiFi acin:", "1) On phone open WiFi:"));

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(8, 38);
  tft.println(ap_ssid);
  tft.setCursor(8, 50);
  tft.print(getText("   sifre: ", "   pass: "));
  tft.println(ap_password);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 64);
  tft.println(getText("2) Tarayicida acin:", "2) In browser open:"));

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(8, 76);
  tft.println("http://192.168.4.1");

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 90);
  tft.println(getText("3) Ev agi SSID + sifre", "3) Enter home SSID + pass"));
  tft.setCursor(4, 102);
  tft.println(getText("   (SSID bos birakmayin)", "   (do not leave SSID empty)"));

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 116);
  tft.println(getText("Kayit -> kart yeniden baslar", "Save -> device restarts"));

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(4, 130);
  tft.println(getText("Encoder: ana sayfa (iptal)", "Encoder: home (cancel)"));

  String apStatus = getText("AP acildi, telefon bekleniyor", "AP on, waiting for phone");
  drawWifiSetupStatus(apStatus.c_str());
}

void sanitizeWifiPrefs() {
  prefs.begin("wifi", false);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  ssid.trim();
  pass.trim();
  if (ssid.length() == 0) {
    prefs.clear();
    Serial.println("WiFi prefs temizlendi (bos SSID)");
  } else {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
  }
  prefs.end();
}

void exitWifiSetupMode() {
  if (!wifiSetupMode) {
    return;
  }
  wifiSetupMode = false;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);

  tft.fillScreen(ST77XX_BLACK);
  drawMainDashboard();
  lastTimeUpdate = 0;
  lastBatteryRead = 0;
  Serial.println("WiFi kurulumdan cikildi — ana sayfa");
}

void startWifiSetupPortal() {
  if (wifiSetupMode) {
    return;
  }
  wifiSetupMode = true;

  Serial.println("========================================");
  Serial.println("WiFi kurulum AP baslatiliyor...");
  Serial.println("AP: " + String(ap_ssid) + "  sifre: " + String(ap_password));
  Serial.println("http://192.168.4.1");
  Serial.println("========================================");

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                    IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(ap_ssid, ap_password)) {
    Serial.println("HATA: softAP acilamadi!");
  }
  delay(300);

  drawWifiSetupScreen();

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Web sunucusu hazir (port 80)");
}

void drawMainDashboard() {
  resetMainPageCaches();
  prevChargeStatus = "";
  clearMainPageBatteryLabels();
  drawBattery(true, false);
  updateLogoByRSSI();
  drawBottomStatusBar();
  drawDHT11Data(true);
}

// =======================================================
// 🟦 WIFI BAĞLANTISI
// =======================================================
void connectWiFiAndNTP() {
  sanitizeWifiPrefs();

  prefs.begin("wifi", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();
  savedSSID.trim();

  if (savedSSID.length() > 0) {
    Serial.print("Kayitli WiFi'ye baglaniliyor: ");
    Serial.println(savedSSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    
    // 10 saniye bekle (20 x 500ms)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi BAGLANDI!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      
      ipAddress = WiFi.localIP().toString();
      wifiConnectedTime = millis();
      wifiWasConnected = true;
      wifiSetupMode = false;
      WiFi.setSleep(WIFI_PS_NONE);
      configureWifiDns();
      invalidateRegionalWeather();

      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      
      struct tm timeinfo;
      int ntpAttempts = 0;
      while (!getLocalTime(&timeinfo) && ntpAttempts < 10) {
        Serial.println("NTP alinamadi...");
        delay(500);
        ntpAttempts++;
      }
      
      return; // Başarılı, normal çalışmaya devam et
    }
    
    Serial.println("\nWiFi baglanamadi — cevrimdisi ana sayfa");
  } else {
    Serial.println("Kayitli WiFi yok — cevrimdisi ana sayfa");
    Serial.println("WiFi kur: Menu > WiFi Ayarlari");
  }
}

// =======================================================
// 🟦 TOPLAM ÇALIŞMA SÜRESİNİ KAYDET
// =======================================================
void saveTotalUptime() {
  unsigned long currentUptimeSeconds = (millis() - systemStartTime) / 1000;
  unsigned long newTotalSeconds = totalUptimeSeconds + currentUptimeSeconds;
  
  prefs.begin("stats", false);
  prefs.putULong64("totalUptime", newTotalSeconds);
  prefs.end();
  
  // Kayıt yapıldıktan sonra systemStartTime'ı sıfırla ve totalUptimeSeconds'ı güncelle
  totalUptimeSeconds = newTotalSeconds;
  systemStartTime = millis();
  
  Serial.print("Toplam calisma suresi kaydedildi: ");
  Serial.print(totalUptimeSeconds / 86400);
  Serial.print(" gun, ");
  Serial.print((totalUptimeSeconds % 86400) / 3600);
  Serial.print(" saat, ");
  Serial.print((totalUptimeSeconds % 3600) / 60);
  Serial.println(" dakika");
}

// =======================================================
// 🟦 TOPLAM ÇALIŞMA SÜRESİNİ YÜKLE
// =======================================================
void loadTotalUptime() {
  prefs.begin("stats", true);  // Read-only mode
  totalUptimeSeconds = prefs.getULong64("totalUptime", 0);
  prefs.end();
  
  if (totalUptimeSeconds > 0) {
    Serial.print("Kayitli toplam calisma suresi yuklendi: ");
    Serial.print(totalUptimeSeconds / 86400);
    Serial.print(" gun, ");
    Serial.print((totalUptimeSeconds % 86400) / 3600);
    Serial.print(" saat, ");
    Serial.print((totalUptimeSeconds % 3600) / 60);
    Serial.println(" dakika");
  } else {
    Serial.println("Kayitli calisma suresi bulunamadi (ilk calistirma)");
  }
}

// =======================================================
// 🟦 DİL SİSTEMİ - ÇEVİRİ FONKSİYONLARI
// =======================================================
String getText(const char* tr, const char* en) {
  return (currentLanguage == 0) ? String(tr) : String(en);
}

// Menü item'ları için
String getMenuText(int index) {
  const char* menuItemsTR[] = {
    "Parlaklik",
    "WiFi Ayarlari",
    "Dil",
    "Istatistikler",
    "Pil Durumu",
    "Sistem Bilgileri",
    "WiFi Sifirla",
    "Geri Don"
  };
  const char* menuItemsEN[] = {
    "Brightness",
    "WiFi Settings",
    "Language",
    "Statistics",
    "Battery",
    "System Info",
    "Reset WiFi",
    "Back"
  };
  
  if (currentLanguage == 0) {
    return String(menuItemsTR[index]);
  } else {
    return String(menuItemsEN[index]);
  }
}

// Sayfa başlıkları için
String getTitleText(int index) {
  const char* titlesTR[] = {
    "AYARLAR",
    "PARLAKLIK",
    "DIL",
    "WIFI BILGILERI",
    "ISTATISTIKLER",
    "SISTEM BILGILERI",
    "EMIN MISINIZ?",
    "WiFi SIFIRLA",
    "PIL DURUMU"
  };
  const char* titlesEN[] = {
    "SETTINGS",
    "BRIGHTNESS",
    "LANGUAGE",
    "WIFI INFO",
    "STATISTICS",
    "SYSTEM INFO",
    "ARE YOU SURE?",
    "RESET WiFi",
    "BATTERY"
  };
  
  if (currentLanguage == 0) {
    return String(titlesTR[index]);
  } else {
    return String(titlesEN[index]);
  }
}

// =======================================================
// 🟦 DİL TERCİHİNİ YÜKLE
// =======================================================
void loadLanguage() {
  prefs.begin("settings", true);  // Read-only mode
  currentLanguage = prefs.getInt("language", 0);  // Varsayılan: Türkçe (0)
  prefs.end();
  
  Serial.print("Dil yuklendi: ");
  Serial.println(currentLanguage == 0 ? "Turkce" : "English");
}

// =======================================================
// 🟦 DİL TERCİHİNİ KAYDET
// =======================================================
void saveLanguage() {
  prefs.begin("settings", false);
  prefs.putInt("language", currentLanguage);
  prefs.end();
  
  Serial.print("Dil kaydedildi: ");
  Serial.println(currentLanguage == 0 ? "Turkce" : "English");
}

// =======================================================
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== ESP32-S3 Super Mini boot ===");
  Serial.println(VERSION_TEXT);
  printChipInfo();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);

  // Deep Sleep'ten uyanma sebebini kontrol et
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
    case ESP_SLEEP_WAKEUP_GPIO:
      Serial.println("UYANMA SEBEBI: GPIO (Buton)");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("UYANMA SEBEBI: Timer");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
      Serial.println("UYANMA SEBEBI: Normal baslatma veya belirsiz");
      break;
  }
  
  // Kayıtlı toplam çalışma süresini yükle
  loadTotalUptime();
  
  // Dil tercihini yükle
  loadLanguage();

  fetchLocationData();
  
  // Sistem başlangıç zamanını kaydet
  systemStartTime = millis();

  printPinMap();

  initDisplay();

#if USE_DHT11
  dht.begin();
  Serial.println("DHT11 baslatildi!");
#else
  Serial.println("DHT11 kapali (GPIO2 = BL)");
#endif

  pinMode(BAT_ADC_PIN, INPUT);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  Serial.println("Batarya olcumu baslatildi (GPIO1 / A0)");

  Serial.println("Buzzer baslatildi (GPIO11)");

  setBrightness(brightness);

  // Encoder başlatma
  initEncoder();

  pinMode(PIN_CHRG, INPUT_PULLUP);
  pinMode(PIN_STDBY, INPUT_PULLUP);

  initOnboardRgb();

  showSplashScreen();
  connectWiFiAndNTP();

  if (WiFi.status() == WL_CONNECTED) {
    delay(2000);
    fetchTemperatureData();
  }

  prevDate = "";
  prevTime = "";
  drawMainDashboard();
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    drawTimeAndDate(timeinfo);
  }
  resetLeftInfoPanelCache();
  drawDHT11Data();
  drawBottomStatusBar();
  if (WiFi.status() == WL_CONNECTED) {
    startOTA();
  }
  
  // İlk aktivite zamanını kaydet
  lastActivityTime = millis();
}

// =======================================================
void loop() {
  if (wifiSetupMode) {
    server.handleClient();

    if (encoderButtonPressed) {
      encoderButtonPressed = false;
      exitWifiSetupMode();
      return;
    }

    static unsigned long lastPulse = 0;
    static uint8_t pulse = 0;
    if (millis() - lastPulse >= 600) {
      lastPulse = millis();
      pulse = (pulse + 1) % 4;
      String st = getText("telefon/baglanti", "phone/link");
      for (uint8_t i = 0; i < pulse; i++) {
        st += ".";
      }
      drawWifiSetupStatus(st.c_str());
    }

    updateOnboardRgb();
    return;
  }
  
  // OTA — ekran koruyucudan once; upload sirasinda da dinlemeli
  if (WiFi.status() == WL_CONNECTED) {
    startOTA();
    ArduinoOTA.handle();
  }

  // Ekran koruyucu kontrolü
  checkScreenSaver();
  
  updateTimeIfNeeded();
  updateRegionalWeather();
  handleEncoderNavigation();
  checkWiFiConnection();
  updateOnboardRgb();

  unsigned long now = millis();
  if (now - lastUptimeSave >= uptimeSaveInterval) {
    lastUptimeSave = now;
    saveTotalUptime();
  }
}
