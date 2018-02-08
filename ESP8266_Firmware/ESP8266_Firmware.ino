#include <FSWebServerLib.h>
#include <AsyncMqttClient.h>
#include <ESPAsyncTCP.h>
#include <Bounce2.h>
#include <JSONtoSPIFFS.h>

#include "Modul_Info_1O.h"

#define CONFIGFILE_MQTT "config_MQTT.json"
#define CONFIGFILE_IO "config_IO.json"

//************************ MQTT Client ***************************
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

//************************ Event Source ***************************
AsyncEventSource _evsUser = AsyncEventSource("/userevents");
Ticker sendMQTTStateTimer;

//************************ IOs ***************************
Bounce debounceIn1 = Bounce();
Bounce debounceIn2 = Bounce();

//************************ Config ***************************
JSONtoSPIFFS Config;
#define MQTT_CONF 1
#define IO_CONF 2

typedef struct {
	String topic = "";
	String payloadOn = "";
	String payloadOff = "";
	int qos = 0;
	bool retain = false;
} strMQTTMsg;

typedef struct {
	bool enabled = false;
	IPAddress host = IPAddress(192, 168, 1, 100);
	int port = 1883;
	bool auth = false;
	String username = "";
	String password = "";
	String devID = "ESP_Default";
	int keepAlive = 60;
	bool lastWillEnable = false;
	strMQTTMsg lastWill;
	bool sendConMsg = false;
	strMQTTMsg conMsg;
	bool FWUpdateEN;
	strMQTTMsg updCmd;
	strMQTTMsg updState;
} strMQTTConfig;

typedef struct {
	bool enableSubscribe = false;
	strMQTTMsg cmd;
	bool enablePublish = false;
	strMQTTMsg state;
	bool initState = false; //true = Output is high after startup
} strOutputConfig;

typedef struct {
	bool enablePublish = false;
	strMQTTMsg cmd;
	int ctrlOutput1RisingEdge = 0; //0 = Disabled; 1 = On; 2 = Off; 3 = Toggle
	int ctrlOutput1FallingEdge = 0; //0 = Disabled; 1 = On; 2 = Off; 3 = Toggle
	int ctrlOutput2RisingEdge = 0; //0 = Disabled; 1 = On; 2 = Off; 3 = Toggle
	int ctrlOutput2FallingEdge = 0; //0 = Disabled; 1 = On; 2 = Off; 3 = Toggle
} strInputConfig;

//******************************************************************************************
// global variables
//******************************************************************************************
bool output[2], output_old[2], lockOutput[2] = { false,false };
unsigned long lockUntilMs[2];
strMQTTConfig MqttConf;
strOutputConfig OutConf[2];
strInputConfig InConf[2];

//******************************************************************************************
// SETUP
//******************************************************************************************
void setup() {
	Serial.begin(115200);
	//Starte Filesystem
	SPIFFS.begin();
	Config.begin(&SPIFFS);
	//Load Config
	loadAllConfig();
	IOSetup();
	//************************ Wifi Callbacks ***************************
	wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
	wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
	//************************ Setup ***************************
	mqttSetup();
	webserverSetup();
}

//******************************************************************************************
// LOOP
//******************************************************************************************
void loop() {
	//************************ Inputs ***************************
	evaluateInput(1, debounceIn1);
	evaluateInput(2, debounceIn2);

	//************************ Outputs ***************************
	if (output[0] != output_old[0]) {
		digitalWrite(OUTPUT1_PIN, output[0]);
		sendOutputState(1);
		output_old[0] = output[0];
		lockOutput[0] = true;
		lockUntilMs[0] = millis() + 500;
	}
	if (OUTPUT2_PIN >= 0) {
		if (output[1] != output_old[1]) {
			digitalWrite(OUTPUT2_PIN, output[1]);
			sendOutputState(2);
			output_old[1] = output[1];
			lockOutput[1] = true;
			lockUntilMs[1] = millis() + 500;
		}
	}
	//Unlock
	if (lockOutput[0] && (lockUntilMs[0] - millis() <= 0)) lockOutput[0] = false;
	if (lockOutput[1] && (lockUntilMs[1] - millis() <= 0)) lockOutput[1] = false;

	//************************ OTA ***************************
	ESPHTTPServer.handle();
}

//******************************************************************************************
// local functions
//******************************************************************************************
void connectMQTT() {
	mqttClient.connect();
}

void disconnectMQTT() {
	//Send disconnected Message
	if (MqttConf.lastWillEnable) mqttClient.publish(MqttConf.lastWill.topic.c_str(), MqttConf.lastWill.qos, MqttConf.lastWill.retain, MqttConf.lastWill.payloadOn.c_str());
	//disconnect
	mqttClient.disconnect();
}

void evaluateInput(int x, Bounce &debouncer) {
	int tmpInNr = x - 1;
	if (tmpInNr < 0 || tmpInNr > 1) return;
	debouncer.update();
	//Rising Edge
	if (debouncer.rose()) {
		switch (InConf[tmpInNr].ctrlOutput1RisingEdge) {
		case 1: //ON
			if (!lockOutput[0]) output[0] = true;
			break;
		case 2: //OFF
			if (!lockOutput[0]) output[0] = false;
			break;
		case 3: //Toggle
			if (!lockOutput[0]) output[0] = !output[0];
			break;
		default:
			break;
		}
		switch (InConf[tmpInNr].ctrlOutput2RisingEdge) {
		case 1: //ON
			if (!lockOutput[1]) output[1] = true;
			break;
		case 2: //OFF
			if (!lockOutput[1]) output[1] = false;
			break;
		case 3: //Toggle
			if (!lockOutput[1]) output[1] = !output[1];
			break;
		default:
			break;
		}
		// Publish to MQTT
		if (!MqttConf.enabled) return;
		int tmpInNr = x - 1;
		if (tmpInNr < 0 || tmpInNr > 1) return;
		if (InConf[tmpInNr].enablePublish) {
			mqttClient.publish(InConf[tmpInNr].cmd.topic.c_str(), InConf[tmpInNr].cmd.qos, InConf[tmpInNr].cmd.retain, InConf[tmpInNr].cmd.payloadOn.c_str());
		}
	}
	//Falling Edge
	else if (debouncer.fell()) {
		switch (InConf[tmpInNr].ctrlOutput1FallingEdge) {
		case 1: //ON
			if (!lockOutput[0]) output[0] = true;
			break;
		case 2: //OFF
			if (!lockOutput[0]) output[0] = false;
			break;
		case 3: //Toggle
			if (!lockOutput[0]) output[0] = !output[0];
			break;
		default:
			break;
		}
		switch (InConf[tmpInNr].ctrlOutput2FallingEdge) {
		case 1: //ON
			if (!lockOutput[1]) output[1] = true;
			break;
		case 2: //OFF
			if (!lockOutput[1]) output[1] = false;
			break;
		case 3: //Toggle
			if (!lockOutput[1]) output[1] = !output[1];
			break;
		default:
			break;
		}
		// Publish to MQTT
		if (!MqttConf.enabled) return;
		int tmpInNr = x - 1;
		if (tmpInNr < 0 || tmpInNr > 1) return;
		if (InConf[tmpInNr].enablePublish) {
			mqttClient.publish(InConf[tmpInNr].cmd.topic.c_str(), InConf[tmpInNr].cmd.qos, InConf[tmpInNr].cmd.retain, InConf[tmpInNr].cmd.payloadOff.c_str());
		}
	}
}

void sendOutputState(int x) {
	// Send Event for Webserver
	if (_evsUser.count() > 0) {
		String data = "{";
		data += "\"output1\":\"" + String(output[0] ? 1 : 0) + "\",";
		if (OUTPUT2_PIN >= 0) data += "\"output2\":\"" + String(output[1] ? 1 : 0) + "\"";
		else data += "\"output2\":\"0\"";
		data += "}\r\n";
		_evsUser.send(data.c_str(), "outputs",0,500);
	}
	// Publish to MQTT
	if (!MqttConf.enabled) return;
	int tmpOutNr = x - 1;
	if (tmpOutNr < 0 || tmpOutNr > 1) return;
	if (OutConf[tmpOutNr].enablePublish) {
		String value = (output[tmpOutNr] ? OutConf[tmpOutNr].state.payloadOn : OutConf[tmpOutNr].state.payloadOff);
		mqttClient.publish(OutConf[tmpOutNr].state.topic.c_str(), OutConf[tmpOutNr].state.qos, OutConf[tmpOutNr].state.retain, value.c_str());
		value = "";
	}
}

void sendMqttState() {
	_evsUser.send((mqttClient.connected() ? "1" : "0"), "mqttState",0,500);
}

bool loadConfig(int conf) {
	bool okay = true;
	switch (conf) {
	case MQTT_CONF:
	{
		if (Config.loadConfigFile(CONFIGFILE_MQTT)) {
			okay &= Config.getValue("enabled", MqttConf.enabled);
			okay &= Config.getValue("host", MqttConf.host);
			okay &= Config.getValue("port", MqttConf.port);
			okay &= Config.getValue("auth", MqttConf.auth);
			okay &= Config.getValue("username", MqttConf.username);
			okay &= Config.getValue("password", MqttConf.password);
			okay &= Config.getValue("devID", MqttConf.devID);
			okay &= Config.getValue("keepAlive", MqttConf.keepAlive);
			okay &= Config.getValue("lastWillEnable", MqttConf.lastWillEnable);
			okay &= Config.getValue("lastWill.topic", MqttConf.lastWill.topic);
			okay &= Config.getValue("lastWill.payload", MqttConf.lastWill.payloadOn);
			okay &= Config.getValue("lastWill.qos", MqttConf.lastWill.qos);
			okay &= Config.getValue("lastWill.retain", MqttConf.lastWill.retain);
			okay &= Config.getValue("sendConMsg", MqttConf.sendConMsg);
			okay &= Config.getValue("conMsg.topic", MqttConf.conMsg.topic);
			okay &= Config.getValue("conMsg.payload", MqttConf.conMsg.payloadOn);
			okay &= Config.getValue("conMsg.qos", MqttConf.conMsg.qos);
			okay &= Config.getValue("conMsg.retain", MqttConf.conMsg.retain);
			okay &= Config.getValue("FWUpdateEN", MqttConf.FWUpdateEN);
			okay &= Config.getValue("updCmd.topic", MqttConf.updCmd.topic);
			okay &= Config.getValue("updCmd.payloadVer", MqttConf.updCmd.payloadOn);
			okay &= Config.getValue("updCmd.payloadUpd", MqttConf.updCmd.payloadOff);
			okay &= Config.getValue("updCmd.qos", MqttConf.updCmd.qos);
			okay &= Config.getValue("updState.topic", MqttConf.updState.topic);
			okay &= Config.getValue("updState.qos", MqttConf.updState.qos);
			okay &= Config.getValue("updState.retain", MqttConf.updState.retain);
			okay &= Config.closeConfigFile();
		}
	}
	break;
	case IO_CONF:
	{
		if (Config.loadConfigFile(CONFIGFILE_IO)) {
			for (int i = 1; i <= 2; i++) {
				okay &= Config.getValue("Out" + String(i) + "_enableSubscribe", OutConf[i - 1].enableSubscribe);
				okay &= Config.getValue("Out" + String(i) + "_cmd.topic", OutConf[i - 1].cmd.topic);
				okay &= Config.getValue("Out" + String(i) + "_cmd.payloadOn", OutConf[i - 1].cmd.payloadOn);
				okay &= Config.getValue("Out" + String(i) + "_cmd.payloadOff", OutConf[i - 1].cmd.payloadOff);
				okay &= Config.getValue("Out" + String(i) + "_cmd.qos", OutConf[i - 1].cmd.qos);
				okay &= Config.getValue("Out" + String(i) + "_enablePublish", OutConf[i - 1].enablePublish);
				okay &= Config.getValue("Out" + String(i) + "_state.topic", OutConf[i - 1].state.topic);
				okay &= Config.getValue("Out" + String(i) + "_state.payloadOn", OutConf[i - 1].state.payloadOn);
				okay &= Config.getValue("Out" + String(i) + "_state.payloadOff", OutConf[i - 1].state.payloadOff);
				okay &= Config.getValue("Out" + String(i) + "_state.qos", OutConf[i - 1].state.qos);
				okay &= Config.getValue("Out" + String(i) + "_state.retain", OutConf[i - 1].state.retain);
				okay &= Config.getValue("Out" + String(i) + "_initState", OutConf[i - 1].initState);

				okay &= Config.getValue("In" + String(i) + "_enablePublish", InConf[i - 1].enablePublish);
				okay &= Config.getValue("In" + String(i) + "_cmd.topic", InConf[i - 1].cmd.topic);
				okay &= Config.getValue("In" + String(i) + "_cmd.payloadOn", InConf[i - 1].cmd.payloadOn);
				okay &= Config.getValue("In" + String(i) + "_cmd.payloadOff", InConf[i - 1].cmd.payloadOff);
				okay &= Config.getValue("In" + String(i) + "_cmd.qos", InConf[i - 1].cmd.qos);
				okay &= Config.getValue("In" + String(i) + "_cmd.retain", InConf[i - 1].cmd.retain);
				okay &= Config.getValue("In" + String(i) + "_ctrlOutput1RisingEdge", InConf[i - 1].ctrlOutput1RisingEdge);
				okay &= Config.getValue("In" + String(i) + "_ctrlOutput1FallingEdge", InConf[i - 1].ctrlOutput1FallingEdge);
				okay &= Config.getValue("In" + String(i) + "_ctrlOutput2RisingEdge", InConf[i - 1].ctrlOutput2RisingEdge);
				okay &= Config.getValue("In" + String(i) + "_ctrlOutput2FallingEdge", InConf[i - 1].ctrlOutput2FallingEdge);
			}
			okay &= Config.closeConfigFile();
		}
	}
	break;
	}
	return okay;
}

bool saveConfig(int conf) {
	bool okay = true;
	switch (conf) {
	case MQTT_CONF:
	{
		if (Config.loadConfigFile(CONFIGFILE_MQTT)) {
			okay &= Config.setValue("enabled", MqttConf.enabled);
			okay &= Config.setValue("host", MqttConf.host);
			okay &= Config.setValue("port", MqttConf.port);
			okay &= Config.setValue("auth", MqttConf.auth);
			okay &= Config.setValue("username", MqttConf.username);
			okay &= Config.setValue("password", MqttConf.password);
			okay &= Config.setValue("devID", MqttConf.devID);
			okay &= Config.setValue("keepAlive", MqttConf.keepAlive);
			okay &= Config.setValue("lastWillEnable", MqttConf.lastWillEnable);
			okay &= Config.setValue("lastWill.topic", MqttConf.lastWill.topic);
			okay &= Config.setValue("lastWill.payload", MqttConf.lastWill.payloadOn);
			okay &= Config.setValue("lastWill.qos", MqttConf.lastWill.qos);
			okay &= Config.setValue("lastWill.retain", MqttConf.lastWill.retain);
			okay &= Config.setValue("sendConMsg", MqttConf.sendConMsg);
			okay &= Config.setValue("conMsg.topic", MqttConf.conMsg.topic);
			okay &= Config.setValue("conMsg.payload", MqttConf.conMsg.payloadOn);
			okay &= Config.setValue("conMsg.qos", MqttConf.conMsg.qos);
			okay &= Config.setValue("conMsg.retain", MqttConf.conMsg.retain);
			okay &= Config.setValue("FWUpdateEN", MqttConf.FWUpdateEN);
			okay &= Config.setValue("updCmd.topic", MqttConf.updCmd.topic);
			okay &= Config.setValue("updCmd.payloadVer", MqttConf.updCmd.payloadOn);
			okay &= Config.setValue("updCmd.payloadUpd", MqttConf.updCmd.payloadOff);
			okay &= Config.setValue("updCmd.qos", MqttConf.updCmd.qos);
			okay &= Config.setValue("updState.topic", MqttConf.updState.topic);
			okay &= Config.setValue("updState.qos", MqttConf.updState.qos);
			okay &= Config.setValue("updState.retain", MqttConf.updState.retain);
			okay &= Config.saveConfigFile();
		}
		else return false;
	}
	break;
	case IO_CONF:
	{
		if (Config.loadConfigFile(CONFIGFILE_IO)) {
			for (int i = 1; i <= 2; i++) {
				okay &= Config.setValue("Out" + String(i) + "_enableSubscribe", OutConf[i - 1].enableSubscribe);
				okay &= Config.setValue("Out" + String(i) + "_cmd.topic", OutConf[i - 1].cmd.topic);
				okay &= Config.setValue("Out" + String(i) + "_cmd.payloadOn", OutConf[i - 1].cmd.payloadOn);
				okay &= Config.setValue("Out" + String(i) + "_cmd.payloadOff", OutConf[i - 1].cmd.payloadOff);
				okay &= Config.setValue("Out" + String(i) + "_cmd.qos", OutConf[i - 1].cmd.qos);
				okay &= Config.setValue("Out" + String(i) + "_enablePublish", OutConf[i - 1].enablePublish);
				okay &= Config.setValue("Out" + String(i) + "_state.topic", OutConf[i - 1].state.topic);
				okay &= Config.setValue("Out" + String(i) + "_state.payloadOn", OutConf[i - 1].state.payloadOn);
				okay &= Config.setValue("Out" + String(i) + "_state.payloadOff", OutConf[i - 1].state.payloadOff);
				okay &= Config.setValue("Out" + String(i) + "_state.qos", OutConf[i - 1].state.qos);
				okay &= Config.setValue("Out" + String(i) + "_state.retain", OutConf[i - 1].state.retain);
				okay &= Config.setValue("Out" + String(i) + "_initState", OutConf[i - 1].initState);

				okay &= Config.setValue("In" + String(i) + "_enablePublish", InConf[i - 1].enablePublish);
				okay &= Config.setValue("In" + String(i) + "_cmd.topic", InConf[i - 1].cmd.topic);
				okay &= Config.setValue("In" + String(i) + "_cmd.payloadOn", InConf[i - 1].cmd.payloadOn);
				okay &= Config.setValue("In" + String(i) + "_cmd.payloadOff", InConf[i - 1].cmd.payloadOff);
				okay &= Config.setValue("In" + String(i) + "_cmd.qos", InConf[i - 1].cmd.qos);
				okay &= Config.setValue("In" + String(i) + "_cmd.retain", InConf[i - 1].cmd.retain);
				okay &= Config.setValue("In" + String(i) + "_ctrlOutput1RisingEdge", InConf[i - 1].ctrlOutput1RisingEdge);
				okay &= Config.setValue("In" + String(i) + "_ctrlOutput1FallingEdge", InConf[i - 1].ctrlOutput1FallingEdge);
				okay &= Config.setValue("In" + String(i) + "_ctrlOutput2RisingEdge", InConf[i - 1].ctrlOutput2RisingEdge);
				okay &= Config.setValue("In" + String(i) + "_ctrlOutput2FallingEdge", InConf[i - 1].ctrlOutput2FallingEdge);
			}
			okay &= Config.saveConfigFile();
		}
		else return false;
	}
	break;
	}
	return okay;
}

void loadAllConfig() {
	if (!loadConfig(MQTT_CONF)) {
		saveConfig(MQTT_CONF);
	}
	if (!loadConfig(IO_CONF)) {
		saveConfig(IO_CONF);
	}
}

void saveAllConfig() {
	saveConfig(MQTT_CONF);
	saveConfig(IO_CONF);
}

//******************************************************************************************
// Callback functions
//******************************************************************************************
//************************ ESP Restart Callback ***************************
void onESPRestart() {
	if (MqttConf.enabled) {
		disconnectMQTT();
	}
}
//************************ WiFi Callbacks ***************************
void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
	mqttReconnectTimer.detach();
}
void onWifiConnect(const WiFiEventStationModeGotIP& event) {
	if (MqttConf.enabled) connectMQTT();
}
//************************ MQTT Callbacks ***************************
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
	if (MqttConf.enabled) mqttReconnectTimer.once(5, connectMQTT);
	sendMqttState();
}
void onMqttConnect(bool sessionPresent) {
	mqttReconnectTimer.detach();
	//Send connected Message
	if (MqttConf.sendConMsg) mqttClient.publish(MqttConf.conMsg.topic.c_str(), MqttConf.conMsg.qos, MqttConf.conMsg.retain, MqttConf.conMsg.payloadOn.c_str());
	//Subscribe to Topics
	if (OutConf[0].enableSubscribe) mqttClient.subscribe(OutConf[0].cmd.topic.c_str(), OutConf[0].cmd.qos);
	if (OutConf[1].enableSubscribe) mqttClient.subscribe(OutConf[1].cmd.topic.c_str(), OutConf[1].cmd.qos);
	if (MqttConf.FWUpdateEN) mqttClient.subscribe(MqttConf.updCmd.topic.c_str(), MqttConf.updCmd.qos);
	//Send Output State
	sendOutputState(1);
	sendOutputState(2);
	//Send MQTT State
	sendMqttState();
}
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
	String _payload = String(payload).substring(0, len);
	Serial.println();
	//Evaluate publishes here
	//Output 1
	if (OutConf[0].enableSubscribe && (String(topic) == OutConf[0].cmd.topic)) {
		if (String(_payload) == OutConf[0].cmd.payloadOff) output[0] = false;
		else if (String(_payload) == OutConf[0].cmd.payloadOn) output[0] = true;
	}
	//Output 2
	if (OutConf[1].enableSubscribe && (String(topic) == OutConf[1].cmd.topic)) {
		if (String(_payload) == OutConf[1].cmd.payloadOff) output[1] = false;
		else if (String(_payload) == OutConf[1].cmd.payloadOn) output[1] = true;
	}
	//Update Firmware
	if (MqttConf.FWUpdateEN && (String(topic) == MqttConf.updCmd.topic)) {
		if (String(_payload) == MqttConf.updCmd.payloadOn) {
			ESPHTTPServer.checkUpdate();
			mqttClient.publish(MqttConf.updState.topic.c_str(), MqttConf.updState.qos, MqttConf.updState.retain, "CH.ACK");
		}
		else if (String(_payload) == MqttConf.updCmd.payloadOff) {
			if (!ESPHTTPServer.runUpdate()) mqttClient.publish(MqttConf.updState.topic.c_str(), MqttConf.updState.qos, MqttConf.updState.retain, "UP.CUF");
			else mqttClient.publish(MqttConf.updState.topic.c_str(), MqttConf.updState.qos, MqttConf.updState.retain, "UP.ACK");
		}
	}
}
void onMqttFWUpdateCb(bool upd, bool error, bool updatePossible, enumFirmwareLastError lastError, const String &serverVersion, const uint32_t &updateSize) {
	if (MqttConf.FWUpdateEN) {
		String pl = "";
		if (upd) {
			if (error) pl = "UP.ERR_" + String(lastError);
			else pl = "UP.OKA";
		}
		else {
			if (error) pl = "CH.ERR_" + String(lastError);
			else if (updatePossible) pl = "CH.UPP_" + serverVersion + "_" + VERSION;
			else pl = "CH.NUP_" + serverVersion + "_" + VERSION;
		}
		mqttClient.publish(MqttConf.updState.topic.c_str(), MqttConf.updState.qos, MqttConf.updState.retain, pl.c_str());
	}
}

//************************ HTTP Callbacks ***************************
void onHTTP_POST(AsyncWebServerRequest *request) {
	if (request->url() == "/post/output") {
		evaluate_output_post_request(request);
	}
	else if (request->url() == "/post/mqtt") {
		evaluate_mqtt_post_request(request);
	}
	else if (request->url() == "/post/io") {
		evaluate_io_post_request(request);
	}
	else {
		request->send(404, "text/plain", "FileNotFound");
	}
}

void onHTTP_REST(AsyncWebServerRequest *request) {
	if (request->url() == "/rest/mqtt") {
		String values = "enabled|" + String(MqttConf.enabled ? "checked" : "") + "|chk\n";
		values += "host|" + MqttConf.host.toString() + "|input\n";
		values += "port|" + String(MqttConf.port) + "|input\n";
		values += "auth|" + String(MqttConf.auth ? "checked" : "") + "|chk\n";
		values += "username|" + MqttConf.username + "|input\n";
		values += "password|" + MqttConf.password + "|input\n";
		values += "devID|" + MqttConf.devID + "|input\n";
		values += "keepAlive|" + String(MqttConf.keepAlive) + "|input\n";
		values += "lastWillEnable|" + String(MqttConf.lastWillEnable ? "checked" : "") + "|chk\n";
		values += "lastWill.topic|" + MqttConf.lastWill.topic + "|input\n";
		values += "lastWill.payload|" + MqttConf.lastWill.payloadOn + "|input\n";
		values += "lastWill.qos|" + String(MqttConf.lastWill.qos) + "|input\n";
		values += "lastWill.retain|" + String(MqttConf.lastWill.retain ? "checked" : "") + "|chk\n";
		values += "sendConMsg|" + String(MqttConf.sendConMsg ? "checked" : "") + "|chk\n";
		values += "conMsg.topic|" + MqttConf.conMsg.topic + "|input\n";
		values += "conMsg.payload|" + MqttConf.conMsg.payloadOn + "|input\n";
		values += "conMsg.qos|" + String(MqttConf.conMsg.qos) + "|input\n";
		values += "conMsg.retain|" + String(MqttConf.conMsg.retain ? "checked" : "") + "|chk\n";
		values += "FWUpdateEN|" + String(MqttConf.FWUpdateEN ? "checked" : "") + "|chk\n";
		values += "updCmd.topic|" + MqttConf.updCmd.topic + "|input\n";
		values += "updCmd.payloadVer|" + MqttConf.updCmd.payloadOn + "|input\n";
		values += "updCmd.payloadUpd|" + MqttConf.updCmd.payloadOff + "|input\n";
		values += "updCmd.qos|" + String(MqttConf.updCmd.qos) + "|input\n";
		values += "updState.topic|" + MqttConf.updState.topic + "|input\n";
		values += "updState.qos|" + String(MqttConf.updState.qos) + "|input\n";
		values += "updState.retain|" + String(MqttConf.updState.retain ? "checked" : "") + "|chk\n";
		request->send(200, "text/plain", values);
		values = "";
	}
	else if (request->url() == "/rest/io") {
		String values = "";
		for (uint8_t j = 1; j <= 2; j++) {
			values += "in" + String(j) + "enablePublish|" + String(InConf[j - 1].enablePublish ? "checked" : "") + "|chk\n";
			values += "in" + String(j) + "cmd.topic|" + InConf[j - 1].cmd.topic + "|input\n";
			values += "in" + String(j) + "cmd.payloadOn|" + InConf[j - 1].cmd.payloadOn + "|input\n";
			values += "in" + String(j) + "cmd.payloadOff|" + InConf[j - 1].cmd.payloadOff + "|input\n";
			values += "in" + String(j) + "cmd.qos|" + String(InConf[j - 1].cmd.qos) + "|input\n";
			values += "in" + String(j) + "cmd.retain|" + String(InConf[j - 1].cmd.retain ? "checked" : "") + "|chk\n";
			values += "in" + String(j) + "ctrlOutput1RisingEdge|" + InConf[j - 1].ctrlOutput1RisingEdge + "|input\n";
			values += "in" + String(j) + "ctrlOutput1FallingEdge|" + InConf[j - 1].ctrlOutput1FallingEdge + "|input\n";
			if (OUTPUT2_PIN >= 0) {
				values += "in" + String(j) + "ctrlOutput2RisingEdge|" + InConf[j - 1].ctrlOutput2RisingEdge + "|input\n";
				values += "in" + String(j) + "ctrlOutput2FallingEdge|" + InConf[j - 1].ctrlOutput2FallingEdge + "|input\n";
			}
			if (!(OUTPUT2_PIN >= 0) && (j > 1)) continue; //Wenn Ausgang 2 ausgeschaltet => überspringen
			values += "out" + String(j) + "enableSubscribe|" + String(OutConf[j - 1].enableSubscribe ? "checked" : "") + "|chk\n";
			values += "out" + String(j) + "cmd.topic|" + OutConf[j - 1].cmd.topic + "|input\n";
			values += "out" + String(j) + "cmd.payloadOn|" + OutConf[j - 1].cmd.payloadOn + "|input\n";
			values += "out" + String(j) + "cmd.payloadOff|" + OutConf[j - 1].cmd.payloadOff + "|input\n";
			values += "out" + String(j) + "cmd.qos|" + String(OutConf[j - 1].cmd.qos) + "|input\n";
			values += "out" + String(j) + "enablePublish|" + String(OutConf[j - 1].enablePublish ? "checked" : "") + "|chk\n";
			values += "out" + String(j) + "state.topic|" + OutConf[j - 1].state.topic + "|input\n";
			values += "out" + String(j) + "state.payloadOn|" + OutConf[j - 1].state.payloadOn + "|input\n";
			values += "out" + String(j) + "state.payloadOff|" + OutConf[j - 1].state.payloadOff + "|input\n";
			values += "out" + String(j) + "state.qos|" + String(OutConf[j - 1].state.qos) + "|input\n";
			values += "out" + String(j) + "state.retain|" + String(OutConf[j - 1].state.retain ? "checked" : "") + "|chk\n";
			values += "out" + String(j) + "initState|" + String(OutConf[j - 1].initState ? "1" : "0") + "|input\n";
		}
		request->send(200, "text/plain", values);
		values = "";
	}
	else {
		request->send(404, "text/plain", "FileNotFound");
	}
}

void onEvtConnect(AsyncEventSourceClient *client) {
	sendOutputState(-1);
	sendMQTTStateTimer.once_ms(100,sendMqttState);
}

void evaluate_output_post_request(AsyncWebServerRequest *request) {
	for (uint8_t i = 0; i < request->args(); i++) {
		if (request->argName(i) == "nr") {
			if (request->arg(i).toInt() == 1) {
				output[0] = !output[0];
			}
			else if (request->arg(i).toInt() == 2) {
				output[1] = !output[1];
			}
		}
	}
	request->send(200, "text/plain", "OK");
}

void evaluate_mqtt_post_request(AsyncWebServerRequest *request) {
	strMQTTConfig tmpMqttConf;
	bool okay = true;
	tmpMqttConf = MqttConf;
	if (request->args() == 26) {
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "enabled") {
				tmpMqttConf.enabled = ((request->arg(i) == "true") ? true : false);
				continue;
			}
			if (request->argName(i) == "host") {
				okay &= tmpMqttConf.host.fromString(request->arg(i));
				continue;
			}
			if (request->argName(i) == "port") {
				if (isNumeric(request->arg(i))) tmpMqttConf.port = request->arg(i).toInt();
				continue;
			}
			if (request->argName(i) == "auth") {
				tmpMqttConf.auth = ((request->arg(i) == "true") ? true : false);
				continue;
			}
			if (request->argName(i) == "username") {
				tmpMqttConf.username = request->arg(i);
				continue;
			}
			if (request->argName(i) == "password") {
				tmpMqttConf.password = request->arg(i);
				continue;
			}
			if (request->argName(i) == "devID") {
				tmpMqttConf.devID = request->arg(i);
				continue;
			}
			if (request->argName(i) == "keepAlive") {
				if (isNumeric(request->arg(i))) tmpMqttConf.keepAlive = request->arg(i).toInt();
				continue;
			}
			if (request->argName(i) == "lastWillEnable") {
				tmpMqttConf.lastWillEnable = ((request->arg(i) == "true") ? true : false);
				continue;
			}
			if (request->argName(i) == "lastWill.topic") {
				tmpMqttConf.lastWill.topic = request->arg(i);
				continue;
			}
			if (request->argName(i) == "lastWill.payload") {
				tmpMqttConf.lastWill.payloadOn = request->arg(i);
				continue;
			}
			if (request->argName(i) == "lastWill.qos") {
				if (isNumeric(request->arg(i))) tmpMqttConf.lastWill.qos = request->arg(i).toInt();
				continue;
			}
			if (request->argName(i) == "lastWill.retain") {
				tmpMqttConf.lastWill.retain = ((request->arg(i) == "true") ? true : false);
				continue;
			}
			if (request->argName(i) == "sendConMsg") {
				tmpMqttConf.sendConMsg = ((request->arg(i) == "true") ? true : false);
				continue;
			}
			if (request->argName(i) == "conMsg.topic") {
				tmpMqttConf.conMsg.topic = request->arg(i);
				continue;
			}
			if (request->argName(i) == "conMsg.payload") {
				tmpMqttConf.conMsg.payloadOn = request->arg(i);
				continue;
			}
			if (request->argName(i) == "conMsg.qos") {
				if (isNumeric(request->arg(i))) tmpMqttConf.conMsg.qos = request->arg(i).toInt();
				continue;
			}
			if (request->argName(i) == "conMsg.retain") {
				tmpMqttConf.conMsg.retain = ((request->arg(i) == "true") ? true : false);
				continue;
			}

			if (request->argName(i) == "FWUpdateEN") {
				tmpMqttConf.FWUpdateEN = ((request->arg(i) == "true") ? true : false);
				continue;
			}
			if (request->argName(i) == "updCmd.topic") {
				tmpMqttConf.updCmd.topic = request->arg(i);
				continue;
			}
			if (request->argName(i) == "updCmd.payloadVer") {
				tmpMqttConf.updCmd.payloadOn = request->arg(i);
				continue;
			}
			if (request->argName(i) == "updCmd.payloadUpd") {
				tmpMqttConf.updCmd.payloadOff = request->arg(i);
				continue;
			}
			if (request->argName(i) == "updCmd.qos") {
				if (isNumeric(request->arg(i))) tmpMqttConf.updCmd.qos = request->arg(i).toInt();
				continue;
			}
			if (request->argName(i) == "updState.topic") {
				tmpMqttConf.updState.topic = request->arg(i);
				continue;
			}
			if (request->argName(i) == "updState.qos") {
				if (isNumeric(request->arg(i))) tmpMqttConf.updState.qos = request->arg(i).toInt();
				continue;
			}
			if (request->argName(i) == "updState.retain") {
				tmpMqttConf.updState.retain = ((request->arg(i) == "true") ? true : false);
				continue;
			}
		}
		//**********Check Values
		okay &= checkRange(tmpMqttConf.port, 0, 65535);
		okay &= (checkRange(tmpMqttConf.username.length(), 0, 30) && checkName(tmpMqttConf.username));
		if (tmpMqttConf.auth) {
			okay &= (checkRange(tmpMqttConf.username.length(), 1, 30) && checkName(tmpMqttConf.username));
			okay &= (checkRange(tmpMqttConf.password.length(), 1, 30) && checkName(tmpMqttConf.password));
		}
		else {
			tmpMqttConf.username = MqttConf.username;
			tmpMqttConf.password = MqttConf.password;
		}
		okay &= (checkRange(tmpMqttConf.devID.length(), 1, 23) && checkName(tmpMqttConf.devID));
		okay &= checkRange(tmpMqttConf.keepAlive, 2, 300);
		if (tmpMqttConf.lastWillEnable) {
			okay &= checkTopic(tmpMqttConf.lastWill.topic);
			okay &= checkName(tmpMqttConf.lastWill.payloadOn);
			okay &= checkRange(tmpMqttConf.lastWill.qos, 0, 2);
		}
		else {
			tmpMqttConf.lastWill.topic = MqttConf.lastWill.topic;
			tmpMqttConf.lastWill.payloadOn = MqttConf.lastWill.payloadOn;
			tmpMqttConf.lastWill.qos = MqttConf.lastWill.qos;
		}		
		if (tmpMqttConf.sendConMsg) {
			okay &= checkTopic(tmpMqttConf.conMsg.topic);
			okay &= checkName(tmpMqttConf.conMsg.payloadOn);
			okay &= checkRange(tmpMqttConf.conMsg.qos, 0, 2);
		}
		else {
			tmpMqttConf.conMsg.topic = MqttConf.conMsg.topic;
			tmpMqttConf.conMsg.payloadOn = MqttConf.conMsg.payloadOn;
			tmpMqttConf.conMsg.qos = MqttConf.conMsg.qos;
		}

		if (tmpMqttConf.FWUpdateEN) {
			okay &= checkTopic(tmpMqttConf.updCmd.topic);
			okay &= checkName(tmpMqttConf.updCmd.payloadOn);
			okay &= checkName(tmpMqttConf.updCmd.payloadOff);
			okay &= checkRange(tmpMqttConf.updCmd.qos, 0, 2);
			okay &= checkTopic(tmpMqttConf.updState.topic);
			okay &= checkRange(tmpMqttConf.updState.qos, 0, 2);
		}
		else {
			tmpMqttConf.updCmd.topic = MqttConf.updCmd.topic;
			tmpMqttConf.updCmd.payloadOn = MqttConf.updCmd.payloadOn;
			tmpMqttConf.updCmd.payloadOn = MqttConf.updCmd.payloadOn;
			tmpMqttConf.updCmd.qos = MqttConf.updCmd.qos;
			tmpMqttConf.updCmd.topic = MqttConf.updState.topic;
			tmpMqttConf.updCmd.qos = MqttConf.updState.qos;
		}

		if (okay) {
			//Try to save config
			MqttConf = tmpMqttConf;
			if (saveConfig(MQTT_CONF)) {
				request->send(200, "text/plain", "OK");
			}
			else request->send(200, "text/plain", "NOK: Error");
		}
		else {
			request->send(200, "text/plain", "NOK: Error values");
		}
	}
	else {
		request->send(200, "text/plain", "NOK: Bad Args");
	}
}

void evaluate_io_post_request(AsyncWebServerRequest *request) {
	strOutputConfig tmpOutConf[2];
	strInputConfig tmpInConf[2];
	bool okay = true;
	tmpOutConf[0] = OutConf[0];
	tmpOutConf[1] = OutConf[1];
	tmpInConf[0] = InConf[0];
	tmpInConf[1] = InConf[1];
	if ( ((request->args() == 44) && (OUTPUT2_PIN >= 0)) || ((request->args() == 28) && !(OUTPUT2_PIN >= 0)) ) {
		for (uint8_t i = 0; i < request->args(); i++) {
			for (uint8_t j = 1; j <= 2; j++) {
				int nr = j - 1;
				if (request->argName(i) == "in" + String(j) + "enablePublish") {
					tmpInConf[nr].enablePublish = ((request->arg(i) == "true") ? true : false);
					continue;
				}
				if (request->argName(i) == "in" + String(j) + "cmd.topic") {
					tmpInConf[nr].cmd.topic = request->arg(i);
					continue;
				}
				if (request->argName(i) == "in" + String(j) + "cmd.payloadOn") {
					tmpInConf[nr].cmd.payloadOn = request->arg(i);
					continue;
				}
				if (request->argName(i) == "in" + String(j) + "cmd.payloadOff") {
					tmpInConf[nr].cmd.payloadOff = request->arg(i);
					continue;
				}
				if (request->argName(i) == "in" + String(j) + "cmd.qos") {
					if (isNumeric(request->arg(i))) tmpInConf[nr].cmd.qos = request->arg(i).toInt();
					continue;
				}
				if (request->argName(i) == "in" + String(j) + "cmd.retain") {
					tmpInConf[nr].cmd.retain = ((request->arg(i) == "true") ? true : false);
					continue;
				}
				if (request->argName(i) == "in" + String(j) + "ctrlOutput1RisingEdge") {
					if (isNumeric(request->arg(i))) tmpInConf[nr].ctrlOutput1RisingEdge = request->arg(i).toInt();
					continue;
				}
				if (request->argName(i) == "in" + String(j) + "ctrlOutput1FallingEdge") {
					if (isNumeric(request->arg(i))) tmpInConf[nr].ctrlOutput1FallingEdge = request->arg(i).toInt();
					continue;
				}
				if (OUTPUT2_PIN >= 0) {
					if (request->argName(i) == "in" + String(j) + "ctrlOutput2RisingEdge") {
						if (isNumeric(request->arg(i))) tmpInConf[nr].ctrlOutput2RisingEdge = request->arg(i).toInt();
						continue;
					}
					if (request->argName(i) == "in" + String(j) + "ctrlOutput2FallingEdge") {
						if (isNumeric(request->arg(i))) tmpInConf[nr].ctrlOutput2FallingEdge = request->arg(i).toInt();
						continue;
					}
				}
				//Outputs
				if (!(OUTPUT2_PIN >= 0) && (j > 1)) continue; //Wenn Ausgang 2 ausgeschaltet => überspringen
				if (request->argName(i) == "out" + String(j) + "enableSubscribe") {
					tmpOutConf[nr].enableSubscribe = ((request->arg(i) == "true") ? true : false);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "cmd.topic") {
					tmpOutConf[nr].cmd.topic = request->arg(i);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "cmd.payloadOn") {
					tmpOutConf[nr].cmd.payloadOn = request->arg(i);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "cmd.payloadOff") {
					tmpOutConf[nr].cmd.payloadOff = request->arg(i);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "cmd.qos") {
					if (isNumeric(request->arg(i))) tmpOutConf[nr].cmd.qos = request->arg(i).toInt();
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "enablePublish") {
					tmpOutConf[nr].enablePublish = ((request->arg(i) == "true") ? true : false);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "state.topic") {
					tmpOutConf[nr].state.topic = request->arg(i);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "state.payloadOn") {
					tmpOutConf[nr].state.payloadOn = request->arg(i);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "state.payloadOff") {
					tmpOutConf[nr].state.payloadOff = request->arg(i);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "state.qos") {
					if (isNumeric(request->arg(i))) tmpOutConf[nr].state.qos = request->arg(i).toInt();
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "state.retain") {
					tmpOutConf[nr].state.retain = ((request->arg(i) == "true") ? true : false);
					continue;
				}
				if (request->argName(i) == "out" + String(j) + "initState") {
					tmpOutConf[nr].initState = ((request->arg(i) == "1") ? true : false);
					continue;
				}
			}
		}
		for (uint8_t j = 0; j <= 1; j++) {
			//**********Check Values
			//Inputs
			if (tmpInConf[j].enablePublish) {
				okay &= checkTopic(tmpInConf[j].cmd.topic);
				okay &= checkName(tmpInConf[j].cmd.payloadOn);
				okay &= checkName(tmpInConf[j].cmd.payloadOff);
				okay &= checkRange(tmpInConf[j].cmd.qos, 0, 2);
			}
			else {
				tmpInConf[j].cmd.topic = InConf[j].cmd.topic;
				tmpInConf[j].cmd.payloadOn = InConf[j].cmd.payloadOn;
				tmpInConf[j].cmd.payloadOff = InConf[j].cmd.payloadOff;
				tmpInConf[j].cmd.qos = InConf[j].cmd.qos;
			}

			//Outputs
			if (tmpOutConf[j].enableSubscribe) {
				okay &= checkTopic(tmpOutConf[j].cmd.topic);
				okay &= (checkName(tmpOutConf[j].cmd.payloadOn) || (tmpOutConf[j].cmd.payloadOn == ""));
				okay &= (checkName(tmpOutConf[j].cmd.payloadOff) || (tmpOutConf[j].cmd.payloadOff == ""));
				okay &= checkRange(tmpOutConf[j].cmd.qos, 0, 2);
			}
			else {
				tmpOutConf[j].cmd.topic = OutConf[j].cmd.topic;
				tmpOutConf[j].cmd.payloadOn = OutConf[j].cmd.payloadOn;
				tmpOutConf[j].cmd.payloadOff = OutConf[j].cmd.payloadOff;
				tmpOutConf[j].cmd.qos = OutConf[j].cmd.qos;
			}
			if (tmpOutConf[j].enablePublish) {
				okay &= checkTopic(tmpOutConf[j].state.topic);
				okay &= (checkName(tmpOutConf[j].state.payloadOn) || (tmpOutConf[j].state.payloadOn == ""));
				okay &= (checkName(tmpOutConf[j].state.payloadOff) || (tmpOutConf[j].state.payloadOff == ""));
				okay &= checkRange(tmpOutConf[j].state.qos, 0, 2);
			}
			else {
				tmpOutConf[j].state.topic = OutConf[j].state.topic;
				tmpOutConf[j].state.payloadOn = OutConf[j].state.payloadOn;
				tmpOutConf[j].state.payloadOff = OutConf[j].state.payloadOff;
				tmpOutConf[j].state.qos = OutConf[j].state.qos;
			}
		}
		if (okay) {
			bool tmpPublish[2], tmpSubscribe[2];
			for (uint8_t j = 0; j <= 1; j++) {
				tmpPublish[j] = false;
				tmpSubscribe[j] = false;
				//Handle Publish Output State
				if ((tmpOutConf[j].enablePublish && !OutConf[j].enablePublish) || (tmpOutConf[j].state.topic != OutConf[j].state.topic) || (tmpOutConf[j].state.qos != OutConf[j].state.qos) || (tmpOutConf[j].state.retain != OutConf[j].state.retain)) tmpPublish[j] = true;
				//Handle Unsubscribe
				if ((!tmpOutConf[j].enableSubscribe && OutConf[j].enableSubscribe) || (tmpOutConf[j].cmd.topic != OutConf[j].cmd.topic) || (tmpOutConf[j].cmd.qos != OutConf[j].cmd.qos)) mqttClient.unsubscribe(OutConf[j].cmd.topic.c_str());
				//Handle Subscribe
				if ((tmpOutConf[j].enableSubscribe && !OutConf[j].enableSubscribe) || (tmpOutConf[j].cmd.topic != OutConf[j].cmd.topic) || (tmpOutConf[j].cmd.qos != OutConf[j].cmd.qos)) tmpSubscribe[j] = true;
			}
			//Try to save config
			OutConf[0] = tmpOutConf[0];
			OutConf[1] = tmpOutConf[1];
			InConf[0] = tmpInConf[0];
			InConf[1] = tmpInConf[1];
			if (saveConfig(IO_CONF)) request->send(200, "text/plain", "OK");
			else request->send(200, "text/plain", "NOK: Error saving to SPIFFS");
			for (uint8_t j = 0; j <= 1; j++) {
				if (tmpPublish[j]) sendOutputState(j + 1);
				if (tmpSubscribe[j]) mqttClient.subscribe(OutConf[j].cmd.topic.c_str(), OutConf[j].cmd.qos);
			}
		}
		else {
			request->send(200, "text/plain", "NOK: Error values");
		}
	}
	else {
		request->send(200, "text/plain", "NOK: Bad Args");
	}
}

//******************************************************************************************
// Setup functions
//******************************************************************************************

void mqttSetup() {
	// Set callbacks
	mqttClient.onConnect(onMqttConnect);
	mqttClient.onDisconnect(onMqttDisconnect);
	mqttClient.onMessage(onMqttMessage);
	mqttClient.setKeepAlive(MqttConf.keepAlive);
	mqttClient.setClientId(MqttConf.devID.c_str());
	mqttClient.setServer(MqttConf.host, MqttConf.port);
	if (MqttConf.lastWillEnable) mqttClient.setWill(MqttConf.lastWill.topic.c_str(), MqttConf.lastWill.qos, MqttConf.lastWill.retain, MqttConf.lastWill.payloadOn.c_str());
	if (MqttConf.auth) mqttClient.setCredentials(MqttConf.username.c_str(), MqttConf.password.c_str());
}

void webserverSetup() {
	// Start WebServer
	ESPHTTPServer.begin(&SPIFFS);
	// Set Model & Version for Updater
	ESPHTTPServer.setModelName(MODEL_NAME);
	ESPHTTPServer.setVersionString(VERSION);
	ESPHTTPServer.setUpdateCallback(onMqttFWUpdateCb);

	ESPHTTPServer.setSaveConfigCallback(saveAllConfig);
	// set EventSource callbacks
	_evsUser.onConnect(onEvtConnect);
	ESPHTTPServer.addHandler(&_evsUser);
	// set callbacks for Webserver
	ESPHTTPServer.setPOSTCallback(onHTTP_POST);
	ESPHTTPServer.setRESTCallback(onHTTP_REST);
	ESPHTTPServer.setRestartCallback(onESPRestart);
}

void IOSetup() {
	debounceIn1.attach(INPUT1_PIN, INPUT_PULLUP);
	debounceIn1.interval(5);
	debounceIn2.attach(INPUT2_PIN, INPUT_PULLUP);
	debounceIn2.interval(5);
	pinMode(OUTPUT1_PIN, OUTPUT);
	output[0] = OutConf[0].initState;	
	output_old[0] = output[0];	
	digitalWrite(OUTPUT1_PIN, output[0]);
	if (OUTPUT2_PIN >= 0) {
		pinMode(OUTPUT2_PIN, OUTPUT);
		output[1] = OutConf[1].initState;
		output_old[1] = output[1];
		digitalWrite(OUTPUT2_PIN, output[1]);
	}	
}

bool checkRange(const int val, const int min, const int max) {
	return ((val >= min) && (val <= max));
}

bool isNumeric(const String& s) {
	for (uint16_t i = 0; i < s.length(); i++) {
		if (!isDigit(s.charAt(i))) return false;
	}
	return true;
}

bool checkName(const String& s) {
	for (uint16_t i = 0; i < s.length(); i++) {
		if (!(((s.charAt(i) >= 'A') && (s.charAt(i) <= 'Z')) ||
			((s.charAt(i) >= 'a') && (s.charAt(i) <= 'z')) ||
			((s.charAt(i) >= '0') && (s.charAt(i) <= '9')) ||
			(s.charAt(i) == '_') || (s.charAt(i) == '-') || (s.charAt(i) == '.') || (s.charAt(i) == '/'))) return false;
	}
	return true;
}

bool checkTopic(const String& s) {
	for (uint16_t i = 0; i < s.length(); i++) {
		if (s.length() > 128) return false; //max Topic length => 128
		//Topic should not begin or end with '/'
		if ((s.charAt(0) == '/') || (s.charAt(s.length()) == '/')) return false;

		if (!(((s.charAt(i) >= 'A') && (s.charAt(i) <= 'Z')) ||
			((s.charAt(i) >= 'a') && (s.charAt(i) <= 'z')) ||
			((s.charAt(i) >= '0') && (s.charAt(i) <= '9')) ||
			(s.charAt(i) == '_') || (s.charAt(i) == '-') || (s.charAt(i) == '/'))) return false;
	}
	return true;
}