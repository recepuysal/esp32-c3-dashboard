# ESP32-C3 Smart Dashboard

<div align="center">

![Version](https://img.shields.io/badge/version-1.5.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP32--C3-green.svg)
![License](https://img.shields.io/badge/license-MIT-orange.svg)
![PlatformIO](https://img.shields.io/badge/PlatformIO-PlatformIO-orange.svg)
![Arduino](https://img.shields.io/badge/Arduino-Compatible-blue.svg)
![GitHub](https://img.shields.io/github/stars/recepuysal/esp32-c3-dashboard?style=social)

**Akıllı Dijital Dashboard Projesi**

ESP32-C3 Super Mini ile ST7789 TFT ekran kullanarak gerçek zamanlı veri gösterimi yapan bir dashboard projesi.

[Özellikler](#-özellikler) • [Kurulum](#-kurulum) • [Kullanım](#-kullanım) • [Donanım](#-donanım) • [Geliştirme](#-geliştirme)

</div>

---

## 📋 İçindekiler

- [Özellikler](#-özellikler)
- [Ekran Görüntüleri](#-ekran-görüntüleri)
- [Donanım Gereksinimleri](#-donanım-gereksinimleri)
- [Kurulum](#-kurulum)
- [Yapılandırma](#-yapılandırma)
- [Kullanım](#-kullanım)
- [Pin Bağlantıları](#-pin-bağlantıları)
- [Proje Yapısı](#-proje-yapısı)
- [Özellik Detayları](#-özellik-detayları)
- [Sorun Giderme](#-sorun-giderme)
- [Geliştirme](#-geliştirme)
- [Katkıda Bulunma](#-katkıda-bulunma)
- [Lisans](#-lisans)

---

## ✨ Özellikler

### 🎯 Ana Özellikler

- **📺 ST7789 TFT Ekran Desteği**
  - 320x172 piksel çözünürlük
  - Optimize edilmiş grafik gösterimi
  - Düşük güç tüketimi

- **🌡️ DHT11 Sensör Entegrasyonu**
  - Gerçek zamanlı sıcaklık ölçümü
  - Nem ölçümü
  - Otomatik veri güncelleme

- **📶 WiFi Bağlantısı**
  - Otomatik WiFi bağlantısı
  - Sinyal gücü gösterimi (RSSI)
  - IP adresi gösterimi
  - Dinamik WiFi ikonları

- **🕐 NTP Zaman Senkronizasyonu**
  - Otomatik zaman senkronizasyonu
  - GMT+3 zaman dilimi desteği
  - Tarih ve saat gösterimi

- **🔄 OTA (Over-The-Air) Güncelleme**
  - Kablosuz firmware güncelleme
  - İlerleme çubuğu ve yüzde gösterimi
  - Güvenli güncelleme mekanizması

- **🎛️ Rotary Encoder Kontrolü**
  - Menü navigasyonu
  - Parlaklık kontrolü
  - Buton ile menü açma/kapama
  - Debounce koruması

- **📱 Menü Sistemi**
  - Ayarlar menüsü
  - Parlaklık ayarı sayfası
  - WiFi bilgileri sayfası
  - Encoder ile kolay navigasyon

- **💡 Parlaklık Kontrolü**
  - PWM ile ekran parlaklığı kontrolü
  - 0-100% arası ayar
  - Progress bar ile görsel gösterim
  - Gerçek zamanlı güncelleme

- **🔁 Otomatik WiFi Yeniden Bağlanma**
  - Bağlantı koptuğunda otomatik yeniden bağlanma
  - Non-blocking, CPU dostu
  - Periyodik durum kontrolü

- **🎨 Modern Arayüz**
  - Temiz ve okunabilir tasarım
  - Renkli bilgi gösterimi
  - Optimize edilmiş ekran güncellemeleri
  - Menü butonu ile kolay erişim

- **💾 Kalıcı Veri Saklama**
  - Toplam çalışma süresi kalıcı olarak kaydedilir
  - Preferences API ile güvenli veri saklama

- **🌐 WiFiManager + Captive Portal**
  - Kod değişikliği olmadan WiFi yapılandırması
  - Web tabanlı yapılandırma arayüzü
  - Otomatik Access Point (AP) modu
  - Captive portal desteği
  - Mobil uyumlu arayüz
  - WiFi ayarlarını sıfırlama özelliği
  - Onay ekranı ile güvenli sıfırlama

- **💤 Ekran Koruyucu (Screen Saver)**
  - 1 dakika hareketsizlik sonrası otomatik devreye girer
  - Açılış logosunu gösterir
  - Ekran parlaklığını %10'a düşürür (güç tasarrufu)
  - Encoder hareketi veya buton ile normal moda döner

- **🌙 Deep Sleep Modu**
  - 5 dakika ekran koruyucuda kalırsa otomatik devreye girer
  - Maksimum güç tasarrufu sağlar (mikroamper seviyesinde)
  - Encoder butonu ile uyandırılabilir
  - Yedek timer wake-up (1 saat sonra otomatik uyanır)

### 📊 Ekranda Gösterilen Bilgiler

- **Tarih ve Saat** (Merkez, üst)
  - Tarih: Cyan renk, 2x boyut
  - Saat: Beyaz renk, 3x boyut

- **Sıcaklık** (Sol üst)
  - Beyaz renk, 1x boyut
  - Format: "XX.X C"

- **Nem** (Sol üst, sıcaklığın altında)
  - Beyaz renk, 1x boyut
  - Format: "XX.X%"

- **Menü Butonu** (Alt orta)
  - Beyaz renk, 1x boyut
  - "MENU" yazısı
  - Encoder butonuna basarak menüye erişim

- **IP Adresi** (Sol alt)
  - Beyaz renk, 1x boyut

- **Versiyon Bilgisi** (Sağ alt)
  - Beyaz renk, 1x boyut
  - Format: "vX.X.X"

---

## 🖼️ Ekran Görüntüleri

### Proje Kurulumu

<div align="center">

![Proje Kurulumu](docs/images/project-setup.jpg)

*ESP32-C3 Super Mini, ST7789 TFT Ekran, DHT11 Sensör ve Breadboard Kurulumu*

</div>

### Ana Ekran
```
┌─────────────────────────────────┐
│  25.5 C                          │
│  60.0%                           │
│                                  │
│     01.01.2024                   │
│     14:30:45                     │
│                                  │
│                                  │
│              MENU                │
│                                  │
│ 192.168.1.100            v1.2.1  │
└─────────────────────────────────┘
```

### OTA Güncelleme Ekranı
```
┌─────────────────────────────────┐
│                                  │
│      OTA Yukleniyor             │
│                                  │
│  ┌─────────────────────────┐    │
│  │████████████░░░░░░░░░░░░░│    │
│  └─────────────────────────┘    │
│                                  │
│      45% tamamlandi              │
│                                  │
└─────────────────────────────────┘
```

---

## 🔧 Donanım Gereksinimleri

### Gerekli Bileşenler

| Bileşen | Miktar | Açıklama |
|---------|--------|----------|
| ESP32-C3 Super Mini | 1 | Ana mikrodenetleyici |
| ST7789 TFT Ekran | 1 | 320x172 piksel |
| DHT11 Sensör | 1 | Sıcaklık ve nem sensörü |
| 4.7kΩ - 10kΩ Direnç | 1 | DHT11 pull-up (modülde olabilir) |
| Jumper Kablolar | - | Bağlantı için |
| Breadboard | 1 | Prototipleme için (opsiyonel) |

### Önerilen Modüller

- **DHT11 Modülü**: Pull-up direnci dahil
- **ST7789 Modülü**: SPI bağlantılı

### Güç Yönetimi (Opsiyonel - Taşınabilir Kullanım İçin)

| Bileşen | Miktar | Açıklama |
|---------|--------|----------|
| TP4056 Şarj Modülü | 1 | LiPo batarya şarj modülü |
| DD0606SA Boost Converter | 1 | 3.7V'den 5V'a voltaj yükseltici |
| LiPo Batarya (653095P) | 1 | 3.7V, 2500mAh güç kaynağı |
| Toggle Switch | 1 | Açma/kapama butonu |

**Güç Yönetimi Avantajları:**
- ✅ Taşınabilir ve bağımsız çalışma
- ✅ Temiz ve kontrollü 5V güç kaynağı
- ✅ Batarya şarj yönetimi (TP4056 ile USB-C üzerinden)
- ✅ Güvenli açma/kapama kontrolü (Toggle Switch)
- ✅ Uzun çalışma süresi (2500mAh batarya)

**Güç Yönetimi Çalışma Prensibi:**
1. **TP4056 Şarj Modülü**: LiPo bataryayı USB-C üzerinden şarj eder
2. **LiPo Batarya (653095P)**: 3.7V, 2500mAh kapasiteli güç kaynağı
3. **Toggle Switch**: Batarya gücünü açma/kapama kontrolü
4. **DD0606SA Boost Converter**: 3.7V batarya voltajını 5V'a yükseltir
5. **5V Çıkış**: ESP32-C3 ve tüm sensörlere temiz, kontrollü 5V güç sağlar

**Önemli Notlar:**
- ESP32-C3 Super Mini 5V girişi destekler (VIN pin)
- Boost converter çıkışı ESP32'nin VIN pinine bağlanmalıdır
- Toggle switch batarya ile boost converter arasına yerleştirilmelidir
- TP4056 modülü bataryayı şarj ederken sistem çalışmaya devam edebilir

---

## 📦 Kurulum

### 1. PlatformIO Kurulumu

PlatformIO IDE veya PlatformIO Core kurulu olmalıdır.

**PlatformIO IDE:**
- [VS Code Extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide) kurun

**PlatformIO Core:**
```bash
pip install platformio
```

### 2. Projeyi Klonlayın

```bash
git clone https://github.com/recepuysal/esp32-c3-dashboard.git
cd esp32-c3-dashboard
```

### 3. Bağımlılıkları Yükleyin

PlatformIO otomatik olarak `platformio.ini` dosyasındaki kütüphaneleri yükleyecektir:

```bash
pio pkg install
```

### 4. Yapılandırma

`src/main.cpp` dosyasında WiFi bilgilerinizi güncelleyin:

```cpp
const char* ssid = "WiFi_Adiniz";
const char* password = "WiFi_Sifreniz";
```

### 5. Derleme ve Yükleme

```bash
# Projeyi derle
pio run

# ESP32'ye yükle
pio run -t upload

# Seri monitörü aç
pio device monitor
```

---

## ⚙️ Yapılandırma

### WiFi Ayarları

`src/main.cpp` dosyasında:

```cpp
const char* ssid = "WiFi_Adiniz";
const char* password = "WiFi_Sifreniz";
```

### NTP Ayarları

```cpp
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3 * 3600;  // GMT+3 için
const int daylightOffset_sec = 0;
```

### OTA Ayarları

```cpp
const char* otaName = "sp_dashboard";
const char* otaPass = "1234";  // Güvenlik için değiştirin!
```

### Pin Ayarları

```cpp
// TFT Pinleri
#define TFT_CS   10
#define TFT_DC    7
#define TFT_RST   5
#define TFT_SCLK  4
#define TFT_MOSI  6

// DHT11 Pin
#define DHT_PIN   2
```

---

## 🚀 Kullanım

### İlk Çalıştırma

1. Donanım bağlantılarını yapın (Pin Bağlantıları bölümüne bakın)
2. Kodu derleyip ESP32'ye yükleyin (WiFi bilgileri kod içinde değil!)
3. ESP32 açıldığında:
   - Splash ekranı gösterilir (3 saniye)
   - Eğer WiFi bilgisi yoksa, Access Point (AP) modu açılır
   - Ekranda WiFi yapılandırma bilgileri gösterilir
4. **WiFi Yapılandırması (Yeni!):**
   - Telefon/tablet WiFi listesinde "ESP32-Dashboard-Setup" ağını bulun
   - Şifre: `1234`
   - Tarayıcıda `http://192.168.4.1` adresini açın (captive portal otomatik açılabilir)
   - WiFi ağ adınızı (SSID) ve şifrenizi girin
   - "WiFi'ye Bağlan" butonuna tıklayın
   - ESP32 otomatik olarak yeniden başlar ve WiFi'ye bağlanır
5. Dashboard ekranı görüntülenir

### OTA Güncelleme

1. ESP32'yi WiFi'ye bağlayın
2. Arduino IDE veya PlatformIO'dan OTA yükleme seçeneğini kullanın
3. Hostname: `sp_dashboard`
4. Şifre: `1234` (yapılandırmada değiştirdiyseniz onu kullanın)
5. Güncelleme sırasında ekranda ilerleme görüntülenir

### Ekran Koruyucu ve Deep Sleep

**Ekran Koruyucu:**
- 1 dakika hareketsizlik sonrası otomatik devreye girer
- Açılış logosunu gösterir ve parlaklığı %10'a düşürür
- Encoder hareket ettirerek veya butona basarak normal moda dönebilirsiniz
- Menü açıkken veya OTA güncellemesi sırasında devreye girmez

**Deep Sleep Modu:**
- 5 dakika ekran koruyucuda kalırsa otomatik Deep Sleep moduna geçer
- Maksimum güç tasarrufu sağlar (mikroamper seviyesinde)
- Encoder butonuna basıp basılı tutarak uyandırabilirsiniz
- Yedek olarak 1 saat sonra otomatik uyanır

### Seri Monitör

```bash
pio device monitor
```

Seri monitörde şu bilgiler görüntülenir:
- WiFi bağlantı durumu
- IP adresi
- DHT11 okuma durumu
- OTA güncelleme durumu
- Deep Sleep uyanma sebebi (GPIO veya Timer)
- Toplam çalışma süresi kayıt durumu
- Ekran koruyucu durumu
- Deep Sleep uyanma sebebi (GPIO veya Timer)
- Toplam çalışma süresi kayıt durumu
- Ekran koruyucu durumu

---

## 🔌 Pin Bağlantıları

### ESP32-C3 Super Mini Pinout

```
ESP32-C3 Super Mini
┌─────────────────┐
│                 │
│  [USB-C]        │
│                 │
│  GPIO4  ────┐   │
│  GPIO5  ────┤   │
│  GPIO6  ────┤   │
│  GPIO7  ────┤   │
│  GPIO10 ────┤   │
│  GPIO2  ────┘   │
│                 │
└─────────────────┘
```

### ST7789 TFT Ekran Bağlantıları

| ST7789 | ESP32-C3 | Açıklama |
|--------|----------|----------|
| VCC | 3.3V | Güç |
| GND | GND | Toprak |
| CS | GPIO10 | Chip Select |
| DC | GPIO7 | Data/Command |
| RST | GPIO5 | Reset |
| SCLK | GPIO4 | SPI Clock |
| MOSI | GPIO6 | SPI Data |
| BLK | GPIO1 | Backlight (PWM kontrolü) |

### DHT11 Bağlantıları

| DHT11 | ESP32-C3 | Açıklama |
|-------|----------|----------|
| VCC | 3.3V | Güç |
| GND | GND | Toprak |
| DATA | GPIO2 | Veri (4.7kΩ pull-up gerekli) |

**Not:** DHT11 modülü kullanıyorsanız pull-up direnci genellikle modülde mevcuttur.

### Rotary Encoder Bağlantıları

| Rotary Encoder | ESP32-C3 | Açıklama |
|----------------|----------|----------|
| VCC | 3.3V | Güç |
| GND | GND | Toprak |
| CLK | GPIO8 | Clock (Encoder çıkışı) |
| DT | GPIO9 | Data (Encoder çıkışı) |
| SW | GPIO3 | Switch (Buton, pull-up gerekli) |

**Not:** Rotary encoder modülü genellikle dahili pull-up dirençleri içerir. SW pinine harici pull-up direnci gerekebilir.

### Güç Yönetimi Bağlantı Şeması

**Batarya Güç Sistemi:**
```
LiPo Batarya (653095P)
  3.7V, 2500mAh
    │
    ├─── TP4056 Şarj Modülü (USB-C girişi)
    │
    └─── Toggle Switch
         │
         └─── DD0606SA Boost Converter
              │
              └─── 5V Çıkış ──── ESP32-C3 (VIN/5V)
                                   │
                                   ├─── ST7789 (VCC)
                                   └─── DHT11 (VCC)
```

**Not:** ESP32-C3 Super Mini 5V girişi destekler. Boost converter çıkışı ESP32'nin VIN pinine bağlanmalıdır.

### Bağlantı Şeması

```
ESP32-C3          ST7789          DHT11          Rotary Encoder
─────────         ──────          ─────          ──────────────
5V (VIN)  ──────── VCC    ──────── VCC    ──────── VCC
GPIO1     ──────── BLK
GND       ──────── GND    ──────── GND    ──────── GND
GPIO10    ──────── CS
GPIO7     ──────── DC
GPIO5     ──────── RST
GPIO4     ──────── SCLK
GPIO6     ──────── MOSI
GPIO2     ──────────────────────── DATA
GPIO8     ──────────────────────────────────────── CLK
GPIO9     ──────────────────────────────────────── DT
GPIO3     ──────────────────────────────────────── SW
```

---

## 📁 Proje Yapısı

```
esp32-c3-dashboard/
│
├── src/
│   ├── main.cpp          # Ana program dosyası
│   └── logo.h           # Logo bitmap verileri
│
├── include/             # Header dosyaları (boş)
├── lib/                 # Kütüphaneler (boş)
├── test/                # Test dosyaları (boş)
│
├── platformio.ini       # PlatformIO yapılandırması
├── README.md            # Bu dosya
└── .gitignore           # Git ignore dosyası
```

### Dosya Açıklamaları

- **`src/main.cpp`**: Ana program kodu, tüm fonksiyonlar ve loop
- **`src/logo.h`**: WiFi sinyal ikonları ve splash ekran bitmap'leri
- **`platformio.ini`**: PlatformIO yapılandırması, kütüphaneler ve build ayarları

---

## 🔍 Özellik Detayları

### Ekran Güncelleme Optimizasyonu

Proje, ekran performansını optimize etmek için sadece değişen kısımları günceller:

- Tarih: Sadece tarih değiştiğinde güncellenir
- Saat: Her saniye güncellenir
- Sıcaklık/Nem: Değer değiştiğinde güncellenir
- WiFi Sinyal: Değer değiştiğinde güncellenir

### OTA Güncelleme Mekanizması

- Progress bar sadece yeni eklenen kısmı çizer (kırpma yok)
- Yüzde bilgisi gerçek zamanlı gösterilir
- Hata durumunda kırmızı hata mesajı

### WiFi Sinyal İkonları

Sinyal gücüne göre dinamik ikonlar:
- **Yüksek** (≥ -60 dBm): Tam sinyal ikonu
- **Orta** (-60 to -80 dBm): Orta sinyal ikonu
- **Düşük** (< -80 dBm): Düşük sinyal ikonu

---

## 🐛 Sorun Giderme

### WiFi Bağlanmıyor

**Sorun:** ESP32 WiFi'ye bağlanamıyor

**Çözümler:**
- WiFi SSID ve şifresini kontrol edin
- WiFi sinyal gücünü kontrol edin
- Serial Monitor'da hata mesajlarını kontrol edin
- ESP32'yi resetleyin

### DHT11 Okuma Hatası

**Sorun:** "DHT11 okuma hatasi!" mesajı

**Çözümler:**
- DHT11 bağlantılarını kontrol edin
- Pull-up direncinin (4.7kΩ-10kΩ) bağlı olduğundan emin olun
- DHT11'in 3.3V ile beslendiğinden emin olun
- GPIO2 pininin doğru bağlandığını kontrol edin
- DHT11'in 2 saniye aralıklarla okunması gerektiğini unutmayın

### Ekran Görüntülenmiyor

**Sorun:** ST7789 ekranı çalışmıyor

**Çözümler:**
- Tüm SPI bağlantılarını kontrol edin
- CS, DC, RST pinlerinin doğru bağlandığını kontrol edin
- Ekranın 3.3V ile beslendiğinden emin olun
- `platformio.ini`'deki pin tanımlarını kontrol edin

### OTA Güncelleme Çalışmıyor

**Sorun:** OTA güncelleme başlamıyor

**Çözümler:**
- ESP32'nin WiFi'ye bağlı olduğundan emin olun
- OTA hostname ve şifresini kontrol edin
- Aynı ağda olduğunuzdan emin olun
- Firewall ayarlarını kontrol edin

### NTP Zaman Alınamıyor

**Sorun:** Saat gösterilmiyor veya yanlış

**Çözümler:**
- WiFi bağlantısını kontrol edin
- NTP sunucusuna erişilebildiğinden emin olun
- GMT offset değerini kontrol edin
- İnternet bağlantısını kontrol edin

---

## 🛠️ Geliştirme

### Yeni Özellik Ekleme

1. **Yeni Sensör Ekleme:**
   ```cpp
   // Pin tanımı
   #define NEW_SENSOR_PIN 3
   
   // Okuma fonksiyonu
   void readNewSensor() {
     // Sensör okuma kodu
   }
   
   // Ekrana yazdırma
   void drawNewSensorData() {
     // Ekran çizim kodu
   }
   ```

2. **Yeni Ekran Modu:**
   - `updateTimeIfNeeded()` fonksiyonunu genişletin
   - Buton/encoder ile mod değiştirme ekleyin

3. **Web Sunucusu Ekleme:**
   - ESPAsyncWebServer kütüphanesini ekleyin
   - API endpoint'leri oluşturun

### Kod Yapısı

Ana fonksiyonlar:
- `setup()`: Başlangıç ayarları
- `loop()`: Ana döngü
- `drawTimeAndDate()`: Tarih/saat çizimi
- `drawDHT11Data()`: Sensör verileri
- `drawWiFiSignal()`: WiFi bilgileri
- `startOTA()`: OTA başlatma
- `updateTimeIfNeeded()`: Zamanlı güncellemeler

### Test Etme

```bash
# Kod derleme
pio run

# Test yükleme
pio run -t upload

# Seri monitör
pio device monitor
```

---

## 🤝 Katkıda Bulunma

Katkılarınızı bekliyoruz! Lütfen:

1. Fork yapın
2. Feature branch oluşturun (`git checkout -b feature/AmazingFeature`)
3. Değişikliklerinizi commit edin (`git commit -m 'Add some AmazingFeature'`)
4. Branch'inizi push edin (`git push origin feature/AmazingFeature`)
5. Pull Request açın

### Katkı Kuralları

- Kod standartlarına uyun
- Yorum satırlarını Türkçe veya İngilizce yazın
- Test edilmiş kod gönderin
- README'yi güncelleyin

---

## 📝 Versiyon Geçmişi

### v1.5.0 (Mevcut)
- 🔋 Pil seviye ikonları (battery20–100) ve şarj ikonları (charge20–100)
- ⚡ TP4056 şarj durumu (CHRG/STDBY), tamamlandığında `full` ikonu
- 📊 Pil Durumu menüsü: şarj metni, pin durumu, ekran ikonu bilgisi
- 🔧 Pil voltaj kalibrasyonu ve üst çubuk (WiFi + pil) düzeni
- 📡 PlatformIO OTA ortamı (`esp32c3_super_mini_ota`)

### v1.4.0
- 🌐 WiFiManager + Captive Portal entegrasyonu
- 📱 Web tabanlı WiFi yapılandırması (kod değişikliği gerekmez!)
- 🔄 WiFi ayarlarını sıfırlama özelliği
- ✅ Güvenli onay ekranı
- 📋 Gelişmiş menü sistemi (kaydırma özelliği)

### v1.3.0
- ✅ Kalıcı toplam çalışma süresi
- ✅ Ekran koruyucu (Screen Saver)
- ✅ Deep Sleep modu

### v1.2.0
- ✅ Rotary encoder desteği eklendi
- ✅ Menü sistemi eklendi (Ayarlar, Parlaklık, WiFi Bilgileri)
- ✅ PWM ile parlaklık kontrolü (0-100%)
- ✅ Otomatik WiFi yeniden bağlanma
- ✅ WiFi bilgileri sayfası (SSID, IP, RSSI)
- ✅ Menü butonu eklendi (ana sayfada)
- ✅ Menü renkleri ana sayfa ile uyumlu hale getirildi
- ✅ UI iyileştirmeleri (başlıklar ortalandı, progress bar optimizasyonu)
- ✅ Progress bar iz kalma sorunu düzeltildi

### v1.1.0
- ✅ DHT11 sensör desteği eklendi
- ✅ Sıcaklık ve nem gösterimi
- ✅ OTA ilerleme ekranı iyileştirildi
- ✅ Progress bar optimizasyonu (kırpma önlendi)
- ✅ Yüzde bilgisi gösterimi
- ✅ Ekran güncelleme optimizasyonları

### v1.0.3
- ✅ İlk stabil sürüm
- ✅ WiFi bağlantısı
- ✅ NTP zaman senkronizasyonu
- ✅ ST7789 TFT ekran desteği
- ✅ OTA güncelleme desteği
- ✅ Temel dashboard arayüzü
- ✅ WiFi sinyal gücü gösterimi
- ✅ Dinamik WiFi ikonları
- ✅ Versiyon bilgisi gösterimi

---

## 📄 Lisans

Bu proje MIT lisansı altında lisanslanmıştır. Detaylar için `LICENSE` dosyasına bakın.

---

## 👤 Yazar

**Recep UYSAL**
- GitHub: [@recepuysal](https://github.com/recepuysal)

---

## 🙏 Teşekkürler

- [Adafruit](https://www.adafruit.com/) - GFX ve ST7789 kütüphaneleri
- [PlatformIO](https://platformio.org/) - Geliştirme ortamı
- [ESP32 Community](https://www.espressif.com/) - Desteği için

---

## 📞 İletişim

Sorularınız veya önerileriniz için:
- Issue açın: [GitHub Issues](https://github.com/recepuysal/esp32-c3-dashboard/issues)

---

<div align="center">

**⭐ Beğendiyseniz yıldız vermeyi unutmayın! ⭐**

Made with ❤️ by [recepuysal](https://github.com/recepuysal)

</div>
