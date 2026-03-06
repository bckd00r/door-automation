/*
 * ============================================================
 *  Arduino Nano - El Dezenfektan Kapi Gecis Sistemi
 * ============================================================
 *  Bilgi: Bu kod Arduino Uno ve LCD Keypad Shield icin tasarlandi,
 *  ancak ATmega328P kullanilan Arduino Nano ile %100 uyumludur.
 *  Nano Klemens Cevirici (Terminal Adapter Shield) kullanarak asagidaki
 *  kablolamayla devreyi kurabilirsiniz.
 * 
 *  Donanim Baglantilari (Arduino Nano Klemens):
 *    [LCD 16x2]
 *    - RS -> D8
 *    - EN -> D9
 *    - D4 -> D4
 *    - D5 -> D5
 *    - D6 -> D6
 *    - D7 -> D7
 *    - Arka Plan Isigi (Backlight) -> D10
 *    
 *    [Butonlar - Direnc Agi (Resistor Ladder)]
 *    - Analog Okuma Pin -> A0
 *
 *    [Sensör & Röleler]
 *    - Sol El Sensoru (IR, LOW)   -> D2
 *    - Sag El Sensoru (IR, LOW)   -> D3
 *    - Turnike Sensor (IR, LOW)   -> D13
 *    - Dezenfektan Sprey Rolesi   -> D11
 *    - Kapi Kilit Rolesi          -> D12 (AKTIF=ACIK, PASIF=KITLI)
 *
 *  Is Akisi:
 *    1. Calisan ellerini sensor bolmesine koyar
 *    2. 2 sensor de algiladiginda dezenfektan sikilir
 *    3. Sikma tamamlaninca kapi acilir
 *    4. Calisan kapidan gecer (turnike donme sensoru)
 *    5. Gecis algilanir, veri iletilir, kapi kilitlenir
 *    6. Sonraki calisan icin dongu baslar
 *
 *  Ayarlar Menusu (SELECT butonu):
 *    UP/DOWN   = Ayar secimi (sayfalar arasi gezinme)
 *    LEFT/RIGHT = Deger degistirme
 *    SELECT    = Kaydet ve cik
 *    LEFT (ilk sayfada) = Kaydetmeden cik
 * ============================================================
 */

#include <LiquidCrystal.h>
#include <EEPROM.h>

// ============================================================
//  PIN TANIMLARI
// ============================================================
// LCD Keypad Shield pinleri (sabit, shield uzerinde bagli)
#define LCD_RS        8
#define LCD_EN        9
#define LCD_D4        4
#define LCD_D5        5
#define LCD_D6        6
#define LCD_D7        7
#define LCD_BACKLIGHT 10
#define LCD_BUTTONS   A0

// Sensor pinleri
#define SENSOR_LEFT   2    // Sol el sensoru (IR, LOW aktif)
#define SENSOR_RIGHT  3    // Sag el sensoru (IR, LOW aktif)
#define SENSOR_GATE   13   // Turnike donme sensoru (LOW aktif = dondu)

// Role pinleri (Active LOW)
#define RELAY_SPRAY   11   // Dezenfektan sprey rolesi
#define RELAY_GATE    12   // Kapi kilit rolesi (AKTIF=ACIK, PASIF=KITLI)

// ============================================================
//  YAPILANDIRMA SABITLERI (VARSAYILANLAR)
// ============================================================
#define RELAY_ACTIVE    LOW    // Role aktif seviyesi
#define RELAY_INACTIVE  HIGH   // Role pasif seviyesi
#define SENSOR_ACTIVE   LOW    // Sensor algilama seviyesi (nesne var)

#define LCD_UPDATE_MS   250    // LCD guncelleme araligi (ms)

// LCD buton esik degerleri (analog A0)
#define BTN_RIGHT_MAX   50
#define BTN_UP_MAX      195
#define BTN_DOWN_MAX    380
#define BTN_LEFT_MAX    555
#define BTN_SELECT_MAX  790

// ============================================================
//  EEPROM ADRESLERI
// ============================================================
#define EEPROM_FLAG_ADDR        0   // Ayar baslangic bayragi
#define EEPROM_SPRAY_ADDR       1   // Sikma suresi (saniye)
#define EEPROM_GATE_TIMEOUT_ADDR 2  // Kapi zaman asimi (saniye)
#define EEPROM_HAND_CONFIRM_ADDR 3  // El onay suresi (x100ms)
#define EEPROM_PASS_DELAY_ADDR  4   // Gecis bekleme (saniye)
#define EEPROM_BACKLIGHT_ADDR   5   // Arka isik (0/1)
#define EEPROM_DEBOUNCE_ADDR    6   // Sensor debounce (x10ms)
#define EEPROM_INIT_FLAG        0xAA

// ============================================================
//  AYAR LIMITLERI
// ============================================================
// Sikma suresi (saniye)
#define SPRAY_MIN  1
#define SPRAY_MAX  15
#define SPRAY_DEF  3

// Kapi zaman asimi (saniye)
#define GATE_TO_MIN  10
#define GATE_TO_MAX  120
#define GATE_TO_DEF  15

// El onay suresi (x100ms -> 100ms - 3000ms)
#define HAND_CONF_MIN  1
#define HAND_CONF_MAX  30
#define HAND_CONF_DEF  5   // 500ms

// Gecis sonrasi bekleme (saniye)
#define PASS_DLY_MIN  1
#define PASS_DLY_MAX  10
#define PASS_DLY_DEF  2

// Sensor debounce (x10ms -> 10ms - 200ms)
#define DEBOUNCE_MIN  1
#define DEBOUNCE_MAX  20
#define DEBOUNCE_DEF  5   // 50ms

// ============================================================
//  ENUM TANIMLARI
// ============================================================
enum SystemState {
  STATE_IDLE,             // Bekleme - elleri koyun
  STATE_HANDS_DETECTED,   // Eller algilandi, onay bekleniyor
  STATE_DISINFECTING,     // Dezenfektan sikiliyor
  STATE_GATE_OPEN,        // Kapi ACIK, turnike donmesi bekleniyor
  STATE_PASSAGE_DETECTED, // Gecis algilandi, kapi KITLANIYOR
  STATE_SETTINGS          // Ayarlar menusu
};

enum LcdButton {
  BTN_NONE,
  BTN_RIGHT,
  BTN_UP,
  BTN_DOWN,
  BTN_LEFT,
  BTN_SELECT
};

// Ayar menusu sayfalari
enum SettingsPage {
  SET_SPRAY_DURATION,   // Sikma Suresi
  SET_GATE_TIMEOUT,     // Kapi Zaman Asimi
  SET_HAND_CONFIRM,     // El Onay Suresi
  SET_PASS_DELAY,       // Gecis Bekleme
  SET_DEBOUNCE,         // Sensor Hassasiyet
  SET_BACKLIGHT,        // Arka Isik
  SET_RESET_COUNTER,    // Sayac Sifirla
  SET_PAGE_COUNT        // Toplam sayfa sayisi (enum sonu)
};

// ============================================================
//  GLOBAL DEGISKENLER
// ============================================================
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Sistem durumu
SystemState currentState = STATE_IDLE;
SystemState previousState = STATE_IDLE;

// Zamanlar
unsigned long stateEntryTime    = 0;
unsigned long lastLcdUpdate     = 0;
unsigned long handsDetectedTime = 0;

// Sensor debounce
bool sensorLeftStable   = false;
bool sensorRightStable  = false;
bool sensorGateStable   = false;
unsigned long sensorLeftLastChange  = 0;
unsigned long sensorRightLastChange = 0;
unsigned long sensorGateLastChange  = 0;
bool sensorLeftLast   = false;
bool sensorRightLast  = false;
bool sensorGateLast   = false;

// ============================================================
//  AYARLAR YAPISI
// ============================================================
struct Settings {
  uint8_t sprayDurationSec;  // Sikma suresi (saniye)
  uint8_t gateTimeoutSec;    // Kapi zaman asimi (saniye)
  uint8_t handConfirmUnit;   // El onay suresi (x100ms)
  uint8_t passDelaySec;      // Gecis sonrasi bekleme (saniye)
  uint8_t debounceUnit;      // Sensor debounce (x10ms)
  uint8_t backlightOn;       // Arka isik (0=kapali, 1=acik)
};

Settings settings;      // Aktif ayarlar
Settings tempSettings;  // Ayar ekranindaki gecici kopya

// Sayac
uint16_t workerCount = 0;

// Kapi kilit durumu
bool gateLocked = true; // true = KITLI, false = ACIK

// Ayar menusu
uint8_t settingsPageIndex = 0;
bool settingsModified = false;

// Buton yonetimi
LcdButton lastButton      = BTN_NONE;
bool      buttonProcessed = true;
unsigned long lastButtonTime = 0;
#define BUTTON_REPEAT_MS 250

// LCD ozel karakterler
byte progressFull[8]  = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
byte progressEmpty[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
byte arrowRight[8]    = {0x00, 0x04, 0x06, 0x1F, 0x1F, 0x06, 0x04, 0x00};
byte checkMark[8]     = {0x00, 0x01, 0x03, 0x16, 0x1C, 0x08, 0x00, 0x00};
byte handIcon[8]      = {0x0E, 0x11, 0x11, 0x11, 0x0E, 0x04, 0x1F, 0x04};
byte arrowUp[8]       = {0x04, 0x0E, 0x1F, 0x04, 0x04, 0x04, 0x04, 0x00};
byte arrowDown[8]     = {0x00, 0x04, 0x04, 0x04, 0x04, 0x1F, 0x0E, 0x04};
byte gearIcon[8]      = {0x00, 0x0E, 0x11, 0x15, 0x11, 0x0E, 0x00, 0x00};

// ============================================================
//  FONKSIYON PROTOTIPLERI
// ============================================================
void          changeState(SystemState newState);
LcdButton     readButton();
bool          readSensorDebounced(int pin, bool &stable, bool &lastReading, unsigned long &lastChange);
void          loadSettings();
void          saveSettings();
void          applyDefaults();
void          displayIdle();
void          displayHandsDetected();
void          displayDisinfecting();
void          displayGateOpen();
void          displayPassageDetected();
void          displaySettingsPage();
void          handleIdle();
void          handleHandsDetected();
void          handleDisinfecting();
void          handleGateOpen();
void          handlePassageDetected();
void          handleSettings();
void          setSprayRelay(bool active);
void          setGateRelay(bool active);
void          sendPassageLog();
void          drawProgressBar(uint8_t row, float progress);
unsigned long getHandConfirmMs();
unsigned long getGateTimeoutMs();
unsigned long getPassDelayMs();
unsigned long getDebounceMs();

// ============================================================
//  YARDIMCI FONKSIYONLAR - Ayar birimlerini ms'ye cevirme
// ============================================================
unsigned long getHandConfirmMs() {
  return (unsigned long)settings.handConfirmUnit * 100UL;
}

unsigned long getGateTimeoutMs() {
  return (unsigned long)settings.gateTimeoutSec * 1000UL;
}

unsigned long getPassDelayMs() {
  return (unsigned long)settings.passDelaySec * 1000UL;
}

unsigned long getDebounceMs() {
  return (unsigned long)settings.debounceUnit * 10UL;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  // Seri port baslat
  Serial.begin(9600);
  Serial.println(F("{\"event\":\"boot\",\"msg\":\"Sistem baslatiliyor...\"}"));

  // LCD baslat
  lcd.begin(16, 2);
  pinMode(LCD_BACKLIGHT, OUTPUT);

  // Ozel karakterleri yukle
  lcd.createChar(0, progressFull);
  lcd.createChar(1, progressEmpty);
  lcd.createChar(2, arrowRight);
  lcd.createChar(3, checkMark);
  lcd.createChar(4, handIcon);
  lcd.createChar(5, arrowUp);
  lcd.createChar(6, arrowDown);
  lcd.createChar(7, gearIcon);

  // Sensor pinleri
  pinMode(SENSOR_LEFT, INPUT_PULLUP);
  pinMode(SENSOR_RIGHT, INPUT_PULLUP);
  pinMode(SENSOR_GATE, INPUT_PULLUP);

  // Role pinleri - guvenli baslangic (pasif)
  pinMode(RELAY_SPRAY, OUTPUT);
  pinMode(RELAY_GATE, OUTPUT);
  setSprayRelay(false);
  setGateRelay(false);

  // Ayarlari EEPROM'dan yukle
  loadSettings();

  // Arka isik ayarina gore ac/kapat
  digitalWrite(LCD_BACKLIGHT, settings.backlightOn ? HIGH : LOW);

  // Baslangic durumu
  changeState(STATE_IDLE);

  // Ayarlari seri porta yazdir
  Serial.print(F("{\"event\":\"ready\""));
  Serial.print(F(",\"spray_sec\":"));  Serial.print(settings.sprayDurationSec);
  Serial.print(F(",\"gate_to\":"));    Serial.print(settings.gateTimeoutSec);
  Serial.print(F(",\"hand_ms\":"));    Serial.print(getHandConfirmMs());
  Serial.print(F(",\"pass_sec\":"));   Serial.print(settings.passDelaySec);
  Serial.print(F(",\"debounce\":"));   Serial.print(getDebounceMs());
  Serial.print(F(",\"backlight\":")); Serial.print(settings.backlightOn);
  Serial.println(F("}"));
}

// ============================================================
//  ANA DONGU
// ============================================================
void loop() {
  // Sensorleri oku (debounced)
  readSensorDebounced(SENSOR_LEFT, sensorLeftStable, sensorLeftLast, sensorLeftLastChange);
  readSensorDebounced(SENSOR_RIGHT, sensorRightStable, sensorRightLast, sensorRightLastChange);
  readSensorDebounced(SENSOR_GATE, sensorGateStable, sensorGateLast, sensorGateLastChange);

  // Durum isleyicileri
  switch (currentState) {
    case STATE_IDLE:
      handleIdle();
      break;
    case STATE_HANDS_DETECTED:
      handleHandsDetected();
      break;
    case STATE_DISINFECTING:
      handleDisinfecting();
      break;
    case STATE_GATE_OPEN:
      handleGateOpen();
      break;
    case STATE_PASSAGE_DETECTED:
      handlePassageDetected();
      break;
    case STATE_SETTINGS:
      handleSettings();
      break;
  }
}

// ============================================================
//  DURUM YONETIMI
// ============================================================
void changeState(SystemState newState) {
  previousState  = currentState;
  currentState   = newState;
  stateEntryTime = millis();
  lastLcdUpdate  = 0; // LCD'yi hemen guncelle

  // Durum girisinde yapilacaklar
  switch (newState) {
    case STATE_IDLE:
      setSprayRelay(false);
      setGateRelay(false);
      gateLocked = true;
      displayIdle();
      break;

    case STATE_HANDS_DETECTED:
      handsDetectedTime = millis();
      displayHandsDetected();
      break;

    case STATE_DISINFECTING:
      setSprayRelay(true);
      displayDisinfecting();
      break;

    case STATE_GATE_OPEN:
      setSprayRelay(false);
      setGateRelay(true);  // Kapi ACIK (kilit acildi)
      gateLocked = false;
      workerCount++;
      Serial.println(F("{\"event\":\"gate_unlocked\",\"lock\":\"ACIK\"}"));
      displayGateOpen();
      break;

    case STATE_PASSAGE_DETECTED:
      setGateRelay(false); // Kapi KITLI (kilit kapandi)
      gateLocked = true;
      Serial.println(F("{\"event\":\"gate_locked\",\"lock\":\"KITLI\"}"));
      sendPassageLog();
      displayPassageDetected();
      break;

    case STATE_SETTINGS:
      settingsPageIndex = 0;
      settingsModified = false;
      tempSettings = settings; // Ayarlarin kopyasini al
      displaySettingsPage();
      break;
  }
}

// ============================================================
//  DURUM ISLEYICILERI
// ============================================================

// --- IDLE: Elleri bekleme ---
void handleIdle() {
  unsigned long now = millis();

  // LCD periyodik guncelleme (animasyon)
  if (now - lastLcdUpdate >= LCD_UPDATE_MS * 4) {
    lastLcdUpdate = now;
    displayIdle();
  }

  // SELECT butonu ile ayarlara gec
  LcdButton btn = readButton();
  if (btn == BTN_SELECT) {
    changeState(STATE_SETTINGS);
    return;
  }

  // Her iki sensor de aktifse -> eller algilandi
  if (sensorLeftStable && sensorRightStable) {
    changeState(STATE_HANDS_DETECTED);
  }
}

// --- HANDS_DETECTED: Kararli algilama onay suresi ---
void handleHandsDetected() {
  unsigned long now = millis();

  // LCD guncelle
  if (now - lastLcdUpdate >= LCD_UPDATE_MS) {
    lastLcdUpdate = now;
    displayHandsDetected();
  }

  // Eller cekilirse beklemeye don
  if (!sensorLeftStable || !sensorRightStable) {
    changeState(STATE_IDLE);
    return;
  }

  // Eller yeterli sure kararli kalirsa dezenfekteye gec
  if (now - handsDetectedTime >= getHandConfirmMs()) {
    changeState(STATE_DISINFECTING);
  }
}

// --- DISINFECTING: Sprey sikma ---
void handleDisinfecting() {
  unsigned long now     = millis();
  unsigned long elapsed = now - stateEntryTime;
  unsigned long totalMs = (unsigned long)settings.sprayDurationSec * 1000UL;

  // LCD guncelle (progress bar)
  if (now - lastLcdUpdate >= LCD_UPDATE_MS) {
    lastLcdUpdate = now;
    displayDisinfecting();
  }

  // Sure doldu mu?
  if (elapsed >= totalMs) {
    changeState(STATE_GATE_OPEN);
  }
}

// --- GATE_OPEN: Kapi ACIK, turnike donmesi bekleniyor ---
void handleGateOpen() {
  unsigned long now = millis();

  // LCD guncelle
  if (now - lastLcdUpdate >= LCD_UPDATE_MS * 2) {
    lastLcdUpdate = now;
    displayGateOpen();
  }

  // Turnike donme sensoru tetiklendi mi? (calisan kapidan gecti)
  if (sensorGateStable) {
    changeState(STATE_PASSAGE_DETECTED);
    return;
  }

  // Guvenlik zaman asimi
  if (now - stateEntryTime >= getGateTimeoutMs()) {
    Serial.println(F("{\"event\":\"safety_timeout\",\"lock\":\"KITLI\",\"msg\":\"Guvenlik zaman asimi\"}"));
    workerCount--; // Gecis olmadi, sayaci geri al
    gateLocked = true;
    changeState(STATE_IDLE);
  }
}

// --- PASSAGE_DETECTED: Gecis tamamlandi ---
void handlePassageDetected() {
  unsigned long now = millis();

  // LCD guncelle
  if (now - lastLcdUpdate >= LCD_UPDATE_MS) {
    lastLcdUpdate = now;
    displayPassageDetected();
  }

  // Bekleme sonrasi basa don
  if (now - stateEntryTime >= getPassDelayMs()) {
    changeState(STATE_IDLE);
  }
}

// ============================================================
//  AYARLAR MENUSU ISLEYICISI
// ============================================================
void handleSettings() {
  LcdButton btn = readButton();
  if (btn == BTN_NONE) return;

  switch (btn) {
    // --- UP: Onceki ayar sayfasi ---
    case BTN_UP:
      if (settingsPageIndex > 0) {
        settingsPageIndex--;
        displaySettingsPage();
      }
      break;

    // --- DOWN: Sonraki ayar sayfasi ---
    case BTN_DOWN:
      if (settingsPageIndex < SET_PAGE_COUNT - 1) {
        settingsPageIndex++;
        displaySettingsPage();
      }
      break;

    // --- LEFT: Degeri azalt / Cikis ---
    case BTN_LEFT:
      settingsModified = true;
      switch ((SettingsPage)settingsPageIndex) {
        case SET_SPRAY_DURATION:
          if (tempSettings.sprayDurationSec > SPRAY_MIN) tempSettings.sprayDurationSec--;
          break;
        case SET_GATE_TIMEOUT:
          if (tempSettings.gateTimeoutSec > GATE_TO_MIN) tempSettings.gateTimeoutSec -= 5;
          if (tempSettings.gateTimeoutSec < GATE_TO_MIN) tempSettings.gateTimeoutSec = GATE_TO_MIN;
          break;
        case SET_HAND_CONFIRM:
          if (tempSettings.handConfirmUnit > HAND_CONF_MIN) tempSettings.handConfirmUnit--;
          break;
        case SET_PASS_DELAY:
          if (tempSettings.passDelaySec > PASS_DLY_MIN) tempSettings.passDelaySec--;
          break;
        case SET_DEBOUNCE:
          if (tempSettings.debounceUnit > DEBOUNCE_MIN) tempSettings.debounceUnit--;
          break;
        case SET_BACKLIGHT:
          tempSettings.backlightOn = 0;
          digitalWrite(LCD_BACKLIGHT, LOW);
          break;
        case SET_RESET_COUNTER:
          // LEFT'te bir sey yapma
          settingsModified = false;
          break;
        default: break;
      }
      displaySettingsPage();
      break;

    // --- RIGHT: Degeri artir ---
    case BTN_RIGHT:
      settingsModified = true;
      switch ((SettingsPage)settingsPageIndex) {
        case SET_SPRAY_DURATION:
          if (tempSettings.sprayDurationSec < SPRAY_MAX) tempSettings.sprayDurationSec++;
          break;
        case SET_GATE_TIMEOUT:
          if (tempSettings.gateTimeoutSec < GATE_TO_MAX) tempSettings.gateTimeoutSec += 5;
          if (tempSettings.gateTimeoutSec > GATE_TO_MAX) tempSettings.gateTimeoutSec = GATE_TO_MAX;
          break;
        case SET_HAND_CONFIRM:
          if (tempSettings.handConfirmUnit < HAND_CONF_MAX) tempSettings.handConfirmUnit++;
          break;
        case SET_PASS_DELAY:
          if (tempSettings.passDelaySec < PASS_DLY_MAX) tempSettings.passDelaySec++;
          break;
        case SET_DEBOUNCE:
          if (tempSettings.debounceUnit < DEBOUNCE_MAX) tempSettings.debounceUnit++;
          break;
        case SET_BACKLIGHT:
          tempSettings.backlightOn = 1;
          digitalWrite(LCD_BACKLIGHT, HIGH);
          break;
        case SET_RESET_COUNTER:
          // RIGHT ile sayaci sifirla
          workerCount = 0;
          break;
        default: break;
      }
      displaySettingsPage();
      break;

    // --- SELECT: Kaydet ve cik ---
    case BTN_SELECT:
      if (settingsModified) {
        settings = tempSettings;
        saveSettings();
        digitalWrite(LCD_BACKLIGHT, settings.backlightOn ? HIGH : LOW);

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.write((uint8_t)3);
        lcd.print(F(" Kaydedildi!"));
        lcd.setCursor(0, 1);
        lcd.print(F("Ayarlar aktif"));
        delay(1200);
      }
      changeState(STATE_IDLE);
      return;

    default:
      break;
  }
}

// ============================================================
//  LCD GORUNTU FONKSIYONLARI
// ============================================================
void displayIdle() {
  lcd.clear();

  // Satir 1: Baslik
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)4); // El ikonu
  lcd.print(F(" Dezenfektan  "));

  // Satir 2: Yonerge
  lcd.setCursor(0, 1);
  lcd.print(F("Ellerinizi Koyun"));
}

void displayHandsDetected() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Eller Algilandi!"));

  lcd.setCursor(0, 1);

  // Onay bekleme suresi progress
  unsigned long elapsed = millis() - handsDetectedTime;
  unsigned long total = getHandConfirmMs();
  float pct = (float)elapsed / (float)total;
  if (pct > 1.0f) pct = 1.0f;

  uint8_t bars = (uint8_t)(pct * 16.0f);
  for (uint8_t i = 0; i < 16; i++) {
    lcd.print(i < bars ? '\xFF' : '.');
  }
}

void displayDisinfecting() {
  unsigned long elapsed = millis() - stateEntryTime;
  unsigned long totalMs = (unsigned long)settings.sprayDurationSec * 1000UL;
  float progress = (float)elapsed / (float)totalMs;
  if (progress > 1.0f) progress = 1.0f;

  uint8_t remainSec = settings.sprayDurationSec - (uint8_t)(elapsed / 1000UL);
  if (elapsed >= totalMs) remainSec = 0;

  lcd.clear();

  // Satir 1: Durum ve kalan sure
  lcd.setCursor(0, 0);
  lcd.print(F("Sikma: "));
  lcd.print(remainSec);
  lcd.print(F(" sn"));

  // Satir 2: Ilerleme cubugu
  drawProgressBar(1, progress);
}

void displayGateOpen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)3);
  lcd.print(F(" Kapi: ACIK"));

  lcd.setCursor(0, 1);
  lcd.print(F("Kapidan Gecin "));
  lcd.write((uint8_t)2);
  lcd.write((uint8_t)2);
}

void displayPassageDetected() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)3);
  lcd.print(F(" Kapi: KITLI"));

  lcd.setCursor(0, 1);
  lcd.print(F("Gecis OK #"));
  lcd.print(workerCount);
}

// ============================================================
//  AYARLAR SAYFASI GORUNTULEME
// ============================================================
void displaySettingsPage() {
  lcd.clear();

  // Satir 1: Ayar adi + navigasyon oklari
  lcd.setCursor(0, 0);

  // Yukari ok (ilk sayfa degilse)
  if (settingsPageIndex > 0) {
    lcd.write((uint8_t)5); // Yukari ok
  } else {
    lcd.print(' ');
  }

  // Sayfa numarasi
  lcd.print(settingsPageIndex + 1);
  lcd.print('/');
  lcd.print((uint8_t)SET_PAGE_COUNT);
  lcd.print(' ');

  // Asagi ok (son sayfa degilse)
  if (settingsPageIndex < SET_PAGE_COUNT - 1) {
    lcd.write((uint8_t)6); // Asagi ok
  } else {
    lcd.print(' ');
  }

  lcd.print(' ');

  // Ayar adi (kisa)
  switch ((SettingsPage)settingsPageIndex) {
    case SET_SPRAY_DURATION:
      lcd.print(F("Sikma Sur"));
      break;
    case SET_GATE_TIMEOUT:
      lcd.print(F("Kapi Z.As"));
      break;
    case SET_HAND_CONFIRM:
      lcd.print(F("El Onay"));
      break;
    case SET_PASS_DELAY:
      lcd.print(F("Gecis Bkl"));
      break;
    case SET_DEBOUNCE:
      lcd.print(F("Hassasiyet"));
      break;
    case SET_BACKLIGHT:
      lcd.print(F("Arka Isik"));
      break;
    case SET_RESET_COUNTER:
      lcd.print(F("Sayac"));
      break;
    default: break;
  }

  // Satir 2: Deger gosterimi
  lcd.setCursor(0, 1);

  switch ((SettingsPage)settingsPageIndex) {
    case SET_SPRAY_DURATION:
      lcd.print(F("<"));
      printCentered(tempSettings.sprayDurationSec, F("sn"), SPRAY_MIN, SPRAY_MAX);
      lcd.print(F(">"));
      break;

    case SET_GATE_TIMEOUT:
      lcd.print(F("<"));
      printCentered(tempSettings.gateTimeoutSec, F("sn"), GATE_TO_MIN, GATE_TO_MAX);
      lcd.print(F(">"));
      break;

    case SET_HAND_CONFIRM: {
      unsigned int ms = (unsigned int)tempSettings.handConfirmUnit * 100;
      lcd.print(F("<"));
      lcd.print(F(" "));
      lcd.print(ms);
      lcd.print(F(" ms"));
      // Sag tarafi doldur
      uint8_t len = 4; // " ms" + digit count
      if (ms >= 1000) len += 4;
      else if (ms >= 100) len += 3;
      else len += 2;
      for (uint8_t i = len; i < 14; i++) lcd.print(' ');
      lcd.print(F(">"));
      break;
    }

    case SET_PASS_DELAY:
      lcd.print(F("<"));
      printCentered(tempSettings.passDelaySec, F("sn"), PASS_DLY_MIN, PASS_DLY_MAX);
      lcd.print(F(">"));
      break;

    case SET_DEBOUNCE: {
      unsigned int ms = (unsigned int)tempSettings.debounceUnit * 10;
      lcd.print(F("<"));
      lcd.print(F(" "));
      lcd.print(ms);
      lcd.print(F(" ms"));
      uint8_t len = 4;
      if (ms >= 100) len += 3;
      else if (ms >= 10) len += 2;
      else len += 1;
      for (uint8_t i = len; i < 14; i++) lcd.print(' ');
      lcd.print(F(">"));
      break;
    }

    case SET_BACKLIGHT:
      lcd.print(F("<"));
      if (tempSettings.backlightOn) {
        lcd.print(F("     ACIK      "));
      } else {
        lcd.print(F("    KAPALI     "));
      }
      lcd.setCursor(15, 1);
      lcd.print(F(">"));
      break;

    case SET_RESET_COUNTER:
      lcd.print(F(" #"));
      lcd.print(workerCount);
      lcd.print(F("  R>Sifirla"));
      break;

    default:
      break;
  }
}

// Ortalanmis sayi + birim yazdirma
void printCentered(uint8_t value, const __FlashStringHelper* unit, uint8_t minVal, uint8_t maxVal) {
  // "< XX sn         >" seklinde gosterim (14 karakter alan)
  lcd.print(F(" "));

  // Sol ok (azaltma mumkunse)
  if (value > minVal) lcd.print(F("<<")); else lcd.print(F("  "));
  lcd.print(F(" "));

  if (value < 10) lcd.print(' ');
  if (value < 100) lcd.print(' ');
  lcd.print(value);
  lcd.print(' ');
  lcd.print(unit);

  lcd.print(F(" "));
  if (value < maxVal) lcd.print(F(">>")); else lcd.print(F("  "));
}

// Ilerleme cubugu cizimi (16 karakter genislik)
void drawProgressBar(uint8_t row, float progress) {
  lcd.setCursor(0, row);

  uint8_t filledChars = (uint8_t)(progress * 16.0f);

  for (uint8_t i = 0; i < 16; i++) {
    if (i < filledChars) {
      lcd.write((uint8_t)0); // Dolu blok
    } else {
      lcd.write((uint8_t)1); // Bos blok
    }
  }
}

// ============================================================
//  SENSOR OKUMA (DEBOUNCED) - Ayarlanabilir debounce suresi
// ============================================================
bool readSensorDebounced(int pin, bool &stable, bool &lastReading, unsigned long &lastChange) {
  bool currentReading = (digitalRead(pin) == SENSOR_ACTIVE);
  unsigned long now = millis();

  if (currentReading != lastReading) {
    lastChange  = now;
    lastReading = currentReading;
  }

  if ((now - lastChange) >= getDebounceMs()) {
    stable = lastReading;
  }

  return stable;
}

// ============================================================
//  BUTON OKUMA
// ============================================================
LcdButton readButton() {
  unsigned long now = millis();
  int adc = analogRead(LCD_BUTTONS);

  LcdButton btn = BTN_NONE;

  if (adc < BTN_RIGHT_MAX) {
    btn = BTN_RIGHT;
  } else if (adc < BTN_UP_MAX) {
    btn = BTN_UP;
  } else if (adc < BTN_DOWN_MAX) {
    btn = BTN_DOWN;
  } else if (adc < BTN_LEFT_MAX) {
    btn = BTN_LEFT;
  } else if (adc < BTN_SELECT_MAX) {
    btn = BTN_SELECT;
  }

  // Buton tekrar engelleme
  if (btn == BTN_NONE) {
    lastButton      = BTN_NONE;
    buttonProcessed = false;
    return BTN_NONE;
  }

  // Ayni buton basiliysa ve tekrar suresi dolmadiysa atla
  if (btn == lastButton && buttonProcessed && (now - lastButtonTime < BUTTON_REPEAT_MS)) {
    return BTN_NONE;
  }

  lastButton      = btn;
  buttonProcessed = true;
  lastButtonTime  = now;

  return btn;
}

// ============================================================
//  ROLE KONTROL
// ============================================================
void setSprayRelay(bool active) {
  digitalWrite(RELAY_SPRAY, active ? RELAY_ACTIVE : RELAY_INACTIVE);
}

// Kapi kilit rolesi
// active = true  -> Role AKTIF  -> Kapi ACIK  (kilit acildi, gecis serbest)
// active = false -> Role PASIF  -> Kapi KITLI (kilit kapali, gecis engellendi)
void setGateRelay(bool active) {
  digitalWrite(RELAY_GATE, active ? RELAY_ACTIVE : RELAY_INACTIVE);
}

// ============================================================
//  EEPROM AYAR YONETIMI
// ============================================================
void applyDefaults() {
  settings.sprayDurationSec = SPRAY_DEF;
  settings.gateTimeoutSec   = GATE_TO_DEF;
  settings.handConfirmUnit  = HAND_CONF_DEF;
  settings.passDelaySec     = PASS_DLY_DEF;
  settings.debounceUnit     = DEBOUNCE_DEF;
  settings.backlightOn      = 1;
}

void loadSettings() {
  uint8_t flag = EEPROM.read(EEPROM_FLAG_ADDR);

  if (flag == EEPROM_INIT_FLAG) {
    // Ayarlar daha once kaydedilmis
    settings.sprayDurationSec = EEPROM.read(EEPROM_SPRAY_ADDR);
    settings.gateTimeoutSec   = EEPROM.read(EEPROM_GATE_TIMEOUT_ADDR);
    settings.handConfirmUnit  = EEPROM.read(EEPROM_HAND_CONFIRM_ADDR);
    settings.passDelaySec     = EEPROM.read(EEPROM_PASS_DELAY_ADDR);
    settings.backlightOn      = EEPROM.read(EEPROM_BACKLIGHT_ADDR);
    settings.debounceUnit     = EEPROM.read(EEPROM_DEBOUNCE_ADDR);

    // Gecerlilik kontrolu - aralik disi deger varsa varsayilana don
    bool valid = true;
    if (settings.sprayDurationSec < SPRAY_MIN || settings.sprayDurationSec > SPRAY_MAX) valid = false;
    if (settings.gateTimeoutSec < GATE_TO_MIN || settings.gateTimeoutSec > GATE_TO_MAX) valid = false;
    if (settings.handConfirmUnit < HAND_CONF_MIN || settings.handConfirmUnit > HAND_CONF_MAX) valid = false;
    if (settings.passDelaySec < PASS_DLY_MIN || settings.passDelaySec > PASS_DLY_MAX) valid = false;
    if (settings.debounceUnit < DEBOUNCE_MIN || settings.debounceUnit > DEBOUNCE_MAX) valid = false;
    if (settings.backlightOn > 1) valid = false;

    if (!valid) {
      applyDefaults();
      saveSettings();
    }
  } else {
    // Ilk calistirma - varsayilan degerleri yaz
    applyDefaults();
    saveSettings();
  }

  Serial.print(F("{\"event\":\"settings_loaded\""));
  Serial.print(F(",\"spray\":"));   Serial.print(settings.sprayDurationSec);
  Serial.print(F(",\"gate_to\":")); Serial.print(settings.gateTimeoutSec);
  Serial.print(F(",\"hand\":"));    Serial.print(settings.handConfirmUnit);
  Serial.print(F(",\"pass\":"));    Serial.print(settings.passDelaySec);
  Serial.print(F(",\"dbnc\":"));    Serial.print(settings.debounceUnit);
  Serial.print(F(",\"bl\":"));      Serial.print(settings.backlightOn);
  Serial.println(F("}"));
}

void saveSettings() {
  EEPROM.update(EEPROM_FLAG_ADDR, EEPROM_INIT_FLAG);
  EEPROM.update(EEPROM_SPRAY_ADDR, settings.sprayDurationSec);
  EEPROM.update(EEPROM_GATE_TIMEOUT_ADDR, settings.gateTimeoutSec);
  EEPROM.update(EEPROM_HAND_CONFIRM_ADDR, settings.handConfirmUnit);
  EEPROM.update(EEPROM_PASS_DELAY_ADDR, settings.passDelaySec);
  EEPROM.update(EEPROM_BACKLIGHT_ADDR, settings.backlightOn);
  EEPROM.update(EEPROM_DEBOUNCE_ADDR, settings.debounceUnit);

  Serial.print(F("{\"event\":\"settings_saved\""));
  Serial.print(F(",\"spray\":"));   Serial.print(settings.sprayDurationSec);
  Serial.print(F(",\"gate_to\":")); Serial.print(settings.gateTimeoutSec);
  Serial.print(F(",\"hand\":"));    Serial.print(settings.handConfirmUnit);
  Serial.print(F(",\"pass\":"));    Serial.print(settings.passDelaySec);
  Serial.print(F(",\"dbnc\":"));    Serial.print(settings.debounceUnit);
  Serial.print(F(",\"bl\":"));      Serial.print(settings.backlightOn);
  Serial.println(F("}"));
}

// ============================================================
//  SERI PORT LOGLAMA
// ============================================================
void sendPassageLog() {
  Serial.print(F("{\"event\":\"passage\",\"worker\":"));
  Serial.print(workerCount);
  Serial.print(F(",\"spray_sec\":"));
  Serial.print(settings.sprayDurationSec);
  Serial.print(F(",\"millis\":"));
  Serial.print(millis());
  Serial.println(F("}"));
}
