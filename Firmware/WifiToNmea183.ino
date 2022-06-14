#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESP_EEPROM.h>

#define TRIGGER_PIN 4
#define Button_pin 13
#define Portal_pin 12
#define Connected_pin 14

// wifimanager can run in a blocking mode or a non blocking mode
// Be sure to know how to process loops with no delay() if using non blocking
bool wm_nonblocking = false; // change to true to use non blocking

WiFiManager wm; // global wm instance
WiFiManagerParameter ip; // global param ( for non blocking w params )
WiFiManagerParameter port;

int customFieldLength = 40;

String wind_ip;
String wind_port;
bool changes_made = false;
bool first_run = true;
IPAddress server;
WiFiClient client;
char charBuf[50];
char eeprom_ip[50];
String eeprom_port;
unsigned long tik = 0;
const unsigned long timeout = 10000;
bool tik_first = true;

void setup() {
	//WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
	Serial.begin(4800);
	EEPROM.begin(200);
	Serial.setDebugOutput(false);
	delay(3000);
	Serial.println("\n Starting");

	pinMode(TRIGGER_PIN, INPUT_PULLUP);
	pinMode(Button_pin, OUTPUT);
	pinMode(Portal_pin, OUTPUT);
	pinMode(Connected_pin, OUTPUT);
	digitalWrite(Button_pin, LOW);
	digitalWrite(Portal_pin, LOW);
	digitalWrite(Connected_pin, LOW);

	// wm.resetSettings(); // wipe settings

	if (wm_nonblocking)
		wm.setConfigPortalBlocking(false);

	// add a custom input field

	new (&ip) WiFiManagerParameter("ip",
			"Wind instrument ip adress (Default 192.168.10.1 if connected to instrument hotspot)",
			"", customFieldLength, "placeholder=\"\"");
	new (&port) WiFiManagerParameter("port",
			"Wind instrument server port(Default 6666)", "", customFieldLength,
			"placeholder=\"\"");

	// test custom html input type(checkbox)
	//new (&check) WiFiManagerParameter("check", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\" type=\"checkbox\""); // custom html type

	// test custom html(radio)
	//const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
	//new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input

	wm.addParameter(&ip);
	wm.addParameter(&port);
	wm.setSaveParamsCallback(saveParamCallback);

	// custom menu via array or vector
	//
	// menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
	// const char* menu[] = {"wifi","info","param","sep","restart","exit"};
	// wm.setMenu(menu,6);
	std::vector<const char*> menu = { "wifi", "info", "param", "sep", "restart",
			"exit" };
	wm.setMenu(menu);

	// set dark theme
	wm.setClass("invert");

	//set static ip
	// wm.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // set static ip,gw,sn
	// wm.setShowStaticFields(true); // force show static ip fields
	// wm.setShowDnsFields(true);    // force show dns field always

	wm.setConnectTimeout(20); // how long to try to connect for before continuing
	//wm.setConfigPortalTimeout(30); // auto close configportal after n seconds
	// wm.setCaptivePortalEnable(false); // disable captive portal redirection
	// wm.setAPClientCheck(true); // avoid timeout if client connected to softap

	// wifi scan settings
	// wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names (true)
	// wm.setMinimumSignalQuality(20);  // set min RSSI (percentage) to show in scans, null = 8%
	// wm.setShowInfoErase(false);      // do not show erase button on info page
	// wm.setScanDispPerc(true);       // show RSSI as percentage not graph icons

	// wm.setBreakAfterConfig(true);   // always exit configportal even if wifi save fails

	EEPROM.get(0, eeprom_ip);
	EEPROM.get(100, eeprom_port);
	EEPROM.end();
	//Serial.println(eeprom_ip);
	//Serial.println(eeprom_port);

	bool res;
	// res = wm.autoConnect(); // auto generated AP name from chipid
	digitalWrite(Portal_pin, HIGH);
	res = wm.autoConnect("Wifi2Nmea183"); // anonymous ap
	//res = wm.autoConnect("Wifi2Nmea183", "12345678"); // password protected ap

	if (!res) {
		Serial.println("Failed to connect or hit timeout");
		digitalWrite(Portal_pin, LOW);
		// ESP.restart();
	} else {
		//if you get here you have connected to the WiFi
		Serial.println("connected...yeey :)");
		digitalWrite(Portal_pin, LOW);
	}

}

void checkButton() {
	// check for button press
	if (digitalRead(TRIGGER_PIN) == LOW) {
		// poor mans debounce/press-hold, code not ideal for production
		delay(50);
		if (digitalRead(TRIGGER_PIN) == LOW) {
			Serial.println("Button Pressed");
			digitalWrite(Button_pin, HIGH);
			digitalWrite(Portal_pin, LOW);
			digitalWrite(Connected_pin, LOW);
			// still holding button for 8000 ms, reset settings, code not idea for production
			delay(8000); // reset delay hold
			if (digitalRead(TRIGGER_PIN) == LOW) {
				Serial.println("Button Held");
				Serial.println("Erasing Config and EEPROM, restarting");
				wm.resetSettings();
				EEPROM.begin(200);
				for (int i = 0; i <= 200; i++) {
					EEPROM.write(i, 0);
				}
				boolean ok1 = EEPROM.commit();
				Serial.println((ok1) ? "commit OK" : "Commit failed");
				EEPROM.end();
				digitalWrite(Button_pin, LOW);
				digitalWrite(Portal_pin, LOW);
				digitalWrite(Connected_pin, LOW);
				for (int i = 0; i < 6; ++i) {
					digitalWrite(Button_pin, !digitalRead(Button_pin));
					digitalWrite(Portal_pin, !digitalRead(Portal_pin));
					digitalWrite(Connected_pin, !digitalRead(Connected_pin));
					delay(500);
				}
				ESP.restart();

			}

			// start portal w delay
			Serial.println("Starting config portal");
			client.stop();
			first_run = true ;
			//wm.setConfigPortalTimeout(120);
			digitalWrite(Button_pin, LOW);
			digitalWrite(Portal_pin, HIGH);
			if (!wm.startConfigPortal("Wifi2Nmea183")) {
				Serial.println("failed to connect or hit timeout");
				digitalWrite(Portal_pin, LOW);
				delay(3000);
				ESP.restart();
			} else {
				//if you get here you have connected to the WiFi
				Serial.println("connected...yeey :)");
				digitalWrite(Portal_pin, LOW);
			}
		}
	}

}

String getParam(String name) {
//read parameter from server, for customhtml input
	String value;
	if (wm.server->hasArg(name)) {
		value = wm.server->arg(name);
	}
	return value;
}

void switch_server() {
	client.stop();
	delay(1000);
	EEPROM.begin(200);
	EEPROM.get(0, eeprom_ip);
	EEPROM.get(100, eeprom_port);
	EEPROM.end();
	Serial.println(eeprom_ip);
	Serial.println(eeprom_port);

	first_run = true;
	changes_made = false;

}

void saveParamCallback() {
	String temp_ip = getParam("ip");
	String temp_port = getParam("port");
	temp_ip.toCharArray(charBuf, temp_ip.length() + 1);
	Serial.println(charBuf);
	Serial.println(temp_port);
	EEPROM.begin(200);
	EEPROM.put(0, charBuf);
	EEPROM.put(100, temp_port);
	boolean ok1 = EEPROM.commit();
	Serial.println((ok1) ? "commit OK" : "Commit failed");
	EEPROM.end();
	changes_made = true;

}

void loop() {

	if (wm_nonblocking)
		wm.process(); // avoid delays() in loop when non-blocking and other long running code

	checkButton();
// put your main code here, to run repeatedly:
	if (WiFi.status() == WL_CONNECTED) {
		if (first_run) {
			digitalWrite(Portal_pin, LOW);
			Serial.println("Initial Delay");
			delay(5000);
			Serial.print("Connecting to:");

			if (server.fromString(eeprom_ip) && eeprom_port.toInt() > 0
					&& eeprom_port.toInt() <= 65536) {
				Serial.print(server);
				Serial.print(" : ");
				Serial.print(eeprom_port.toInt());
				client.setTimeout(500);
				while (!client.connect(server, eeprom_port.toInt())) {

					Serial.print(".");
					digitalWrite(Connected_pin, !digitalRead(Connected_pin));
					checkButton();

				}
				Serial.println(".");
				Serial.println("CONNECTED");
				digitalWrite(Connected_pin, HIGH);
				first_run = false;

			} else {
				digitalWrite(Connected_pin, LOW);
				Serial.println("Not valid IP or port");
				Serial.println("Starting config portal");
				digitalWrite(Portal_pin, HIGH);
				if (!wm.startConfigPortal("Wifi2Nmea183")) {
					digitalWrite(Portal_pin, LOW);
					Serial.println("failed to connect or hit timeout");
					delay(3000);
					ESP.restart();
				} else {
					//if you get here you have connected to the WiFi
					Serial.println("connected...yeey :)");
					digitalWrite(Portal_pin, LOW);

				}
			}
		}

		if (changes_made) {

			switch_server();

		}

		if (client.connected()) {
			digitalWrite(Connected_pin, HIGH);
			if (client.available()) {
				String line = client.readStringUntil('\r');
				Serial.print(line);
				tik_first = true;
			} else {
				if (tik_first) {
					tik = millis();
					tik_first = false;
				}
				if (millis() - tik > timeout) {
					first_run = true;
					digitalWrite(Connected_pin, LOW);
					client.stop();
					tik_first = true;
				}

			}

		} else {
			digitalWrite(Connected_pin, LOW);
			first_run = true;
		}
	} else {

		digitalWrite(Connected_pin, !digitalRead(Connected_pin));
		delay(100);
	}
}
