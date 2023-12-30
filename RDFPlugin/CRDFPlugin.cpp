#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

const double pi = 3.141592653589793;
const double EarthRadius = 3438.0; // nautical miles, referred to internal CEuroScopeCoord
constexpr double GEOM_RAD_FROM_DEG(double deg) { return deg * pi / 180.0; };
constexpr double GEOM_DEG_FROM_RAD(double rad) { return rad / pi * 180.0; };

inline int FrequencyConvert(double freq) { // frequency * 1000 => int
	return round(freq * 1000.0);
}
inline bool FrequencyCompare(int freq1, int freq2) { // return true if same frequency, frequency *= 1000
	return abs(freq1 - freq2) <= 10;
}

CRDFPlugin::CRDFPlugin()
	: EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
		MY_PLUGIN_NAME,
		MY_PLUGIN_VERSION,
		MY_PLUGIN_DEVELOPER,
		MY_PLUGIN_COPYRIGHT)
{
	// RDF window
	RegisterClass(&windowClassRDF);
	hiddenWindowRDF = CreateWindow(
		"RDFHiddenWindowClass",
		"RDFHiddenWindow",
		NULL,
		0,
		0,
		0,
		0,
		NULL,
		NULL,
		GetModuleHandle(NULL),
		reinterpret_cast<LPVOID>(this)
	);
	if (GetLastError() != S_OK) {
		DisplayWarnMessage("Unable to open communications for RDF");
	}

	// AFV bridge window
	RegisterClass(&windowClassAFV);
	hiddenWindowAFV = CreateWindow(
		"AfvBridgeHiddenWindowClass",
		"AfvBridgeHiddenWindow",
		NULL,
		0,
		0,
		0,
		0,
		NULL,
		NULL,
		GetModuleHandle(NULL),
		reinterpret_cast<LPVOID>(this)
	);
	if (GetLastError() != S_OK) {
		DisplayWarnMessage("Unable to open communications for AFV bridge");
	}

	LoadSettings();

	rdGenerator = std::mt19937(randomDevice());
	disBearing = std::uniform_real_distribution<>(0.0, 360.0);
	disDistance = std::normal_distribution<>(0, 1.0);

	DisplayInfoMessage(std::string("Version " + std::string(MY_PLUGIN_VERSION) + " loaded"));

	// detach thread for VectorAudio
	threadVectorAudioMain = std::make_unique<std::thread>(&CRDFPlugin::VectorAudioMainLoop, this);
	threadVectorAudioMain->detach();
	threadVectorAudioTXRX = std::make_unique<std::thread>(&CRDFPlugin::VectorAudioTXRXLoop, this);
	threadVectorAudioTXRX->detach();

}

CRDFPlugin::~CRDFPlugin()
{
	// close detached thread
	threadMainRunning = false;
	threadTXRXRunning = false;

	if (hiddenWindowRDF != NULL) {
		DestroyWindow(hiddenWindowRDF);
	}
	UnregisterClass("RDFHiddenWindowClass", NULL);

	if (hiddenWindowAFV != NULL) {
		DestroyWindow(hiddenWindowAFV);
	}
	UnregisterClass("AfvBridgeHiddenWindowClass", NULL);

	threadMainClosed.wait(false);
	threadTXRXClosed.wait(false);
}

auto CRDFPlugin::HiddenWndProcessRDFMessage(const std::string& message) -> void
{
	{
		std::lock_guard<std::mutex> lock(messageLock);
		if (message.size()) {
			DisplayDebugMessage(std::string("AFV message: ") + message);
			std::set<std::string> strings;
			std::istringstream f(message);
			std::string s;
			while (std::getline(f, s, ':')) {
				strings.insert(s);
			}
			messages.push(strings);
		}
		else {
			messages.push(std::set<std::string>());
		}
	}
	ProcessRDFQueue();
}

auto CRDFPlugin::HiddenWndProcessAFVMessage(const std::string& message) -> void
{
	// functions as AFV bridge
	if (!message.size()) return;
	DisplayDebugMessage(std::string("AFV message: ") + message);
	// format: xxx.xxx:True:False + xxx.xx0:True:False

	// parse message
	std::queue<std::string> strings;
	std::istringstream f(message);
	std::string s;
	while (std::getline(f, s, ':')) {
		strings.push(s);
	}
	if (strings.size() != 3) return; // in case of incomplete message
	int msgFrequency;
	bool transmitX, receiveX;
	try {
		msgFrequency = FrequencyConvert(stod(strings.front()));
		strings.pop();
		receiveX = strings.front() == "True";
		strings.pop();
		transmitX = strings.front() == "True";
		strings.pop();
	}
	catch (...) {
		DisplayDebugMessage("Error when parsing AFV message: " + message);
		return;
	}

	// abort if frequency is prim
	if (FrequencyCompare(msgFrequency, FrequencyConvert(ControllerMyself().GetPrimaryFrequency())))
		return;

	// match frequency to callsign
	std::string msgCallsign = ControllerMyself().GetCallsign();
	for (auto c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c)) {
		if (FrequencyCompare(FrequencyConvert(c.GetPrimaryFrequency()), msgFrequency)) {
			msgCallsign = c.GetCallsign();
			break;
		}
	}

	// find channel and toggle
	for (auto c = GroundToArChannelSelectFirst(); c.IsValid(); c = GroundToArChannelSelectNext(c)) {
		int chFreq = FrequencyConvert(c.GetFrequency());
		if (c.GetIsPrimary() || c.GetIsAtis() || !FrequencyCompare(chFreq, msgFrequency))
			continue;
		std::string chName = c.GetName();
		if (msgCallsign == chName) {
			ToggleChannels(c, transmitX, receiveX);
			return;
		}
		else {
			size_t posc = msgCallsign.find('_');
			if (chName.starts_with(msgCallsign.substr(0, posc))) {
				ToggleChannels(c, transmitX, receiveX);
				return;
			}
		}
	}

	// no possible match, toggle the first same frequency
	for (auto c = GroundToArChannelSelectFirst(); c.IsValid(); c = GroundToArChannelSelectNext(c)) {
		if (!c.GetIsPrimary() && !c.GetIsAtis() &&
			FrequencyCompare(FrequencyConvert(c.GetFrequency()), msgFrequency)) {
			ToggleChannels(c, transmitX, receiveX);
			return;
		}
	}
}

auto CRDFPlugin::GetRGB(COLORREF& color, const char* settingValue) -> void
{
	unsigned int r, g, b;
	sscanf_s(settingValue, "%u:%u:%u", &r, &g, &b);
	if (r <= 255 && g <= 255 && b <= 255) {
		DisplayDebugMessage(std::string("R: ") + std::to_string(r) + std::string(" G: ") + std::to_string(g) + std::string(" B: ") + std::to_string(b));
		color = RGB(r, g, b);
	}
}

auto CRDFPlugin::LoadSettings(void) -> void
{
	addressVectorAudio = "127.0.0.1:49080";
	connectionTimeout = 300; // milliseconds, range: [100, 1000]
	pollInterval = 200; // milliseconds, range: [100, +inf)
	retryInterval = 5; // seconds, range: [1, +inf)

	rdfRGB = RGB(255, 255, 255);	// Default: white
	rdfConcurRGB = RGB(255, 0, 0);	// Default: red

	circleRadius = 20; // Default: 20 (nautical miles or pixel), range: (0, +inf)
	circleThreshold = -1; // Default: -1 (always use pixel)
	circlePrecision = 0; // Default: no offset (nautical miles), range: [0, +inf)
	lowAltitude = 0; // Default: 0 (feet)
	lowPrecision = 0; // Default: 0 (nautical miles), range: [0, +inf)
	highAltitude = 0; // Default: 0 (feet)
	highPrecision = 0; // Default: 0 (nautical miles), range: [0, +inf)
	// Schematic: high altitude/precision optional. low altitude used for filtering regardless of others
	// threshold < 0 will use circleRadius in pixel, circlePrecision for offset, low/high settings ignored
	// lowPrecision > 0 and highPrecision > 0 and lowAltitude < highAltitude, will override circleRadius and circlePrecision with dynamic precision/radius
	// lowPrecision > 0 but not meeting the above, will use lowPrecision (> 0) or circlePrecision

	drawController = false;

	try
	{
		const char* cstrAddrVA = GetDataFromSettings(SETTING_VECTORAUDIO_ADDRESS);
		if (cstrAddrVA != NULL)
		{
			addressVectorAudio = cstrAddrVA;
			DisplayDebugMessage(std::string("Address: ") + addressVectorAudio);
		}
		const char* cstrTimeout = GetDataFromSettings(SETTING_VECTORAUDIO_TIMEOUT);
		if (cstrTimeout != NULL)
		{
			int parsedTimeout = atoi(cstrTimeout);
			if (parsedTimeout >= 100 && parsedTimeout <= 1000) {
				connectionTimeout = parsedTimeout;
				DisplayDebugMessage(std::string("Timeout: ") + std::to_string(connectionTimeout));
			}
		}
		const char* cstrPollInterval = GetDataFromSettings(SETTING_VECTORAUDIO_POLL_INTERVAL);
		if (cstrPollInterval != NULL)
		{
			int parsedInterval = atoi(cstrPollInterval);
			if (parsedInterval >= 100) {
				pollInterval = parsedInterval;
				DisplayDebugMessage(std::string("Poll interval: ") + std::to_string(pollInterval));
			}
		}
		const char* cstrRetryInterval = GetDataFromSettings(SETTING_VECTORAUDIO_RETRY_INTERVAL);
		if (cstrRetryInterval != NULL)
		{
			int parsedInterval = atoi(cstrRetryInterval);
			if (parsedInterval >= 1) {
				retryInterval = parsedInterval;
				DisplayDebugMessage(std::string("Retry interval: ") + std::to_string(retryInterval));
			}
		}
		const char* cstrRGB = GetDataFromSettings(SETTING_RGB);
		if (cstrRGB != NULL)
		{
			GetRGB(rdfRGB, cstrRGB);
		}
		cstrRGB = GetDataFromSettings(SETTING_CONCURRENT_RGB);
		if (cstrRGB != NULL)
		{
			GetRGB(rdfConcurRGB, cstrRGB);
		}
		const char* cstrRadius = GetDataFromSettings(SETTING_CIRCLE_RADIUS);
		if (cstrRadius != NULL)
		{
			int parsedRadius = atoi(cstrRadius);
			if (parsedRadius > 0) {
				circleRadius = parsedRadius;
				DisplayDebugMessage(std::string("Radius: ") + std::to_string(circleRadius));
			}
		}
		const char* cstrThreshold = GetDataFromSettings(SETTING_THRESHOLD);
		if (cstrThreshold != NULL)
		{
			circleThreshold = atoi(cstrThreshold);
			DisplayDebugMessage(std::string("Threshold: ") + std::to_string(circleThreshold));
		}
		const char* cstrPrecision = GetDataFromSettings(SETTING_PRECISION);
		if (cstrPrecision != NULL)
		{
			int parsedPrecision = atoi(cstrPrecision);
			if (parsedPrecision >= 0) {
				circlePrecision = parsedPrecision;
				DisplayDebugMessage(std::string("Precision: ") + std::to_string(circlePrecision));
			}
		}
		const char* cstrLowAlt = GetDataFromSettings(SETTING_LOW_ALTITUDE);
		if (cstrLowAlt != NULL)
		{
			int parsedAlt = atoi(cstrLowAlt);
			lowAltitude = parsedAlt;
			DisplayDebugMessage(std::string("Low Altitude: ") + std::to_string(lowAltitude));
		}
		const char* cstrHighAlt = GetDataFromSettings(SETTING_HIGH_ALTITUDE);
		if (cstrHighAlt != NULL)
		{
			int parsedAlt = atoi(cstrHighAlt);
			if (parsedAlt > 0) {
				highAltitude = parsedAlt;
				DisplayDebugMessage(std::string("High Altitude: ") + std::to_string(highAltitude));
			}
		}
		const char* cstrLowPrecision = GetDataFromSettings(SETTING_LOW_PRECISION);
		if (cstrLowPrecision != NULL)
		{
			int parsedPrecision = atoi(cstrLowPrecision);
			if (parsedPrecision >= 0) {
				lowPrecision = parsedPrecision;
				DisplayDebugMessage(std::string("Low Precision: ") + std::to_string(lowPrecision));
			}
		}
		const char* cstrHighPrecision = GetDataFromSettings(SETTING_HIGH_PRECISION);
		if (cstrHighPrecision != NULL)
		{
			int parsedPrecision = atoi(cstrHighPrecision);
			if (parsedPrecision >= 0) {
				highPrecision = parsedPrecision;
				DisplayDebugMessage(std::string("High Precision: ") + std::to_string(highPrecision));
			}
		}
		const char* cstrController = GetDataFromSettings(SETTING_DRAW_CONTROLLERS);
		if (cstrController != NULL)
		{
			drawController = (bool)atoi(cstrController);
			DisplayDebugMessage(std::string("Draw controllers and observers: ") + std::to_string(drawController));
		}
	}
	catch (std::runtime_error const& e)
	{
		DisplayWarnMessage(std::string("Error: ") + e.what());
	}
	catch (...)
	{
		DisplayWarnMessage(std::string("Unexpected error: ") + std::to_string(GetLastError()));
	}
}

auto CRDFPlugin::ParseSharedSettings(const std::string& command, CRDFScreen* screen) -> bool
{
	// deals with settings available for asr
	std::smatch match;
	auto SaveSetting = [&](const auto& varName, const auto& varDescr, const auto& val) {
		if (screen != nullptr) {
			screen->SaveDataToAsr(varName, varDescr, val);
			DisplayInfoMessage(std::string(varDescr) + ": " + std::string(val) + " (ASR)");
		}
		else {
			SaveDataToSettings(varName, varDescr, val);
			DisplayInfoMessage(std::string(varDescr) + ": " + std::string(val));
		}
		};
	try
	{
		std::regex rxRGB("^.RDF (RGB|CTRGB) (\\S+)$", std::regex_constants::icase);
		if (regex_match(command, match, rxRGB)) {
			auto bufferMode = match[1].str();
			auto bufferRGB = match[2].str();
			std::transform(bufferMode.begin(), bufferMode.end(), bufferMode.begin(), ::toupper);
			if (bufferMode == "RGB") {
				COLORREF prevRGB = rdfRGB;
				GetRGB(rdfRGB, bufferRGB.c_str());
				if (rdfRGB != prevRGB) {
					SaveSetting(SETTING_RGB, "RGB", bufferRGB.c_str());
					return true;
				}
			}
			else {
				COLORREF prevRGB = rdfConcurRGB;
				GetRGB(rdfConcurRGB, bufferRGB.c_str());
				if (rdfConcurRGB != prevRGB) {
					SaveSetting(SETTING_CONCURRENT_RGB, "Concurrent RGB", bufferRGB.c_str());
					return true;
				}
			}
		}
		// no need for regex
		std::string cmd = command;
		std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
		int bufferRadius;
		if (sscanf_s(cmd.c_str(), ".RDF RADIUS %d", &bufferRadius) == 1) {
			if (bufferRadius > 0) {
				circleRadius = bufferRadius;
				SaveSetting(SETTING_CIRCLE_RADIUS, "Radius", std::to_string(circleRadius).c_str());
				return true;
			}
		}
		int bufferThreshold;
		if (sscanf_s(cmd.c_str(), ".RDF THRESHOLD %d", &bufferThreshold) == 1) {
			circleThreshold = bufferThreshold;
			SaveSetting(SETTING_THRESHOLD, "Threshold", std::to_string(circleThreshold).c_str());
			return true;
		}
		int bufferAltitude;
		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE L%d", &bufferAltitude) == 1) {
			lowAltitude = bufferAltitude;
			SaveSetting(SETTING_LOW_ALTITUDE, "Altitude (low)", std::to_string(lowAltitude).c_str());
			return true;
		}
		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE H%d", &bufferAltitude) == 1) {
			highAltitude = bufferAltitude;
			SaveSetting(SETTING_HIGH_ALTITUDE, "Altitude (high)", std::to_string(highAltitude).c_str());
			return true;
		}
		int bufferPrecision;
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION L%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				lowPrecision = bufferPrecision;
				SaveSetting(SETTING_LOW_PRECISION, "Precision (low)", std::to_string(lowPrecision).c_str());
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION H%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				highPrecision = bufferPrecision;
				SaveSetting(SETTING_HIGH_PRECISION, "Precision (high)", std::to_string(highPrecision).c_str());
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION %d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				circlePrecision = bufferPrecision;
				SaveSetting(SETTING_PRECISION, "Precision", std::to_string(circlePrecision).c_str());
				return true;
			}
		}
		int bufferCtrl;
		if (sscanf_s(cmd.c_str(), ".RDF CONTROLLER %d", &bufferCtrl) == 1) {
			drawController = bufferCtrl;
			SaveSetting(SETTING_DRAW_CONTROLLERS, "Draw controllers", std::to_string(bufferCtrl).c_str());
			return true;
		}
	}
	catch (const std::exception& e)
	{
		DisplayWarnMessage(e.what());
	}
	return false;
}

auto CRDFPlugin::ProcessRDFQueue(void) -> void
{
	std::lock_guard<std::mutex> lock(messageLock);
	// Process all incoming messages
	while (messages.size() > 0) {
		std::set<std::string> amessage = messages.front();
		messages.pop();

		// remove existing records
		for (auto itr = activeStations.begin(); itr != activeStations.end();) {
			if (amessage.erase(itr->first)) { // remove still transmitting from message
				itr++;
			}
			else {
				// no removal, means stopped transmission, need to also remove from map
				activeStations.erase(itr++);
			}
		}

		// add new active transmitting records
		for (const auto& callsign : amessage) {
			auto radarTarget = RadarTargetSelect(callsign.c_str());
			auto controller = ControllerSelect(callsign.c_str());
			if (!radarTarget.IsValid() && controller.IsValid() && callsign.back() >= 'A' && callsign.back() <= 'Z') {
				// dump last character and find callsign again
				std::string callsign_dump = callsign.substr(0, callsign.size() - 1);
				radarTarget = RadarTargetSelect(callsign_dump.c_str());
			}
			if (radarTarget.IsValid()) {
				int alt = radarTarget.GetPosition().GetPressureAltitude();
				if (alt >= lowAltitude) { // need to draw, see Schematic in LoadSettings
					EuroScopePlugIn::CPosition pos = radarTarget.GetPosition().GetPosition();
					double radius = circleRadius;
					// determines offset
					double offset = circlePrecision;
					if (circleThreshold >= 0 && lowPrecision > 0) {
						if (highPrecision > 0 && highAltitude > lowAltitude) {
							offset = (double)lowPrecision + (double)(alt - lowAltitude) * (double)(highPrecision - lowPrecision) / (double)(highAltitude - lowAltitude);
						}
						else {
							offset = lowPrecision > 0 ? lowPrecision : circlePrecision;
						}
						radius = offset;
					}
					if (offset > 0) { // add random offset
						double distance = abs(disDistance(rdGenerator)) / 3.0 * offset;
						double bearing = disBearing(rdGenerator);
						AddOffset(pos, bearing, distance);
					}
					activeStations[callsign] = { pos, radius };
				}
			}
			else if (drawController && controller.IsValid()) {
				auto pos = controller.GetPosition();
				activeStations[callsign] = { pos, (double)lowPrecision };
			}
		}

		if (!activeStations.empty()) {
			previousStations = activeStations;
		}
	}
}

auto CRDFPlugin::UpdateVectorAudioChannels(const std::string& line, const bool& mode_tx) -> void
{
	// parse message and returns number of total toggles
	std::map<std::string, int> channelFreq;
	std::istringstream ssLine(line);
	std::string strChnl;
	int freqMe = FrequencyConvert(ControllerMyself().GetPrimaryFrequency());
	while (getline(ssLine, strChnl, ',')) {
		size_t colon = strChnl.find(':');
		std::string channel = strChnl.substr(0, colon);
		try {
			int frequency = FrequencyConvert(stod(strChnl.substr(colon + 1)));
			if (!FrequencyCompare(freqMe, frequency)) {
				channelFreq.insert({ channel, frequency });
			}
		}
		catch (...) {
			DisplayDebugMessage("Error when parsing frequencies: " + strChnl);
			continue;
		}
	}

	for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
		if (chnl.GetIsPrimary() || chnl.GetIsAtis()) { // make sure primary and ATIS are not affected
			continue;
		}
		std::string chName = chnl.GetName();
		int chFreq = FrequencyConvert(chnl.GetFrequency());
		auto it = channelFreq.find(chName);
		if (it != channelFreq.end() && FrequencyCompare(it->second, chFreq)) { // allows 0.010 of deviation
			channelFreq.erase(it);
			goto _toggle_on;
		}
		else if (it == channelFreq.end()) {
			auto itc = channelFreq.begin();
			for (; itc != channelFreq.end() && !FrequencyCompare(itc->second, chFreq); itc++); // locate a matching freq
			if (itc != channelFreq.end()) {
				size_t posi = itc->first.find('_');
				if (chName.starts_with(itc->first.substr(0, posi))) {
					channelFreq.erase(itc);
					goto _toggle_on;
				}
			}
		}
		// toggle off
		if (mode_tx) {
			ToggleChannels(chnl, 0, -1);
		}
		else {
			ToggleChannels(chnl, -1, 0);
		}
		continue;
	_toggle_on:
		if (mode_tx) {
			ToggleChannels(chnl, 1, -1);
		}
		else {
			ToggleChannels(chnl, -1, 1);
		}
	}
}

auto CRDFPlugin::ToggleChannels(EuroScopePlugIn::CGrountToAirChannel Channel, const int& tx, const int& rx) -> void
{
	// pass tx/rx = -1 to skip
	if (tx >= 0 && tx != (int)Channel.GetIsTextTransmitOn()) {
		Channel.ToggleTextTransmit();
		DisplayDebugMessage(
			std::string("TX toggle: ") + Channel.GetName() + " frequency: " + std::to_string(Channel.GetFrequency())
		);
	}
	if (rx >= 0 && rx != (int)Channel.GetIsTextReceiveOn()) {
		Channel.ToggleTextReceive();
		DisplayDebugMessage(
			std::string("RX toggle: ") + Channel.GetName() + " frequency: " + std::to_string(Channel.GetFrequency())
		);
	}
}

auto CRDFPlugin::AddOffset(EuroScopePlugIn::CPosition& position, const double& heading, const double& distance) -> void
{
	// from ES internal void CEuroScopeCoord :: Move ( double heading, double distance )
	if (distance < 0.000001)
		return;

	double m_Lat = position.m_Latitude;
	double m_Lon = position.m_Longitude;

	double distancePerR = distance / EarthRadius;
	double cosDistancePerR = cos(distancePerR);
	double sinDistnacePerR = sin(distancePerR);

	double fi2 = asin(sin(GEOM_RAD_FROM_DEG(m_Lat)) * cosDistancePerR + cos(GEOM_RAD_FROM_DEG(m_Lat)) * sinDistnacePerR * cos(GEOM_RAD_FROM_DEG(heading)));
	double lambda2 = GEOM_RAD_FROM_DEG(m_Lon) + atan2(sin(GEOM_RAD_FROM_DEG(heading)) * sinDistnacePerR * cos(GEOM_RAD_FROM_DEG(m_Lat)),
		cosDistancePerR - sin(GEOM_RAD_FROM_DEG(m_Lat)) * sin(fi2));

	position.m_Latitude = GEOM_DEG_FROM_RAD(fi2);
	position.m_Longitude = GEOM_DEG_FROM_RAD(lambda2);
}

auto CRDFPlugin::VectorAudioMainLoop(void) -> void
{
	threadMainRunning = true;
	threadMainClosed = false;
	bool getTransmit = false;
	while (true) {
		for (int sleepRemain = getTransmit ? pollInterval : retryInterval * 1000; sleepRemain > 0;) {
			if (threadMainRunning) {
				int sleepThis = min(sleepRemain, pollInterval);
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepThis));
				sleepRemain -= sleepThis;
			}
			else {
				threadMainClosed = true;
				threadMainClosed.notify_all();
				return;
			}
		}

		httplib::Client cli("http://" + addressVectorAudio);
		cli.set_connection_timeout(0, connectionTimeout * 1000);
		if (auto res = cli.Get(getTransmit ? VECTORAUDIO_PARAM_TRANSMIT : VECTORAUDIO_PARAM_VERSION)) {
			if (res->status == 200) {
				DisplayDebugMessage(std::string("VectorAudio message: ") + res->body);
				if (!getTransmit) {
					DisplayInfoMessage("Connected to " + res->body);
					getTransmit = true;
					continue;
				}
				{
					std::lock_guard<std::mutex> lock(messageLock);
					if (!res->body.size()) {
						messages.push(std::set<std::string>());
					}
					else {
						std::set<std::string> strings;
						std::istringstream f(res->body);
						std::string s;
						while (getline(f, s, ',')) {
							strings.insert(s);
						}
						messages.push(strings);
					}
				}
				ProcessRDFQueue();
				continue;
			}
			DisplayDebugMessage("HTTP error on MAIN: " + httplib::to_string(res.error()));
		}
		else {
			DisplayDebugMessage("Not connected");
		}
		if (getTransmit) {
			DisplayWarnMessage("VectorAudio disconnected");
		}
		getTransmit = false;
	}
}

auto CRDFPlugin::VectorAudioTXRXLoop(void) -> void
{
	threadTXRXRunning = true;
	threadTXRXClosed = false;
	bool suppressEmpty = false;
	while (true) {
		for (int sleepRemain = retryInterval * 1000; sleepRemain > 0;) {
			if (threadTXRXRunning) {
				int sleepThis = min(sleepRemain, pollInterval);
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepThis));
				sleepRemain -= sleepThis;
			}
			else {
				threadTXRXClosed = true;
				threadTXRXClosed.notify_all();
				return;
			}
		}

		httplib::Client cli("http://" + addressVectorAudio);
		cli.set_connection_timeout(0, connectionTimeout * 1000);
		bool isActive = false; // false when no active station, for warning
		if (auto res = cli.Get(VECTORAUDIO_PARAM_TX)) {
			if (res->status == 200) {
				DisplayDebugMessage(std::string("VectorAudio message on TX: ") + res->body);
				isActive = isActive || res->body.size();
				UpdateVectorAudioChannels(res->body, true);
			}
			else {
				DisplayDebugMessage("HTTP error on TX: " + httplib::to_string(res.error()));
				suppressEmpty = true;
			}
		}
		else {
			suppressEmpty = true;
		}
		if (auto res = cli.Get(VECTORAUDIO_PARAM_RX)) {
			if (res->status == 200) {
				DisplayDebugMessage(std::string("VectorAudio message on RX: ") + res->body);
				isActive = isActive || res->body.size();
				UpdateVectorAudioChannels(res->body, false);
			}
			else {
				DisplayDebugMessage("HTTP error on RX: " + httplib::to_string(res.error()));
				suppressEmpty = true;
			}
		}
		else {
			suppressEmpty = true;
		}
		if (!isActive && !suppressEmpty) {
			DisplayWarnMessage("No active stations in VecterAudio! Please check configuration.");
			suppressEmpty = true;
		}
		else if (isActive) {
			suppressEmpty = false;
		}
	}
}

auto CRDFPlugin::OnRadarScreenCreated(const char* sDisplayName,
	bool NeedRadarContent,
	bool GeoReferenced,
	bool CanBeSaved,
	bool CanBeCreated)
	-> EuroScopePlugIn::CRadarScreen*
{
	DisplayInfoMessage(std::string("Radio Direction Finder plugin activated on ") + sDisplayName);

	return new CRDFScreen(this);
}

auto CRDFPlugin::OnCompileCommand(const char* sCommandLine) -> bool
{
	std::string cmd = sCommandLine;
	std::smatch match; // all regular expressions will ignore cases
	try
	{
		std::regex rxReload("^.RDF RELOAD$", std::regex_constants::icase);
		if (regex_match(cmd, match, rxReload)) {
			LoadSettings();
			return true;
		}
		std::regex rxAddress("^.RDF ADDRESS (\\S+)$", std::regex_constants::icase);
		if (regex_match(cmd, match, rxAddress)) {
			addressVectorAudio = match[1].str();
			DisplayInfoMessage(std::string("Address: ") + addressVectorAudio);
			SaveDataToSettings(SETTING_VECTORAUDIO_ADDRESS, "VectorAudio address", addressVectorAudio.c_str());
			return true;
		}
		// no need for regex any more
		std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
		int bufferTimeout;
		if (sscanf_s(cmd.c_str(), ".RDF TIMEOUT %d", &bufferTimeout) == 1) {
			if (bufferTimeout >= 100 && bufferTimeout <= 1000) {
				connectionTimeout = bufferTimeout;
				DisplayInfoMessage(std::string("Timeout: ") + std::to_string(connectionTimeout));
				SaveDataToSettings(SETTING_VECTORAUDIO_TIMEOUT, "VectorAudio timeout", std::to_string(connectionTimeout).c_str());
				return true;
			}
		}
		int bufferPollInterval;
		if (sscanf_s(cmd.c_str(), ".RDF POLL %d", &bufferPollInterval) == 1) {
			if (bufferPollInterval >= 100) {
				pollInterval = bufferPollInterval;
				DisplayInfoMessage(std::string("Poll interval: ") + std::to_string(bufferPollInterval));
				SaveDataToSettings(SETTING_VECTORAUDIO_POLL_INTERVAL, "VectorAudio poll interval", std::to_string(pollInterval).c_str());
				return true;
			}
		}
		int bufferRetryInterval;
		if (sscanf_s(cmd.c_str(), ".RDF RETRY %d", &bufferRetryInterval) == 1) {
			if (bufferRetryInterval >= 1) {
				retryInterval = bufferRetryInterval;
				DisplayInfoMessage(std::string("Retry interval: ") + std::to_string(retryInterval));
				SaveDataToSettings(SETTING_VECTORAUDIO_RETRY_INTERVAL, "VectorAudio retry interval", std::to_string(retryInterval).c_str());
				return true;
			}
		}
		return ParseSharedSettings(sCommandLine);
	}
	catch (const std::exception& e)
	{
		DisplayWarnMessage(e.what());
	}
	return false;
}
