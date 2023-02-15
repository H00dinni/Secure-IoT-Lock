#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#else
#include <ESP8266WiFi.h>
#define ASYNC_TCP_SSL_ENABLED 1
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebSrv.h>
#include <Servo.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secret.h"

const char* http_username = "smartlockdemo"; //Username for webpage login
const char* http_password = "Pass1234!@#$"; //Password for webpage login

int otp;
int ds;

const char* PARAM_INPUT_1 = "state";
const char* input_parameter1 = "input_integer";
const char* input_parameter2 = "input_string";

const int output = 2;
int count;
int timeout;

unsigned long lastMillis = 0;
unsigned long previousMillis = 0;
const long interval = 5000;

#define AWS_IOT_PUBLISH_TOPIC   "esp8266/pub" //MQTT Publish Topic
#define AWS_IOT_SUBSCRIBE_TOPIC "esp8266/sub" //MQTT Subscribe Topic

WiFiClientSecure net;
BearSSL::X509List cert(cacert); //AWS Root Certificate
BearSSL::X509List client_crt(client_cert); //AWS Device Certificate
BearSSL::PrivateKey key(privkey); //AWS Private Key
PubSubClient client(net);
AsyncWebServer server(80); //Web Server
Servo servo;

time_t now;
time_t nowish = 1510592825;

void NTPConnect(void)
{
  Serial.print("Setting time using SNTP");
  configTime(TIME_ZONE * 3600, 0 * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  struct tm *newtime;
  time_t ltime;
  time(&ltime);
  newtime = localtime(&ltime);
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(newtime));
}

void connectAWS()
{
  NTPConnect();

  net.setTrustAnchors(&cert);
  net.setClientRSACert(&client_crt, &key);

  client.setServer(MQTT_HOST, 8883);

  Serial.println("Connecting to AWS IOT");

  while (!client.connect(THINGNAME))
  {
    Serial.print(".");
    delay(1000);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }

  Serial.println("AWS IoT Connected!");
}

void publishMessage()
{
  StaticJsonDocument<200> doc;
  doc["OTP"] = otp;
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  Serial.println("OTP has been sent");
}

void logaccessdenied()
{
  StaticJsonDocument<200> doc;
  doc["Log"] = "Someone has been access denied";
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  Serial.println("Log has been sent");
}

void loglock()
{
  StaticJsonDocument<200> doc;
  if(ds == 0){
  doc["Log"] = "Lock has been unlocked";
  Serial.println("Log: Lock has been unlocked");
  }
  else if(ds == 1){
  doc["Log"] = "Lock has been locked";
  Serial.println("Log: Lock has been locked");
  }
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  Serial.println("Log has been sent");
}

void otpgenerator() {
  otp = random(100000, 999999);
  Serial.println(String("OTP generated is: ") + String(otp));
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>LOCK CONTROL</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h2 {font-size: 2.6rem;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 10px;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 34px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}
    input:checked+.slider {background-color: #2196F3}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <h2>LOCK CONTROL</h2>
  <button onclick="logoutButton()">Logout</button>
  <p><strong> Please lock the door before logging out!</strong></p>
  <p>Lock Status <span id="state">%STATE%</span></p>
  %BUTTONPLACEHOLDER%
<script>function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked){ 
    xhr.open("GET", "/update?state=1", true); 
    document.getElementById("state").innerHTML = "Locked";
    logcount = 1;  
  }
  else { 
    xhr.open("GET", "/update?state=0", true); 
    document.getElementById("state").innerHTML = "Unlocked";
    logcount = 1;     
  }
  xhr.send();
}
function logoutButton() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/logout", true);
  xhr.send();
  setTimeout(function(){ window.open("/logged-out","_self"); }, 1000);
}
</script>
</body>
</html>
)rawliteral";

const char logout_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <p><a href="/">return</a>.</p>
</body>
</html>
)rawliteral";

const char denied_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <p><strong>ACCESS DENIED</strong></p>
  <form action="/deniedconfirm">
    Type confirm: <input type="text" name="input_string" autocomplete="off">
    <input type="submit" value="Submit">
    <p>There will be a timeout before you can attempt login again</p>
    <p>Please close the browser and enter IP address again when timeout has ended</p>
  </form><br>
</body>
</html>
)rawliteral";

const char otp_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <p>OTP has been sent to your mobile device</p>
  <form action="/getotp">
    Enter OTP: <input type="text" name="input_integer" autocomplete="off">
    <input type="submit" value="Submit">
  </form><br>
</body>
</html>
)rawliteral";

// Replaces placeholder with button section in your web page
String processor(const String& var){
  //Serial.println(var);
  if(var == "BUTTONPLACEHOLDER"){
    String buttons ="";
    String outputStateValue = outputState();
    buttons+= "<p><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"output\" " + outputStateValue + "><span class=\"slider\"></span></label></p>";
    return buttons;
  }
  if (var == "STATE"){
    if(digitalRead(output)){
      return "Unlocked";
    }
    else {
      return "Locked";
    }
  }
  return String();
}

String outputState(){
  if(digitalRead(output)){
    return "checked";
  }
  else {
    return "";
  }
  return "";
}

void setup(){
  // Serial port for debugging purposes
  Serial.begin(115200);
  servo.attach(D1);
  servo.write(120);
  pinMode(output, OUTPUT);
  digitalWrite(output, LOW);
  timeout = 0;
  
  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }

  // Print ESP Local IP Address
  Serial.println("Wifi Successfully Connected!");
  Serial.println("Please use the IP address below to access webpage");
  Serial.println(WiFi.localIP());

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ //start of server / login page
    if(!request->authenticate(http_username, http_password)){
      count += 1;
      Serial.println(count);
      if(count < 5){
      return request->requestAuthentication();
      }
      else{
        request->send_P(200, "text/html", denied_html, processor); // account lock out
        count = 0;
      }
      
    } 
    else{
      otpgenerator();
      delay(500);
      publishMessage();
      Serial.println("Publishing message");
      request->send_P(200, "text/html", otp_html, processor); //to otp code input
      count = 0;
    }
  });
    
  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(401);
  });

  server.on("/logged-out", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", logout_html, processor);
  });

  server.on("/getotp", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate(http_username, http_password)){
      return request->requestAuthentication();
      }
    String input_message;
    String input_parameter;
    if (request->hasParam(input_parameter1)) {
      input_message = request->getParam(input_parameter1)->value();
      //input_parameter = input_parameter1;
    }
    Serial.println(input_message);
    if(input_message = otp){
      request->send_P(200, "text/html", index_html, processor);
    }
    else{
      request->send_P(200, "text/html", otp_html, processor);
    }
  });

  server.on("/deniedconfirm", HTTP_GET, [](AsyncWebServerRequest *request){
    String input_message;
    String input_parameter;
    if (request->hasParam(input_parameter2)) {
      input_message = request->getParam(input_parameter2)->value();
    }
    Serial.println(input_message);
    if(input_message == "confirm"){
      delay(20000);
      count = 0;
      logaccessdenied();
      Serial.println("Denied Access to web page");
    }
    else{
      request->send_P(200, "text/html", denied_html, processor);
    }
  });

  // Send a GET request to <ESP_IP>/update?state=<inputMessage>
  server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username, http_password)){
      return request->requestAuthentication();
    }
    String inputMessage;
    String inputParam;
    // GET input1 value on <ESP_IP>/update?state=<inputMessage>
    if (request->hasParam(PARAM_INPUT_1)) {
      inputMessage = request->getParam(PARAM_INPUT_1)->value();
      inputParam = PARAM_INPUT_1;
      digitalWrite(output, inputMessage.toInt());
      servo.write(inputMessage.toInt() * 120);
    }
    else {
      inputMessage = "No message sent";
      inputParam = "none";
    }
    int doorstatus = inputMessage.toInt();
    ds = doorstatus;
    Serial.println(doorstatus);
    if(doorstatus == 1){
      Serial.println("Door is locked");
      loglock();
    }
    if(doorstatus == 0){
      Serial.println("Door is unlocked");
      loglock();
    }
    //Serial.println(inputMessage.toInt());
    request->send(200, "text/plain", "OK");
  });
  
  // Start server
  server.begin();
  Serial.println("Server started");
  connectAWS();
  count = 0;
}
  
void loop() {
  if (!client.connected())
  {
    connectAWS();
  }
  else
  {
    client.loop();
  }
}

  
