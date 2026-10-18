# ⚡ wifiBOOM · Wireless Network Pentesting Tool

[English](#english) | [Türkçe](#türkçe)

---

## English

An 802.11 passive monitoring and controlled penetration-testing tool developed with ESP-IDF for ESP32 microcontrollers. **wifiBOOM was developed using an ESP-32S development board.**

### Technical Highlights & Sanity Check Bypass

In standard ESP-IDF builds, the `ieee80211_raw_frame_sanity_check` function within `libnet80211.a` restricts transmission of certain 802.11 management frames. wifiBOOM overrides this validation function using the weak-symbol mechanism:

```c
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}
```

The original symbol in `libnet80211.a` is marked as **weak**, allowing it to be replaced by an implementation of `ieee80211_raw_frame_sanity_check` that always returns `0`. This bypasses the default validation restrictions and permits raw transmission of management frames, including Deauthentication and Disassociation frames, through `esp_wifi_80211_tx()`. Actual transmission behavior may still depend on the hardware and ESP-IDF version.

*For a detailed technical analysis of this mechanism, see the research article:*  
👉 [ESP32 Wi-Fi Deauth Send Bypass Writeup](https://semsyapar.github.io/2026/10/18/esp32-wifi-deauth-send-bypass.html)

### Key Features

- **Passive Scanning & Channel Rotation:** Scans 802.11 channels (1–13) with configurable dwell times to capture Beacon, Probe Response, Data, and QoS frames.
- **STA–AP Association Tracking:** Tracks client stations associated with access points using FNV-1a hash-based matching, recording RSSI values and packet statistics.
- **PMF Detection:** Parses RSN information elements (IE Tag 48) to identify Protected Management Frames (PMF) policies: *Disabled*, *Optional*, *Required*, or *Invalid*.
- **Frame Injection Engine:** Transmits custom raw Deauthentication and Disassociation frames addressed to broadcast destinations, specific APs, or individual STA–AP pairs.
- **Embedded Web Control Panel:** Hosts a web interface over SoftAP (`Heart` / `boom1234`) with JSON API endpoints (`/api/status`, `/api/networks`, `/api/stations`, `/api/attack`, `/api/scan`, `/api/table/manage`).
- **Physical BOOT Button Control:** Uses the built-in BOOT button connected to GPIO0 on the **ESP-32S development board**. The button returns the device to Control Mode from any state or, when already in Control Mode, directly starts the unlimited all-networks test (*Infinity Attack All*).
- **Visual LED Feedback:** Uses the GPIO2 LED to indicate state transitions, scan completion, errors, and active frame transmission.

### System Architecture & Workflow

```text
[STARTING] → [SCANNING] ──(Timeout / BOOT)──┐
                  ▲                         ▼
                  │ (Start Scan)      [CONTROL PANEL]
                  └─────────────────────────┬─────┘
                                            │     ▲
                       (Start Attack / BOOT)│     │ (Timeout / BOOT)
                                            ▼     │
                                       [ATTACKING]┘
```

### Hardware Controls (GPIO Pinout)

The following controls reflect the **ESP-32S development board used during wifiBOOM's development**. GPIO wiring and onboard peripherals may differ on other boards.

| Component | GPIO Pin | Description |
| :--- | :--- | :--- |
| **Built-in BOOT Button** | `GPIO0` | State switching / Infinity Attack All trigger / Return to Control Mode |
| **Status LED** | `GPIO2` | State indication / Scan completion blink / Error indication |

### Web Dashboard API Endpoints

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | `GET` | Serves the embedded responsive web interface (`index.html`) |
| `/api/status` | `GET` | Returns system state, dropped packet counts, and capacity information as JSON |
| `/api/networks` | `GET` | Returns discovered APs and their PMF policies |
| `/api/stations` | `GET` | Returns observed STA–AP pairs and RSSI information |
| `/api/scan` | `POST` | Starts a passive channel-scanning session |
| `/api/attack` | `POST` | Configures and starts a frame-injection test session |
| `/api/table/manage` | `POST` | Deletes entries from or clears internal tracking tables |

### Building and Flashing

#### Prerequisites

- **ESP-IDF v5.0+** installed and configured.
- **Development hardware used:** ESP-32S development board (ESP32 target). Other ESP32 variants have not been established as tested hardware here.

#### 1. Clone the Repository

```bash
git clone https://github.com/SemsYapar/wifiBOOM.git
cd wifiBOOM
```

#### 2. Check the Component CMake Configuration

Make sure `main/CMakeLists.txt` embeds `index.html`:

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "index.html")
```

#### 3. Build and Flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p COM7 flash monitor
```

Replace `COM7` with the serial port assigned to your development board.

### Legal Disclaimer

> **IMPORTANT LEGAL NOTICE**  
> This software is designed exclusively for authorized security testing, educational research, and defensive assessment of wireless networks. Transmitting injected frames or performing deauthentication attacks against networks without the owner's explicit written permission may violate applicable laws. The author accepts no responsibility or liability for damage or legal consequences arising from misuse or unauthorized deployment of this software.

---

## Türkçe

ESP32 mikrodenetleyicileri için ESP-IDF ortamında geliştirilmiş, 802.11 pasif izleme ve kontrollü sızma testi aracı. **wifiBOOM'un geliştirme sürecinde ESP-32S geliştirme kartı kullanılmıştır.**

### Teknik Öne Çıkanlar ve Sanity Check Bypass

Standart ESP-IDF derlemelerinde, `libnet80211.a` içerisindeki `ieee80211_raw_frame_sanity_check` fonksiyonu bazı 802.11 management frame'lerinin gönderimini kısıtlar. wifiBOOM içerisinde bu doğrulama fonksiyonu weak sembol yöntemiyle ezilmektedir:

```c
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}
```

`libnet80211.a` içerisindeki orijinal sembol **weak** olarak işaretlenerek `ieee80211_raw_frame_sanity_check` fonksiyonu, her zaman `0` döndüren bir uygulamayla değiştirilir. Bu sayede varsayılan doğrulama kısıtlamaları bypass edilir ve Deauthentication ile Disassociation çerçeveleri dahil management frame'leri doğrudan `esp_wifi_80211_tx()` üzerinden iletilebilir. Gerçek iletim davranışı kullanılan donanıma ve ESP-IDF sürümüne bağlı olarak değişebilir.

*Bu mekanizmanın detaylı teknik incelemesi için araştırma yazısına göz atabilirsiniz:*  
👉 [ESP32 Wi-Fi Deauth Send Bypass Araştırma Yazısı](https://semsyapar.github.io/2026/10/18/esp32-wifi-deauth-send-bypass.html)

### Temel Özellikler

- **Pasif Tarama ve Kanal Dönüşümü:** Beacon, Probe Response, Data ve QoS çerçevelerini yakalamak için 802.11 kanallarını (1–13) ayarlanabilir sürelerle tarar.
- **STA–AP Bağlantı Takibi:** Access Point'lere bağlanan istemci istasyonlarını FNV-1a hash eşleşmesiyle takip eder; RSSI ve paket istatistiklerini kaydeder.
- **PMF Tespiti:** Korumalı Yönetim Çerçevesi (PMF) politikalarını (*Disabled*, *Optional*, *Required*, *Invalid*) RSN bilgi öğelerini (IE Tag 48) ayrıştırarak tespit eder.
- **Frame Enjeksiyon Motoru:** Yayın adreslerini, belirli AP'leri veya özel STA–AP çiftlerini hedef alan ham Deauthentication ve Disassociation çerçeveleri iletir.
- **Gömülü Web Kontrol Paneli:** SoftAP (`Heart` / `boom1234`) üzerinden çalışan ve JSON API'leri sunan web arayüzü (`/api/status`, `/api/networks`, `/api/stations`, `/api/attack`, `/api/scan`, `/api/table/manage`).
- **Fiziksel BOOT Butonu Kontrolü:** **ESP-32S geliştirme kartında yerleşik bulunan ve GPIO0'a bağlı BOOT butonu**, herhangi bir durumdan Kontrol Moduna geri dönmeyi sağlar veya Kontrol Modundayken doğrudan tüm ağlara yönelik sınırsız testi (*Infinity Attack All*) başlatır.
- **Görsel LED Geri Bildirimi:** Durum geçişleri, tarama tamamlanması, hata durumları ve aktif çerçeve iletimi için GPIO2 LED kullanılır.

### Sistem Mimarisi ve İş Akışı

```text
[STARTING] → [SCANNING] ──(Timeout / BOOT)──┐
                  ▲                         ▼
                  │ (Tarama Başlat)   [CONTROL PANEL]
                  └─────────────────────────┬─────┘
                                            │     ▲
                      (Saldırı Başlat / BOOT)│     │ (Timeout / BOOT)
                                            ▼     │
                                       [ATTACKING]┘
```

### Donanım Bağlantıları (GPIO Pin Yapılandırması)

Aşağıdaki pin yapılandırması, wifiBOOM'un geliştirilmesinde kullanılan **ESP-32S geliştirme kartını** esas alır. Diğer geliştirme kartlarında yerleşik bileşenler ve GPIO bağlantıları farklı olabilir.

| Bileşen | GPIO Pini | Açıklama |
| :--- | :--- | :--- |
| **Yerleşik BOOT Butonu** | `GPIO0` | Durum değiştirme / Infinity Attack All tetikleme / Kontrol Moduna dönüş |
| **Durum LED'i** | `GPIO2` | Durum göstergesi / Tarama tamamlanma flaşı / Hata bildirimi |

### Web Paneli API Endpoint'leri

| Endpoint | Metot | Açıklama |
| :--- | :--- | :--- |
| `/` | `GET` | Gömülü duyarlı web arayüzünü sunar (`index.html`) |
| `/api/status` | `GET` | Sistem durumunu, düşen paketleri ve kapasite bilgilerini JSON olarak döndürür |
| `/api/networks` | `GET` | Tespit edilen AP listesini ve PMF politikalarını döndürür |
| `/api/stations` | `GET` | Gözlemlenen STA–AP çiftlerini ve RSSI verilerini döndürür |
| `/api/scan` | `POST` | Pasif kanal tarama oturumunu başlatır |
| `/api/attack` | `POST` | Çerçeve enjeksiyon test oturumunu yapılandırır ve başlatır |
| `/api/table/manage` | `POST` | Dahili izleme tablolarındaki kayıtları siler veya temizler |

### Kurulum ve Derleme

#### Ön Gereksinimler

- **ESP-IDF v5.0+** geliştirme ortamının kurulu ve yapılandırılmış olması.
- **Geliştirme sürecinde kullanılan donanım:** ESP-32S geliştirme kartı (`esp32` hedefi). Diğer ESP32 varyantları bu README kapsamında test edilmiş olarak belirtilmemektedir.

#### 1. Depoyu Klonlayın

```bash
git clone https://github.com/SemsYapar/wifiBOOM.git
cd wifiBOOM
```

#### 2. CMake Yapılandırmasını Kontrol Edin

`main/CMakeLists.txt` dosyasında `index.html` dosyasının gömülü dosya olarak tanımlandığından emin olun:

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "index.html")
```

#### 3. Derleyin ve Yükleyin

```bash
idf.py set-target esp32
idf.py build
idf.py -p COM7 flash monitor
```

`COM7` yerine geliştirme kartınızın bağlı olduğu seri portu yazın.

### Yasal Sorumluluk Reddi

> **ÖNEMLİ YASAL UYARI**  
> Bu yazılım yalnızca yetkili güvenlik testleri, eğitim araştırmaları ve kablosuz ağların savunma amaçlı değerlendirilmesi için tasarlanmıştır. Ağ sahibinin açık yazılı izni olmadan kablosuz ağlara paket enjeksiyonu veya deauthentication saldırısı gerçekleştirmek yürürlükteki mevzuata aykırı olabilir. Yazar, yazılımın kötüye kullanımından veya yetkisiz kullanımından kaynaklanan zarar ve hukuki sonuçlardan sorumluluk kabul etmez.
