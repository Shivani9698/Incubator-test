#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <esp_sleep.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi credentials
const char* ssid = "LIFETRUSTOFFICE";
const char* password = "Lifetrust@123";

// Server URL
const char* serverName = "http://app.antzsystems.com/api/v1/iot/enclosure/metric/update";

// DHT sensor setup
#define DHTPIN 27          // Pin where the data wire is connected
#define DHTTYPE DHT22      // DHT 22 (AM2302)
DHT dht(DHTPIN, DHTTYPE);

// DS18B20 sensor setup
#define ONE_WIRE_BUS 5     // Pin where the data wire is connected
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// OLED display setup
#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 32   // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// NTP client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Deep sleep setup
#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  900         // Time ESP32 will go to sleep (in seconds, 900 seconds = 15 minutes)

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("Connecting");

  // Initialize the OLED display with the correct I2C address
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Timer set to 15 minutes (deep sleep), it will take 15 minutes before publishing the next reading.");

  dht.begin(); // Initialize the DHT sensor
  sensors.begin(); // Initialize the DS18B20 sensor
  timeClient.begin(); // Initialize the NTP client
  timeClient.setTimeOffset(19800); // Set offset time in seconds for IST (5 hours 30 minutes)
}

String getFormattedTime() {
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);

  char formattedTime[25];
  sprintf(formattedTime, "%04d-%02d-%02dT%02d:%02d:%02d",
          ptm->tm_year + 1900,
          ptm->tm_mon + 1,
          ptm->tm_mday,
          ptm->tm_hour,
          ptm->tm_min,
          ptm->tm_sec);
  return String(formattedTime);
}

void displayData(float dhtTemperature, float dhthumidity, float dsTemperature) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("DHT22 Temp: ");
  display.print(dhtTemperature);
  display.println(" C");
  display.setCursor(0, 8);
  display.print("DHT22 Hum:  ");
  display.print(dhthumidity);
  display.println(" %");
  display.setCursor(0, 24);
  display.print("Analog Temp: ");
  display.print(dsTemperature);
  display.println(" C");
  display.display();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // Reading temperature and humidity from DHT22
    float dhtTemperature = dht.readTemperature();
    float humidity = dht.readHumidity(); // Corrected variable name

    // Reading temperature from DS18B20
    sensors.requestTemperatures();
    float dsTemperature = sensors.getTempCByIndex(0);

    // Check if any reads failed and exit early (to try again).
    if (isnan(dhtTemperature) || isnan(humidity) || dsTemperature == DEVICE_DISCONNECTED_C) {
      Serial.println("Failed to read from sensors!");
    } else {
      Serial.println("Sending HTTP request...");

      // Display data on OLED
      displayData(dhtTemperature, humidity, dsTemperature);

      // Specify content-type header as application/json
      HTTPClient http;

      // Use WiFiClient for HTTP (insecure)
      WiFiClient client;

      http.begin(client, serverName);

      // Get current time in IST
      String eventDate = getFormattedTime();

      // Create JSON data
      DynamicJsonDocument jsonDoc(512); // Adjust the buffer size as needed
      JsonObject root = jsonDoc.to<JsonObject>();
      root["enclosure_id"] = 111;

      JsonArray values = root.createNestedArray("values");

      JsonObject dhtTemperatureObj = values.createNestedObject();
      dhtTemperatureObj["key"] = "DHT22 Temperature";
      dhtTemperatureObj["value"] = dhtTemperature;
      dhtTemperatureObj["uom"] = "celsius";
      dhtTemperatureObj["event_date"] = eventDate;

      JsonObject humidityObj = values.createNestedObject();
      humidityObj["key"] = "DHT22 Humidity";
      humidityObj["value"] = humidity;
      humidityObj["uom"] = "%";
      humidityObj["event_date"] = eventDate;

      JsonObject dsTemperatureObj = values.createNestedObject();
      dsTemperatureObj["key"] = "Analog Temperature";
      dsTemperatureObj["value"] = dsTemperature;
      dsTemperatureObj["uom"] = "celsius";
      dsTemperatureObj["event_date"] = eventDate;

      // Serialize JSON to a string
      String jsonString;
      serializeJson(root, jsonString);
      Serial.println("JSON Data: " + jsonString);

      // Send HTTP POST request with JSON data
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode = http.POST(jsonString);

      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);

      // Check for a successful HTTP response (200 OK)
      if (httpResponseCode == 200) {
        // Parse and print the response
        String response = http.getString();
        Serial.println("Server response: " + response);
      } else {
        Serial.println("HTTP request failed");
      }

      // Free resources
      http.end();
    }
  } else {
    Serial.println("WiFi Disconnected");
  }

  // Enter deep sleep
  Serial.println("Entering deep sleep for 15 minutes...");
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}
