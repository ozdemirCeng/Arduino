#include <LiquidCrystal.h>

// --- Pin tanımları ---
const int PIN_BTN_MOTOR    = 22; 
const int PIN_BTN_SEATBELT = 23;
const int PIN_SWITCH_DOOR  = 24;

const int PIN_LM35         = A0;
const int PIN_LDR          = A1;
const int PIN_FUEL_POT     = A2;

const int LED_SEATBELT     = 30; // Kırmızı LED - Emniyet kemeri
const int LED_HEADLIGHT    = 31; // Mavi LED - Farlar
const int LED_FUEL         = 32; // Sarı LED - Yakıt

const int RGB_RED_PIN      = 33;
const int RGB_GREEN_PIN    = 34;
const int RGB_BLUE_PIN     = 35;

const int PIN_BUZZER       = 36;
const int MOTOR_ENGINE     = 37;
const int MOTOR_FAN        = 38;

LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

// --- Durum kodları ---
enum State {
  ST_NONE, ST_DOOR, ST_SEATBELT, ST_FUEL_EMPTY,
  ST_CRIT_FUEL, ST_LOW_FUEL, ST_CLIMATE, ST_HEADLIGHT, ST_MOTOR, ST_NORMAL
};

// --- Değişkenler ---
bool lastMotorBtn = false;
bool lastSeatbeltBtn = false;
bool motorRunning = false;
bool seatbeltOn = false;
bool doorOpen = false;
bool headlightsOn = false;
bool yakitBittiMesajiGosterildi = false;

// --- Rotasyon için durum değişkenleri ---
State activeWarnings[5]; // Aktif uyarıları saklamak için dizi
int activeWarningCount = 0; // Aktif uyarı sayısı
int currentWarningIndex = 0; // Şu anki gösterilen uyarı indeksi
unsigned long lastWarningRotation = 0; // Son uyarı rotasyon zamanı
const unsigned long warningRotationInterval = 3000; // Uyarı rotasyon aralığı (ms)

// --- Debounce için değişkenler ---
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// --- Far mesajları için zamanlama ---
unsigned long farMessageTime = 0;
bool showingFarMessage = false;

// --- Son LCD güncelleme zamanı ---
unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 1000;  // LCD güncelleme sıklığı (ms)

void setup() {
  pinMode(PIN_BTN_MOTOR,    INPUT_PULLUP);
  pinMode(PIN_BTN_SEATBELT, INPUT_PULLUP);
  pinMode(PIN_SWITCH_DOOR,  INPUT_PULLUP);

  pinMode(LED_SEATBELT,   OUTPUT);
  pinMode(LED_HEADLIGHT,  OUTPUT);
  pinMode(LED_FUEL,       OUTPUT);
  pinMode(RGB_RED_PIN,    OUTPUT);
  pinMode(RGB_GREEN_PIN,  OUTPUT);
  pinMode(RGB_BLUE_PIN,   OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  pinMode(MOTOR_ENGINE,   OUTPUT);
  pinMode(MOTOR_FAN,      OUTPUT);

  // Başlangıçta tüm LED'leri kapat
  digitalWrite(LED_SEATBELT,  LOW);
  digitalWrite(LED_HEADLIGHT, LOW);
  digitalWrite(LED_FUEL,      LOW);
  setRGBColor(HIGH, HIGH, HIGH); // RGB LED kapalı
  
  // Motor kesinlikle kapalı başlasın
  digitalWrite(MOTOR_ENGINE, LOW);
  motorRunning = false;

  lcd.begin(16, 2);
  lcd.print("Arac Guvenlik");
  lcd.setCursor(0, 1);
  lcd.print("Sistemi Hazir");
  delay(2000);
  lcd.clear();
}

void loop() {
  // --- 1) Sensörleri Oku ---
  doorOpen = (digitalRead(PIN_SWITCH_DOOR) == LOW);
  bool motorBtn = (digitalRead(PIN_BTN_MOTOR) == LOW);
  bool seatbeltBtn = (digitalRead(PIN_BTN_SEATBELT) == LOW);
  
  int rawTemp = analogRead(PIN_LM35);
  float tempC = rawTemp * (5.0 / 1023.0) * 100.0;
  int ldrVal = analogRead(PIN_LDR);
  float fuelPct = analogRead(PIN_FUEL_POT) * 100.0 / 1023.0;

  // --- 2) ÖNCELİKLİ YAKIIT KONTROLÜ ---
  // Yakıt seviyesi %0 ise motor kesinlikle durmalı
  if (fuelPct <= 1) {
    // Yakıt bitti - herşeyi kapat
    motorRunning = false; // Yazılım durumu
    digitalWrite(MOTOR_ENGINE, LOW); // Donanım durumu - MOTOR KAPATILDI!
    digitalWrite(MOTOR_FAN, LOW); // Klima kapatıldı
    digitalWrite(LED_HEADLIGHT, LOW); // Farlar kapatıldı
    headlightsOn = false;
    
    // Yakıt bitti mesajını göster (sadece bir kez)
    if (!yakitBittiMesajiGosterildi) {
      lcd.clear();
      lcd.print("Yakit Bitti!");
      lcd.setCursor(0, 1);
      lcd.print("Motor Durdu!");
      delay(2000); // Mesajın görülmesini sağla
      yakitBittiMesajiGosterildi = true;
    }
  } else {
    // Yakıt var, mesaj gösterildi bayrağını sıfırla
    yakitBittiMesajiGosterildi = false;
  }

  // --- 3) Buton Durumlarını İşle (debounce ile) ---
  // Emniyet kemeri butonu kontrolü
  if (seatbeltBtn != lastSeatbeltBtn) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      seatbeltOn = seatbeltBtn; // Doğrudan buton durumunu oku
      lastDebounceTime = millis();
    }
  }
  lastSeatbeltBtn = seatbeltBtn;
    
  // Motor butonu kontrolü - YAKIIT KONTROLÜ İLE
  if (motorBtn != lastMotorBtn) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (motorBtn && !lastMotorBtn) { // Kenar algılama - Basılma anında
        // YAKIIT KONTROLÜ: Yakıt sıfırsa motor hiçbir koşulda çalışmamalı
        if (fuelPct <= 0) {
          showSpecialMessage("Yakit Yok!", "Motor calisamaz!");
          delay(1500);
          motorRunning = false; // Ekstra güvenlik
          digitalWrite(MOTOR_ENGINE, LOW); // Donanım seviyesinde
        }
        // Diğer koşullar - kemer takılı, kapı kapalı
        else if (seatbeltOn && !doorOpen) {
          if (!motorRunning) {
            // Motor kapalıysa aç
            motorRunning = true;
            digitalWrite(MOTOR_ENGINE, HIGH);
          } else {
            // Motor açıksa kapat
            motorRunning = false;
            digitalWrite(MOTOR_ENGINE, LOW);
            digitalWrite(MOTOR_FAN, LOW);
            digitalWrite(LED_HEADLIGHT, LOW);
            headlightsOn = false;
          }
        } else {
          // Motor çalıştırma koşulları sağlanmadı, uyarı göster
          if (!seatbeltOn) {
            showSpecialMessage("Kemer takmadan", "motor calismaz!");
            delay(1500);
          } else if (doorOpen) {
            showSpecialMessage("Kapi Acik!", "Motor calisamaz!");
            delay(1500);
          }
        }
      }
      lastDebounceTime = millis();
    }
  }
  lastMotorBtn = motorBtn;

  // --- 4) ÖNEMLİ GÜVENLİK KONTROLLERİ ---
  
  // TEKRAR YAKIIT KONTROLÜ: Hiçbir yerde gözden kaçırılmamalı
  if (fuelPct <= 0) {
    motorRunning = false;
    digitalWrite(MOTOR_ENGINE, LOW);
    digitalWrite(MOTOR_FAN, LOW);
    digitalWrite(LED_HEADLIGHT, LOW);
    headlightsOn = false;
  }
  
  // Kapı kontrolü - kapı açıksa motor çalışamaz
  if (doorOpen) {
    setRGBColor(LOW, HIGH, LOW);  // Pembe (kırmızı+mavi)
    if (motorRunning) {
      motorRunning = false;  // Motor çalışıyorsa durdur
      digitalWrite(MOTOR_ENGINE, LOW);
      digitalWrite(MOTOR_FAN, LOW);
      digitalWrite(LED_HEADLIGHT, LOW);
      headlightsOn = false;
    }
  } else {
    setRGBColor(HIGH, HIGH, HIGH);  // RGB LED kapalı
  }
  
  // Emniyet kemeri kontrolü - motor çalışırken kontrolü
  if (motorRunning && !seatbeltOn) {
    tone(PIN_BUZZER, 1000);  // Buzzer çalıştır
    digitalWrite(LED_SEATBELT, HIGH);  // Kırmızı LED yak
  } else {
    noTone(PIN_BUZZER);  // Buzzer durdur
    digitalWrite(LED_SEATBELT, LOW);  // Kırmızı LED söndür
  }

  // --- 5) MOTOR DURUMU İLE İLGİLİ KONTROLLER ---
  
  // Motor çalışmadığında, motor bağlantısı kesinlikle kapalı olmalı
  if (!motorRunning) {
    digitalWrite(MOTOR_ENGINE, LOW);
  }
  
  // Motor çalışıyorsa, motor bağlantısı açık olmalı
  if (motorRunning) {
    digitalWrite(MOTOR_ENGINE, HIGH);
  }
  
  // --- 6) FAR VE IŞIK KONTROLÜ ---
  
  // Far kontrolü - sadece motor çalışırken aktif olsun
  // Yakıt seviyesi sıfırsa farlar hiçbir şekilde açılmamalı
  if (fuelPct <= 0) {
    headlightsOn = false;
    digitalWrite(LED_HEADLIGHT, LOW);
  } else {
    // Normal far kontrolü
    bool shouldHeadlightsBeOn = (motorRunning && ldrVal <= 250);
    if (shouldHeadlightsBeOn != headlightsOn) {
      headlightsOn = shouldHeadlightsBeOn;
      // Far durumu değişti, mesaj göster
      showingFarMessage = true;
      farMessageTime = millis();
      displayFarMessage(headlightsOn);
    }
    digitalWrite(LED_HEADLIGHT, headlightsOn);
  }

  // Yakıt LED kontrolü
  if (fuelPct <= 0) {
    digitalWrite(LED_FUEL, LOW);  // Yakıt bittiğinde söndür
  } else if (fuelPct < 5.0) {
    // Yakıt kritik seviyede - yanıp sön
    digitalWrite(LED_FUEL, millis() % 500 < 250 ? HIGH : LOW);
  } else if (fuelPct < 10.0) {
    digitalWrite(LED_FUEL, HIGH);  // Yakıt düşük - sürekli yan
  } else {
    digitalWrite(LED_FUEL, LOW);  // Normal yakıt
  }

  // --- 7) KLIMA KONTROLÜ ---
  
  // Klima kontrolü - sadece motor çalışırken ve yakıt varken aktif olsun
  if (fuelPct <= 0) {
    digitalWrite(MOTOR_FAN, LOW); // Yakıt yoksa klima kapalı
  } else {
    bool klimaOn = (motorRunning && tempC > 25.0);
    digitalWrite(MOTOR_FAN, klimaOn);
  }

  // --- 8) LCD EKRANI GÜNCELLE ---
  
  // Yakıt bitti mesajı görüntüleniyorsa diğer mesajları gösterme
  if (fuelPct <= 0) {
    // Yakıt bitti mesajı gösterilmeye devam edecek
  }
  // Far mesajından sonra normal ekrana dön
  else if (showingFarMessage && (millis() - farMessageTime > 1500)) {
    showingFarMessage = false;
    lastDisplayUpdate = 0; // Hemen diğer mesajları göstermek için
  }
  
  if (!showingFarMessage && fuelPct > 0) {
    // Normal durum güncellemesi - 1 saniyede bir güncelle
    if (millis() - lastDisplayUpdate > displayUpdateInterval) {
      updateWarnings(doorOpen, seatbeltOn, motorRunning, tempC, ldrVal, fuelPct);
      lastDisplayUpdate = millis();
    }
    
    // Uyarı rotasyonu için kontrol
    if (activeWarningCount > 1) {
      if (millis() - lastWarningRotation > warningRotationInterval) {
        currentWarningIndex = (currentWarningIndex + 1) % activeWarningCount;
        lastWarningRotation = millis();
        updateDisplay(tempC, fuelPct);
      }
    }
  }
  
  delay(50);  // Stabil çalışma için kısa bir gecikme
}

// Aktif uyarıları toplama fonksiyonu
void updateWarnings(bool doorOpen, bool seatbeltOn, bool motorRunning, 
                    float tempC, int ldrVal, float fuelPct) {
  // Uyarıları sıfırla
  activeWarningCount = 0;
  
  // ÖNCELİKLİ YAKIIT KONTROLÜ: En önemli kontrol, diğerlerinden önce yapılmalı
  if (fuelPct <= 0) {
    activeWarnings[activeWarningCount++] = ST_FUEL_EMPTY;
    motorRunning = false;
    digitalWrite(MOTOR_ENGINE, LOW);
    return; // Yakıt bittiyse başka uyarıları eklemeye gerek yok
  }
  
  // Diğer tüm olası uyarıları kontrol et ve aktif olanları kaydet
  if (doorOpen) {
    activeWarnings[activeWarningCount++] = ST_DOOR;
  }
  
  if (motorRunning && !seatbeltOn) {
    activeWarnings[activeWarningCount++] = ST_SEATBELT;
  }
  
  if (motorRunning) {
    if (fuelPct < 5.0) {
      activeWarnings[activeWarningCount++] = ST_CRIT_FUEL;
    } else if (fuelPct < 10.0) {
      activeWarnings[activeWarningCount++] = ST_LOW_FUEL;
    }
    
    if (tempC > 25.0) {
      activeWarnings[activeWarningCount++] = ST_CLIMATE;
    }
    
    if (ldrVal <= 250) {
      activeWarnings[activeWarningCount++] = ST_HEADLIGHT;
    }
    
    // Normal motor çalışma durumu (sadece bir uyarı yoksa eklenir)
    if (activeWarningCount == 0) {
      activeWarnings[activeWarningCount++] = ST_MOTOR;
    }
  } else if (activeWarningCount == 0) {
    // Hiçbir uyarı yoksa ve motor çalışmıyorsa normal durum
    activeWarnings[activeWarningCount++] = ST_NORMAL;
  }
  
  // İlk görüntülemede, ilk uyarıyı göster
  if (activeWarningCount > 0) {
    currentWarningIndex = 0;
    updateDisplay(tempC, fuelPct);
  }
}

// LCD ekranı güncelleme fonksiyonu
void updateDisplay(float tempC, float fuelPct) {
  if (activeWarningCount == 0) return;
  
  State currentState = activeWarnings[currentWarningIndex];
  lcd.clear();
  
  switch (currentState) {
    case ST_DOOR:
      lcd.print("UYARI: KAPI ACIK");
      lcd.setCursor(0, 1);
      lcd.print("Motor Calismaz");
      break;
      
    case ST_SEATBELT:
      lcd.print("Emniyet Kemeri");
      lcd.setCursor(0, 1);
      lcd.print("Takili Degil!");
      break;
      
    case ST_FUEL_EMPTY:
      lcd.print("Yakit Bitti!");
      lcd.setCursor(0, 1);
      lcd.print("Motor Durdu!");
      break;
      
    case ST_MOTOR:
      lcd.print("Motor Calisiyor");
      break;
      
    case ST_CLIMATE:
      lcd.print("Sicaklik: ");
      lcd.print(int(tempC));
      lcd.print((char)223);
      lcd.print("C");
      lcd.setCursor(0, 1);
      lcd.print("Klima Acildi");
      break;
      
    case ST_HEADLIGHT:
      lcd.print("Farlar Acik");
      break;
      
    case ST_CRIT_FUEL:
      lcd.print("Kritik: Yakit");
      lcd.setCursor(0, 1);
      lcd.print("Cok Az - %");
      lcd.print(int(fuelPct));
      break;
      
    case ST_LOW_FUEL:
      lcd.print("Uyari: Yakit");
      lcd.setCursor(0, 1);
      lcd.print("Sev. Dusuk - %");
      lcd.print(int(fuelPct));
      break;
      
    case ST_NORMAL:
    default:
      lcd.print("Sistem Hazir");
      break;
  }
  
  // Birden fazla uyarı varsa, sayıyı ve şu anki uyarıyı göster
  if (activeWarningCount > 1) {
    lcd.setCursor(13, 0);
    lcd.print(currentWarningIndex + 1);
    lcd.print("/");
    lcd.print(activeWarningCount);
  }
}

// Far açıldı/kapandı mesajını göster
void displayFarMessage(bool isOn) {
  lcd.clear();
  if (isOn) {
    lcd.print("Farlar Acik");
  } else {
    lcd.print("Farlar Kapandi");
  }
}

// Özel mesaj gösterme fonksiyonu
void showSpecialMessage(const char* line1, const char* line2) {
  lcd.clear();
  lcd.print(line1);
  if (line2) {
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }
}

// RGB LED kontrolü için yardımcı fonksiyon
void setRGBColor(int red, int green, int blue) {
  digitalWrite(RGB_RED_PIN, red);
  digitalWrite(RGB_GREEN_PIN, green);
  digitalWrite(RGB_BLUE_PIN, blue);
}