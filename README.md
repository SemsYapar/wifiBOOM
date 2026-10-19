# ⚡ wifiBOOM · Wireless Network Pentesting Tool

[English](#english) | [Türkçe](#türkçe)

---

## English

An 802.11 passive monitoring and controlled penetration-testing tool developed with ESP-IDF for ESP32 microcontrollers. **wifiBOOM was developed using an ESP-32S development board and ESP-IDF v5.5.5.**

### Technical Highlights & Custom Wi-Fi Library

wifiBOOM includes a custom `main/libnet80211.a` archive for the original ESP32 target. In this archive, `ieee80211_raw_frame_sanity_check` is marked as a **weak symbol**. In this way, a strong definition of the same symbol to take precedence at link time, rather than requiring users to patch the ESP-IDF archive themselves.

**No manual symbol patching or library modification is required.** The project's root `CMakeLists.txt` automatically backs up the ESP-IDF archive as `libnet80211.a.original` (if a backup does not already exist) and copies the bundled custom archive into the ESP-IDF library directory before configuring the project.

Note that marking a symbol as weak does not, by itself, disable the sanity check: the effective behavior depends on the implementation selected by the linker. Raw frame transmission also depends on the ESP-IDF version and hardware.

*For background on the weak-symbol mechanism, see the research article:*  
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

#### Automatic Library Installation

The root `CMakeLists.txt` performs the following operations during CMake configuration:

1. Confirms that the build target is `esp32` (when `IDF_TARGET` is set).
2. Locates the ESP-IDF archive at `$IDF_PATH/components/esp_wifi/lib/esp32/libnet80211.a`.
3. Creates a one-time backup named `libnet80211.a.original`.
4. Copies `main/libnet80211.a` over the ESP-IDF archive, skipping the copy if the files are identical.

**Warning:** This modifies the shared ESP-IDF installation and may affect other projects using the same ESP-IDF environment. To restore the original archive, copy `libnet80211.a.original` back to `libnet80211.a` while no build is running. Ensure your ESP-IDF version is compatible with the bundled archive.

#### 2. Build and Flash

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

ESP32 mikrodenetleyicileri için ESP-IDF ortamında geliştirilmiş, 802.11 pasif izleme ve kontrollü sızma testi aracı. **wifiBOOM'un geliştirme sürecinde ESP-32S geliştirme kartı ve ESP-IDF v5.5.5 kullanılmıştır.**

### Teknik Öne Çıkanlar ve Özel Wi-Fi Kütüphanesi

wifiBOOM, orijinal ESP32 hedefi için özelleştirilmiş bir `main/libnet80211.a` arşivi içerir. Bu arşivde `ieee80211_raw_frame_sanity_check` sembolü **weak (zayıf sembol)** olarak işaretlenmiştir. Böylece aynı sembolün güçlü (`strong`) bir tanımı linking aşamasında öncelik kazanır; kullanıcıların ESP-IDF arşivini kendilerinin patch'lemesine gerek kalmaz.

**Manuel sembol değiştirme veya kütüphane düzenleme işlemi yapmanız gerekmez.** Projenin kök dizinindeki `CMakeLists.txt`, ESP-IDF arşivini (daha önce yedeklenmediyse) `libnet80211.a.original` adıyla yedekler ve projeyle gelen özel arşivi ESP-IDF kütüphane dizinine otomatik olarak kopyalar.

Bir sembolün `weak` olması, tek başına sanity check mekanizmasını devre dışı bırakmaz: geçerli davranış, linker'ın seçtiği fonksiyon uygulamasına bağlıdır. Ham çerçeve iletimi ayrıca ESP-IDF sürümüne ve donanıma bağlıdır.

*Weak sembol mekanizmasının teknik arka planı için araştırma yazısı:*  
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

#### Otomatik Kütüphane Kurulumu

Kök dizindeki `CMakeLists.txt`, CMake yapılandırması sırasında şu işlemleri gerçekleştirir:

1. `IDF_TARGET` tanımlıysa hedefin `esp32` olduğunu kontrol eder.
2. `$IDF_PATH/components/esp_wifi/lib/esp32/libnet80211.a` dosyasını bulur.
3. Orijinal arşivi bir kez `libnet80211.a.original` adıyla yedekler.
4. `main/libnet80211.a` dosyasını ESP-IDF arşivinin üzerine kopyalar; dosyalar aynıysa gereksiz kopyalama yapmaz.

**Uyarı:** Bu işlem ortak ESP-IDF kurulumunu değiştirir ve aynı kurulumu kullanan diğer projeleri etkileyebilir. Orijinal arşive dönmek için herhangi bir derleme çalışmıyorken `libnet80211.a.original` dosyasını `libnet80211.a` adıyla geri kopyalayın. Kullanılan ESP-IDF sürümünün projeyle gelen arşivle uyumlu olduğundan emin olun.

#### 2. Derleyin ve Yükleyin

```bash
idf.py set-target esp32
idf.py build
idf.py -p COM7 flash monitor
```

`COM7` yerine geliştirme kartınızın bağlı olduğu seri portu yazın.

### Yasal Sorumluluk Reddi

> **ÖNEMLİ YASAL UYARI**  
> Bu yazılım yalnızca yetkili güvenlik testleri, eğitim araştırmaları ve kablosuz ağların savunma amaçlı değerlendirilmesi için tasarlanmıştır. Ağ sahibinin açık yazılı izni olmadan kablosuz ağlara paket enjeksiyonu veya deauthentication saldırısı gerçekleştirmek yürürlükteki mevzuata aykırı olabilir. Yazar, yazılımın kötüye kullanımından veya yetkisiz kullanımından kaynaklanan zarar ve hukuki sonuçlardan sorumluluk kabul etmez.
