#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>

int DoorStatusAddress = 2;
/*
 WIFI CONFIGURATION
 */
char SSID[] = "lumbergh";
char pwd[] = "hunterisbusted";

/*
 SLACK CONFIGURATION
 */
const String slack_hook_url = "https://hooks.slack.com/services/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";
const String slack_emoji = "heart";
const String ToiletInUseMessage = "Bathroom is currently occupied as of: ";
const String ToiletIsFree = "Bathroom has become vacant as of: ";
float SleepTime = 10e6; // time in microseconds



WiFiUDP ntpUDP;
const String slack_username = "tayjojo";
int AnalogData;
bool isBathroomInUse = false;
int DoorReadPin = 5; //D1

// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
NTPClient timeClient(ntpUDP, "2.us.pool.ntp.org", 21600); //21600 is the offset, add another argument at end to set update interval
void connect() {

  

  WiFi.begin(SSID, pwd);
  timeClient.begin();
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  // OTA setup
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}


bool postMessageToSlack(String msg)
{
  const char* host = "hooks.slack.com";
  Serial.print("Connecting to ");
  Serial.println(host);

  // Use WiFiClient class to create TCP connections
  WiFiClientSecure client;
  const int httpsPort = 443;
  if (!client.connect(host, httpsPort)) {
    Serial.println("Connection failed :-(");
    return false;
  }

  // We now create a URI for the request

  Serial.print("Posting to URL: ");
  Serial.println(slack_hook_url);

  String postData="payload={\"link_names\": 1, \"icon_emoji\": \"" + slack_emoji + "\", \"username\": \"" + slack_username + "\", \"text\": \"" + msg + "\"}";

  // This will send the request to the server
  client.print(String("POST ") + slack_hook_url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Content-Type: application/x-www-form-urlencoded\r\n" +
               "Connection: close" + "\r\n" +
               "Content-Length:" + postData.length() + "\r\n" +
               "\r\n" + postData);
  Serial.println("Request sent");
  String line = client.readStringUntil('\n');
  Serial.printf("Response code was: ");
  Serial.println(line);
  if (line.startsWith("HTTP/1.1 200 OK")) {
    return true;
  } else {
    return false;
  }
}
void loop() {
  
}

bool handleDoorHasOpened() {
  bool isSuccessful;
  timeClient.update(); //get current time
  String CombinedVacant = (ToiletIsFree + timeClient.getFormattedTime()); // concatenate vacant message with timestamp
  Serial.println("door has opened");
  Serial.println(timeClient.getFormattedTime());
  isSuccessful = postMessageToSlack(CombinedVacant); // send message to slack
  Serial.println("toilet is free");
  writeDoorStatusToEEPROM(DoorStatusAddress, false); // store new door status (door is open)

  return isSuccessful;
}

bool handleDoorHasClosed() {
  bool isSuccessful;
  timeClient.update(); //get current time
  String CombinedOccupied = (ToiletInUseMessage + timeClient.getFormattedTime()); // concatenate occupied message with timestamp
  Serial.println("door has closed");
  Serial.println(timeClient.getFormattedTime());
  isSuccessful = postMessageToSlack(CombinedOccupied); // send message to slack
  writeDoorStatusToEEPROM(DoorStatusAddress, true); // store new door status (door is open)
  
  return isSuccessful;
}

void writeDoorStatusToEEPROM(int address, bool doorstatus) {
  EEPROM.write(address, doorstatus);
  EEPROM.commit();
}




void setup() {
  EEPROM.begin(4);
  Serial.begin(115200);
  Serial.setTimeout(2000);
  pinMode(DoorReadPin, INPUT);
  //0 is open
  //1 is closed

  //when the door is closed, rst and wake will be connected and it will wake the device
  //when the device is awake perform these actions:
  //-1. make sure it's not after 6pm
  //0. read EEPROM to see if door status has changed
  //1. read pin 0 and see if the door is opened or closed
  //2. send message to slack if the digitalRead returns HIGH
  //3. go to sleep for 30 seconds
  //4. upon waking up again, check the status of digitalRead, if it is LOW, send message to slack and go to sleep
 
  bool PreviousDoorStatus = EEPROM.read(DoorStatusAddress);
 // bool DigitalReading = digitalRead(DoorReadPin);
  bool CurrentDoorStatus = digitalRead(DoorReadPin);
  
  bool isSuccessful;

  Serial.print("EEPROM has begun, door status is: ");
  Serial.println(PreviousDoorStatus);
  Serial.print("Current door status is: ");
  Serial.println(CurrentDoorStatus);

  if(PreviousDoorStatus == CurrentDoorStatus) { // if there has been no change to the door
    Serial.println("no change in door status");
    ESP.deepSleep(SleepTime);
  } else if(CurrentDoorStatus < PreviousDoorStatus) { // if door has been opened
    Serial.println("Door seems to have opened");
    connect();
    isSuccessful = handleDoorHasOpened();
    ESP.deepSleep(SleepTime);
  } else if(CurrentDoorStatus > PreviousDoorStatus) { // if door has been closed
    Serial.println("door seems to have closed");
    connect();
    isSuccessful = handleDoorHasClosed();
    ESP.deepSleep(SleepTime);
  }
  Serial.println("is successful? " + isSuccessful);
  // Serial.println(requestsuccess);
  // Start handling OTA updates
  // ArduinoOTA.handle();
  //Serial.println(AnalogData);

  // delay(2000);
}
