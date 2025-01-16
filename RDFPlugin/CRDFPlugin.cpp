#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

const double pi = 3.141592653589793;
const double EarthRadius = 3438.0; // nautical miles, referred to internal CEuroScopeCoord
static constexpr auto GEOM_RAD_FROM_DEG(const double& deg) -> double { return deg * pi / 180.0; };
static constexpr auto GEOM_DEG_FROM_RAD(const double& rad) -> double { return rad / pi * 180.0; };

inline static auto FrequencyFromMHz(const double& freq) -> int {
	return (int)round(freq * 1000.0);
}
inline static auto FrequencyFromHz(const double& freq) -> int {
	return (int)round(freq / 1000.0);
}
inline static auto FrequencyIsSame(const auto& freq1, const auto& freq2) -> bool { // return true if same frequency, frequency in kHz
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
		DisplayWarnMessage("Unable to open communications for RDF.");
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
		DisplayWarnMessage("Unable to open communications for AFV bridge.");
	}

	// registration
	RegisterTagItemType("RDF state", TAG_ITEM_TYPE_RDF_STATE);

	// initialize default settings
	ix::initNetSystem();
	socketTrackAudio.setHandshakeTimeout(TRACKAUDIO_TIMEOUT_SEC);
	socketTrackAudio.setMaxWaitBetweenReconnectionRetries(TRACKAUDIO_HEARTBEAT_SEC * 2000); // ms
	socketTrackAudio.setPingInterval(TRACKAUDIO_HEARTBEAT_SEC);
	socketTrackAudio.setOnMessageCallback(std::bind_front(&CRDFPlugin::TrackAudioMessageHandler, this));
	setScreen[-1] = std::make_shared<draw_settings>();
	LoadTrackAudioSettings();
	LoadDrawingSettings();

	DisplayInfoMessage(std::format("Version {} Loaded.", MY_PLUGIN_VERSION));
}

CRDFPlugin::~CRDFPlugin()
{
	// disconnect TrackAudio connection
	socketTrackAudio.stop();

	if (hiddenWindowRDF != nullptr) {
		DestroyWindow(hiddenWindowRDF);
	}
	UnregisterClass("RDFHiddenWindowClass", nullptr);

	if (hiddenWindowAFV != nullptr) {
		DestroyWindow(hiddenWindowAFV);
	}
	UnregisterClass("AfvBridgeHiddenWindowClass", nullptr);

	ix::uninitNetSystem();
}

auto CRDFPlugin::HiddenWndProcessRDFMessage(const std::string& message) -> void
{
	std::unique_lock tlock(mtxTransmission);
	if (message.size()) {
		DisplayDebugMessage(std::string("AFV message: ") + message);
		std::vector<std::string> callsigns;
		std::istringstream f(message);
		std::string s;
		while (std::getline(f, s, ':')) {
			callsigns.push_back(s);
		}
		// skip existing callsigns and clear redundant
		std::erase_if(curTransmission, [&callsigns](const auto& item) {
			return !std::erase(callsigns, item.first);
			});
		// add new stations
		for (const auto& cs : callsigns) {
			auto dp = GenerateDrawPosition(cs);
			if (dp.radius > 0) {
				curTransmission[cs] = dp;
			}
		}
		preTransmission = curTransmission;
	}
	else {
		curTransmission.clear();
	}
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
		msgFrequency = FrequencyFromMHz(stod(strings.front()));
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

	// match frequency to callsign
	std::string msgCallsign = ControllerMyself().GetCallsign();
	if (!FrequencyIsSame(msgFrequency, FrequencyFromMHz(ControllerMyself().GetPrimaryFrequency()))) { // not myself
		for (auto c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c)) {
			if (FrequencyIsSame(FrequencyFromMHz(c.GetPrimaryFrequency()), msgFrequency)) {
				msgCallsign = c.GetCallsign();
				break;
			}
		}
	}

	// deal with increment/decrement of stations
	std::unique_lock flock(mtxFrequency);
	if (receiveX) {
		curFrequencies[msgCallsign].frequency = msgFrequency;
		curFrequencies[msgCallsign].tx = transmitX;
	}
	else {
		curFrequencies.erase(msgCallsign);
	}
	flock.unlock();
	UpdateChannels();
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

auto CRDFPlugin::LoadTrackAudioSettings(void) -> void
{
	addressTrackAudio = "127.0.0.1:49080";
	const char* cstrEndpoint = GetDataFromSettings(SETTING_ENDPOINT);
	if (cstrEndpoint != nullptr) {
		addressTrackAudio = cstrEndpoint;
		DisplayDebugMessage(std::string("Address: ") + addressTrackAudio);
	}

	// initialize TrackAudio WebSocket
	socketTrackAudio.stop();

	// clears records
	std::unique_lock lock1(mtxTransmission), lock2(mtxFrequency);
	curTransmission.clear();
	preTransmission.clear();
	curFrequencies.clear();
	lock1.unlock();
	lock2.unlock();
	UpdateChannels();

	// get TrackAudio helper mode
	modeTrackAudio = 1;
	const char* cstrMode = GetDataFromSettings(SETTING_HELPER_MODE);
	if (cstrMode != nullptr) {
		int mode = std::stoi(cstrMode);
		if (mode >= -1 && mode <= 2) {
			modeTrackAudio = mode;
		}
	}
	DisplayDebugMessage(std::format("TAMode: {}", modeTrackAudio));

	socketTrackAudio.setUrl(std::format("ws://{}{}", addressTrackAudio, TRACKAUDIO_PARAM_WS));
	if (modeTrackAudio != -1) {
		socketTrackAudio.start();
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
			auto ds = vecScreen[screenID]->GetDataFromAsr(varName);
			if (ds != nullptr) {
				return ds;
			}
		} // fallback onto plugin setting
		auto d = GetDataFromSettings(varName);
		return d == nullptr ? "" : d;
		};

	try
	{
		std::unique_lock<std::shared_mutex> lock(mtxScreen);
		// initialize settings
		std::shared_ptr<draw_settings> targetSetting;;
		auto itrSetting = setScreen.find(screenID);
		if (itrSetting != setScreen.end()) { // use existing
			targetSetting = itrSetting->second;
		}
		else { // create new based on plugin setting
			DisplayDebugMessage("Inserting new settings");
			targetSetting = std::make_shared<draw_settings>(*setScreen[-1]);
			setScreen.insert({ screenID, targetSetting });
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

auto CRDFPlugin::ProcessDrawingCommand(const std::string& command, const int& screenID) -> bool
{
	// pass screenID = -1 to use plugin settings, otherwise use ASR settings
	// deals with settings available for asr
	auto SaveSetting = [&](const auto& varName, const auto& varDescr, const auto& val) -> void {
		if (screenID != -1) {
			vecScreen[screenID]->AddAsrDataToBeSaved(varName, varDescr, val);
			DisplayInfoMessage(std::format("{}: {} (ASR)", varDescr, val));
		}
		else {
			SaveDataToSettings(varName, varDescr, val);
			DisplayInfoMessage(std::format("{}: {}", varDescr, val));
		}
		};
	try
	{
		std::unique_lock lock(mtxScreen);
		std::shared_ptr<draw_settings> targetSetting = setScreen[screenID];
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

auto CRDFPlugin::GenerateDrawPosition(std::string callsign) -> draw_position
{
	// return radius=0 for no draw

	// randoms
	static std::random_device randomDevice;
	static std::mt19937 rdGenerator(randomDevice());
	static std::uniform_real_distribution<> disBearing(0.0, 360.0);
	static std::normal_distribution<> disDistance(0, 1.0);

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
			if (circleThreshold >= 0 && (lowPrecision > 0 || circlePrecision > 0)) {
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
			return draw_position(pos, radius);
		}
	}
	else if (drawController && controller.IsValid()) {
		auto pos = controller.GetPosition();
		return draw_position(pos, circleRadius);
	}
	return draw_position();
}

auto CRDFPlugin::TrackAudioTransmissionHandler(const nlohmann::json& data, const bool& rxEnd) -> void
{
	std::unique_lock tlock(mtxTransmission);
	std::string callsign = data["callsign"];
	auto it = curTransmission.find(callsign);
	if (it != curTransmission.end()) {
		if (rxEnd) {
			curTransmission.erase(it);
		}
	}
	else if (!rxEnd) {
		auto dp = GenerateDrawPosition(callsign);
		if (dp.radius > 0) {
			curTransmission[callsign] = dp;
		}
	}
	if (curTransmission.size()) {
		preTransmission = curTransmission;
	}
}

auto CRDFPlugin::TrackAudioChannelHandler(const nlohmann::json& data) -> void
{
	// parse message and returns number of total toggles
	// json format as described in TrackAudio SDK ["value"]
	// internal process in kHz
	std::unique_lock flock(mtxFrequency);
	curFrequencies.clear();
	for (auto& elem : data["rx"]) {
		std::string callsign = elem["pCallsign"];
		int freq = FrequencyFromHz(elem["pFrequencyHz"]);
		curFrequencies[callsign].frequency = freq;
	}
	for (auto& elem : data["tx"]) {
		std::string callsign = elem["pCallsign"];
		int freq = FrequencyFromHz(elem["pFrequencyHz"]);
		curFrequencies[callsign].frequency = freq;
		curFrequencies[callsign].tx = true;
	}
	flock.unlock();
	UpdateChannels();
}

auto CRDFPlugin::UpdateChannels(void) -> void
{
	std::shared_lock recLock(mtxFrequency);
	std::string myName = ControllerMyself().GetCallsign();
	int myFreq = FrequencyFromMHz(ControllerMyself().GetPrimaryFrequency());
	std::set<int> usingFrequencies = { myFreq };
	for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
		int chFreq = FrequencyFromMHz(chnl.GetFrequency());
		std::string chName = chnl.GetName();
		if (usingFrequencies.contains(chFreq)) { // skip frequencies already in use
			continue;
		}
		else if (chName == myName || chnl.GetIsPrimary() || chnl.GetIsAtis()) { // make sure primary and ATIS are not affected
			usingFrequencies.insert(chFreq);
			continue;
		}
		auto it1 = curFrequencies.find(chName);
		if (it1 != curFrequencies.end() && FrequencyIsSame(it1->second.frequency, chFreq)) {
			ToggleChannels(chnl, it1->second.tx, 1);
			usingFrequencies.insert(chFreq);
			continue;
		}
		else if (it1 == curFrequencies.end()) {
			auto it2 = std::find_if(curFrequencies.begin(), curFrequencies.end(),
				[chFreq](const auto& i) {return FrequencyIsSame(i.second.frequency, chFreq); }); // locate a matching freq
			if (it2 != curFrequencies.end()) {
				size_t posi = it2->first.find('_');
				if (chName.starts_with(it2->first.substr(0, posi))) {
					ToggleChannels(chnl, it2->second.tx, 1);
					usingFrequencies.insert(chFreq);
					continue;
				}
			}
		}
		ToggleChannels(chnl, 0, 0); // toggle off
	}
}

auto CRDFPlugin::ToggleChannels(EuroScopePlugIn::CGrountToAirChannel Channel, const int& tx, const int& rx) -> void
{
	// pass tx/rx = -1 to skip
	if (rx >= 0 && rx != (int)Channel.GetIsTextReceiveOn()) {
		Channel.ToggleTextReceive();
		DisplayDebugMessage(
			std::string("RX toggle: ") + Channel.GetName() + " frequency: " + std::to_string(Channel.GetFrequency())
		);
	}
	if (tx >= 0 && tx != (int)Channel.GetIsTextTransmitOn()) {
		Channel.ToggleTextTransmit();
		DisplayDebugMessage(
			std::string("TX toggle: ") + Channel.GetName() + " frequency: " + std::to_string(Channel.GetFrequency())
		);
	}
}

auto CRDFPlugin::GetDrawingParam(void) -> draw_settings const
{
	draw_settings res;
	try {
		std::shared_lock<std::shared_mutex> lock(mtxScreen);
		res = *setScreen.at(vidScreen);
	}
	catch (...) {
		DisplayDebugMessage(std::format("GetDrawingParam error, ID {}", (int)vidScreen));
	}
	return res;
}

auto CRDFPlugin::GetDrawStations(void) -> callsign_position
{
	std::shared_lock tlock(mtxTransmission);
	return curTransmission.empty() && GetAsyncKeyState(VK_MBUTTON) ? preTransmission : curTransmission;
}

auto CRDFPlugin::TrackAudioMessageHandler(const ix::WebSocketMessagePtr& msg) -> void
{
	try {
		DisplayDebugMessage(std::format("WS msg type {}: {}", (int)msg->type, msg->str));
		if (msg->type == ix::WebSocketMessageType::Message) {
			auto data = nlohmann::json::parse(msg->str);
			std::string msgType = data["type"];
			nlohmann::json msgValue = data["value"];
			if (msgType == "kRxBegin") {
				TrackAudioTransmissionHandler(msgValue, false);
			}
			else if (msgType == "kRxEnd") {
				TrackAudioTransmissionHandler(msgValue, true);
			}
			else if (msgType == "kFrequencyStateUpdate") {
				TrackAudioChannelHandler(msgValue);
			}
		}
		else if (msg->type == ix::WebSocketMessageType::Open) {
			// check for TrackAudio presense
			httplib::Client cli(std::format("http://{}", addressTrackAudio));
			cli.set_connection_timeout(TRACKAUDIO_TIMEOUT_SEC);
			if (auto res = cli.Get(TRACKAUDIO_PARAM_VERSION)) {
				if (res->status == 200 && res->body.size()) {
					DisplayInfoMessage(std::format("Connected to {} on {}.", res->body, addressTrackAudio));
				}
			}
		}
		else if (msg->type == ix::WebSocketMessageType::Error) {
			DisplayDebugMessage(std::format("WS msg ERROR! reason: {}, #retries: {}, wait_time: {}, http_status: {}",
				msg->errorInfo.reason, (int)msg->errorInfo.retries, msg->errorInfo.wait_time, msg->errorInfo.http_status));
		}
		else if (msg->type == ix::WebSocketMessageType::Close) {
			DisplayDebugMessage(std::format("WS msg CLOSE! code: {}, reason: {}",
				(int)msg->closeInfo.code, msg->closeInfo.reason));
			DisplayWarnMessage("TrackAudio WebSocket disconnected!");
		}
	}
	catch (std::exception& exc) {
		DisplayDebugMessage(exc.what());
	}
}

auto CRDFPlugin::OnRadarScreenCreated(const char* sDisplayName,
	bool NeedRadarContent,
	bool GeoReferenced,
	bool CanBeSaved,
	bool CanBeCreated)
	-> EuroScopePlugIn::CRadarScreen*
{
	size_t i = vecScreen.size(); // should not be -1
	DisplayInfoMessage(std::format("Radio Direction Finder plugin activated on {}.", sDisplayName));
	std::shared_ptr<CRDFScreen> screen = std::make_shared<CRDFScreen>(i);
	vecScreen.push_back(screen);
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
			LoadTrackAudioSettings();
			{
				std::unique_lock lock(mtxScreen); // cautious for overlapped lock
				setScreen.clear();
				setScreen[-1] = std::make_shared<draw_settings>(); // initialize default settings
			}
			LoadDrawingSettings(-1); // restore plugin settings
			for (auto& s : vecScreen) { // reload asr settings
				s->newAsrData.clear();
				LoadDrawingSettings(s->m_ID);
			}
			return true;
		}
		return ProcessDrawingCommand(sCommandLine);
	}
	catch (const std::exception& e)
	{
		DisplayWarnMessage(e.what());
	}
	return false;
}

auto CRDFPlugin::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) -> void
{
	if (!FlightPlan.IsValid() || ItemCode != TAG_ITEM_TYPE_RDF_STATE) return;
	std::string callsign = FlightPlan.GetCallsign();
	std::shared_lock tlock(mtxTransmission);
	if (preTransmission.contains(callsign)) {
		strcpy_s(sItemString, 2, "!");
	}
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
