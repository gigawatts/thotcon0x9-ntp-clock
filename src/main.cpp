/*
 * NTP Clock
 * Running on Thotcon 0x9 (SparkFun ESP8266 Thing) hardware
 * To upload through terminal you can use: curl -F "image=@firmware.bin" thot0x9.local/update
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <U8g2lib.h>
#include <U8x8lib.h>
#include <PubSubClient.h>

const long utcOffsetInSeconds = -21600;  //CST
//const long utcOffsetInSeconds = -18000;  //CDT

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// Variables to save date and time
String formattedDate;
String dateStamp;
String timeStamp;

// Vars to save last cheerlights color
char lastColorName[16];
char lastColorCode[8];

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

#ifndef STASSID
#define STASSID "YourSSID"
#define STAPSK  "YourPSK"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;
const char* host = "thot0x9";
const char* mqtt_server = "mqtt.cheerlights.com";  //URL of cheerlights MQTT server
const char* mqtt_topic_name = "cheerlights";       //Topic for cheerlights color name
const char* mqtt_topic_code = "cheerlightsRGB";    //Topic for cheerlights RGB hex code value

WiFiClient espClient;
PubSubClient client(espClient);

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

U8G2_UC1701_EA_DOGS102_1_4W_SW_SPI u8g2(U8G2_R0, 14, 13, 15, 12); //128 x 64

unsigned long previousMillis = 0;
const long interval = 1000;

#define SERIAL_ENABLE

// START Functions --------------------------------------------------

void printTime(void) {
  // The formattedDate comes with the following format:
  // 2018-05-28T16:00:13Z
  // We need to extract date and time
  //Serial.println(formattedDate);

  // Extract date
  Serial.print("DATE: ");
  Serial.print(dateStamp);

  // Extract time
  Serial.print("  TIME: ");
  Serial.println(timeStamp);
}

void showTime(void) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_8x13B_tr);  // Small font for color, day of week, and date
    u8g2.drawStr(20, 10, lastColorName);
    //u8g2.drawStr(15, 10, daysOfTheWeek[timeClient.getDay()] );  // Show day of the week
    u8g2.drawStr(11, 23, dateStamp.c_str() );  // Show current date in YYYY-MM-DD format

    u8g2.setFont(u8g2_font_logisoso38_tn);  // Large font for hours and minutes
    char hour[3];
    char minute[3];
    sprintf(hour, "%02d", timeClient.getHours());
    sprintf(minute, "%02d", timeClient.getMinutes());

    u8g2.drawStr(-2,63,hour);
    u8g2.drawStr(44,63,":");
    u8g2.drawStr(54,63,minute);
  } while ( u8g2.nextPage() );
}

void dispInit(void) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_8x13B_tr);
    u8g2.drawStr(0,30, "Initializing" );

  } while ( u8g2.nextPage() );
}


// MQTT functions -----------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  if( strcmp(topic, "cheerlights") == 0 ) {
    memset(lastColorName, 0, sizeof(lastColorName)); // clear the last saved color name
  }
  
  if( strcmp(topic, "cheerlightsRGB") == 0 ) {
    memset(lastColorCode, 0, sizeof(lastColorCode)); // clear the last saved color code
  }
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    if( strcmp(topic, "cheerlights") == 0 ) lastColorName[i] = payload[i];  // copy the color name to a global var
    if( strcmp(topic, "cheerlightsRGB") == 0 ) lastColorCode[i] = payload[i];  // copy the color code to a global var
  }
  Serial.println();
}

void reconnect() {
  while (!client.connected()) {  // Loop until we're reconnected
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESPclient-";  // Create a random client ID
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {  // Attempt to connect
      Serial.println("connected");
      client.subscribe(mqtt_topic_name);
      client.subscribe(mqtt_topic_code);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);  // Wait 5 seconds before retrying
    }
  }
}

// http server functions
void handleNotFound(void)
{
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += httpServer.uri();
	message += "\nMethod: ";
	message += (httpServer.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += httpServer.args();
	message += "\n";
	for (uint8_t i=0; i<httpServer.args(); i++)
	{
		message += " " + httpServer.argName(i) + ": " + httpServer.arg(i) + "\n";
	}
	httpServer.send(404, "text/plain", message);
}

// END Functions --------------------------------------------------

void setup(void) {
  u8g2.begin();
  u8g2.clearDisplay();
  u8g2.setDisplayRotation(U8G2_R2);  // rotate the display 180° clockwise
                                     // U8G2_R0 = No rotation, landscape
                                     // U8G2_R1 = 90° clockwise rotation
                                     // U8G2_R3 = 270° clockwise rotation
                                     // U8G2_MIRROR = No rotation, landscape, display content is mirrored
  dispInit();

  #ifdef SERIAL_ENABLE
    Serial.begin(115200);
    Serial.println();
    Serial.println("Booting Sketch...");
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.hostname(host);  // Set hostname on the LAN
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    #ifdef SERIAL_ENABLE
      Serial.println("WiFi failed, retrying.");
    #endif
  }

  #ifdef SERIAL_ENABLE
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  #endif

  MDNS.begin(host);
  timeClient.begin();

  httpUpdater.setup(&httpServer);

  httpServer.on("/", []() {
    String message;
    message += "<html>\n<head> \
<style>body{background-color: black; color: #ccc; font-family: Tahoma; font-size: 24px;}</style>\n \
</head>\n \
<body>\nCheerlights Color: <font color=\""+ (String)lastColorCode + "\">" + (String)lastColorName + "</font>\n<body></html>";

    httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    httpServer.sendHeader("Pragma", "no-cache");
    httpServer.sendHeader("Expires", "-1");
    httpServer.send(200, "text/html", message);
  });

  httpServer.onNotFound(handleNotFound);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  #ifdef SERIAL_ENABLE
    Serial.printf("HTTPUpdateServer ready! Open http://%s.lan/update in your browser\n", host);
  #endif

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  randomSeed(micros());
} //END setup


void loop(void) {
  httpServer.handleClient();
  MDNS.update();

  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    formattedDate = timeClient.getFormattedDate();
    int splitT = formattedDate.indexOf("T");
    dateStamp = formattedDate.substring(0, splitT);
    timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);

    printTime();
    showTime();
  }

  if (!client.connected()) {
    reconnect();
  }
  
  client.loop();

} //END loop