# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.4.0] - 2024-12-XX

### Added
- 🌐 **WiFiManager + Captive Portal Entegrasyonu**
  - Kod değişikliği olmadan WiFi yapılandırması
  - WiFiManager kütüphanesi entegrasyonu
  - Web tabanlı yapılandırma arayüzü
  - Otomatik Access Point (AP) modu ("ESP32-Dashboard-Setup")
  - Captive portal desteği (otomatik web sayfası açılması)
  - Mobil uyumlu HTML form arayüzü
  - Türkçe karakter desteği
  - WiFi bilgileri Preferences API ile güvenli saklanır

- 🔄 **WiFi Ayarlarını Sıfırlama Özelliği**
  - Menüden "WiFi Sifirla" seçeneği
  - Güvenli onay ekranı ("Emin misiniz?")
  - Evet/Hayır seçenekleri ile encoder kontrolü
  - WiFi ayarları sıfırlandığında AP modu tekrar açılır

- 📱 **Gelişmiş Menü Sistemi**
  - Menü kaydırma (scroll) özelliği
  - WiFi Sifirla en alta taşındı
  - Otomatik kaydırma ile tüm menü itemları görüntülenir
  - Scroll göstergesi eklendi

### Changed
- 🔧 WiFi yapılandırması artık kod içinde değil, web arayüzünden yapılıyor
- 📋 Menü sıralaması güncellendi: Parlaklik, WiFi Ayarlari, Istatistikler, Sistem Bilgileri, WiFi Sifirla, Geri Don
- 🎨 Onay ekranı için modern arayüz eklendi

### Technical
- WiFiManager kütüphanesi eklendi (tzapu/WiFiManager@^0.16.0)
- WebServer kütüphanesi entegrasyonu
- HTTP POST endpoint'leri (`/`, `/save`)
- Preferences API ile WiFi bilgilerinin saklanması
- Non-blocking web sunucusu yapısı
- AP modu yönetimi ve durum kontrolü

## [1.3.0] - 2024-12-XX

### Added
- 💾 **Kalıcı Toplam Çalışma Süresi**
  - İstatistikler sayfasında "Toplam Çalışma Süresi" gösterimi
  - Çalışma süresi Preferences API ile kalıcı olarak kaydedilir
  - 60 saniyede bir otomatik kayıt
  - Cihaz yeniden başlatıldığında toplam süre korunur ve devam eder
  - Toplam süre = kayıtlı toplam + mevcut oturum süresi

- 💤 **Ekran Koruyucu (Screen Saver)**
  - 1 dakika hareketsizlik sonrası otomatik devreye girer
  - Açılış logosunu gösterir
  - Ekran parlaklığını %10'a düşürür (güç tasarrufu)
  - Normal parlaklık ayarı otomatik kaydedilir
  - Encoder hareketi veya buton ile normal moda döner
  - Menü açıkken veya OTA güncellemesi sırasında devreye girmez

- 🌙 **Deep Sleep Modu**
  - 5 dakika ekran koruyucuda kalırsa otomatik devreye girer
  - Maksimum güç tasarrufu sağlar (mikroamper seviyesinde)
  - Deep Sleep'e geçmeden önce tüm veriler kaydedilir
  - Encoder butonu (GPIO3) ile uyandırılabilir
  - Yedek timer wake-up eklendi (1 saat sonra otomatik uyanır)
  - Wake-up sebebi seri monitörde gösterilir

### Changed
- 📊 İstatistikler sayfasında "Çalışma Süresi" → "Toplam Çalışma Süresi" olarak değiştirildi
- 🔄 Ekran koruyucudan çıkınca ekran düzgün şekilde yeniden çiziliyor
- ⚡ Güç yönetimi iyileştirildi (ekran koruyucu ve Deep Sleep ile)

### Technical
- Preferences API kullanılarak kalıcı veri saklama eklendi
- ESP32-C3 Deep Sleep API entegrasyonu (esp_sleep.h, driver/gpio.h)
- Ekran koruyucu zamanlayıcı sistemi
- Aktivite takip mekanizması (encoder ve buton)

## [1.2.1] - 2024-12-XX

### Added
- 📊 İstatistikler sayfası eklendi
  - Ortalama sıcaklık/nem gösterimi
  - Maksimum/minimum değerler
  - Çalışma süresi (uptime) - canlı güncelleme
  - WiFi bağlantı süresi - canlı güncelleme
- 💻 Sistem bilgileri sayfası eklendi
  - CPU frekansı
  - Bellek kullanımı (KB ve %)
  - Chip ID
  - Firmware versiyonu
  - Uptime (çalışma süresi)
  - WiFi durumu

### Fixed
- 🔧 Menüden ana sayfaya dönerken gecikme sorunu düzeltildi
- 🔧 İstatistikler sayfasında sürelerin canlı güncellenmesi sağlandı
- 🔧 Değişken tekrar tanımlama hataları düzeltildi

## [1.2.0] - 2024-12-XX

### Added
- 🔄 Rotary encoder desteği eklendi
- 📱 Menü sistemi eklendi (Ayarlar menüsü)
- 💡 Parlaklık kontrolü (PWM) eklendi
  - Encoder ile parlaklık ayarı (0-100%)
  - Progress bar ile görsel gösterim
  - Gerçek zamanlı PWM kontrolü
- 📶 WiFi bilgileri sayfası eklendi
  - SSID gösterimi
  - IP adresi gösterimi
  - Sinyal gücü (RSSI) gösterimi ve seviye bilgisi
  - Otomatik güncelleme
- 🔁 Otomatik WiFi yeniden bağlanma
  - Bağlantı koptuğunda otomatik yeniden bağlanma
  - Non-blocking, CPU dostu
  - 10 saniyede bir durum kontrolü
- 🎨 Menü butonu eklendi (ana sayfada)
  - Alt ortada konumlandırılmış
  - Kullanıcı dostu görünüm

### Changed
- 🎨 Menü renkleri ana sayfa ile uyumlu hale getirildi
  - CYAN başlıklar
  - WHITE normal text
  - YELLOW vurgular
- 📱 WiFi sinyal gücü ana sayfadan kaldırıldı
  - WiFi bilgileri artık menüde gösteriliyor
- 🎯 UI iyileştirmeleri
  - Tüm başlıklar ortalandı
  - "Geri dön" yazıları ortalandı
  - Progress bar temizleme optimizasyonu
  - SSID tam görüntüleme (kısaltma kaldırıldı)

### Fixed
- 🔧 Progress bar iz kalma sorunu düzeltildi
- 🔧 WiFi bilgileri sayfasında yazı boyutu optimize edildi
- 🔧 Menü sayfaları arası geçiş sorunları düzeltildi
- 🔧 Parlaklık menüsünde yüzde gösterimi iz kalma sorunu düzeltildi

## [1.1.0] - 2024-01-XX

### Added
- DHT11 sensör desteği eklendi
- Sıcaklık ve nem gösterimi
- OTA ilerleme ekranı iyileştirildi
- Progress bar optimizasyonu (kırpma önlendi)
- Yüzde bilgisi gösterimi

### Changed
- Ekran güncelleme optimizasyonları
- Sıcaklık ve nem konumları güncellendi
- IP adresi konumu değiştirildi
- Renk şemaları güncellendi

### Fixed
- OTA progress bar kırpma sorunu düzeltildi
- Derece simgesi görüntüleme sorunu düzeltildi

## [1.0.3] - 2024-01-XX

### Added
- İlk stabil sürüm
- WiFi bağlantısı
- NTP zaman senkronizasyonu
- ST7789 TFT ekran desteği
- OTA güncelleme desteği
- Temel dashboard arayüzü
- WiFi sinyal gücü gösterimi
- Dinamik WiFi ikonları
- Versiyon bilgisi gösterimi

---

[1.3.0]: https://github.com/serialprint/esp32-c3-dashboard/releases/tag/v1.3.0
[1.2.1]: https://github.com/serialprint/esp32-c3-dashboard/releases/tag/v1.2.1
[1.2.0]: https://github.com/serialprint/esp32-c3-dashboard/releases/tag/v1.2.0
[1.1.0]: https://github.com/serialprint/esp32-c3-dashboard/releases/tag/v1.1.0
[1.0.3]: https://github.com/serialprint/esp32-c3-dashboard/releases/tag/v1.0.3

