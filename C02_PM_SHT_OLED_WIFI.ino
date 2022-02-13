/*
This is the code for the AirGradient DIY Air Quality Sensor with an ESP8266 Microcontroller.

It is a high quality sensor showing PM2.5, CO2, Temperature and Humidity on a small display and can send data over Wifi.

For build instructions please visit https://www.airgradient.com/diy/

Compatible with the following sensors:
Plantower PMS5003 (Fine Particle Sensor)
SenseAir S8 (CO2 Sensor)
SHT30/31 (Temperature/Humidity Sensor)

Please install ESP8266 board manager (tested with version 3.0.0)

The codes needs the following libraries installed:
"WifiManager by tzapu, tablatronix" tested with Version 2.0.3-alpha
"ESP8266 and ESP32 OLED driver for SSD1306 displays by ThingPulse, Fabrice Weinberg" tested with Version 4.1.0

If you have any questions please visit our forum at https://forum.airgradient.com/

Configuration:
Please set in the code below which sensor you are using and if you want to connect it to WiFi.
You can also switch PM2.5 from ug/m3 to US AQI and Celcius to Fahrenheit

If you are a school or university contact us for a free trial on the AirGradient platform.
https://www.airgradient.com/schools/

Kits with all required components are available at https://www.airgradient.com/diyshop/

MIT License
*/

#include <AirGradient.h>

#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include "SSD1306Wire.h"

#define DEVICE "ESP8266"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

WiFiManager wifiManager;

AirGradient ag = AirGradient();

SSD1306Wire display(0x3c, SDA, SCL);

// set sensors that you do not use to false
boolean hasPM = true;
boolean hasCO2 = true;
boolean hasSHT = true;

// set to true to switch PM2.5 from ug/m3 to US AQI
boolean inUSaqi = false;

// set to true to switch from Celcius to Fahrenheit
boolean inF = false;

// set to true if you want to connect to wifi. The display will show values only when the sensor has wifi connection
boolean connectWIFI = true;

// InfluxDB v2 server url, e.g. https://eu-central-1-1.aws.cloud2.influxdata.com (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "<-- InfluxDB URL -->"
// InfluxDB v2 server or cloud API token (Use: InfluxDB UI -> Data -> API Tokens -> Generate API Token)
#define INFLUXDB_TOKEN "<-- InfluxDB Token -->"
// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG ""<-- InfluxDB Org -->""
// InfluxDB v2 bucket name (Use: InfluxDB UI ->  Data -> Buckets)
#define INFLUXDB_BUCKET ""<-- InfluxDB Bucket -->""

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "AST4ADT"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Data points
Point sensor("air_gradient");

void setup() {
  Serial.begin(57600);

  display.init();
  display.flipScreenVertically();
  showTextRectangle("Init", String(ESP.getChipId(), HEX), true);

  if (hasPM)
    ag.PMS_Init();
    
  if (hasCO2)
    ag.CO2_Init();
    
  if (hasSHT)
    ag.TMP_RH_Init(0x44);

  if (connectWIFI)
    connectToWifi();

  // Add tags
  sensor.addTag("device", String(ESP.getChipId(), HEX));
  sensor.addTag("SSID", WiFi.SSID());

  // Accurate time is necessary for certificate validation and writing in batches
  // For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial.
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Check server connection
  if (client.validateConnection())
  {
    showTextRectangle("Conn:", client.getServerUrl(), true);
  }
  else
  {
    showTextRectangle("Err:", client.getLastErrorMessage(), true);
    delay(10000);
    exit(1);
  }
  
  delay(2000);
}

void loop() {

  // Clear fields for reusing the point. Tags will remain untouched
  sensor.clearFields();

  if (hasPM)
  {
    int PM2 = ag.getPM2_Raw();
    
    // Report PM2.5
    sensor.addField("pm02", PM2);

    if (inUSaqi)
    {
      showTextRectangle("AQI", String(PM_TO_AQI_US(PM2)), false);
    }
    else
    {
      showTextRectangle("PM2", String(PM2), false);
    }

    delay(3000);
  }

  if (hasCO2)
  {
    int CO2 = ag.getCO2_Raw();

    // Report CO2
    sensor.addField("co2", CO2);
    
    showTextRectangle("CO2", String(CO2), false);
    delay(3000);
  }

  if (hasSHT)
  {
    TMP_RH result = ag.periodicFetchData();

    // Report temperature and humidity
    sensor.addField("atmp", result.t);
    sensor.addField("rhum", result.rh);

    if (inF)
    {
      showTextRectangle(String((result.t * 9 / 5) + 32), String(result.rh) + "%", false);
    }
    else
    {
      showTextRectangle(String(result.t), String(result.rh) + "%", false);
    }

    delay(3000);
  }

  // send payload
  if (connectWIFI)
  {
    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(sensor.toLineProtocol());

    // If no Wifi signal, try to reconnect it
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Wifi connection lost");
      WiFi.reconnect();
    }

    // Write point
    if (!client.writePoint(sensor))
    {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
      WiFi.reconnect();
    }
    
    delay(1000);
  }
}

// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  if (small) {
    display.setFont(ArialMT_Plain_16);
  } else {
    display.setFont(ArialMT_Plain_24);
  }
  display.drawString(32, 16, ln1);
  display.drawString(32, 36, ln2);
  display.display();
}

// Wifi Manager
void connectToWifi()
{
  //WiFi.disconnect(); //to delete previous saved hotspot
  String HOTSPOT = "AIRGRADIENT-" + String(ESP.getChipId(), HEX);
  wifiManager.setTimeout(120);
  
  if (!wifiManager.autoConnect((const char * ) HOTSPOT.c_str()))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
}

// Calculate PM2.5 US AQI
int PM_TO_AQI_US(int pm02)
{
  if (pm02 <= 12.0) return ((50 - 0) / (12.0 - .0) * (pm02 - .0) + 0);
  else if (pm02 <= 35.4) return ((100 - 50) / (35.4 - 12.0) * (pm02 - 12.0) + 50);
  else if (pm02 <= 55.4) return ((150 - 100) / (55.4 - 35.4) * (pm02 - 35.4) + 100);
  else if (pm02 <= 150.4) return ((200 - 150) / (150.4 - 55.4) * (pm02 - 55.4) + 150);
  else if (pm02 <= 250.4) return ((300 - 200) / (250.4 - 150.4) * (pm02 - 150.4) + 200);
  else if (pm02 <= 350.4) return ((400 - 300) / (350.4 - 250.4) * (pm02 - 250.4) + 300);
  else if (pm02 <= 500.4) return ((500 - 400) / (500.4 - 350.4) * (pm02 - 350.4) + 400);
  else return 500;
};
