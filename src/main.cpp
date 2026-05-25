#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include "logo.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include <WiFiManager.h>
#include <WebServer.h>

#define VERSION_TEXT "v1.5.0"   // 🔥 SAG ALTA GÖRÜNECEK VERSİYON

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

// 🔹 TFT Pinleri
#define TFT_CS   10
#define TFT_DC    7
#define TFT_RST   5
#define TFT_SCLK  4
#define TFT_MOSI  6

// 🔹 DHT11 Pin
#define DHT_PIN   2   // GPIO2 - DHT11 DATA pini

// 🔹 Batarya ölçümü (LiPo → R1 100k → GPIO0 ← R2 200k → GND)
#define BAT_ADC_PIN       0
// Tek nokta kalibrasyon: multimetre pil / ESP analogRead (aynı anda ölçülmeli)
// İnce ayar: multimetre 4.07V, ekran 4.14V → ESP_ADC *= 4.14/4.07
#define BAT_CALIB_VBAT        4.07f
#define BAT_CALIB_ESP_ADC_V   2.803f
#define BAT_VOLT_SCALE        (BAT_CALIB_VBAT / BAT_CALIB_ESP_ADC_V)
#define BAT_FULL_V            4.20f
#define BAT_EMPTY_V           3.00f
#define BAT_DISPLAY_MAX_V     4.35f

// 🔹 TP4056 (active LOW) — CHRG=şarj, STDBY=şarj tamam
#define PIN_CHRG   21
#define PIN_STDBY  20

// 🔹 Encoder Pinleri
#define ENCODER_CLK   8   // GPIO8 - CLK pini
#define ENCODER_DT    9   // GPIO9 - DT pini  
#define ENCODER_SW    3   // GPIO3 - Switch/Button pini

// 🔹 Backlight Pin (PWM)
#define TFT_BACKLIGHT 1   // GPIO1 - Backlight PWM pini
#define LEDC_FREQ 5000    // PWM frekansı (Hz)
#define LEDC_RESOLUTION 8 // 8-bit çözünürlük (0-255)
#define USE_PWM true      // PWM kullanılsın
#define BACKLIGHT_LEDC_CHANNEL 0

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

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
DHT dht(DHT_PIN, DHT11);

// =====================================================
unsigned long lastTimeUpdate = 0;
const unsigned long timeInterval = 1000;

String prevTime = "";
String prevDate = "";
String prevTemp = "";
String prevHum = "";
String prevBattery = "";
String prevChargeStatus = "";
static int lastBatteryPctForIcon = 0;
String ipAddress = "";
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
int currentMenuPage = 0;  // 0:Ana 1:Ayarlar 2:Parlaklik 3:WiFi 4:Istatistik 5:Sistem 6:WiFiSifirlaOnay 7:Dil 8:PilDurumu
int menuItem = 0;

// 🔹 İstatistikler
float minTemp = 999.0;
float maxTemp = -999.0;
float minHum = 999.0;
float maxHum = -999.0;
float sumTemp = 0.0;
float sumHum = 0.0;
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

// =======================================================
//  🟦 OTA BAŞLATMA
// =======================================================
static bool otaInitialized = false;

void resetOTA() {
  otaInitialized = false;
}

void startOTA() {
  if (otaInitialized) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi bagli degil - OTA baslatilmiyor");
    return;
  }

  Serial.println("OTA yukleme servisi baslatiliyor...");

  if (!MDNS.begin(otaName)) {
    Serial.println("mDNS baslatilamadi!");
  }

  ArduinoOTA.setHostname(otaName);
  ArduinoOTA.setPassword(otaPass);
  ArduinoOTA.setRebootOnSuccess(true);

  ArduinoOTA
      .onStart([]() {
        Serial.println("OTA Basladi!");
        prevOTAPercent = -1;  // Reset progress tracking
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
        Serial.println("OTA Tamamlandi!");
        prevOTAPercent = -1;  // Reset
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(10, 70);
        tft.setTextColor(ST77XX_GREEN);
        tft.setTextSize(2);
        tft.println("OTA Tamamlandi!");
        delay(2000);
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        int percent = (progress * 100) / total;
        
        // Progress bar genişliği (çerçeve içinde)
        int barWidth = TFT_WIDTH - 22;
        int filledWidth = (progress * barWidth) / total;
        
        // Sadece yeni eklenen kısmı çiz (kırpma önleme)
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
  Serial.println("PlatformIO: env esp32c3_super_mini_ota");
  Serial.println("========================================");
}

// =======================================================
// 🟦 VERSİYON YAZISI (SAĞ-ALT)
// =======================================================
void drawVersionText() {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  int16_t x1, y1;
  uint16_t w, h;

  tft.getTextBounds(VERSION_TEXT, 0, 0, &x1, &y1, &w, &h);

  int x = TFT_WIDTH - w - 6;
  int y = TFT_HEIGHT - h - 6;

  tft.fillRect(x, y, w + 2, h + 2, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.println(VERSION_TEXT);
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
  
  // Encoder butonunu (GPIO3) wake-up pini olarak ayarla
  // ESP32-C3'te Deep Sleep için GPIO0-5 (RTC GPIO'lar) kullanılabilir
  // GPIO3 RTC GPIO olduğu için kullanılabilir
  // Buton pull-up olduğu için basıldığında LOW olur
  
  // Encoder interrupt'ını devre dışı bırak (Deep Sleep için gerekli)
  detachInterrupt(digitalPinToInterrupt(ENCODER_CLK));
  
  // GPIO'yu INPUT olarak yapılandır (Deep Sleep için gerekli)
  // Pull-up aktif olmalı (buton basıldığında LOW olacak)
  pinMode(ENCODER_SW, INPUT_PULLUP);
  delay(200);  // Yapılandırmanın tamamlanması için bekle
  
  // ESP32-C3 için Deep Sleep GPIO wake-up yapılandırması
  // ESP32-C3'te esp_deep_sleep_enable_gpio_wakeup() direkt kullanılır
  // Buton pull-up olduğu için basıldığında LOW olur
  // GPIO3 bit mask: (1ULL << 3)
  
  // Yedek olarak timer wake-up ekle (1 saat sonra otomatik uyanır)
  // Eğer GPIO wake-up çalışmazsa en azından timer ile uyanır
  esp_sleep_enable_timer_wakeup(3600000000ULL);  // 1 saat = 3600 saniye * 1000000 mikrosaniye
  
  // GPIO wake-up'ı etkinleştir (bitmask ile)
  // ESP_GPIO_WAKEUP_GPIO_LOW = LOW seviyesinde uyandır
  esp_deep_sleep_enable_gpio_wakeup((1ULL << ENCODER_SW), ESP_GPIO_WAKEUP_GPIO_LOW);
  
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

  // Tarih
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.getTextBounds(bufDate, 0, 0, &x1, &y1, &w, &h);

  int dateX = (TFT_WIDTH - w) / 2;
  int dateY = 14;

  if (curDate != prevDate) {
    tft.fillRect(0, dateY, TFT_WIDTH - topBarReservedWidth(), h + 4, ST77XX_BLACK);
    tft.setCursor(dateX, dateY);
    tft.println(curDate);
    prevDate = curDate;
    drawBatteryIcon();
    updateLogoByRSSI();
  }

  // Saat
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.getTextBounds(bufTime, 0, 0, &x1, &y1, &w, &h);

  int timeX = (TFT_WIDTH - w) / 2;
  int timeY = dateY + h + 8;

  if (curTime != prevTime) {
    tft.fillRect(0, timeY, TFT_WIDTH - topBarReservedWidth(), h + 6, ST77XX_BLACK);
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
  // Menü butonu - alt ortada (yazı olarak)
  tft.setTextSize(3);  // TextSize 2'den 3'e çıkarıldı
  tft.setTextColor(ST77XX_CYAN);
  
  String menuText = "MENU";
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(menuText, 0, 0, &x1, &y1, &w, &h);
  
  // Alt ortada konumlandır
  int x = (TFT_WIDTH - w) / 2;
  int y = TFT_HEIGHT - h - 8;
  
  // Önceki yazıyı temizle
  tft.fillRect(x - 2, y - 2, w + 4, h + 4, ST77XX_BLACK);
  
  // Menü yazısını çiz
  tft.setCursor(x, y);
  tft.println(menuText);
}

void drawLogo(const unsigned char *bitmap, int w, int h) {
  int x = wifiIconX();
  int y = wifiIconY();
  tft.fillRect(x, y, w, h, ST77XX_BLACK);
  tft.drawBitmap(x, y, bitmap, w, h, ST77XX_WHITE);
}

void updateLogoByRSSI() {
  int rssi = WiFi.RSSI();

  if (rssi >= -60)
    drawLogo(epd_bitmap_High, 17, 13);
  else if (rssi >= -80)
    drawLogo(epd_bitmap_Mid, 17, 13);
  else
    drawLogo(epd_bitmap_Low, 17, 13);
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

  if (ipAddress != "") {

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);

    int16_t x1, y1;
    uint16_t w, h;

    tft.getTextBounds(ipAddress.c_str(), 0, 0, &x1, &y1, &w, &h);

    int x = 6;
    int y = TFT_HEIGHT - h - 6;  // Bir alt satıra indirildi

    tft.fillRect(x, y, w + 2, h + 4, ST77XX_BLACK);
    tft.setCursor(x, y);
    tft.println(ipAddress);
  }
}

// =======================================================
// 🟦 BATARYA ÖLÇÜMÜ (voltaj bölücü GPIO0)
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
  const int y = 34;  // Nem satirinin alti (bos alan)

  tft.fillRect(x, y, 120, h + 4, ST77XX_BLACK);
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

void drawBattery(bool forceRead = false) {
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

  tft.setTextSize(1);
  tft.setTextColor(batteryColor(pct));

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(batStr, 0, 0, &x1, &y1, &w, &h);

  int textX = (TFT_WIDTH - w) / 2;
  int textY = (TFT_HEIGHT - h) / 2;

  drawBatteryIcon();

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
void drawDHT11Data() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT11 okuma hatasi!");
    return;
  }

  // İstatistikleri güncelle
  if (temperature < minTemp) minTemp = temperature;
  if (temperature > maxTemp) maxTemp = temperature;
  if (humidity < minHum) minHum = humidity;
  if (humidity > maxHum) maxHum = humidity;
  sumTemp += temperature;
  sumHum += humidity;
  readingCount++;

  String tempStr = String(temperature, 1) + " C";
  String humStr = String(humidity, 1) + "%";

  // Sıcaklık güncellemesi - SOL ÜST
  if (tempStr != prevTemp) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);

    int x = 6;
    int y = 6;

    tft.fillRect(x, y, w + 4, h + 4, ST77XX_BLACK);
    tft.setCursor(x, y);
    tft.println(tempStr);
    prevTemp = tempStr;
  }

  // Nem güncellemesi - SOL ÜST (Sıcaklığın altında)
  if (humStr != prevHum) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(humStr, 0, 0, &x1, &y1, &w, &h);

    int x = 6;
    int y = 20;  // Sıcaklığın hemen altında

    tft.fillRect(x, y, w + 4, h + 4, ST77XX_BLACK);
    tft.setCursor(x, y);
    tft.println(humStr);
    prevHum = humStr;
  }
}

// =======================================================
// 🟦 İSTATİSTİKLER SAYFASI
// =======================================================
void showStatisticsMenu(bool reset = false) {
  static bool firstDraw = true;
  static String prevUptimeStr = "";
  static String prevWifiUptimeStr = "";
  
  // Değişkenleri fonksiyonun başında tanımla
  int lineHeight = 18;
  int yPos = 35;
  
  if (reset) {
    firstDraw = true;
    prevUptimeStr = "";
    prevWifiUptimeStr = "";
    return;
  }
  
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
    tft.setTextColor(ST77XX_WHITE);
    
    yPos = 35;  // Başlangıç pozisyonu
    
    // Ortalama Sıcaklık
    if (readingCount > 0) {
      float avgTemp = sumTemp / readingCount;
      String avgTempStr = "Ort. Sicaklik: " + String(avgTemp, 1) + " C";
      tft.setCursor(10, yPos);
      tft.println(avgTempStr);
      yPos += lineHeight;
      
      // Maksimum/Minimum Sıcaklık
      String tempRangeStr = "Sicaklik: " + String(minTemp, 1) + " / " + String(maxTemp, 1) + " C";
      tft.setCursor(10, yPos);
      tft.println(tempRangeStr);
      yPos += lineHeight;
      
      // Ortalama Nem
      float avgHum = sumHum / readingCount;
      String avgHumStr = "Ort. Nem: " + String(avgHum, 1) + " %";
      tft.setCursor(10, yPos);
      tft.println(avgHumStr);
      yPos += lineHeight;
      
      // Maksimum/Minimum Nem
      String humRangeStr = "Nem: " + String(minHum, 1) + " / " + String(maxHum, 1) + " %";
      tft.setCursor(10, yPos);
      tft.println(humRangeStr);
      yPos += lineHeight;
    } else {
      String noDataStr = "Henuz veri yok";
      tft.setCursor(10, yPos);
      tft.println(noDataStr);
      yPos += lineHeight * 2;
    }
    
    // Talimat (ortalanmış)
    tft.setTextColor(ST77XX_CYAN);
    String instructionText = "Buton ile geri don";
    tft.getTextBounds(instructionText, 0, 0, &x1, &y1, &w, &h);
    int instX = (TFT_WIDTH - w) / 2;
    tft.setCursor(instX, 155);
    tft.println(instructionText);
    
    // İlk çizimde süreleri de göster
    prevUptimeStr = "";
    prevWifiUptimeStr = "";
  }
  
  // Sadece süreleri güncelle (canlı)
  // İstatistik verilerinin yüksekliğini hesapla
  yPos = 35;  // Başlangıç pozisyonu
  if (readingCount > 0) {
    yPos += lineHeight * 4;  // 4 satır istatistik
  } else {
    yPos += lineHeight * 2;  // "Henuz veri yok" mesajı
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
  
  // CPU Frekansı (ESP32-C3 için)
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
  
  // Menü kaydırma - eğer menuItem 4 veya üzeri ise kaydır
  int startIndex = 0;
  if (menuItem >= 4) {
    startIndex = menuItem - 3;  // En fazla 4 item göster, seçili item ortada olsun
    if (startIndex > 4) startIndex = 4;  // 8 item, 4 gorunur
  }
  
  // Ekranda gösterilecek item sayısı (maksimum 4 item)
  int visibleItems = 4;
  int endIndex = startIndex + visibleItems;
  if (endIndex > 8) endIndex = 8;  // 8 item (Pil Durumu eklendi)
  
  for (int i = startIndex; i < endIndex; i++) {
    int displayIndex = i - startIndex;
    if (i == menuItem) {
      // Seçili item: CYAN arka plan, WHITE text (ana sayfa uyumlu)
      tft.fillRect(15, 38 + (displayIndex * 25), TFT_WIDTH - 30, 22, ST77XX_CYAN);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setCursor(20, 40 + (displayIndex * 25));
    tft.println(getMenuText(i));
  }
  
  // Scroll göstergesi (eğer kaydırma varsa)
  if (startIndex > 0 || endIndex < 8) {
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
  
  // WiFi durumu kontrolü
  if (WiFi.status() != WL_CONNECTED) {
    if (firstDraw || prevRSSI != -999) {
      tft.fillRect(10, 40, TFT_WIDTH - 20, 100, ST77XX_BLACK);
      tft.setTextSize(2);
      tft.setTextColor(ST77XX_RED);
      String errorText = getText("BAGLI DEGIL!", "NOT CONNECTED!");
      int16_t x1, y1;
      uint16_t w, h;
      tft.getTextBounds(errorText, 0, 0, &x1, &y1, &w, &h);
      int errorX = (TFT_WIDTH - w) / 2;
      tft.setCursor(errorX, 50);
      tft.println(errorText);
      prevRSSI = -999;
    }
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
      // Ayarlar menüsünde item seçimi
      menuItem += diff;
      if (menuItem < 0) menuItem = 0;
      if (menuItem > 7) menuItem = 7;  // 8 item (0-7)
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
  }
  
  // Buton kontrolü
  checkEncoderButton();
  
  if (encoderButtonPressed) {
    encoderButtonPressed = false;
    
    // Buton basımı = aktivite (ekran koruyucuyu kapat)
    updateActivity();
    
    if (currentMenuPage == 0) {
      // Ana sayfadan ayarlar menüsüne geç
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
        // "WiFi Ayarlari" seçildi - WiFi bilgileri sayfasına geç
        currentMenuPage = 3;
        showWiFiInfoMenu(true);  // Reset
        showWiFiInfoMenu();      // İlk çizim
      } else if (menuItem == 2) {
        // "Dil" seçildi - Dil seçim sayfasına geç
        currentMenuPage = 7;  // Dil seçim sayfası
        languageSelectionItem = currentLanguage;  // Mevcut dili seçili göster
        showLanguageMenu();
      } else if (menuItem == 3) {
        // "Istatistikler" seçildi - İstatistikler sayfasına geç
        currentMenuPage = 4;
        showStatisticsMenu(true);  // Reset
        showStatisticsMenu();      // İlk çizim
      } else if (menuItem == 4) {
        // "Pil Durumu" seçildi
        currentMenuPage = 8;
        showBatteryHealthMenu(true);
        showBatteryHealthMenu();
      } else if (menuItem == 5) {
        // "Sistem Bilgileri" seçildi
        currentMenuPage = 5;
        showSystemInfoMenu(true);
        showSystemInfoMenu();
      } else if (menuItem == 6) {
        // "WiFi Sifirla" seçildi
        currentMenuPage = 6;
        showWiFiResetConfirm();
      } else if (menuItem == 7) {
        // "Geri Don" seçildi - ana sayfaya dön
        currentMenuPage = 0;
        tft.fillScreen(ST77XX_BLACK);
        
        // Önceki değerleri sıfırla ki tekrar çizilsin
        prevTime = "";
        prevDate = "";
        prevTemp = "";
        prevHum = "";
        prevBattery = "";
        
        // lastTimeUpdate'i sıfırla ki hemen güncellensin
        lastTimeUpdate = 0;
        lastBatteryRead = 0;
        
        // Ana sayfa çizimlerini hemen yeniden çiz
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
          drawTimeAndDate(timeinfo);
          drawDHT11Data();
          drawChargeStatus();
          drawBattery(true);
          updateLogoByRSSI();
          drawIPAddress();
          drawMenuButton();     // 🔥 MENÜ BUTONU
          drawVersionText();
        }
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
      // İstatistikler sayfasından ayarlar menüsüne geri dön
      currentMenuPage = 1;
      menuItem = 3;  // Istatistikler seçili kalsın (index 3)
      showStatisticsMenu(true);  // Reset
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
        menuItem = 6;
        wifiResetConfirmItem = 0;  // Reset
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
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        prevTime = "";  // Yeniden çizilsin
        prevDate = "";
        prevTemp = "";
        prevHum = "";
        prevBattery = "";
        prevChargeStatus = "";
        drawTimeAndDate(timeinfo);
        drawDHT11Data();
        drawChargeStatus();
        drawBattery(true);
        updateLogoByRSSI();
        drawIPAddress();
        drawMenuButton();
        drawVersionText();
      }
    }
  }

  if (now - lastTimeUpdate >= timeInterval) {
    lastTimeUpdate = now;
    
    // Ekran koruyucu aktifse zaman güncelleme yapma
    if (screenSaverActive) {
      return;
    }

    if (currentMenuPage == 0) {
      // Ana sayfa çizimleri
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        drawTimeAndDate(timeinfo);
        drawDHT11Data();      // 🔥 DHT11 VERİLERİNİ GÖSTER
        drawChargeStatus();
        drawBattery();
        updateLogoByRSSI();
        drawIPAddress();
        drawMenuButton();     // 🔥 MENÜ BUTONU
        drawVersionText();   // 🔥 VERSİYON SÜREKLİ GÜNCELLENİR
      }
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
        lastReconnectAttempt = 0;  // Hemen dene
        wifiWasConnected = false;  // Bağlantı koptu
        resetOTA();
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
        
        if (savedSSID.length() > 0 && savedPass.length() > 0) {
          // WiFi'yi durdur ve yeniden başlat (hızlı, blocking değil)
          WiFi.disconnect();
          delay(100);  // Kısa delay, blocking değil
          WiFi.mode(WIFI_STA);
          WiFi.begin(savedSSID.c_str(), savedPass.c_str());
          
          Serial.println("baslatildi (non-blocking)");
        } else {
          Serial.println("Kayitli WiFi bilgisi yok, WiFiManager aciliyor...");
          // WiFi bilgisi yoksa veya şifre yoksa WiFiManager'ı başlat
          wifiSetupMode = true;
          WiFi.disconnect();
          delay(100);
          // WiFiManager loop() içinde handle edilecek
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
        Serial.println("WiFi YENİDEN BAGLANDI!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        
        ipAddress = WiFi.localIP().toString();
        
        // WiFi bağlantı zamanını güncelle (yeniden bağlandı)
        wifiConnectedTime = millis();
        wifiWasConnected = true;
        
        // NTP'yi yeniden yapılandır (hızlı, blocking değil)
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

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
  html += "<input type='text' id='ssid' name='ssid' required placeholder='WiFi ağ adını girin'>";
  html += "</div>";
  html += "<div class='form-group'>";
  html += "<label for='pass'>WiFi Şifresi:</label>";
  html += "<input type='password' id='pass' name='pass' required placeholder='WiFi şifresini girin'>";
  html += "</div>";
  html += "<button type='submit'>🔗 WiFi'ye Bağlan</button>";
  html += "</form>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
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
    
    // WiFiManager'a da kaydet
    wifiManager.setSTAStaticIPConfig(IPAddress(), IPAddress(), IPAddress());
    
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
// 🟦 WIFI MANAGER İLE BAĞLANTI
// =======================================================
void connectWiFiAndNTP() {
  // Önce kayıtlı WiFi bilgilerini kontrol et
  prefs.begin("wifi", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();
  
  // Eğer kayıtlı WiFi bilgisi varsa dene
  if (savedSSID.length() > 0 && savedPass.length() > 0) {
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
      
      // NTP yapılandırması
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
    
    Serial.println("\nBaglanamadi, WiFiManager aciliyor...");
    // Bağlanamadı, AP moduna geç
  } else {
    Serial.println("Kayitli WiFi bilgisi yok, WiFiManager aciliyor...");
  }
  
  // WiFi bilgisi yoksa veya bağlanamadıysa WiFiManager'ı başlat
  wifiSetupMode = true;
  
  Serial.println("========================================");
  Serial.println("WiFiManager AP Modu baslatiliyor...");
  Serial.println("AP Ag Adi: " + String(ap_ssid));
  Serial.println("AP Sifresi: " + String(ap_password));
  Serial.println("AP IP: 192.168.4.1");
  Serial.println("========================================");
  
  // WiFi'yi temizle
  WiFi.disconnect();
  delay(500);
  
  // Ekranda bilgilendirme göster
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(20, 20);
  tft.println("WiFi AYARLARI");
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 50);
  tft.println("Ag adi:");
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(10, 65);
  tft.println(ap_ssid);
  
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 85);
  tft.println("Sifre:");
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(10, 100);
  tft.println(ap_password);
  
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 125);
  tft.println("Telefonunuzdan baglanip");
  tft.setCursor(10, 140);
  tft.println("ayarlari yapiniz");
  
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 158);
  tft.println("192.168.4.1");
  
  // WiFiManager ayarları
  wifiManager.setConfigPortalTimeout(180);  // 3 dakika timeout
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), 
                                   IPAddress(192,168,4,1), 
                                   IPAddress(255,255,255,0));
  
  // WiFi'yi açıkça AP moduna geçir
  WiFi.mode(WIFI_AP_STA);  // Hem AP hem Station modu
  
  Serial.println("AP modu aciliyor...");
  delay(1000);  // AP'nin açılması için bekleme
  
  // WiFiManager'ı non-blocking modda başlat
  // startConfigPortal() web sunucusunu başlatır ama blocking değil
  Serial.println("========================================");
  Serial.println("WiFiManager web sunucusu baslatiliyor...");
  Serial.println("Tarayicida http://192.168.4.1 adresini acin");
  Serial.println("========================================");
  
  // startConfigPortal() web sunucusunu başlatır (non-blocking)
  wifiManager.startConfigPortal(ap_ssid, ap_password);
  
  Serial.println("Web sunucusu baslatildi! http://192.168.4.1");
  
  // Web sunucusu route'larını ayarla
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Web sunucusu route'lari ayarlandi: / ve /save");
  
  // WiFi bağlantısı web arayüzünden yapılacak
  // handleSave() fonksiyonu WiFi bilgilerini kaydedip ESP32'yi yeniden başlatacak
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
  delay(500);
  
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
  
  // Sistem başlangıç zamanını kaydet
  systemStartTime = millis();

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(TFT_HEIGHT, TFT_WIDTH);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  // DHT11 başlatma
  dht.begin();
  Serial.println("DHT11 baslatildi!");

  // Batarya ADC (GPIO0, voltaj bölücü)
  pinMode(BAT_ADC_PIN, INPUT);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  Serial.println("Batarya olcumu baslatildi (GPIO0)");

  // Backlight PWM başlatma
  Serial.print("Backlight pin'i ayarlaniyor: GPIO");
  Serial.println(TFT_BACKLIGHT);
  Serial.print("PWM Frekans: ");
  Serial.print(LEDC_FREQ);
  Serial.print(" Hz, Cozunurluk: ");
  Serial.print(LEDC_RESOLUTION);
  Serial.println(" bit");
  
  // Pin'i OUTPUT olarak ayarla
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, LOW);  // Önce LOW
  delay(10);
  
  if (USE_PWM) {
    ledcSetup(BACKLIGHT_LEDC_CHANNEL, LEDC_FREQ, LEDC_RESOLUTION);
    ledcAttachPin(TFT_BACKLIGHT, BACKLIGHT_LEDC_CHANNEL);
    delay(10);
    Serial.println("PWM modu aktif - LEDC baslatildi");
  } else {
    // Digital modu (açık/kapalı)
    Serial.println("Digital modu aktif (acik/kapali)");
  }
  
  // Varsayılan parlaklığa ayarla
  setBrightness(brightness);
  
  Serial.println("Backlight baslatildi!");

  // Encoder başlatma
  initEncoder();

  pinMode(PIN_CHRG, INPUT_PULLUP);
  pinMode(PIN_STDBY, INPUT_PULLUP);

  showSplashScreen();
  connectWiFiAndNTP();

  drawVersionText();  // Açılışta da yazılsın
  drawChargeStatus();
  drawBattery(true);  // İlk batarya okuması
  updateLogoByRSSI();

  // OTA'yı sadece WiFi bağlıysa başlat (startOTA içinde de kontrol var ama burada da kontrol edelim)
  if (WiFi.status() == WL_CONNECTED) {
    startOTA();
  }
  
  // İlk aktivite zamanını kaydet
  lastActivityTime = millis();
}

// =======================================================
void loop() {
  // WiFiManager AP modunda çalışıyorsa handle et
  if (wifiSetupMode) {
    wifiManager.process();  // WiFiManager isteklerini işle
    server.handleClient();  // Web sunucusu isteklerini işle
    
    // WiFi bağlandı mı kontrol et
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0) {
      // WiFi bağlandı ama AP modu hala açık
      // WiFiManager otomatik olarak kapatacak, biraz bekle
      delay(1000);
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi'ye baglanildi! AP modu kapatiliyor...");
        wifiSetupMode = false;
        
        // WiFi bilgilerini kaydet
        Preferences wmPrefs;
        String wifiPass = "";
        wmPrefs.begin("wifimanager", true);
        wifiPass = wmPrefs.getString("pwd", "");
        if (wifiPass.length() == 0) {
          wifiPass = wmPrefs.getString("password", "");
        }
        wmPrefs.end();
        
        prefs.begin("wifi", false);
        prefs.putString("ssid", WiFi.SSID());
        if (wifiPass.length() > 0) {
          prefs.putString("pass", wifiPass);
        }
        prefs.end();
        
        ipAddress = WiFi.localIP().toString();
        wifiConnectedTime = millis();
        wifiWasConnected = true;
        
        // NTP yapılandırması
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        // Ekranı temizle
        tft.fillScreen(ST77XX_BLACK);
        
        // Web sunucusunu kapat
        server.stop();

        startOTA();
      }
    }
    
    return;  // AP modundayken diğer işlemleri yapma
  }
  
  // OTA — ekran koruyucudan once; upload sirasinda da dinlemeli
  if (WiFi.status() == WL_CONNECTED) {
    startOTA();
    ArduinoOTA.handle();
  }

  // Ekran koruyucu kontrolü
  checkScreenSaver();
  
  updateTimeIfNeeded();
  handleEncoderNavigation();  // 🔥 ENCODER KONTROLÜ
  checkWiFiConnection();      // 🔥 WIFI YENİDEN BAĞLANMA (Non-blocking, CPU dostu)
  
  // Toplam çalışma süresini belirli aralıklarla kaydet
  unsigned long now = millis();
  if (now - lastUptimeSave >= uptimeSaveInterval) {
    lastUptimeSave = now;
    saveTotalUptime();
  }
}
