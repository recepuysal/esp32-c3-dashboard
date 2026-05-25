# Katkıda Bulunma Rehberi

ESP32-C3 Smart Dashboard projesine katkıda bulunmak istediğiniz için teşekkürler! 🎉

## Nasıl Katkıda Bulunabilirsiniz?

### 🐛 Hata Bildirimi

1. [Issues](https://github.com/recepuysal/esp32-c3-dashboard/issues) sayfasına gidin
2. "New Issue" butonuna tıklayın
3. Hata başlığını ve detaylarını yazın
4. Mümkünse ekran görüntüsü veya log ekleyin

### ✨ Yeni Özellik Önerisi

1. [Issues](https://github.com/recepuysal/esp32-c3-dashboard/issues) sayfasına gidin
2. "New Issue" butonuna tıklayın
3. "Feature Request" etiketi ekleyin
4. Özelliği detaylı açıklayın

### 💻 Kod Katkısı

1. **Fork yapın**
   ```bash
   # GitHub'da Fork butonuna tıklayın
   ```

2. **Repository'yi klonlayın**
   ```bash
   git clone https://github.com/KULLANICI_ADINIZ/esp32-c3-dashboard.git
   cd esp32-c3-dashboard
   ```

3. **Branch oluşturun**
   ```bash
   git checkout -b feature/yeni-ozellik
   ```

4. **Değişikliklerinizi yapın**
   - Kod standartlarına uyun
   - Yorum satırlarını ekleyin
   - Test edin

5. **Commit yapın**
   ```bash
   git add .
   git commit -m "feat: Yeni özellik eklendi"
   ```

6. **Push yapın**
   ```bash
   git push origin feature/yeni-ozellik
   ```

7. **Pull Request oluşturun**
   - GitHub'da "New Pull Request" butonuna tıklayın
   - Değişikliklerinizi açıklayın

## Kod Standartları

### Kod Stili
- PlatformIO kod standartlarına uyun
- Fonksiyon isimleri açıklayıcı olsun
- Yorum satırları Türkçe veya İngilizce olabilir

### Commit Mesajları
- `feat:` - Yeni özellik
- `fix:` - Hata düzeltmesi
- `docs:` - Dokümantasyon
- `style:` - Kod formatı
- `refactor:` - Kod yeniden yapılandırma
- `test:` - Test ekleme
- `chore:` - Diğer değişiklikler

Örnek:
```
feat: BMP280 basınç sensörü desteği eklendi
fix: DHT11 okuma hatası düzeltildi
docs: README güncellendi
```

## Test Etme

Değişikliklerinizi test etmek için:

```bash
# Projeyi derle
pio run

# ESP32'ye yükle
pio run -t upload

# Seri monitörü aç
pio device monitor
```

## Sorularınız mı Var?

- Issue açın: [GitHub Issues](https://github.com/recepuysal/esp32-c3-dashboard/issues)
- Detaylı bilgi için README.md dosyasına bakın

## Teşekkürler! 🙏

Katkılarınız projeyi daha iyi hale getiriyor!

