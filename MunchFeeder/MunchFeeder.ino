/*
 Name:		MunchFeeder.ino
 Created:	3/10/2018 4:27:46 PM
 Author:	Jake Hedlund
*/

#include <AccelStepper.h>
#include <TimeAlarms.h> 
#include <TimeLib.h>
//#include <c_types.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WifiUdp.h>
//#include <ArduinoOTA.h>
//#include <WiFiManager.h>
#include <DNSServer.h>
#include <FS.h>
#include <ArduinoJson.h> // https://arduinojson.org/assistant/ 

#define DBG_OUTPUT_PORT Serial
#define ON LOW
#define OFF HIGH

// Network details
const char *ssid = "PrettyFly4aWiFi";  //  your network SSID (name)
const char *pass = "MiaMunch71317";       // your network password
const char *serverName = "munchfeeder";
const char *www_user = "admin"; 
const char *www_pass = "nwhiting"; 
//ESP8266WiFiMulti wifiMulti; 

// NTP constants
const unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; 
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;// A UDP instance to let us send and receive packets over UDP

// Server variables
ESP8266WebServer server(80);
File fsUploadFile;
//WiFiManager wifiMgr;

// Feeder params
const int NUM_TIMES = 5;
const int led = LED_BUILTIN;
const int stepPins[4] = { D5, D6, D7, D8 };  // Stepper pins being used
const int btnFeed = D1;                      // Dispense now button
int rotationDirection = 0; 
int lastFedAmt = 0; 
bool unjamInProgress = false; 
int unjamStep = 0; 

volatile bool dispensing = false; 
time_t lastFedTime = 0; 
const size_t bufferSize = JSON_ARRAY_SIZE(NUM_TIMES) + JSON_OBJECT_SIZE(1) + NUM_TIMES * JSON_OBJECT_SIZE(3) + 200;
DynamicJsonBuffer jsonBuffer(bufferSize);

// Alarms
AlarmID_t alarms[NUM_TIMES];
int amounts[NUM_TIMES];

// Stepper
AccelStepper stepper(AccelStepper::HALF4WIRE, stepPins[0], stepPins[1], stepPins[2], stepPins[3]);
int maxSpeed = 300; 
int maxAcel = 1500; 
bool stepperEn = false;
const int deg120 = 133;

void setup() {
    pinMode(led, OUTPUT);
    digitalWrite(LED_BUILTIN, ON);
    //digitalWrite(led, 0);

    DBG_OUTPUT_PORT.begin(115200);
    DBG_OUTPUT_PORT.print("\n");
    DBG_OUTPUT_PORT.setDebugOutput(true);

	// Wait for serial port to connect. 
	delay(2000); 
    
    // Server things
    initWifi();
    setupMDNS();
    initNtp();

    // Get the SPIFFS filesystem ready. 
    setupFs();

    server.begin();

    // Setup over-the-air updates. 
    //setupOTA(); 

    stepper.setAcceleration(maxAcel);
    stepper.setMaxSpeed(maxSpeed);

    pinMode(btnFeed, INPUT_PULLUP);
    digitalWrite(LED_BUILTIN, OFF);
    attachInterrupt(btnFeed, dispenseFoodInterrupt, FALLING);

    initSettings();

    DBG_OUTPUT_PORT.println("Ready to rock.");
}

// the loop function runs over and over again until power down or reset
void loop() {
    int tries = 10; 

    // Update time from NTP server if necessary. 
    if (timeStatus() == timeNotSet)
        Serial.println("Time not set.");
    if (timeStatus() == timeNeedsSync)
        Serial.println("Time needs sync.");

    if (timeStatus() == timeNotSet || timeStatus() == timeNeedsSync)
    {
        char tm[100]; 

        Serial.print("Getting NTP time from ");
		Serial.println(timeServerIP);

        digitalWrite(LED_BUILTIN, ON);
        initNtp();
        sendNTPpacket(timeServerIP, udp);
        // wait to see if a reply is available
        delay(1000);

        time_t t = getTimeNtp(udp);

        while (!t && tries > 0)
        {
            WiFi.hostByName(ntpServerName, timeServerIP);
            sendNTPpacket(timeServerIP, udp); 
            delay(1000); 
            t = getTimeNtp(udp);
            delay(500); 
            tries--;
        }

        // Set the time of the TimeLib library to the current PST time
		setTime(t);
        //setTime(t - (hoursToTime_t(isDST(t)? 7 : 8)));

		t = t - (hoursToTime_t(isDST(t) ? 7 : 8));

        snprintf(tm, 100, "Current time set to: %02d:%02d:%02d", numberOfHours(t), numberOfMinutes(t), numberOfSeconds(t));
        DBG_OUTPUT_PORT.println(tm);

        digitalWrite(LED_BUILTIN, OFF);

        initAlarms();
    }

	if (unjamStep > 0 && !unjamInProgress) {
		Serial.print("Unjamming... ");
		Serial.println(unjamStep);

		int dir = (unjamStep % 2) == 0 ? 1 : -1;

		stepper.move(dir * ((15 - unjamStep) * 10));

		unjamInProgress = true;
	}
    
    // Look for OTA updates. 
    //ArduinoOTA.handle(); 

    // Handle HTTP requests
    server.handleClient();

    // Cause alarms to trigger when necessary. 
    Alarm.delay(0);

    // Move the stepper to dispense. 
    if (stepper.distanceToGo() != 0)
    {
        dispensing = true;
        digitalWrite(LED_BUILTIN, ON);
        if (!stepperEn)
        {
            stepper.enableOutputs();
            stepperEn = true;
        }
        stepper.run();
        //DBG_OUTPUT_PORT.println(stepper.distanceToGo());
    }
    
    if (stepper.distanceToGo() == 0 && stepperEn)
    {
		if (unjamInProgress)
		{
			unjamStep--; 
			unjamInProgress = false; 
		}
        DBG_OUTPUT_PORT.println("Stepper done.");
        stepper.stop();
        stepper.disableOutputs();
        stepperEn = false;
        digitalWrite(LED_BUILTIN, OFF);
        dispensing = false;
    }
}

// Load alarms from JSON file. 
void initAlarms() {
    String path = "/times.json"; 
    String json = ""; 

    DBG_OUTPUT_PORT.println("Initializing alarms..."); 

    if (SPIFFS.exists(path)) {

        // Read the json file. 
        File j = SPIFFS.open(path, "r");
        json = j.readString(); 
        JsonObject& root = jsonBuffer.parseObject(json);

        if (!root.success())
        {
            DBG_OUTPUT_PORT.println("Error parsing JSON!"); 
            return;
        }

        JsonArray& times = root["times"];

        // Go through and create alarms
        for (int i = 0; i < NUM_TIMES; i++) {
            JsonObject& timeObj = times[i];
            bool en = timeObj["enabled"];
            int amt = timeObj["amount"];
            String time = timeObj["time"];
            int h = time.substring(0, 2).toInt();
            int m = time.substring(3, 5).toInt();
            
            SetupAlarm(i, h, m, amt, en); 

            DBG_OUTPUT_PORT.printf("Init alarm: (%s) %d - %02d:%02d\n", en ? "enabled":"disabled", i, h, m);
        }
    }
}

// Load previoiusly saved settings. 
void initSettings() {
    String path = "/lastfed.json";
    String json = ""; 

    DBG_OUTPUT_PORT.println("Loading settings."); 

    if (SPIFFS.exists(path))
    {
        // Read the json file. 
        File j = SPIFFS.open(path, "r");
        json = j.readString();
        JsonObject& root = jsonBuffer.parseObject(json);

        if (!root.success())
        {
            DBG_OUTPUT_PORT.println("Error parsing JSON!");
            return;
        }

        lastFedAmt = root["amount"];
        rotationDirection = root["rotation-dir"];
        maxSpeed = root["rotation-speed"];
        stepper.setMaxSpeed(maxSpeed);
    }
}

void initNtp() {

    //get a random server from the pool
    WiFi.hostByName(ntpServerName, timeServerIP);
}

void initWifi() {
    int attemptNum = 50; 
    wl_status_t status = WL_IDLE_STATUS;

    //wifiMulti.addAP(ssid, pass);
    //wifiMulti.addAP("NotFreeWifi", "jhedlund123");
    //wifiMulti.addAP("Deep Ocean Engineering", "DOEIncorporated");

    // We start by connecting to a WiFi network
    DBG_OUTPUT_PORT.print("Connecting to ");
    DBG_OUTPUT_PORT.println(ssid);
    WiFi.mode(WIFI_STA);
    status = WiFi.begin(ssid, pass);

    //status = wifiMulti.run(); 
    
    while (WiFi.status() != WL_CONNECTED ) {
        delay(500);
        Serial.print(".");
        attemptNum--;
        // status = wifiMulti.run(); 
        // status = WiFi.begin(ssid, pass);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println(WiFi.SSID());
        Serial.println("");

        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());

        Serial.println("Starting UDP");
        udp.begin(localPort);
        Serial.print("Local port: ");
        Serial.println(udp.localPort());
    }
    else
    {
		Serial.println("Wifi not connected. ");
        Serial.println("Setting up WifiMgr AP."); 
        //setupWifiMgr(); 
    }
}

void dispenseFoodInterrupt() {
    if (!dispensing) 
    {
        Serial.println("Dispensing from button press.");
        dispensing = true;
        lastFedTime = now(); 
        dispenseFood(3); 
        saveLastFedJSON(lastFedTime, 3);
    }
}
// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address, WiFiUDP& udp)
{
    //Serial.println("sending NTP packet...");
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
                             // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    udp.beginPacket(address, 123); //NTP requests are to port 123
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
}

time_t getTimeNtp(WiFiUDP& udp) {
    int cb = udp.parsePacket();
    time_t newTime = 0;
    int tries = 10; 

    while (!cb && tries >= 0) {
        Serial.print("no packet yet ");
        Serial.println(String(tries)); 
        tries--; 
        delay(200); 
        cb = udp.parsePacket(); 
    }

    if(cb) 
    {
        Serial.print("packet received, length=");
        Serial.println(cb);
        // We've received a packet, read the data from it
        udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

                                                 //the timestamp starts at byte 40 of the received packet and is four bytes,
                                                 // or two words, long. First, esxtract the two words:

        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
        // combine the four bytes (two words) into a long integer
        // this is NTP time (seconds since Jan 1 1900):
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        //Serial.print("Seconds since Jan 1 1900 = ");
        //Serial.println(secsSince1900);

        // now convert NTP time into everyday time:
        //Serial.print("Unix time = ");
        // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
        const unsigned long seventyYears = 2208988800UL;
        // subtract seventy years:
        unsigned long epoch = secsSince1900 - seventyYears;
        // print Unix time:
        //Serial.println(epoch);


        // print the hour, minute and second:
        Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
        Serial.print((epoch % 86400L) / 3600); // print the hour (86400 equals secs per day)
        Serial.print(':');
        if (((epoch % 3600) / 60) < 10) {
            // In the first 10 minutes of each hour, we'll want a leading '0'
            Serial.print('0');
        }
        Serial.print((epoch % 3600) / 60); // print the minute (3600 equals secs per minute)
        Serial.print(':');
        if ((epoch % 60) < 10) {
            // In the first 10 seconds of each minute, we'll want a leading '0'
            Serial.print('0');
        }
        Serial.println(epoch % 60); // print the second

        newTime = epoch;
    }
    return newTime;
}

bool isDST(time_t time) {
    int m = month(time); 
    int d = day(time);

    if ((m == 3 && d > 11) || (m > 3 && m < 11) || (m == 11 && d <= 10)) {
        return true;
    }

    return false;
}

time_t getDstOffset(time_t time) {
	return (time_t)hoursToTime_t( isDST(time) ? 7 : 8 );
}

void setupMDNS() {
    // Set up mDNS responder:
    // - first argument is the domain name, in this example
    //   the fully-qualified domain name is "esp8266.local"
    // - second argument is the IP address to advertise
    //   we send our IP address on the WiFi network
    if (!MDNS.begin(serverName)) {
        DBG_OUTPUT_PORT.println("Error setting up MDNS responder!");
    }
    else
    {
        DBG_OUTPUT_PORT.println("mDNS responder started");

        // Add service to MDNS-SD
        MDNS.addService("http", "tcp", 80);
    }
}

/*void setupOTA() {

    // Port defaults to 8266
    ArduinoOTA.setPort(8888);

    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(serverName);

    // No authentication by default
    ArduinoOTA.setPassword("nwhiting");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_SPIFFS
            type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
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
    Serial.println("OTA Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}*/

void setupWifiMgr() {
    //if (!wifiMgr.autoConnect("MunchFeederSetup", "munchfeeder"))
    //{
    //    Serial.println("failed to connect to a network, timed out..."); 
    //    ESP.reset(); 
    //    delay(1000); 
    //}
}

void handleNotFound() {
    digitalWrite(led, ON);
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";

    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }

    server.send(404, "text/plain", message);
    digitalWrite(led, OFF);
}

void handleRoot() {
    digitalWrite(led, ON);
    char temp[500];
    time_t time = now();

    Serial.println("Responding to root");
    redirect();
//    snprintf(temp, 500,
//        "<html><head><meta http-equiv='refresh' content='5'/><title>Muncher Feeder</title><style>\
//body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
//</style>\
//</head>\
//<body>\
//<h1>Hello from the Munch feeder!</h1>\
//<p>Current Time: %02d:%02d:%02d</p> \
//<a href='feed.htm'>Click here!</a> \
//</body>\
//</html>",
//        numberOfHours(time), numberOfMinutes(time), numberOfSeconds(time)
//        );
//    server.send(200, "text/html", temp);
    digitalWrite(led, OFF);
}

//format bytes
String formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + "B";
    }
    else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0) + "KB";
    }
    else if (bytes < (1024 * 1024 * 1024)) {
        return String(bytes / 1024.0 / 1024.0) + "MB";
    }
    else {
        return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
    }
}

String getContentType(String filename) {
    if (server.hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    else if (filename.endsWith(".json")) return "text/json";
    return "text/plain";
}

bool handleFileRead(String path) {
    //DBG_OUTPUT_PORT.println("handleFileRead: " + path);
    if (path.endsWith("/")) path += "index.htm";
    String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
        if (SPIFFS.exists(pathWithGz))
            path += ".gz";
        File file = SPIFFS.open(path, "r");
        size_t sent = server.streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

void handleFileUpload() {
    if (server.uri() != "/edit") return;
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!filename.startsWith("/")) filename = "/" + filename;
        DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
        fsUploadFile = SPIFFS.open(filename, "w");
        filename = String();
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
        if (fsUploadFile)
            fsUploadFile.write(upload.buf, upload.currentSize);
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (fsUploadFile)
            fsUploadFile.close();
        DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
    }
}

void handleFileDelete() {
    if (server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
    String path = server.arg(0);
    DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
    if (path == "/")
        return server.send(500, "text/plain", "BAD PATH");
    if (!SPIFFS.exists(path))
        return server.send(404, "text/plain", "FileNotFound");
    SPIFFS.remove(path);
    server.send(200, "text/plain", "");
    path = String();
}

void handleFileCreate() {
    if (server.args() == 0)
        return server.send(500, "text/plain", "BAD ARGS");
    String path = server.arg(0);
    DBG_OUTPUT_PORT.println("handleFileCreate: " + path);
    if (path == "/")
        return server.send(500, "text/plain", "BAD PATH");
    if (SPIFFS.exists(path))
        return server.send(500, "text/plain", "FILE EXISTS");
    File file = SPIFFS.open(path, "w");
    if (file)
        file.close();
    else
        return server.send(500, "text/plain", "CREATE FAILED");
    server.send(200, "text/plain", "");
    path = String();
}

void handleFileList() {
    if (!server.hasArg("dir")) { server.send(500, "text/plain", "BAD ARGS"); return; }

    String path = server.arg("dir");
    DBG_OUTPUT_PORT.println("handleFileList: " + path);
    Dir dir = SPIFFS.openDir(path);
    path = String();

    String output = "[";
    while (dir.next()) {
        File entry = dir.openFile("r");
        if (output != "[") output += ',';
        bool isDir = false;
        output += "{\"type\":\"";
        output += (isDir) ? "dir" : "file";
        output += "\",\"name\":\"";
        output += String(entry.name()).substring(1);
        output += "\"}";
        entry.close();
    }

    output += "]";
    server.send(200, "text/json", output);
}

void handleTimesList() {

    handleFileRead("/times.json");
}

void updateTimes() {
    if (server.args() == 0)
    {
        DBG_OUTPUT_PORT.println("bad args! ");
        return server.send(500, "text/plain", "BAD ARGS");
    }

    // Save the times into a .json file.
    //DBG_OUTPUT_PORT.println("Updating times: " + message);
    //String json = constructJSON(); 
    JsonObject& jObj = constructJSON(); 
    String json = "";
    //server.send(200, "text/plain", "Successfully updated the times! \n" + message + "\nJSON: " + json);
    //handleFileRead("/feed.htm");

    File f = SPIFFS.open("/times.json", "w");
    jObj.printTo(json);
    f.print(json);
    f.close();

    // Redirect back to the feeder page. 
    redirect();
}

// Construct a JSON string from the server args describing the form on the website. 
JsonObject &constructJSON() {
    /*String ret = "{";
    bool firstTime = true;*/

    JsonObject& root = jsonBuffer.createObject();
    JsonArray& times = root.createNestedArray("times");

    for (uint8_t idx = 0; idx < server.args(); idx++) {
        String name = server.argName(idx);


        if (!name.startsWith("time"))
            continue;

        if (name.startsWith("time")) {

            // the "id" of the time/alarm being set. 
            char num = name.charAt(name.length() - 1);
            // Time string
            String sTime = server.arg(name);
            // Index of this time. 
            int _inum = num - 0x30 - 1;
            // Amount of food to dispense. 
            String amt = server.arg("amount" + String(num));
            // Enabled
            bool en = server.hasArg("enable" + String(num));

            JsonObject& timeObj = times.createNestedObject(); 

            timeObj["amount"] = amt.toInt(); 
            timeObj["enabled"] = en;
            timeObj["time"] = sTime;

            /*
            if (firstTime)
            {
                ret += "\"times\": [";
                ret += "{";
                firstTime = false;
            }
            else
                ret += ",{";


            ret += "\"amount\":";
            ret += amt + ",";
            ret += "\"enabled\":";
            ret += en ? "true," : "false,";
            ret += "\"time\":\"" + server.arg(name) + "\"}";
            */

            String h = sTime.substring(0, 2);
            String m = sTime.substring(3, 5);
            DBG_OUTPUT_PORT.println("Saving time: " + String(_inum) + " - " + h + ":" + m);


            amounts[_inum] = amt.toInt();

            SetupAlarm(_inum, h.toInt(), m.toInt(), amt.toInt(), en);
        }
    }
    //ret += "]";
    //ret += "}";

    return root;
}

void SetupAlarm(int idx, int h, int m, int amt, bool en) {
    time_t time = ((hoursToTime_t(h) + getDstOffset(now())) % hoursToTime_t(24)) + minutesToTime_t(m);

    if (Alarm.isAllocated(idx)) {
        Alarm.write(idx, time);
        //DBG_OUTPUT_PORT.println("Previous alarm changed: " + String(idx)); 
    }
    else
    {
        idx = Alarm.alarmRepeat(time, alarmTriggered);
        //DBG_OUTPUT_PORT.println("New alarm created: " + String(idx));
        amounts[idx] = amt;
    }

    // Enable or disable as needed. 
    if (en) {
        Alarm.enable(idx);
    }
    else {
        Alarm.disable(idx);
    }
}

void alarmTriggered() {

    lastFedTime = now();

    //digitalWrite(LED_BUILTIN, ON);

    int idx = Alarm.getTriggeredAlarmId(); 
    if (idx < NUM_TIMES) {
        DBG_OUTPUT_PORT.println("Triggered alarm: " + String(idx));
        saveLastFedJSON(lastFedTime, amounts[idx]);
        dispenseFood(amounts[idx]);
    }
}

String timeToJSON(time_t t) {
    String r = String(); 

	t = t - getDstOffset(t); 
    r += "\"";
    r += String(year(t)) + "-";
    r += (month(t) < 10 ? "0" : "") + String(month(t)) + "-";
    r += (day(t) < 10 ? "0" : "") + String(day(t)) + "T";
    r += (hour(t) < 10 ? "0" : "") + String(hour(t)) + ":" + (minute(t) < 10 ? "0" : "") + String(minute(t));
    r += "\"";

    return r;
}

void feedNowHttp() {
    int lastFedAmount = server.arg("amount").toInt(); 

    lastFedTime = now();

    if (server.args() == 0) {
        DBG_OUTPUT_PORT.println("Error getting data"); 
        return;
    }

    String m = saveLastFedJSON(lastFedTime, lastFedAmount);

    server.send(200, "text/plain", m); 

    dispenseFood(server.arg("amount").toInt()); // move 120 * amount degrees forward

	redirect(); 
}

String saveLastFedJSON(time_t time, int amt) {
    String msg = ""; 

    msg += "{ \"feed-amount\":" + String(amt) + ",";
    msg += "\"feed-time\":";
    msg += timeToJSON(lastFedTime);

    msg += ",\"rotation-speed\":" + String(stepper.maxSpeed()); 
    msg += ",\"rotation-dir\":" + String(rotationDirection);
    msg += "}";

    File f = SPIFFS.open("/lastfed.json", "w");
    f.print(msg);
    f.close();
    DBG_OUTPUT_PORT.println("Fed at " + String(hour(lastFedTime - getDstOffset(time))) + ":" + String(minute(lastFedTime)));
    return msg;
}

void saveSettings() {
    Serial.println("Saving settings."); 

    int max = server.arg("speed").toInt(); 
    int dir = server.arg("dir").toInt(); 
    if (max > 600) max = 600; 
    if (max < 100) max = 100; 

    stepper.setMaxSpeed(max); 
    rotationDirection = dir; 
    
    saveLastFedJSON(lastFedTime, lastFedAmt);
}

void dispenseFood(int amt) {
    DBG_OUTPUT_PORT.println("Feeding amount: " + String(amt));
    stepper.moveTo(stepper.targetPosition() + (rotationDirection == 0 ? 1 : -1 ) * deg120 * amt);
}

void rotateAmount() {
    String amt = server.arg("amount");
    DBG_OUTPUT_PORT.println("Rotating: " + amt);
    int amount = amt.toInt(); 
    stepper.move((rotationDirection == 0 ? 1 : -1) * amount);
    // fix empty response
    server.send(200, "text/plain", "");
}

void redirect() {
    server.sendHeader("Location", String("/feed.htm"), true);
    server.send(302, "text/plain", "");
}

void currentTime() {
    server.send(200, "text/plain", String(now()));
    return;
}

void syncTime() {

}

void unjam() {
	unjamStep = 10; 
	server.send(200, "text/plain", ""); 
}

void setupFs(void) {
    SPIFFS.begin();
    {
        Dir dir = SPIFFS.openDir("/");
        while (dir.next()) {
            String fileName = dir.fileName();
            size_t fileSize = dir.fileSize();
            DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
        }
        DBG_OUTPUT_PORT.printf("\n");
    }

    DBG_OUTPUT_PORT.print("Open http://");
    DBG_OUTPUT_PORT.print(serverName);
    DBG_OUTPUT_PORT.println(".local/edit to see the file browser");


    //SERVER INIT
    //list directory
    server.on("/list", HTTP_GET, handleFileList);
    //load editor
    server.on("/edit", HTTP_GET, []() {
        if (!server.authenticate(www_user, www_pass))
        {
            return server.requestAuthentication(); 
        }
        if (!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
    });

    //create file
    server.on("/edit", HTTP_PUT, handleFileCreate);
    //delete file
    server.on("/edit", HTTP_DELETE, handleFileDelete);
    //first callback is called after the request has ended with all parsed arguments
    //second callback handles file uploads at that location
    server.on("/edit", HTTP_POST, []() { server.send(200, "text/plain", ""); }, handleFileUpload);

    //called when the url is not defined here
    //use it to load content from SPIFFS
    server.onNotFound([]() {
        if (!handleFileRead(server.uri()))
            server.send(404, "text/plain", "FileNotFound");
    });

    //get heap status, analog input value and all GPIO statuses in one json call
    //server.on("/all", HTTP_GET, []() {
    //    String json = "{";
    //    json += "\"heap\":" + String(ESP.getFreeHeap());
    //    json += ", \"analog\":" + String(analogRead(A0));
    //    json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    //    json += "}";
    //    server.send(200, "text/json", json);
    //    json = String();
    //});

    server.on("/", handleRoot);
    server.on("/times", handleTimesList);
    server.on("/update-times", HTTP_POST, updateTimes);
    server.on("/update-times", HTTP_GET, redirect);
    server.on("/feednow", HTTP_GET, feedNowHttp);
    server.on("/rotate", HTTP_GET, rotateAmount);
    server.on("/saveSettings", HTTP_GET, saveSettings);
    server.on("/syncTime", HTTP_GET, syncTime);
    server.on("/currentTime", HTTP_GET, currentTime); 
	server.on("/unjam", HTTP_GET, unjam); 

    DBG_OUTPUT_PORT.println("HTTP server started");

}
