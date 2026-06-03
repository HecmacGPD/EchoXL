#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>

// --- Pin Definitions ---
#define LED_COLON    1
#define BTN_PIN      3  
#define TLC_OE_PIN   4  
#define TLC_CLK_PIN  6  
#define LEFT_SDI_PIN 7  
#define TLC_LE_PIN   10 
#define LDR_PIN      0

// --- Brightness & PWM Settings ---
const int PWM_FREQ = 5000;
const int PWM_RES = 8;
const int OE_CHANNEL = 0;
const int COLON_CHANNEL = 1;

enum BrightnessMode { LOW_BRIGHT, MED_BRIGHT, HIGH_BRIGHT, AUTO_BRIGHT };
BrightnessMode currentBrightness = AUTO_BRIGHT;

// --- Clock State & Preferences ---
Preferences preferences;
bool is24hMode = false;
bool isConfigMode = false;
String savedSSID = "";
String savedPW = "";

// --- Button Variables ---
bool btnState = HIGH;
unsigned long btnPressTime = 0;
bool longPressTriggered = false;
unsigned long lastBtnPoll = 0;

// Web/DNS Servers for Captive Portal
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);
IPAddress apIP(192, 168, 4, 1);

// --- 7-Segment Mapping ---
const uint8_t font[10] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111  // 9
};

const uint8_t pin_map[4][7] = {
    {26, 27, 21, 22, 23, 25, 24},  // Visual D1 
    {30, 31, 17, 18, 19, 29, 28},  // Visual D2 
    {2, 3, 13, 14, 15, 1, 0},      // Visual D3 
    {6, 7, 9, 10, 11, 5, 4}        // Visual D4 
};
const uint8_t dp_map[4] = {20, 16, 12, 8}; 

// --- HTML Captive Portal ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { background-color: #121212; color: white; font-family: sans-serif; text-align: center; padding: 20px; }
  form { background: #1e1e1e; padding: 20px; border-radius: 10px; display: inline-block; text-align: left; margin-bottom: 20px; }
  input[type=text], input[type=password], input[type=time] { width: 100%; padding: 10px; margin: 10px 0; border: none; border-radius: 5px; box-sizing: border-box; }
  input[type=submit] { background-color: #4CAF50; color: white; padding: 12px 20px; border: none; border-radius: 5px; cursor: pointer; width: 100%; font-weight: bold; }
  .logo { margin-bottom: 30px; }
</style></head><body>
  <div class="logo">
    <svg viewBox="0 0 300 80" style="width: 100%; max-width: 300px;">
      <path d="M 40 10 L 10 10 L 10 40 L 35 40 M 10 40 L 10 70 L 40 70" stroke="white" stroke-width="8" stroke-linecap="square" fill="none" />
      <path d="M 90 10 L 60 10 L 60 70 L 90 70" stroke="white" stroke-width="8" stroke-linecap="square" fill="none" />
      <path d="M 110 10 L 110 70 M 110 40 L 140 40 M 140 10 L 140 70" stroke="white" stroke-width="8" stroke-linecap="square" fill="none" />
      <path d="M 160 10 L 190 10 L 190 70 L 160 70 Z" stroke="white" stroke-width="8" stroke-linecap="square" fill="none" />
      <text x="205" y="72" fill="white" font-family="sans-serif" font-style="italic" font-weight="900" font-size="70">XL</text>
    </svg>
  </div>
  <h3>Wi-Fi Setup (2.4GHz Only)</h3>
  <form action="/save" method="post">
    <label>SSID (Network Name):</label><input type="text" name="ssid" required>
    <label>Password:</label><input type="password" name="pass">
    <input type="submit" value="Connect & Sync Time">
  </form>
  <h3>Or Set Manually</h3>
  <form action="/manual" method="post">
    <label>Time:</label><input type="time" name="mtime" required>
    <input type="submit" value="Set Time">
  </form>
</body></html>
)rawliteral";

// --- Helper Functions ---
void setDisplayData(uint32_t data) {
  shiftOut(LEFT_SDI_PIN, TLC_CLK_PIN, MSBFIRST, (data >> 24) & 0xFF);
  shiftOut(LEFT_SDI_PIN, TLC_CLK_PIN, MSBFIRST, (data >> 16) & 0xFF);
  shiftOut(LEFT_SDI_PIN, TLC_CLK_PIN, MSBFIRST, (data >> 8) & 0xFF);
  shiftOut(LEFT_SDI_PIN, TLC_CLK_PIN, MSBFIRST, data & 0xFF);
  digitalWrite(TLC_LE_PIN, HIGH);
  delayMicroseconds(5); 
  digitalWrite(TLC_LE_PIN, LOW);
}

void renderDisplay(int h, int m, bool pm_dot, bool config_dot) {
  uint32_t out = 0;
  int digits[4] = { h / 10, h % 10, m / 10, m % 10 };
  
  if (digits[0] == 0) digits[0] = -1; // Blank leading zero

  for(int i = 0; i < 4; i++) {
      if(digits[i] < 0) continue; 
      uint8_t segments = font[digits[i]];
      for(int seg = 0; seg < 7; seg++) {
          if(segments & (1 << seg)) {
              out |= (1UL << pin_map[i][seg]);
          }
      }
  }
  if(pm_dot) out |= (1UL << dp_map[3]); 
  if(config_dot) out |= (1UL << dp_map[0]); 
  
  setDisplayData(out);
}

// --- NEW Helper: Renders "ECHO" text ---
void renderTextECHO(bool config_dot) {
  uint32_t out = 0;
  
  // Custom binary patterns for "E", "C", "H", "O"
  uint8_t echo_segments[4] = {
      0b01111001, // E
      0b00111001, // C
      0b01110110, // H
      0b00111111  // O
  };

  for(int i = 0; i < 4; i++) {
      for(int seg = 0; seg < 7; seg++) {
          if(echo_segments[i] & (1 << seg)) {
              out |= (1UL << pin_map[i][seg]);
          }
      }
  }
  
  if(config_dot) out |= (1UL << dp_map[0]); 
  setDisplayData(out);
}

void startCaptivePortal() {
  isConfigMode = true;
  
  // Force clean slate for Access Point mode
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP); 
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("ECHOXL-Setup");
  
  dnsServer.start(DNS_PORT, "*", apIP);
  
  server.on("/", []() { server.send(200, "text/html", index_html); });
  
  server.on("/save", []() {
    savedSSID = server.arg("ssid");
    savedPW = server.arg("pass");
    savedSSID.trim();
    savedPW.trim();
    preferences.putString("ssid", savedSSID);
    preferences.putString("pass", savedPW);
    server.send(200, "text/html", "Saved! Rebooting clock...");
    delay(1000);
    ESP.restart();
  });

  server.on("/manual", []() {
    String t = server.arg("mtime"); 
    int h = t.substring(0, 2).toInt();
    int m = t.substring(3, 5).toInt();
    
    struct tm tm_struct = {0};
    tm_struct.tm_hour = h;
    tm_struct.tm_min = m;
    tm_struct.tm_sec = 0; 
    tm_struct.tm_year = 124; 
    tm_struct.tm_mon = 0;
    tm_struct.tm_mday = 1;
    time_t t_of_day = mktime(&tm_struct);
    struct timeval tv = { .tv_sec = t_of_day, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    
    isConfigMode = false;
    WiFi.softAPdisconnect(true); 
    server.send(200, "text/html", "Time set! Closing portal...");
  });

  // --- Explicit Android / Chinese ROM Catchers ---
  server.on("/generate_204", []() {
    server.sendHeader("Location", String("http://") + apIP.toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.onNotFound([]() { 
    server.send(200, "text/html", index_html); 
  });
  
  server.begin();
}

void setup() {
  pinMode(TLC_CLK_PIN, OUTPUT);
  pinMode(LEFT_SDI_PIN, OUTPUT);
  pinMode(TLC_LE_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP); 
  
  ledcSetup(OE_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(TLC_OE_PIN, OE_CHANNEL);
  ledcSetup(COLON_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(LED_COLON, COLON_CHANNEL);

  digitalWrite(TLC_LE_PIN, LOW);      
  digitalWrite(TLC_CLK_PIN, LOW);     
  digitalWrite(LEFT_SDI_PIN, LOW); 

  // --- BOOT SPLASH SCREEN ---
  // Push out the "ECHO" display immediately before WiFi blocks the code
  renderTextECHO(false); 
  ledcWrite(OE_CHANNEL, 200); // Dim/Safe Brightness
  ledcWrite(COLON_CHANNEL, 0); // Colon off during boot up
  
  struct tm tm_default = {0};
  tm_default.tm_hour = 12;
  tm_default.tm_min = 0;
  tm_default.tm_sec = 0;
  tm_default.tm_year = 124; 
  tm_default.tm_mday = 1;
  time_t t_of_day = mktime(&tm_default);
  struct timeval tv = { .tv_sec = t_of_day, .tv_usec = 0 };
  settimeofday(&tv, NULL);

  // Initialize Preferences
  preferences.begin("echoxl", false);

  // --- HARDWARE RESET CHECK ---
  // Check if the button is being held down on boot
  if (digitalRead(BTN_PIN) == LOW) {
    preferences.clear();
    delay(3000); 
  }

  is24hMode = preferences.getBool("24h", false);
  savedSSID = preferences.getString("ssid", "");
  savedPW = preferences.getString("pass", "");

  // --- WI-FI FIX ---
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect(true, true);
  delay(200);

  if (savedSSID != "") {
    WiFi.begin(savedSSID.c_str(), savedPW.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500); 
      attempts++;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(10 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    int retry = 0;
    while (time(nullptr) < 100000 && retry < 15) {
        delay(200); 
        retry++;
    }
  } else {
    startCaptivePortal();
  }
}

void loop() {
  unsigned long now = millis();

  if (isConfigMode) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;
  int s = timeinfo.tm_sec;

  bool pm_dot = false;
  if (!is24hMode) {
    if (h >= 12) pm_dot = true;
    if (h == 0) h = 12;
    if (h > 12) h -= 12;
  }

  int dutyCycle = 127; 
  if (currentBrightness == AUTO_BRIGHT) {
    int adc_val = analogRead(LDR_PIN);

    int adc_dark = 3600; 
    int adc_light = 3000;
    
    if (adc_val > adc_dark) adc_val = adc_dark;
    if (adc_val < adc_light) adc_val = adc_light;
    
    // Auto-brightness calculation
    dutyCycle = map(adc_val, adc_light, adc_dark, 0, 253); 
  } else if (currentBrightness == LOW_BRIGHT) {
    dutyCycle = 253; 
  } else if (currentBrightness == MED_BRIGHT) {
    dutyCycle = 200; 
  } else if (currentBrightness == HIGH_BRIGHT) {
    dutyCycle = 0;   
  }
  
  ledcWrite(OE_CHANNEL, dutyCycle);

  // 5. Update Display
  bool config_dot = false;
  if (isConfigMode && (s % 4 == 0)) config_dot = true; 
  
  renderDisplay(h, m, pm_dot, config_dot);
  
  int colonBrightness = 255 - dutyCycle; 
  if (s % 2 == 0) {
    ledcWrite(COLON_CHANNEL, colonBrightness);
  } else {
    ledcWrite(COLON_CHANNEL, 0);
  }

  // --- Button Polling ---
  if (now - lastBtnPoll > 20) {
    bool currentReading = digitalRead(BTN_PIN);
    
    if (currentReading != btnState) {
      btnState = currentReading;
      
      if (btnState == LOW) { // Button Pressed
        btnPressTime = now;
        longPressTriggered = false;
      } 
      else if (btnState == HIGH) { // Button Released
        if (!longPressTriggered) { 
          if (currentBrightness == AUTO_BRIGHT) currentBrightness = LOW_BRIGHT;
          else if (currentBrightness == LOW_BRIGHT) currentBrightness = MED_BRIGHT;
          else if (currentBrightness == MED_BRIGHT) currentBrightness = HIGH_BRIGHT;
          else currentBrightness = AUTO_BRIGHT;
        }
      }
    }

    if (btnState == LOW && !longPressTriggered && (now - btnPressTime) > 1000) {
      longPressTriggered = true;
      is24hMode = !is24hMode;
      preferences.putBool("24h", is24hMode);
    }
    
    lastBtnPoll = now;
  }
}