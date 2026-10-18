# ⚡ wifiBOOM · Wireless Network Pentesting Tool

[English](#english) | [Türkçe](#türkçe)

---

## English

Advanced 802.11 passive observation, management frame inspection, and controlled testing suite built for ESP32 using ESP-IDF.

---

### Technical Highlights & Sanity Check Bypass

Standard ESP-IDF builds restrict raw frame injection of certain management subtypes via checking routines in `libnet80211.a`. In `wifiBOOM`, the proprietary validation routine is overridden:

    int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
        return 0; 
    }

By marking the original symbol in `libnet80211.a` as **weak** (or utilizing weak symbol overriding during linking), `wifiBOOM` replaces `ieee80211_raw_frame_sanity_check` with a custom implementation that always returns `0`. This bypasses the default sanity check constraints, enabling raw frame transmission for **all** management frames (such as Deauthentication and Disassociation packets) directly via `esp_wifi_80211_tx()`.

*For an in-depth writeup on this mechanism, see the research article:*  
👉 [ESP32 Wi-Fi Deauth Send Bypass Writeup](https://semsyapar.github.io/2026/10/18/esp32-wifi-deauth-send-bypass.html)

---

### Key Features

* **Passive Scanning & Dwell Rotation:** Sweeps 802.11 channels (1–13) with customizable dwell times to capture Beacons, Probe Responses, Data, and QoS frames.
* **STA-AP Association Tracking:** Automatically tracks client stations linked to Access Points using FNV-1a hash matching, measuring RSSI and packet statistics.
* **PMF Detection:** Parses RSN information elements (IE Tag 48) to identify Protected Management Frame (PMF) policies (*Disabled*, *Optional*, *Required*, *Invalid*).
* **Frame Injection Engine:** Transmits custom raw Deauthentication and Disassociation frames targeting broadcast addresses, specific APs, or individual STA-AP pairs.
* **Embedded Web Control Panel:** Self-hosted web interface running over SoftAP (`Heart` / `boom1234`) featuring real-time JSON APIs (`/api/status`, `/api/networks`, `/api/stations`, `/api/attack`, `/api/scan`, `/api/table/manage`).
* **Physical BOOT Button Control:** Dedicated interrupt listener on GPIO0. Pressing the physical BOOT button toggles back to Control Mode from any state, or triggers an "Infinity Attack All" broadcast test directly from Control Mode.
* **Visual LED Feedback:** GPIO2 LED indicators for state transitions, scan completions, error states, and active frame transmissions.

---

### System Architecture & Workflow

    [STARTING] ➔ [SCANNING] ──(Timeout / BOOT)┐
                     ▲                        ▼
                     │ (Start Scan)    [CONTROL PANEL]
                     └────────────────────────┬─────┘
                                              │     ▲
                        (Start Attack / BOOT) │     │ (Timeout / BOOT)
                                              ▼     │
                                         [ATTACKING]┘

---

### Physical Controls (GPIO Pinout)

| Component | GPIO Pin | Description |
| :--- | :--- | :--- |
| **BOOT Button** | `GPIO 0` | State Toggle / Infinity Attack Trigger / Return to Control Phase |
| **Status LED** | `GPIO 2` | Active indicator, scan completion blinker, error status indicator |

---

### Web Dashboard Endpoints

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | `GET` | Serves embedded responsive web interface (`index.html`) |
| `/api/status` | `GET` | Returns JSON status (system state, packet drops, capacities) |
| `/api/networks` | `GET` | Returns list of discovered Access Points and PMF policies |
| `/api/stations` | `GET` | Returns list of observed STA-AP pairs and RSSI data |
| `/api/scan` | `POST` | Initiates passive channel scanning session |
| `/api/attack` | `POST` | Configures and starts frame injection test session |
| `/api/table/manage`| `POST` | Clears or deletes entries from internal tracking tables |

---

### Building and Flashing

#### Prerequisites
* **ESP-IDF v5.0+** properly installed and exported in your environment.
* Target board: ESP32 / ESP32-S2 / ESP32-C3 / ESP32-S3.

#### 1. Clone the Repository
    git clone https://github.com/SemsYapar/wifiBOOM.git
    cd wifiBOOM

#### 2. Configure Component CMake Structure
Ensure `main/CMakeLists.txt` includes `index.html` as an embedded file:
    idf_component_register(SRCS "main.c"
                           INCLUDE_DIRS "."
                           EMBED_FILES "index.html")

#### 3. Build & Flash
    idf.py set-target esp32
    idf.py build
    idf.py -p COM7 flash monitor

---

### Disclaimer

> **IMPORTANT LEGAL NOTICE**  
> This software is designed and developed exclusively for authorized security testing, educational research, and defensive assessment of wireless networks. Executing frame injection or deauthentication attacks on networks without explicit written permission from the network owner is illegal and punishable under electronic communications laws. The author assumes no responsibility or liability for damage or legal consequences caused by misuse or unauthorized deployment of this software.

---
---

## Türkçe

ESP32 mikrodenetleyicileri için ESP-IDF ortamında geliştirilmiş, 802.11 pasif izleme, yönetim çerçevesi inceleme ve kontrollü sızma testi aracı.

---

### Teknik Öne Çıkanlar ve Sanity Check Bypass

Standart ESP-IDF derlemeleri, `libnet80211.a` içerisindeki doğrulama fonksiyonları nedeniyle ham yönetim çerçevelerinin (enjeksiyon paketleri) gönderimini kısıtlar. `wifiBOOM` içerisinde bu doğrulama fonksiyonu ezilmektedir:

    int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
        return 0; 
    }

`libnet80211.a` içerisindeki orijinal sembol **weak** olarak işaretlenerek (veya bağlama aşamasında zayıf sembol ezme yöntemi kullanılarak), `ieee80211_raw_frame_sanity_check` fonksiyonu her zaman `0` döndüren özel fonksiyon ile değiştirilir. Bu sayede varsayılan güvenlik denetimleri bypass edilir ve **tüm** yönetim çerçeveleri (Deauthentication ve Disassociation paketleri gibi) doğrudan `esp_wifi_80211_tx()` üzerinden iletilebilir.

*Bu mekanizmanın detaylı teknik incelemesi için araştırma yazısına göz atabilirsiniz:*  
👉 [ESP32 Wi-Fi Deauth Send Bypass Araştırma Yazısı](https://semsyapar.github.io/2026/10/18/esp32-wifi-deauth-send-bypass.html)

---

### Temel Özellikler

* **Pasif Tarama ve Kanal Dönüşümü:** Beacon, Probe Response, Data ve QoS çerçevelerini yakalamak için 802.11 kanallarını (1–13) ayarlanabilir sürelerle tarlar.
* **STA-AP Bağlantı Takibi:** Access Point'lere bağlanan istemci istasyonlarını FNV-1a hash eşleşmesiyle takip eder; RSSI ve paket istatistiklerini kaydeder.
* **PMF Tespiti:** Korumalı Yönetim Çerçevesi (PMF) politikalarını (*Disabled*, *Optional*, *Required*, *Invalid*) RSN bilgi öğelerini (IE Tag 48) ayrıştırarak tespit eder.
* **Çerçeve Enjeksiyon Motoru:** Yayın adreslerini, belirli AP'leri veya özel STA-AP çiftlerini hedef alan ham Deauthentication ve Disassociation çerçeveleri iletir.
* **Gömülü Web Kontrol Paneli:** SoftAP (`Heart` / `boom1234`) üzerinden çalışan ve gerçek zamanlı JSON API'leri sunan web arayüzü (`/api/status`, `/api/networks`, `/api/stations`, `/api/attack`, `/api/scan`, `/api/table/manage`).
* **Fiziksel BOOT Butonu Kontrolü:** GPIO0 üzerinde tanımlı kesme dinleyicisi. BOOT butonuna basılması, herhangi bir durumdan Kontrol Moduna geri dönmeyi sağlar veya Kontrol Modundayken doğrudan tüm ağlara yönelik sınırsız testi ("Infinity Attack All") başlatır.
* **Görsel LED Geri Bildirimi:** Durum geçişleri, tarama tamamlanması, hata durumları ve aktif paket iletimi için GPIO2 LED göstergeleri.

---

### Sistem Mimarisi ve İş Akışı

    [STARTING] ➔ [SCANNING] ──(Timeout / BOOT)┐
                     ▲                        ▼
                     │ (Tarama Başlat) [CONTROL PANEL]
                     └────────────────────────┬─────┘
                                              │     ▲
                      (Saldırı Başlat / BOOT) │     │ (Timeout / BOOT)
                                              ▼     │
                                         [ATTACKING]┘

---

### Donanım Bağlantıları (GPIO Pin Yapılandırması)

| Bileşen | GPIO Pini | Açıklama |
| :--- | :--- | :--- |
| **BOOT Butonu** | `GPIO 0` | Durum Değiştirme / Sınırsız Saldırı Tetikleme / Kontrol Fazına Dönüş |
| **Durum LED'i** | `GPIO 2` | Durum göstergesi, tarama tamamlanma flaşı, hata durumu bildirimi |

---

### Web Paneli API Endpoint'leri

| Endpoint | Metot | Açıklama |
| :--- | :--- | :--- |
| `/` | `GET` | Gömülü duyarlı web arayüzünü sunar (`index.html`) |
| `/api/status` | `GET` | Sistem durumunu, düşen paketleri ve kapasiteleri döndürür |
| `/api/networks` | `GET` | Tespit edilen AP listesini ve PMF politikalarını döndürür |
| `/api/stations` | `GET` | Gözlemlenen STA-AP çiftlerini ve RSSI verilerini döndürür |
| `/api/scan` | `POST` | Pasif kanal tarama oturumunu başlatır |
| `/api/attack` | `POST` | Paket enjeksiyon test oturumunu yapılandırır ve başlatır |
| `/api/table/manage`| `POST` | Dahili izleme tablolarındaki kayıtları siler veya temizler |

---

### Kurulum ve Derleme

#### Ön Gereksinimler
* **ESP-IDF v5.0+** geliştirme ortamının kurulu ve yapılandırılmış olması.
* Hedef kart: ESP32 / ESP32-S2 / ESP32-C3 / ESP32-S3.

#### 1. Depoyu Klonlayın
    git clone https://github.com/SemsYapar/wifiBOOM.git
    cd wifiBOOM

#### 2. CMake Yapılandırmasını Kontrol Edin
`main/CMakeLists.txt` dosyanızda `index.html` dosyasının gömülü dosya olarak tanımlandığından emin olun:
    idf_component_register(SRCS "main.c"
                           INCLUDE_DIRS "."
                           EMBED_FILES "index.html")

#### 3. Derleyin ve Yükleyin
    idf.py set-target esp32
    idf.py build
    idf.py -p COM7 flash monitor

---

### Yasal Sorumluluk Reddi

> **ÖNEMLİ YASAL UYARI**  
> Bu yazılım yalnızca yetkili güvenlik testleri, eğitim araştırmaları ve kablosuz ağların savunma amaçlı değerlendirilmesi için tasarlanmıştır. Sistem sahibinden yazılı izin alınmaksızın kablosuz ağlara paket enjeksiyonu veya deauthentication saldırısı gerçekleştirmek yasa dışıdır. Yazar, bu yazılımın kötüye kullanımından veya yetkisiz kullanımından doğabilecek hiçbir zarardan veya yasal sonuçtan sorumlu tutulamaz.
