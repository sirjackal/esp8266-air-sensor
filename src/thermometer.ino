// Teploměr a vlhkoměr DHT11/22

// připojení knihovny DHT
#include "DHT.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <pubsubClient.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <MHZ19.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson

//SoftwareSerial ss(D6, D7);
SoftwareSerial mzh_ss(D7, D8);
MHZ19 mhz(&mzh_ss);
//#include <Fonts/FreeMono9pt7b.h>
// nastavení čísla pinu s připojeným DHT senzorem
#define pinDHT D5 // data pin pichnout na D6 (GPIO12)

// odkomentování správného typu čidla
//#define typDHT11 DHT11     // DHT 11
#define typDHT22 DHT22 // DHT 22 (AM2302)

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)

const char *hostname = "ESP8266 Air Sensor";
const char *ap_ssid = "AirSensor";

unsigned long measurementIntervalMs;

String tempStr;
String humidStr;
String co2Str;
char tempBuffer[8];
char humidBuffer[8];
char co2Buffer[16];

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// inicializace DHT senzoru s nastaveným pinem a typem senzoru
DHT dhtSensor(pinDHT, DHT22);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

IPAddress ip;
long lastTime;

// define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[64] = "192.168.1.104";
char mqtt_port[6] = "1883";
char mqtt_username[32] = "openhabian";
char mqtt_password[32] = "********";
char mqtt_channel_temperature[128] = "home/garage/temperature";
char mqtt_channel_humidity[128] = "home/garage/humidity";
char mqtt_channel_co2[128] = "home/garage/co2";
char measurement_interval_second[5] = "60";

// flag for saving data
bool shouldSaveConfig = false;

// callback notifying us of the need to save config
void saveConfigCallback()
{
	Serial.println("Should save config");
	shouldSaveConfig = true;
}

bool mqttConnect()
{
	if (mqttClient.connected())
	{
		return true;
	}


	Serial.println("Connecting to MQTT...");

	if (mqttClient.connect("ESP8266Client", mqtt_username, mqtt_password))
	{
		Serial.println("connected");
		return true;
	}
	else
	{
		Serial.print("failed with state ");
		Serial.print(mqttClient.state());
		return false;
	}
}

void setup()
{
	Serial.begin(115200);
	mzh_ss.begin(9600);
	mhz.setAutoCalibration(false);

	// SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
	{
		// Address 0x3D for 128x64
		Serial.println(F("SSD1306 allocation failed"));
		while (true)
		{
		}
	}

	// Show initial display buffer contents on the screen --
	// the library initializes this with an Adafruit splash screen.
	display.display();
	display.clearDisplay();
	display.setTextSize(1); // Normal 1:1 pixel scale
	//  display.setFont(&FreeMono9pt7b);
	display.setTextColor(SSD1306_WHITE); // Draw white text
	display.setCursor(0, 0);
	display.print(F("AP SSID: "));
	display.println();
	display.print(ap_ssid);
	display.println();
	display.println();
	display.print(F("WiFi Manager IP: "));
	display.println();
	display.print("192.168.4.1");
	display.display();

	//read configuration from FS json
	Serial.println("mounting FS...");

	if (SPIFFS.begin())
	{
		Serial.println("mounted file system");
		if (SPIFFS.exists("/config.json"))
		{
			//file exists, reading and loading
			Serial.println("reading config file");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile)
			{
				Serial.println("opened config file");
				size_t size = configFile.size();
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);

				DynamicJsonDocument json(1024);
				auto deserializeError = deserializeJson(json, buf.get());
				serializeJson(json, Serial);
				if (!deserializeError)
				{

					Serial.println("\nparsed json");
					strcpy(mqtt_server, json["mqtt_server"]);
					strcpy(mqtt_port, json["mqtt_port"]);
					strcpy(mqtt_username, json["mqtt_username"]);
					strcpy(mqtt_password, json["mqtt_password"]);
					strcpy(mqtt_channel_temperature, json["mqtt_channel_temperature"]);
					strcpy(mqtt_channel_humidity, json["mqtt_channel_humidity"]);
					strcpy(mqtt_channel_co2, json["mqtt_channel_co2"]);
					strcpy(measurement_interval_second, json["measurement_interval_second"]);
				}
				else
				{
					Serial.println("failed to load json config");
				}
				configFile.close();
			}
		}
	}
	else
	{
		Serial.println("failed to mount FS");
	}
	//end read

	// The extra parameters to be configured (can be either global or just in the setup)
	// After connecting, parameter.getValue() will get you the configured value
	// id/name placeholder/prompt default length
	WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server", mqtt_server, 64);
	WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT port", mqtt_port, 6);
	WiFiManagerParameter custom_mqtt_username("mqtt_username", "MQTT username", mqtt_username, 32);
	WiFiManagerParameter custom_mqtt_password("mqtt_password", "MQTT password", mqtt_password, 32);
	WiFiManagerParameter custom_mqtt_channel_temperature("mqtt_channel_temperature", "MQTT temperature channel", mqtt_channel_temperature, 128);
	WiFiManagerParameter custom_mqtt_channel_humidity("mqtt_channel_humidity", "MQTT humidity channel", mqtt_channel_humidity, 128);
	WiFiManagerParameter custom_mqtt_channel_co2("mqtt_channel_co2", "MQTT CO2 channel", mqtt_channel_co2, 128);
	WiFiManagerParameter custom_measurement_interval_second("measurement_interval", "Measurement interval (s)", measurement_interval_second, 5);

	WiFi.hostname(hostname);
	wifi_station_set_hostname(hostname);

	WiFiManager wifiManager;
	//set config save notify callback
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	//add all your parameters here
	wifiManager.addParameter(&custom_mqtt_server);
	wifiManager.addParameter(&custom_mqtt_port);
	wifiManager.addParameter(&custom_mqtt_username);
	wifiManager.addParameter(&custom_mqtt_password);
	wifiManager.addParameter(&custom_mqtt_channel_temperature);
	wifiManager.addParameter(&custom_mqtt_channel_humidity);
	wifiManager.addParameter(&custom_mqtt_channel_co2);
	wifiManager.addParameter(&custom_measurement_interval_second);

	//reset settings - for testing
	//wifiManager.resetSettings();

	wifiManager.autoConnect(ap_ssid);

	//read updated parameters
	strcpy(mqtt_server, custom_mqtt_server.getValue());
	strcpy(mqtt_port, custom_mqtt_port.getValue());
	strcpy(mqtt_username, custom_mqtt_username.getValue());
	strcpy(mqtt_password, custom_mqtt_password.getValue());
	strcpy(mqtt_channel_temperature, custom_mqtt_channel_temperature.getValue());
	strcpy(mqtt_channel_humidity, custom_mqtt_channel_humidity.getValue());
	strcpy(mqtt_channel_co2, custom_mqtt_channel_co2.getValue());
	strcpy(measurement_interval_second, custom_measurement_interval_second.getValue());

	Serial.println("The values in the file are: ");
	Serial.println("\tmqtt_server: " + String(mqtt_server));
	Serial.println("\tmqtt_port: " + String(mqtt_port));
	Serial.println("\tmqtt_username: " + String(mqtt_username));
	Serial.println("\tmqtt_password : " + String(mqtt_password));
	Serial.println("\tmqtt_channel_temperature : " + String(mqtt_channel_temperature));
	Serial.println("\tmqtt_channel_humidity : " + String(mqtt_channel_humidity));
	Serial.println("\tmqtt_channel_co2 : " + String(mqtt_channel_co2));
	Serial.println("\tmqtt_measurement_interval_second : " + String(measurement_interval_second));
	
	//save the custom parameters to FS
	if (shouldSaveConfig)
	{
		Serial.println("saving config");
		DynamicJsonDocument json(1024);

		json["mqtt_server"] = mqtt_server;
		json["mqtt_port"] = mqtt_port;
		json["mqtt_username"] = mqtt_username;
		json["mqtt_password"] = mqtt_password;
		json["mqtt_channel_temperature"] = mqtt_channel_temperature;
		json["mqtt_channel_humidity"] = mqtt_channel_humidity;
		json["mqtt_channel_co2"] = mqtt_channel_co2;
		json["measurement_interval_second"] = measurement_interval_second;

		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile)
		{
			Serial.println("failed to open config file for writing");
		}

		serializeJson(json, Serial);
		serializeJson(json, configFile);

		configFile.close();
		//end save
	}

	// zapnutí komunikace s teploměrem DHT
	dhtSensor.begin();

	// We start by connecting to a WiFi network

	Serial.println();
	Serial.println();
	Serial.print("Connected to ");
	Serial.println(WiFi.SSID());

	ip = WiFi.localIP();
	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(ip);
	Serial.println("MAC address: ");
	Serial.println(WiFi.macAddress());

	mqttClient.setServer(mqtt_server, strtoul(mqtt_port, NULL, 10));

	measurementIntervalMs = strtoul(measurement_interval_second, NULL, 10) * 1000;
	lastTime = -measurementIntervalMs;
}

void loop()
{
	if (millis() - lastTime < measurementIntervalMs)
	{
		return;
	}

	lastTime = millis();

	display.clearDisplay();
	display.setCursor(0, 0); // Start at top-left corner

	// pomocí funkcí readTemperature a readHumidity načteme
	// do proměnných tep a vlh informace o teplotě a vlhkosti,
	// čtení trvá cca 250 ms
	float fTemp = dhtSensor.readTemperature();
	float fHumid = dhtSensor.readHumidity();
	// kontrola, jestli jsou načtené hodnoty čísla pomocí funkce isnan
	if (isnan(fTemp) || isnan(fHumid))
	{
		// při chybném čtení vypiš hlášku
		Serial.println("Chyba při čtení z DHT senzoru!");

		display.print(F("T: N/A"));
		display.println();
		display.print(F("V: N/A"));
		display.println();
	}
	else
	{
		// pokud jsou hodnoty v pořádku,
		// vypiš je po sériové lince
		Serial.print("Teplota: ");
		Serial.print(fTemp);
		Serial.print(" stupnu Celsia, ");
		Serial.print("vlhkost: ");
		Serial.print(fHumid);
		Serial.println("  %");

		display.print(F("T: "));
		display.print(fTemp);
		display.print(" C");
		display.println();
		display.print(F("V: "));
		display.print(fHumid);
		display.print(" %");
		display.println();

		//tempStr = String(fTemp * 10, 0);
		tempStr = String(fTemp, 1);
		tempStr.toCharArray(tempBuffer, tempStr.length() + 1);

		//humidStr = String(fHumid * 10, 0);
		humidStr = String(fHumid, 1);
		humidStr.toCharArray(humidBuffer, humidStr.length() + 1);

		if (mqttConnect())
		{
			mqttClient.publish(mqtt_channel_temperature, tempBuffer);
			Serial.print("Temperature published to: ");
			Serial.println(mqtt_channel_temperature);
			mqttClient.publish(mqtt_channel_humidity, humidBuffer);
			Serial.print("Humidity published to: ");
			Serial.println(mqtt_channel_humidity);
		}
	}

	MHZ19_RESULT response = mhz.retrieveData();
	if (response == MHZ19_RESULT_OK)
	{
		int co2 = mhz.getCO2();

		Serial.print(F("CO2: "));
		Serial.println(co2);

		display.print(F("CO2: "));
		display.print(co2);
		display.print(" ppm");

		co2Str = String(co2, 10);
		co2Str.toCharArray(co2Buffer, co2Str.length() + 1);

		if (mqttConnect())
		{
			mqttClient.publish(mqtt_channel_co2, co2Buffer);
			Serial.print("CO2 level published to: ");
			Serial.println(mqtt_channel_co2);
		}
	}
	else
	{
		Serial.print(F("CO2 error, code: "));
		Serial.println(response);

		display.print(F("CO2: "));
		display.print("N/A");
	}

	display.println();
	display.println();
	display.print(F("IP: "));
	display.print(ip);

	display.display();

	//mqttClient.loop();
}
