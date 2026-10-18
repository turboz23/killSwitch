#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================ Network & API Credentials ================
// PERSONAL DATA HIDDEN FOR PRIVACY REASONS
const char* ssid = "nopeUdidn'tSEEIT";
const char* password = "byebye";
const char* apiKey = "key";
String targetUrl = "http://45.198.224.164/arm4";
// Bad sites (DO NOT VISIT): https://urlhaus.abuse.ch/browse/

// ================ Hardware Pins ================
const int GREENLED = 48;
const int YELLOWLED = 47;
const int REDLED = 21;

#define BUZZER_PIN 0
#define BUTTON_1_PIN 6 // Screenshot
#define BUTTON_2_PIN 14 // Notify and Silence
#define LED_PIN 5
#define SDA 35
#define SCL 36

// ================ State Definitions ================
#define ALLGOOD 100
#define UHOH 101
#define BAD 102

// Global volatile variable for cross-task communication
volatile int severity = ALLGOOD; 

LiquidCrystal_I2C lcd(0x27, 16, 2);
byte lcdAddress = 0x27;

// --- HARDWARE PULSE FUNCTION ---
void sendNibbleWithPulse(byte dataHalf) {
  Wire.beginTransmission(lcdAddress);
  Wire.write(dataHalf | 0x04); 
  Wire.endTransmission();
  
  delayMicroseconds(1); 
  
  Wire.beginTransmission(lcdAddress);
  Wire.write(dataHalf & ~0x04); 
  Wire.endTransmission();
  
  delayMicroseconds(50); 
}

// --- COMMAND FUNCTION ---
void sendCommand(byte commandByte) {
  byte highNibble = commandByte & 0xF0;            
  byte lowNibble  = (commandByte << 4) & 0xF0;     
  byte controlBits = 0x08; 
  
  sendNibbleWithPulse(highNibble | controlBits);
  sendNibbleWithPulse(lowNibble | controlBits);
  
  if (commandByte == 0x01) {
    delay(2); 
  }
}

// --- CHARACTER FUNCTION ---
void sendCharacter(byte charByte) {
  byte highNibble = charByte & 0xF0;            
  byte lowNibble  = (charByte << 4) & 0xF0;     
  byte controlBits = 0x08 | 0x01; 
  
  sendNibbleWithPulse(highNibble | controlBits);
  sendNibbleWithPulse(lowNibble | controlBits);
}

// --- PHRASE FUNCTION (Fixed) ---
void sendPhrase(const char* message) {
  for (int k = 0; k < 12; k++) {
    if (message[k] != '\0') sendCharacter(message[k]);
    else sendCharacter(' '); 
  }
}

// ================ FreeRTOS Tasks ================

void vApiTask(void *pvParameters) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      
      // 1. Create a secure client and bypass strict SSL checks
      WiFiClientSecure client;
      client.setInsecure(); 
      
      HTTPClient http;
      
      // 2. Pass the secure client into http.begin
      // PERSONAL DATA HIDDEN FOR PRIVACY REASONS
      http.begin(client, "");
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-API-Key", apiKey);

      JsonDocument jsonDoc;
      jsonDoc["url"] = targetUrl;
      String requestBody;
      serializeJson(jsonDoc, requestBody);

      int httpResponseCode = http.POST(requestBody);

      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("HTTP Response code: " + String(httpResponseCode));

        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);

        if (!error) {
          int riskScore = responseDoc["risk_score"]["result"];
          Serial.print("URL Risk Score: ");
          Serial.println(riskScore);
          
          if (riskScore < 20) {
            severity = ALLGOOD;
          } else if (riskScore < 80) {
            severity = UHOH;
          } else {
            severity = BAD;
          }
        } else {
          Serial.println("JSON Parsing failed!");
        }
      } else {
        // 3. Catch the silent failures!
        Serial.print("HTTP Request failed, error: ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
      }
      http.end();
    }
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void vLedTask(void *pvParameters) {
  for (;;) { 
    if (severity == ALLGOOD) {
      digitalWrite(GREENLED, HIGH);
      digitalWrite(YELLOWLED, LOW);
      digitalWrite(REDLED, LOW);
      vTaskDelay(pdMS_TO_TICKS(100));
    } 
    else if (severity == UHOH) {
      digitalWrite(GREENLED, LOW);
      digitalWrite(YELLOWLED, HIGH);
      digitalWrite(REDLED, LOW);
      vTaskDelay(pdMS_TO_TICKS(500));
      digitalWrite(YELLOWLED, LOW);
      vTaskDelay(pdMS_TO_TICKS(500));
    } 
    else if (severity == BAD) {
      digitalWrite(GREENLED, LOW);
      digitalWrite(YELLOWLED, LOW);
      digitalWrite(REDLED, HIGH);
      vTaskDelay(pdMS_TO_TICKS(100));
      digitalWrite(REDLED, LOW);
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

void vBeeperTask(void *pvParameters) {
  for (;;) {
    if (severity == BAD) {
      ledcWrite(BUZZER_PIN, 128); 
      ledcChangeFrequency(BUZZER_PIN, 500, 8); // 500 Hz
      vTaskDelay(pdMS_TO_TICKS(50));
      ledcChangeFrequency(BUZZER_PIN, 700, 8); // 700 Hz
      vTaskDelay(pdMS_TO_TICKS(50));
      ledcChangeFrequency(BUZZER_PIN, 900, 8); // 900 Hz
      vTaskDelay(pdMS_TO_TICKS(50));
      ledcChangeFrequency(BUZZER_PIN, 1100, 8); // 1100 Hz
      vTaskDelay(pdMS_TO_TICKS(50)); 
      ledcWrite(BUZZER_PIN, 0); 
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (severity == UHOH) {
      ledcWrite(BUZZER_PIN, 128);
      ledcChangeFrequency(BUZZER_PIN, 500, 8); // 500 Hz 
      vTaskDelay(pdMS_TO_TICKS(300)); 
      ledcWrite(BUZZER_PIN, 0); 
      vTaskDelay(pdMS_TO_TICKS(300));
    } else {
      ledcWrite(BUZZER_PIN, 0);
      vTaskDelay(pdMS_TO_TICKS(200)); 
    }
  }
}

void vLcdTask(void *pvParameters) {
  int lastSeverity = -1; 
  
  for (;;) {
    if (severity != lastSeverity) {
      sendCommand(0x01); // Clear screen
      
      if (severity == ALLGOOD) {
        sendCommand(0x80);   
        sendPhrase("This is Safe");
      }
      else if (severity == UHOH) {
        sendCommand(0x80);        
        sendPhrase("Proceed with");
        sendCommand(0x80 | 0x40); 
        sendPhrase("Caution     ");
      }
      else if (severity == BAD) {  // Fixed logic bug here
        sendCommand(0x80);        
        sendPhrase("STOP! STOP!");
        sendCommand(0x80 | 0x40); 
        sendPhrase("STOP! UNSAFE.");
      } else {
        sendCommand(0x80);        
        sendPhrase("Error.");
      }
      lastSeverity = severity;
    }
    vTaskDelay(pdMS_TO_TICKS(200)); 
  }
}

void vButtonTask(void *pvParameters) {
  for (;;) { 
    // Button 1: Trigger the Python screenshot
    if(!digitalRead(BUTTON_1_PIN)) {
      Serial.println("SNAP"); 
      vTaskDelay(pdMS_TO_TICKS(1000)); // Debounce
    } 
    
    // Button 2: Notify family and silence alarms
    if(!digitalRead(BUTTON_2_PIN)) {
      // 1. Send the alert to the Serial monitor (or companion app)
      Serial.println("NOTIFY: Threat detected. Alerting family!"); 
      
      // 2. Immediately kill the alarms by forcing the state to safe
      severity = ALLGOOD; 
      
      vTaskDelay(pdMS_TO_TICKS(1000)); // Debounce
    }
    
    // Listen for incoming severity scores from the Python script
    if (Serial.available() > 0) {
      String incoming = Serial.readStringUntil('\n');
      int newScore = incoming.toInt();
      Serial.println(newScore);
      
      if (newScore == 100 || newScore == 101 || newScore == 102) {
        severity = newScore; 
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(50)); // Yield to other tasks
  }
}

void setup() {
  Serial.begin(9600);
  
  pinMode(GREENLED, OUTPUT);
  pinMode(YELLOWLED, OUTPUT);
  pinMode(REDLED, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_1_PIN, INPUT_PULLUP);
  pinMode(BUTTON_2_PIN, INPUT_PULLUP);

  ledcAttach(BUZZER_PIN, 2000, 8); 
  ledcWrite(BUZZER_PIN, 0); 

  Wire.begin(SDA, SCL);
  lcd.init();
  lcd.backlight(); 
  delay(2);

  xTaskCreate(vLcdTask, "LCD Task", 2048, NULL, 1, NULL);

  // Connect to WiFi before launching API Task
  WiFi.mode(WIFI_STA);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    Serial.print("Wi-Fi Event: ");
    Serial.println(event);
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.print("Disconnect Reason Code: ");
      Serial.println(info.wifi_sta_disconnected.reason);
    }
  });
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");

  // Create Tasks (API Task gets larger stack for JSON/HTTPS)
  xTaskCreate(vApiTask, "API Task", 8192, NULL, 1, NULL);
  xTaskCreate(vLedTask, "LED Task", 2048, NULL, 1, NULL);
  xTaskCreate(vBeeperTask, "Beeper Task", 2048, NULL, 1, NULL);
  xTaskCreate(vButtonTask, "Button Task", 2048, NULL, 1, NULL);
}

void loop() {
  // FreeRTOS handles execution now.
  vTaskDelay(pdMS_TO_TICKS(1000));
}