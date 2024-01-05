#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

const double pi = 3.141592653589793;
const double EarthRadius = 3438.0; // nautical miles, referred to internal CEuroScopeCoord
static constexpr auto GEOM_RAD_FROM_DEG(const double& deg) -> double { return deg * pi / 180.0; };
static constexpr auto GEOM_DEG_FROM_RAD(const double& rad) -> double { return rad / pi * 180.0; };

inline static auto FrequencyConvert(const double& freq) -> int { // frequency * 1000 => int
	return round(freq * 1000.0);
}
inline static auto FrequencyCompare(const auto& freq1, const auto& freq2) -> bool { // return true if same frequency, frequency *= 1000
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

	screenSettings[-1] = std::make_shared<draw_settings>(); // initialize default settings
	LoadVectorAudioSettings();
	LoadDrawingSettings();

	rdGenerator = std::mt19937(randomDevice());
	disBearing = std::uniform_real_distribution<>(0.0, 360.0);
	disDistance = std::normal_distribution<>(0, 1.0);

	DisplayInfoMessage(std::format("Version {} Loaded", MY_PLUGIN_VERSION));

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

	if (hiddenWindowRDF != nullptr) {
		DestroyWindow(hiddenWindowRDF);
	}
	UnregisterClass("RDFHiddenWindowClass", nullptr);

	if (hiddenWindowAFV != nullptr) {
		DestroyWindow(hiddenWindowAFV);
	}
	UnregisterClass("AfvBridgeHiddenWindowClass", nullptr);

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

auto CRDFPlugin::GetRGB(COLORREF& color, const std::string& settingValue) -> void
{
	std::regex rxRGB(R"(^(\d{1,3}):(\d{1,3}):(\d{1,3})$)");
	std::smatch match;
	if (std::regex_match(settingValue, match, rxRGB)) {
		UINT r = std::stoi(match[1].str());
		UINT g = std::stoi(match[2].str());
		UINT b = std::stoi(match[3].str());
		if (r <= 255 && g <= 255 && b <= 255) {
			DisplayDebugMessage(std::format("R:{} G:{} B:{}", r, g, b));
			color = RGB(r, g, b);
		}
	}
}

auto CRDFPlugin::LoadVectorAudioSettings(void) -> void
{
	addressVectorAudio = "127.0.0.1:49080";
	connectionTimeout = 300; // milliseconds, range: [100, 1000]
	pollInterval = 200; // milliseconds, range: [100, +inf)
	retryInterval = 5; // seconds, range: [1, +inf)

	try
	{
		const char* cstrAddrVA = GetDataFromSettings(SETTING_VECTORAUDIO_ADDRESS);
		if (cstrAddrVA != nullptr)
		{
			addressVectorAudio = cstrAddrVA;
			DisplayDebugMessage(std::string("Address: ") + addressVectorAudio);
		}
		const char* cstrTimeout = GetDataFromSettings(SETTING_VECTORAUDIO_TIMEOUT);
		if (cstrTimeout != nullptr)
		{
			int parsedTimeout = atoi(cstrTimeout);
			if (parsedTimeout >= 100 && parsedTimeout <= 1000) {
				connectionTimeout = parsedTimeout;
				DisplayDebugMessage(std::string("Timeout: ") + std::to_string(connectionTimeout));
			}
		}
		const char* cstrPollInterval = GetDataFromSettings(SETTING_VECTORAUDIO_POLL_INTERVAL);
		if (cstrPollInterval != nullptr)
		{
			int parsedInterval = atoi(cstrPollInterval);
			if (parsedInterval >= 100) {
				pollInterval = parsedInterval;
				DisplayDebugMessage(std::string("Poll interval: ") + std::to_string(pollInterval));
			}
		}
		const char* cstrRetryInterval = GetDataFromSettings(SETTING_VECTORAUDIO_RETRY_INTERVAL);
		if (cstrRetryInterval != nullptr)
		{
			int parsedInterval = atoi(cstrRetryInterval);
			if (parsedInterval >= 1) {
				retryInterval = parsedInterval;
				DisplayDebugMessage(std::string("Retry interval: ") + std::to_string(retryInterval));
			}
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

auto CRDFPlugin::LoadDrawingSettings(const int& screenID) -> void
{
	// pass screenID = -1 to use plugin settings, otherwise use ASR settings
	// Schematic: high altitude/precision optional. low altitude used for filtering regardless of others
	// threshold < 0 will use circleRadius in pixel, circlePrecision for offset, low/high settings ignored
	// lowPrecision > 0 and highPrecision > 0 and lowAltitude < highAltitude, will override circleRadius and circlePrecision with dynamic precision/radius
	// lowPrecision > 0 but not meeting the above, will use lowPrecision (> 0) or circlePrecision

	DisplayDebugMessage(std::format("Loading drawing settings ID {}", screenID));
	auto GetSetting = [&](const auto& varName) -> std::string {
		if (screenID != -1) {
			auto ds = screenVec[screenID]->GetDataFromAsr(varName);
			if (ds != nullptr) {
				return ds;
			}
		} // fallback onto plugin setting
		auto d = GetDataFromSettings(varName);
		return d == nullptr ? "" : d;
		};

	try
	{
		std::unique_lock<std::shared_mutex> lock(screenLock);
		// initialize settings
		std::shared_ptr<draw_settings> targetSetting;;
		auto itrSetting = screenSettings.find(screenID);
		if (itrSetting != screenSettings.end()) { // use existing
			targetSetting = itrSetting->second;
		}
		else { // create new based on plugin setting
			DisplayDebugMessage("Inserting new settings");
			targetSetting = std::make_shared<draw_settings>(*screenSettings[-1]);
			screenSettings.insert({ screenID, targetSetting });
		}

		auto cstrRGB = GetSetting(SETTING_RGB);
		if (cstrRGB.size())
		{
			GetRGB(targetSetting->rdfRGB, cstrRGB);
		}
		cstrRGB = GetSetting(SETTING_CONCURRENT_RGB);
		if (cstrRGB.size())
		{
			GetRGB(targetSetting->rdfConcurRGB, cstrRGB);
		}
		auto cstrRadius = GetSetting(SETTING_CIRCLE_RADIUS);
		if (cstrRadius.size())
		{
			int parsedRadius = std::stoi(cstrRadius);
			if (parsedRadius > 0) {
				targetSetting->circleRadius = parsedRadius;
				DisplayDebugMessage(std::format("Radius: {}, Load ID: {}", targetSetting->circleRadius, screenID));
			}
		}
		auto cstrThreshold = GetSetting(SETTING_THRESHOLD);
		if (cstrThreshold.size())
		{
			targetSetting->circleThreshold = std::stoi(cstrThreshold);
			DisplayDebugMessage(std::format("Threshold: {}, Load ID: {}", targetSetting->circleThreshold, screenID));
		}
		auto cstrPrecision = GetSetting(SETTING_PRECISION);
		if (cstrPrecision.size())
		{
			int parsedPrecision = std::stoi(cstrPrecision);
			if (parsedPrecision >= 0) {
				targetSetting->circlePrecision = parsedPrecision;
				DisplayDebugMessage(std::format("Precision: {}, Load ID: {}", targetSetting->circlePrecision, screenID));
			}
		}
		auto cstrLowAlt = GetSetting(SETTING_LOW_ALTITUDE);
		if (cstrLowAlt.size())
		{
			targetSetting->lowAltitude = std::stoi(cstrLowAlt);
			DisplayDebugMessage(std::format("Low Altitude: {}, Load ID: {}", targetSetting->lowAltitude, screenID));
		}
		auto cstrHighAlt = GetSetting(SETTING_HIGH_ALTITUDE);
		if (cstrHighAlt.size())
		{
			int parsedAlt = std::stoi(cstrHighAlt);
			if (parsedAlt > 0) {
				targetSetting->highAltitude = parsedAlt;
				DisplayDebugMessage(std::format("High Altitude: {}, Load ID: {}", targetSetting->highAltitude, screenID));
			}
		}
		auto cstrLowPrecision = GetSetting(SETTING_LOW_PRECISION);
		if (cstrLowPrecision.size())
		{
			int parsedPrecision = std::stoi(cstrLowPrecision);
			if (parsedPrecision >= 0) {
				targetSetting->lowPrecision = parsedPrecision;
				DisplayDebugMessage(std::format("Low Precision: {}, Load ID: {}", targetSetting->lowPrecision, screenID));
			}
		}
		auto cstrHighPrecision = GetSetting(SETTING_HIGH_PRECISION);
		if (cstrHighPrecision.size())
		{
			int parsedPrecision = std::stoi(cstrHighPrecision);
			if (parsedPrecision >= 0) {
				targetSetting->highPrecision = parsedPrecision;
				DisplayDebugMessage(std::format("High Precision: {}, Load ID: {}", targetSetting->highPrecision, screenID));
			}
		}
		auto cstrController = GetSetting(SETTING_DRAW_CONTROLLERS);
		if (cstrController.size())
		{
			targetSetting->drawController = (bool)std::stoi(cstrController);
			DisplayDebugMessage(std::format("Draw controllers: {}, Load ID: {}", targetSetting->drawController, screenID));
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

auto CRDFPlugin::ParseDrawingSettings(const std::string& command, const int& screenID) -> bool
{
	// pass screenID = -1 to use plugin settings, otherwise use ASR settings
	// deals with settings available for asr
	auto SaveSetting = [&](const auto& varName, const auto& varDescr, const auto& val) -> void {
		if (screenID != -1) {
			screenVec[screenID]->AddAsrDataToBeSaved(varName, varDescr, val);
			DisplayInfoMessage(std::format("{}: {} (ASR)", varDescr, val));
		}
		else {
			SaveDataToSettings(varName, varDescr, val);
			DisplayInfoMessage(std::format("{}: {}", varDescr, val));
		}
		};
	try
	{
		std::unique_lock<std::shared_mutex> lock(screenLock);
		std::shared_ptr<draw_settings> targetSetting = screenSettings[screenID];
		std::smatch match;
		std::regex rxRGB(R"(^.RDF (RGB|CTRGB) (\S+)$)", std::regex_constants::icase);
		if (regex_match(command, match, rxRGB)) {
			auto bufferMode = match[1].str();
			auto bufferRGB = match[2].str();
			std::transform(bufferMode.begin(), bufferMode.end(), bufferMode.begin(), ::toupper);
			if (bufferMode == "RGB") {
				COLORREF prevRGB = targetSetting->rdfRGB;
				GetRGB(targetSetting->rdfRGB, bufferRGB);
				if (targetSetting->rdfRGB != prevRGB) {
					SaveSetting(SETTING_RGB, "RGB", bufferRGB.c_str());
					return true;
				}
			}
			else {
				COLORREF prevRGB = targetSetting->rdfConcurRGB;
				GetRGB(targetSetting->rdfConcurRGB, bufferRGB);
				if (targetSetting->rdfConcurRGB != prevRGB) {
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
				targetSetting->circleRadius = bufferRadius;
				SaveSetting(SETTING_CIRCLE_RADIUS, "Radius", std::to_string(targetSetting->circleRadius).c_str());
				return true;
			}
		}
		int bufferThreshold;
		if (sscanf_s(cmd.c_str(), ".RDF THRESHOLD %d", &bufferThreshold) == 1) {
			targetSetting->circleThreshold = bufferThreshold;
			SaveSetting(SETTING_THRESHOLD, "Threshold", std::to_string(targetSetting->circleThreshold).c_str());
			return true;
		}
		int bufferAltitude;
		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE L%d", &bufferAltitude) == 1) {
			targetSetting->lowAltitude = bufferAltitude;
			SaveSetting(SETTING_LOW_ALTITUDE, "Altitude (low)", std::to_string(targetSetting->lowAltitude).c_str());
			return true;
		}
		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE H%d", &bufferAltitude) == 1) {
			targetSetting->highAltitude = bufferAltitude;
			SaveSetting(SETTING_HIGH_ALTITUDE, "Altitude (high)", std::to_string(targetSetting->highAltitude).c_str());
			return true;
		}
		int bufferPrecision;
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION L%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				targetSetting->lowPrecision = bufferPrecision;
				SaveSetting(SETTING_LOW_PRECISION, "Precision (low)", std::to_string(targetSetting->lowPrecision).c_str());
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION H%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				targetSetting->highPrecision = bufferPrecision;
				SaveSetting(SETTING_HIGH_PRECISION, "Precision (high)", std::to_string(targetSetting->highPrecision).c_str());
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION %d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				targetSetting->circlePrecision = bufferPrecision;
				SaveSetting(SETTING_PRECISION, "Precision", std::to_string(targetSetting->circlePrecision).c_str());
				return true;
			}
		}
		int bufferCtrl;
		if (sscanf_s(cmd.c_str(), ".RDF CONTROLLER %d", &bufferCtrl) == 1) {
			targetSetting->drawController = bufferCtrl;
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
			auto params = GetDrawingParam();
			int circleRadius = params.circleRadius;
			int circlePrecision = params.circlePrecision;
			int circleThreshold = params.circleThreshold;
			int lowAltitude = params.lowAltitude;
			int highAltitude = params.highAltitude;
			int lowPrecision = params.lowPrecision;
			int highPrecision = params.highPrecision;
			bool drawController = params.drawController;
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

auto CRDFPlugin::GetDrawingParam(void) -> draw_settings const
{
	draw_settings res;
	try {
		std::shared_lock<std::shared_mutex> lock(screenLock);
		res = *screenSettings.at(activeScreenID);
	}
	catch (...) {
		DisplayDebugMessage(std::format("GetDrawingParam error, ID {}", (int)activeScreenID));
	}
	return res;
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
	size_t i = screenVec.size(); // should not be -1
	DisplayInfoMessage(std::format("Radio Direction Finder plugin activated on {}", sDisplayName));
	std::shared_ptr<CRDFScreen> screen = std::make_shared<CRDFScreen>(i);
	screenVec.push_back(screen);
	DisplayDebugMessage(std::format("Screen created: ID {}, type {}", i, sDisplayName));
	return screen.get();
}

auto CRDFPlugin::OnCompileCommand(const char* sCommandLine) -> bool
{
	std::string cmd = sCommandLine;
	std::smatch match; // all regular expressions will ignore cases
	try
	{
		std::regex rxReload(R"(^.RDF RELOAD$)", std::regex_constants::icase);
		if (regex_match(cmd, match, rxReload)) {
			LoadVectorAudioSettings();
			{
				std::unique_lock<std::shared_mutex> lock(screenLock); // cautious for overlapped lock
				screenSettings.clear();
				screenSettings[-1] = std::make_shared<draw_settings>(); // initialize default settings
			}
			LoadDrawingSettings(-1); // restore plugin settings
			for (auto& s : screenVec) { // reload asr settings
				s->newAsrData.clear();
				LoadDrawingSettings(s->m_ID);
			}
			return true;
		}
		std::regex rxAddress(R"(^.RDF ADDRESS (\S+)$)", std::regex_constants::icase);
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
		return ParseDrawingSettings(sCommandLine);
	}
	catch (const std::exception& e)
	{
		DisplayWarnMessage(e.what());
	}
	return false;
}

auto AddOffset(EuroScopePlugIn::CPosition& position, const double& heading, const double& distance) -> void
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
