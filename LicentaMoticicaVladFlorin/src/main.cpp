#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_TSL2561_U.h>
#include <QMC5883LCompass.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons./RTDBHelper.h"


#define DATABASE_URL "https://weatherstation-b6496-default-rtdb.europe-west1.firebasedatabase.app/"
#define API_KEY "AIzaSyCoL3bCdcyMUbX0f-fory5j742nMphOqgQ"
const char* ssid = "GalaxyA52s";
const char* password = "asdf1234";
unsigned long sendDataPrevMillis = 0;
bool signupOK = false;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;


const int PinTempHum = 25;
const int PinHall = 26;
const int PinRain = 32;
const int PinGas = 33;
const int PinDust = 34;
const int PinUV = 35;
#define DHTTYPE DHT11
DHT dht(PinTempHum, DHTTYPE);
Adafruit_BMP085 bmp;
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);
QMC5883LCompass compass;

float temp = 0.0;
int hum = 0; 
int gas = 0;
int rain = 0;
int uv = 0;
int pressureMMHG = 0;
int pressurePA = 0;
int light = 0;
float dust = 0.0;
float dustVoMeasured = 0.0;
float dustCalcVoltage = 0.0;
float speed = 0.0;
const unsigned long interval = 1000;
volatile unsigned int rotationCount = 0;
const float Vol = 3.3;
const int Res = 1024;


void CountRPM() 
{
  rotationCount++;
}

float CalculateWindSpeed(float rpm) 
{
  float circumference = 0.3;
  float rotationsPerSecond = rpm / 60.0;
  float windSpeed = 2 * rotationsPerSecond * circumference;
  return windSpeed;
}

int UVIndex(int sensorValue) 
{
  float voltage = sensorValue * (Vol * 1000) / Res;
  if (voltage < 50) return 0;
  if (voltage < 227) return 1;
  if (voltage < 318) return 2;
  if (voltage < 408) return 3;
  if (voltage < 503) return 4;
  if (voltage < 606) return 5;
  if (voltage < 696) return 6;
  if (voltage < 795) return 7;
  if (voltage < 881) return 8;
  if (voltage < 976) return 9;
  if (voltage < 1079) return 10;
  else return 11;
}


void setup() 
{
  Serial.begin(9600);
  Wire.begin();

  dht.begin();
  bmp.begin(0x77);

  tsl.begin();
  sensor_t sensorTSL;
  tsl.getSensor(&sensorTSL);
  tsl.enableAutoRange(true);
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);

  pinMode(PinHall, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PinHall), CountRPM, RISING);

  compass.init();
  compass.setCalibrationOffsets(-271.00, 24.00, -489.00);
  compass.setCalibrationScales(0.96, 10.89, 0.53);

  Serial.println();
  Serial.print("Conectare la ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(100);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi conectat");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  if(Firebase.signUp(&config, &auth, "", ""))
  {
    Serial.println("SignUp OK");
    signupOK = true;
  }
  else
  {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}


void loop() 
{
  if(Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 2500 || sendDataPrevMillis == 0))
  {
    sendDataPrevMillis = millis();

    temp = dht.readTemperature();
    hum = dht.readHumidity();

    gas = map(analogRead(PinGas), 0, 1023, 0, 255);

    int inverse = map(analogRead(PinRain), 0, 1023, 0, 255);
    rain = map(analogRead(PinRain), 0, 1023, 100, 0);
    if(rain <= 0) rain = 1;
    if(rain >= 100) rain = 100;

    uv = UVIndex(analogRead(PinUV));

    pressureMMHG = (bmp.readPressure())/133.3;
    pressurePA = bmp.readPressure();  

    sensors_event_t eventTSL;
    tsl.getEvent(&eventTSL);
    light = eventTSL.light;

    dustVoMeasured = analogRead(PinDust);
    dustCalcVoltage = dustVoMeasured * (3.3/1024);
    dust = 0.17 * dustCalcVoltage;
    if(dust < 0)
      dust = 0.00;

    detachInterrupt(digitalPinToInterrupt(PinHall));
    float rpm = (rotationCount / 3.0) * (60000.0 / interval);
    float speed = CalculateWindSpeed(rpm);
    rotationCount = 0;
    attachInterrupt(digitalPinToInterrupt(PinHall), CountRPM, RISING);

    compass.read();
    int az = compass.getAzimuth();
    char points[3];
    compass.getDirection(points, az);
    String comp = points;
    comp.remove(3);


    Serial.println();
    if (Firebase.RTDB.setFloat(&fbdo, "Sensor/Temperature", temp)) 
    { 
      Serial.println();
      Serial.print("Temperature: "); 
      Serial.print(temp);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());

    }

    if(hum > 100)
    {
      Firebase.RTDB.setInt(&fbdo, "Sensor/Humidity", 50);
    }
    else
    {
      if (Firebase.RTDB.setInt(&fbdo, "Sensor/Humidity", hum)) 
      { 
        Serial.println();
        Serial.print("Humidity: "); 
        Serial.print(hum);
      }
      else
      {
        Serial.println("FAILED: ");
        Serial.print(fbdo.errorReason());
      }
    }

    if (Firebase.RTDB.setInt(&fbdo, "Sensor/Gas", gas)) 
    { 
      Serial.println();
      Serial.print("Gas: "); 
      Serial.print(gas);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }

    if (Firebase.RTDB.setInt(&fbdo, "Sensor/Rain", rain)) 
    { 
      Serial.println();
      Serial.print("Rain: "); 
      Serial.print(rain);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }

    if (Firebase.RTDB.setInt(&fbdo, "Sensor/UV", uv)) 
    { 
      Serial.println();
      Serial.print("UV: "); 
      Serial.print(uv);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }
    
    if (Firebase.RTDB.setInt(&fbdo, "Sensor/Pressure MMHG", pressureMMHG))
    { 
      Serial.println();
      Serial.print("MMHG: "); 
      Serial.print(pressureMMHG);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }

    if (Firebase.RTDB.setInt(&fbdo, "Sensor/Pressure PA", pressurePA)) 
    { 
      Serial.println();
      Serial.print("PA: "); 
      Serial.print(pressurePA);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }

    if (Firebase.RTDB.setInt(&fbdo, "Sensor/Light", light)) 
    { 
      Serial.println();
      Serial.print("Light: "); 
      Serial.print(light);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "Sensor/Dust", dust)) 
    { 
      Serial.println();
      Serial.print("Dust: "); 
      Serial.print(dust);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());

    }

    if (Firebase.RTDB.setFloat(&fbdo, "Sensor/Speed", speed)) 
    { 
      Serial.println();
      Serial.print("Speed: "); 
      Serial.print(speed);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }

    if (Firebase.RTDB.setString(&fbdo, "Sensor/Compass", comp)) 
    { 
      Serial.println();
      Serial.print("Compass: "); 
      Serial.print(comp);
    }
    else
    {
      Serial.println("FAILED: ");
      Serial.print(fbdo.errorReason());
    }
  }
}