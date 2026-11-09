# Weatherdisplay

En fristående, svenskspråkig väderdisplay byggd med **ESP32-2432S028R (Cheap Yellow Display, CYD)**, pekskärm och en **320 × 240 px** ILI9341-skärm. Prognoser hämtas direkt från **SMHI**, utan Home Assistant eller lokal server.

**Konfigurera Väderdisplay:** https://hgpuke.github.io/weatherdisplay/

## Funktioner

- **Huvudvy:** vald ort, klocka, senaste uppdatering, vädersymbol, temperatur, upplevd temperatur, vind, nederbörd och femdygnsprognos.
- **Dygnsprognos (sida 2):** sex tidsluckor över kommande 24 timmar med vädersymboler, temperaturkurva, vindinformation och nederbördsstaplar (mm).
- **Dag/natt:** särskilda 96 × 96- och 32 × 32-ikoner för klart och delvis molnigt väder nattetid.
- **Första installationen:** QR-kod på displayen leder till en mobilanpassad HTTPS-sida. Användaren väljer ett tillgängligt 2,4 GHz-WiFi-nätverk, anger lösenord och väljer en svensk ort.
- **Omkonfiguration:** kugghjulet på huvudvyn återstartar till konfigurationsläget efter bekräftelse.
- **Lagring:** WiFi-inställningar och ortskoordinater sparas i ESP32:ans NVS och överlever omstarter.

## Hårdvara och teknik

| Del | Användning |
| --- | --- |
| Kort | ESP32-2432S028R, CYD, 4 MB flash, utan PSRAM |
| Skärm | ILI9341 320 × 240, liggande, TFT_eSPI |
| Pekskärm | XPT2046 |
| Grafik | LVGL 8.3, ursprunglig layout via LCD Wizard |
| Prognos | SMHI:s prognos-API (SNOW1gv1), koordinatbaserat |
| Konfiguration | Improv Wi-Fi via Bluetooth LE, NVS |
| Utveckling | PlatformIO + Arduino-ramverket |

Projektet är provat på den CYD-variant som användes vid utvecklingen. Andra kortrevisioner kan behöva andra pin- och skärminställningar.

## Första installationen

1. Flasha program och SPIFFS-bildresurser.
2. Skanna QR-koden som visas på CYD-skärmen.
3. Öppna sidan i **Chrome på Android** och välj **Anslut Väderdisplayen**.
4. Välj enheten via Bluetooth. Välj sedan ett av de upptäckta 2,4 GHz-nätverken, ange lösenord och sök fram ort.
5. Klicka **Anslut och spara**. Enheten startar om och hämtar SMHI-prognosen för de sparade koordinaterna.

Webbsidan finns i [**`docs/index.html`**](docs/index.html) i detta repository och publiceras med GitHub Pages. Lösenordet skickas direkt via Bluetooth mellan webbläsaren och ESP32, inte till GitHub Pages. Web Bluetooth stöds inte av standard-Safari på iPhone.

## Bygg och flasha med PlatformIO

Klona repositoryt och kör i projektets rot:

```bash
pio pkg install
python3 patch_ble.py
pio run
```

För första installationen, eller när bildfiler under `data/` ändrats:

```bash
pio run -t uploadfs --upload-port /dev/ttyUSB0
pio run -t upload --upload-port /dev/ttyUSB0
pio device monitor -p /dev/ttyUSB0 -b 115200
```

Om bara programkoden ändrats behövs enbart `pio run -t upload`. Använd rätt USB-port på din dator. SPIFFS och firmware flashas separat.

## Källkodsstruktur

| Fil | Uppgift |
| --- | --- |
| `Weatherdisplay.ino` | Uppstart, normaldrift och val av BLE-konfigurationsläge |
| `wx_setup_ble.*`, `weather_ble_extra.*` | Improv BLE, upptäckta nätverk och konfigurering |
| `wx_settings.*` | Hantering av inställningar i NVS |
| `wx_settings_ui.*` | Kugghjul och bekräftelsedialog |
| `weather_engine.*` | SMHI-hämtning och prognosdata |
| `weather_ui.*` | Huvudvyn och femdygnsprognosen |
| `weather_hourly_ui.*` | Dygnsprognosen och temperaturgrafen |
| `src/ui/` | LCD Wizard-export samt Barlow-typsnitt |
| `data/` | Bilder i binärformat för SPIFFS |
| `docs/` | Konfigurationswebbsidan (GitHub Pages) |

Bluetooth och den vanliga vädervyn körs i separata uppstarter. Vid normal drift frigörs Bluetooth-minne innan HTTPS-hämtningar, eftersom CYD-kortet inte har PSRAM.

## Kända förbättringsområden

- Värdena för **”Känns som”** behöver använda temperatur och vind från samma prognostidpunkt. Den nuvarande implementationen kan kombinera en representativ temperatur med periodens högsta medelvind.
- Vindvärdet på huvudvyn kan avse ett maximum under en period och avvika från en enskild timmes medelvind i SMHI-appen.
- Serielloggen skriver ibland ut samma lokalt omräknade prognos flera gånger; en kommande ändring ska skilja detta från faktiska hämtningar.

## Sekretess och licenser

**Lägg aldrig upp** `wifi_config.h`, egna WiFi-lösenord, byggfiler, NVS-dumpar eller lokala säkerhetskopior i ett publikt repository. En `.gitignore` ingår. Inställningar sparas normalt endast i enhetens NVS.

Fontlicenserna från LCD Wizard finns under `src/ui/fonts/LICENSES.md`. En övergripande licens för den egenutvecklade koden har ännu inte valts.
