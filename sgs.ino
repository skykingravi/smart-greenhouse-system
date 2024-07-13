// libraries
#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <Servo.h>

// helper functions
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// global variables
#define WIFI_SSID "skyking"
#define WIFI_PASSWORD "ravi2003"
#define API_KEY "AIzaSyBZ5l_weKXIcULtQgwmFtlzVl9kd7sswvU"
#define DATABASE_URL "https://smart-greenhouse-system-f5908-default-rtdb.asia-southeast1.firebasedatabase.app"

// pins
#define SOIL_MOISTURE_PIN A0
#define LDR_PIN D0
#define DHT_PIN D1
#define LIGHT_PIN D2
#define WINDOW_PIN D3
#define BULB_PIN D5
#define PUMP_PIN D6
#define FAN_PIN D7

#define DHTTYPE DHT11

DHT dht(DHT_PIN, DHTTYPE);

Servo servo;

// timer variables
unsigned long sensorPrevMillis = 0, saveDataPrevMillis = 0, actuatorPrevMillis = 0, light_hours = 0;

// helper variables
float temperature, humidity, moisture;
bool ldr, signupOK = false;
int MUTEX = -1, saveDataNum = 0;

// data values
float RANGE[4][2] = {{21, 27}, {60, 70}, {8, 10}, {60, 70}}, DATA[24][4];

// firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// generate firebase json array
String genArray()
{
    String jsonData = "[";
    for (int i = 0; i < 24; i++)
    {
        jsonData += "[";
        for (int j = 0; j < 4; j++)
        {
            jsonData += String(DATA[i][j], 4); // convert float to string with 4 decimal places
            if (j < 3)
            {
                jsonData += ",";
            }
        }
        jsonData += "]";
        if (i < 23)
        {
            jsonData += ",";
        }
    }
    jsonData += "]";
    return jsonData;
}

void setup()
{
    Serial.begin(115200);
    dht.begin();

    // pins mode
    pinMode(SOIL_MOISTURE_PIN, INPUT);
    pinMode(LDR_PIN, INPUT);
    pinMode(DHT_PIN, INPUT);
    pinMode(LIGHT_PIN, OUTPUT);
    pinMode(FAN_PIN, OUTPUT);
    pinMode(BULB_PIN, OUTPUT);
    pinMode(PUMP_PIN, OUTPUT);

    // initial pin values
    digitalWrite(LIGHT_PIN, LOW);
    digitalWrite(FAN_PIN, LOW);
    digitalWrite(BULB_PIN, LOW);
    digitalWrite(PUMP_PIN, LOW);

    // servo setup
    servo.attach(WINDOW_PIN);

    // connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());

    // signup to firebase
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    if (Firebase.signUp(&config, &auth, "", ""))
    {
        signupOK = true;
    }

    // callback function for token generation
    config.token_status_callback = tokenStatusCallback;

    // start firebase
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    if (Firebase.ready() && signupOK)
    {
        Firebase.RTDB.getBool(&fbdo, "actuators/window/closed");
        if (fbdo.boolData())
        {
            servo.write(0);
        }
        else
        {
            servo.write(180);
        }
    }
}

void loop()
{
    // further execution only if firebase is ready
    if (Firebase.ready() && signupOK)
    {

        // fetch & save sensor data every second
        if (millis() - sensorPrevMillis > 1000 || sensorPrevMillis == 0)
        {
            sensorPrevMillis = millis();

            // fetch sensor data
            temperature = dht.readTemperature();
            humidity = dht.readHumidity();
            ldr = (digitalRead(LDR_PIN) == 1) ? false : true;
            moisture = 100.0 - ((analogRead(SOIL_MOISTURE_PIN) / 1024.0) * 100.0);

            Serial.print("Temperature: ");
            Serial.println(temperature);
            Serial.print("Humidity: ");
            Serial.println(humidity);
            Serial.print("Moisture: ");
            Serial.println(moisture);
            Serial.print("LDR: ");
            Serial.println(ldr);

            if (ldr)
                light_hours++;

            // save sensor data
            Firebase.RTDB.setFloat(&fbdo, "sensors/temperature", temperature);
            Firebase.RTDB.setFloat(&fbdo, "sensors/humidity", humidity);
            Firebase.RTDB.setFloat(&fbdo, "sensors/moisture", moisture);
            Firebase.RTDB.setBool(&fbdo, "sensors/light/status", ldr);
            Firebase.RTDB.setFloat(&fbdo, "sensors/light/hours", light_hours / 3600);
        }

        // store sensor data sample every hour for prediction
        if (millis() - saveDataPrevMillis > 3600000 || saveDataPrevMillis == 0)
        {
            saveDataPrevMillis = millis();
            DATA[saveDataNum][0] = temperature;
            DATA[saveDataNum][1] = humidity;
            DATA[saveDataNum][2] = light_hours / 3600;
            DATA[saveDataNum][3] = moisture;

            saveDataNum++;

            // save all 24 sensor data samples at the 24th hr
            if (saveDataNum == 24)
            {
                Firebase.RTDB.getInt(&fbdo, "data/days");
                int day = fbdo.intData();
                Firebase.RTDB.setString(&fbdo, "data/day" + String(day), genArray());
                Firebase.RTDB.setInt(&fbdo, "data/days", day + 1);
                light_hours = 0;
                saveDataNum = 0;
            }
        }

        // handle actuation every second
        if (millis() - actuatorPrevMillis > 1000 || actuatorPrevMillis == 0)
        {
            actuatorPrevMillis = millis();

            // handle window
            if (MUTEX == -1 || MUTEX == 0)
            {
                if (Firebase.RTDB.getBool(&fbdo, "actuators/window/moving"))
                {
                    Serial.println(fbdo.boolData());
                    if (fbdo.boolData())
                    {
                        if (Firebase.RTDB.getBool(&fbdo, "actuators/window/closed"))
                        {
                            if (fbdo.boolData())
                            {
                                MUTEX = 0;
                                servo.write(180);
                                delay(500);
                                Firebase.RTDB.setBool(&fbdo, "actuators/window/moving", false);
                                Firebase.RTDB.setBool(&fbdo, "actuators/window/closed", false);
                                MUTEX = -1;
                            }
                            else
                            {
                                MUTEX = 0;
                                servo.write(0);
                                delay(500);
                                Firebase.RTDB.setBool(&fbdo, "actuators/window/moving", false);
                                Firebase.RTDB.setBool(&fbdo, "actuators/window/closed", true);
                                MUTEX = -1;
                            }
                        }
                    }
                }
            }

            // handle light
            if (MUTEX == -1 || MUTEX == 1)
            {
                if (Firebase.RTDB.getBool(&fbdo, "actuators/light"))
                {
                    Serial.println(fbdo.boolData());
                    if (fbdo.boolData())
                    {
                        MUTEX = 1;
                        light_hours++;
                        digitalWrite(LIGHT_PIN, HIGH);
                    }
                    else
                    {
                        digitalWrite(LIGHT_PIN, LOW);
                        MUTEX = -1;
                    }
                }
            }

            // handle fan
            if (MUTEX == -1 || MUTEX == 2)
            {
                if (Firebase.RTDB.getBool(&fbdo, "actuators/fan"))
                {
                    Serial.println(fbdo.boolData());
                    if (fbdo.boolData())
                    {
                        MUTEX = 2;
                        digitalWrite(FAN_PIN, HIGH);
                    }
                    else
                    {
                        digitalWrite(FAN_PIN, LOW);
                        MUTEX = -1;
                    }
                }
            }

            // handle bulb
            if (MUTEX == -1 || MUTEX == 3)
            {
                if (Firebase.RTDB.getBool(&fbdo, "actuators/bulb"))
                {
                    Serial.println(fbdo.boolData());
                    if (fbdo.boolData())
                    {
                        MUTEX = 3;
                        digitalWrite(BULB_PIN, HIGH);
                    }
                    else
                    {
                        digitalWrite(BULB_PIN, LOW);
                        MUTEX = -1;
                    }
                }
            }

            // handle pump
            if (MUTEX == -1 || MUTEX == 4)
            {
                if (Firebase.RTDB.getBool(&fbdo, "actuators/pump"))
                {
                    Serial.println(fbdo.boolData());
                    if (fbdo.boolData())
                    {
                        MUTEX = 4;
                        digitalWrite(PUMP_PIN, HIGH);
                        while (moisture < RANGE[3][0])
                        {
                            delay(200);
                            moisture = 100.0 - ((analogRead(SOIL_MOISTURE_PIN) / 1024.0) * 100.0);
                            Firebase.RTDB.getBool(&fbdo, "actuators/pump");
                            if (fbdo.boolData() == false)
                                break;
                        }
                        digitalWrite(PUMP_PIN, LOW);
                        Firebase.RTDB.setBool(&fbdo, "actuators/pump", false);
                        MUTEX = -1;
                    }
                }
            }
        }
    }
}
