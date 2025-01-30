#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

CRDFPlugin::CRDFPlugin()
	: EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
		MY_PLUGIN_NAME,
		MY_PLUGIN_VERSION,
		MY_PLUGIN_DEVELOPER,
		MY_PLUGIN_COPYRIGHT)
{
	// initialize plog
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	HMODULE pluginModule = AfxGetInstanceHandle();
	TCHAR pBuffer[MAX_PATH] = { 0 };
	DWORD moduleNameRes = GetModuleFileName(pluginModule, pBuffer, sizeof(pBuffer) / sizeof(TCHAR) - 1);
	std::filesystem::path dllPath = moduleNameRes != 0 ? pBuffer : "";
	auto logPath = dllPath.parent_path() / "RDFPlugin.log";
	static plog::RollingFileAppender<plog::TxtFormatterUtcTime> rollingAppender(logPath.c_str(), 1000000, 1); // 1 MB of 1 file
#ifdef _DEBUG
	auto severity = plog::verbose;
#else
	auto severity = plog::none;
#endif // _DEBUG
	plog::init(severity, &rollingAppender);
	try {
		auto cstrLogLevel = GetDataFromSettings(SETTING_LOG_LEVEL);
		if (cstrLogLevel != nullptr) {
			severity = max(severity, plog::severityFromString(cstrLogLevel));
			plog::get()->setMaxSeverity(severity);
		}
		PLOGI << "log level is set to " << plog::severityToString(severity);
	}
	catch (...) {
		PLOGE << "invalid plog severity";
	}

	// RDF window
	PLOGD << "creating AFV hidden windows";
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
		std::string logMsg = "Unable to create AFV hidden window for RDF.";
		PLOGE << logMsg;
		DisplayMessageUnread(logMsg);
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
		std::string logMsg = "Unable to create AFV hidden window for bridge.";
		PLOGE << logMsg;
		DisplayMessageUnread(logMsg);
	}

	// registration
	RegisterTagItemType("RDF state", TAG_ITEM_TYPE_RDF_STATE);

	// initialize default settings
	PLOGD << "initializing default settings";
	ix::initNetSystem();
	socketTrackAudio.setHandshakeTimeout(TRACKAUDIO_TIMEOUT_SEC);
	socketTrackAudio.setMaxWaitBetweenReconnectionRetries(TRACKAUDIO_HEARTBEAT_SEC * 2000); // ms
	socketTrackAudio.setPingInterval(TRACKAUDIO_HEARTBEAT_SEC);
	socketTrackAudio.setOnMessageCallback(std::bind_front(&CRDFPlugin::TrackAudioMessageHandler, this));

	std::unique_lock dlock(mtxDrawSettings);
	pluginDrawSettings = std::make_shared<RDFCommon::draw_settings>();
	dlock.unlock();

	LoadTrackAudioSettings();
	LoadDrawingSettings(std::nullopt);

	auto logMsg = std::format("Version {} Loaded.", MY_PLUGIN_VERSION);
	PLOGI << logMsg;
	DisplayMessageSilent(logMsg);
}

CRDFPlugin::~CRDFPlugin()
{
	// disconnect TrackAudio connection
	PLOGD << "stopping TrackAudio WS";
	socketTrackAudio.stop();
	ix::uninitNetSystem();

	PLOGD << "destroying AFV hidden windows";
	if (hiddenWindowRDF != nullptr) {
		DestroyWindow(hiddenWindowRDF);
	}
	UnregisterClass("RDFHiddenWindowClass", nullptr);
	if (hiddenWindowAFV != nullptr) {
		DestroyWindow(hiddenWindowAFV);
	}
	UnregisterClass("AfvBridgeHiddenWindowClass", nullptr);

	PLOGI << "RDFPlugin is unloaded";
}

auto CRDFPlugin::HiddenWndProcessRDFMessage(const std::string& message) -> void
{
	PLOGV << "AFV message: " << message;
	std::unique_lock tlock(mtxTransmission);
	if (message.size()) {
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
		// add new station
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
	PLOGV << "AFV message: " << message;
	if (!message.size()) return;
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
	catch (std::exception const& e) {
		PLOGE << "AFV msg parse error: " << message << ", " << e.what();
		return;
	}
	catch (...) {
		PLOGE << "Error parsing AFV message: " << message;
		return;
	}

	// update channel
	RDFCommon::chnl_state state;
	state.frequency = msgFrequency;
	state.rx = receiveX;
	state.tx = transmitX;
	UpdateChannel(std::nullopt, state);
}

auto CRDFPlugin::LoadTrackAudioSettings(void) -> void
{
	// get TrackAudio config
	PLOGD << "loading TrackAudio settings";
	addressTrackAudio = "127.0.0.1:49080";
	modeTrackAudio = 1;
	try {
		const char* cstrEndpoint = GetDataFromSettings(SETTING_ENDPOINT);
		if (cstrEndpoint != nullptr) {
			addressTrackAudio = cstrEndpoint;
		}
		const char* cstrMode = GetDataFromSettings(SETTING_HELPER_MODE);
		if (cstrMode != nullptr) {
			int mode = std::stoi(cstrMode);
			if (mode >= -1 && mode <= 2) {
				modeTrackAudio = mode;
			}
		}
		PLOGI << "TrackAudio address: " << addressTrackAudio << ", mode: " << modeTrackAudio.load();
	}
	catch (std::exception const& e)
	{
		PLOGE << "Error: " << e.what();
		DisplayMessageUnread(std::string("Error: ") + e.what());
	}
	catch (...)
	{
		PLOGE << UNKNOWN_ERROR_MSG;
		DisplayMessageUnread(UNKNOWN_ERROR_MSG);
	}

	// stop TrackAudio WebSocket
	PLOGD << "stopping TrackAudio WebSocket";
	socketTrackAudio.stop();

	// clears records
	PLOGD << "clearing records";
	std::unique_lock tlock(mtxTransmission);
	curTransmission.clear();
	preTransmission.clear();
	tlock.unlock();

	// initialize TrackAudio WebSocket
	socketTrackAudio.setUrl(std::format("ws://{}{}", addressTrackAudio, TRACKAUDIO_PARAM_WS));
	if (modeTrackAudio != -1) {
		UpdateChannel(std::nullopt, std::nullopt);
		socketTrackAudio.start();
		PLOGD << "TrackAudio WebSocket started";
	}
}

auto CRDFPlugin::LoadDrawingSettings(std::optional<std::shared_ptr<CRDFScreen>> screenPtr) -> void
{
	// pass nullopt to load plugin drawing settings, otherwise use ASR settings
	// Schematic: high altitude/precision optional. low altitude used for filtering regardless of others
	// threshold < 0 will use circleRadius in pixel, circlePrecision for offset, low/high settings ignored
	// lowPrecision > 0 and highPrecision > 0 and lowAltitude < highAltitude, will override circleRadius and circlePrecision with dynamic precision/radius
	// lowPrecision > 0 but not meeting the above, will use lowPrecision (> 0) or circlePrecision

	PLOGI << "loading drawing settings, is ASR: " << (bool)screenPtr;
	auto GetSetting = [&](const auto& varName) -> std::string {
		if (screenPtr && (*screenPtr)->m_Opened) {
			auto ds = (*screenPtr)->GetDataFromAsr(varName);
			if (ds != nullptr) {
				return ds;
			}
		} // fallback onto plugin setting
		auto d = GetDataFromSettings(varName);
		return d == nullptr ? "" : d;
		};

	try
	{
		std::unique_lock<std::shared_mutex> lock(mtxDrawSettings);
		// initialize settings
		std::shared_ptr<RDFCommon::draw_settings> targetSetting;
		if (screenPtr) { // use ASR
			targetSetting = (*screenPtr)->m_DrawSettings;
			PLOGD << "using ASR settings";
		}
		else {
			targetSetting = pluginDrawSettings;
			PLOGD << "using plugin settings";
		}

		auto cstrRGB = GetSetting(SETTING_RGB);
		if (cstrRGB.size())
		{
			RDFCommon::GetRGB(targetSetting->rdfRGB, cstrRGB);
		}
		cstrRGB = GetSetting(SETTING_CONCURRENT_RGB);
		if (cstrRGB.size())
		{
			RDFCommon::GetRGB(targetSetting->rdfConcurRGB, cstrRGB);
		}
		auto cstrRadius = GetSetting(SETTING_CIRCLE_RADIUS);
		if (cstrRadius.size())
		{
			int parsedRadius = std::stoi(cstrRadius);
			if (parsedRadius > 0) {
				targetSetting->circleRadius = parsedRadius;
				PLOGV << SETTING_CIRCLE_RADIUS << ": " << targetSetting->circleRadius;
			}
		}
		auto cstrThreshold = GetSetting(SETTING_THRESHOLD);
		if (cstrThreshold.size())
		{
			targetSetting->circleThreshold = std::stoi(cstrThreshold);
			PLOGV << SETTING_THRESHOLD << ": " << targetSetting->circleThreshold;
		}
		auto cstrPrecision = GetSetting(SETTING_PRECISION);
		if (cstrPrecision.size())
		{
			int parsedPrecision = std::stoi(cstrPrecision);
			if (parsedPrecision >= 0) {
				targetSetting->circlePrecision = parsedPrecision;
				PLOGV << SETTING_PRECISION << ": " << targetSetting->circlePrecision;
			}
		}
		auto cstrLowAlt = GetSetting(SETTING_LOW_ALTITUDE);
		if (cstrLowAlt.size())
		{
			targetSetting->lowAltitude = std::stoi(cstrLowAlt);
			PLOGV << SETTING_LOW_ALTITUDE << ": " << targetSetting->lowAltitude;
		}
		auto cstrHighAlt = GetSetting(SETTING_HIGH_ALTITUDE);
		if (cstrHighAlt.size())
		{
			int parsedAlt = std::stoi(cstrHighAlt);
			if (parsedAlt > 0) {
				targetSetting->highAltitude = parsedAlt;
				PLOGV << SETTING_HIGH_ALTITUDE << ": " << targetSetting->highAltitude;
			}
		}
		auto cstrLowPrecision = GetSetting(SETTING_LOW_PRECISION);
		if (cstrLowPrecision.size())
		{
			int parsedPrecision = std::stoi(cstrLowPrecision);
			if (parsedPrecision >= 0) {
				targetSetting->lowPrecision = parsedPrecision;
				PLOGV << SETTING_LOW_PRECISION << ": " << targetSetting->lowPrecision;
			}
		}
		auto cstrHighPrecision = GetSetting(SETTING_HIGH_PRECISION);
		if (cstrHighPrecision.size())
		{
			int parsedPrecision = std::stoi(cstrHighPrecision);
			if (parsedPrecision >= 0) {
				targetSetting->highPrecision = parsedPrecision;
				PLOGV << SETTING_HIGH_PRECISION << ": " << targetSetting->highPrecision;
			}
		}
		auto cstrController = GetSetting(SETTING_DRAW_CONTROLLERS);
		if (cstrController.size())
		{
			targetSetting->drawController = (bool)std::stoi(cstrController);
			PLOGV << SETTING_DRAW_CONTROLLERS << ": " << targetSetting->drawController;
		}
		PLOGD << "drawing settings loaded";
	}
	catch (std::exception const& e)
	{
		PLOGE << "Error: " << e.what();
		DisplayMessageUnread(std::string("Error: ") + e.what());
	}
	catch (...)
	{
		PLOGE << UNKNOWN_ERROR_MSG;
		DisplayMessageUnread(UNKNOWN_ERROR_MSG);
	}
}

auto CRDFPlugin::GenerateDrawPosition(std::string callsign) -> RDFCommon::draw_position
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
	std::shared_lock dlock(mtxDrawSettings);
	int circleRadius = screenDrawSettings->circleRadius;
	int circlePrecision = screenDrawSettings->circlePrecision;
	int circleThreshold = screenDrawSettings->circleThreshold;
	int lowAltitude = screenDrawSettings->lowAltitude;
	int highAltitude = screenDrawSettings->highAltitude;
	int lowPrecision = screenDrawSettings->lowPrecision;
	int highPrecision = screenDrawSettings->highPrecision;
	bool drawController = screenDrawSettings->drawController;
	dlock.unlock();
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
				RDFCommon::AddOffset(pos, bearing, distance);
			}
			return RDFCommon::draw_position(pos, radius);
		}
	}
	else if (drawController && controller.IsValid()) {
		auto pos = controller.GetPosition();
		return RDFCommon::draw_position(pos, circleRadius);
	}
	return RDFCommon::draw_position();
}

auto CRDFPlugin::TrackAudioTransmissionHandler(const nlohmann::json& data, const bool& rxEnd) -> void
{
	// handler for "kRxBegin" & "kRxEnd"
	// pass rxEnd = true for "kRxEnd"
	std::unique_lock tlock(mtxTransmission);
	std::string callsign = data.at("callsign");
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

auto CRDFPlugin::TrackAudioStationStatesHandler(const nlohmann::json& data) -> void
{
	// handler for "kStationStates" <- "kGetStationStates" process
	// deal with all station states and update ES channels
	// data is json["value"]
	// frequencies in kHz
	for (auto& station : data.at("stations")) {
		if (station.at("type") == "kStationStateUpdate") {
			TrackAudioStationStateUpdateHandler(station.at("value"));
		}
	}
}

auto CRDFPlugin::TrackAudioStationStateUpdateHandler(const nlohmann::json& data) -> void
{
	// handler for "kStationStateUpdate"
	// used for update message and for "kStationStates" sections
	// data is json["value"]
	// frequencies in kHz
	if (GetConnectionType() != EuroScopePlugIn::CONNECTION_TYPE_DIRECT) {
		return; // prevent conflict with multiple ES instances. Since AFV hidden window it unique, only disable TrackAudio
	}
	std::string callsign = data.value("callsign", "");
	RDFCommon::chnl_state state;
	state.frequency = FrequencyFromHz(data.value("frequency", FREQUENCY_REDUNDANT));
	state.rx = data.value("rx", false);
	state.tx = data.value("tx", false);
	UpdateChannel(callsign.size() ? std::optional<std::string>(callsign) : std::nullopt, state);
}

auto CRDFPlugin::SelectGroundToAirChannel(const std::optional<std::string>& callsign, const std::optional<int>& frequency) -> EuroScopePlugIn::CGrountToAirChannel
{
	if (callsign && frequency) { // find precise match
		for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
			if (*callsign == chnl.GetName() && FrequencyIsSame(FrequencyFromMHz(chnl.GetFrequency()), *frequency)) {
				PLOGD << "precise match is found: " << *callsign << " - " << *frequency;
				return chnl;
			}
		}
	}
	else if (callsign) { // match callsign only
		for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
			if (*callsign == chnl.GetName()) {
				PLOGD << "callsign match is found: " << *callsign << " - " << FrequencyFromMHz(chnl.GetFrequency());
				return chnl;
			}
		}
	}
	if (frequency) { // find matching frequency that is nearest prim
		// make a copy of EuroScope data for comparison
		std::map<std::string, RDFCommon::chnl_state> allChannels; // channel name (sorted) -> chnl_state
		for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
			allChannels[chnl.GetName()] = RDFCommon::chnl_state(chnl);
		}
		// get primary frequency & callsign
		const auto primChannel = std::find_if(allChannels.begin(), allChannels.end(), [&](const auto& chnl) {
			return chnl.second.isPrim;
			});
		if (primChannel != allChannels.end()) {
			// calculate distance of same frequency
			std::map<std::string, int> nameDistance; // channel name -> distance to prim
			int primDistance = std::distance(allChannels.begin(), primChannel);
			for (auto it = allChannels.begin(); it != allChannels.end(); it++) {
				if (FrequencyIsSame(it->second.frequency, *frequency)) {
					nameDistance[it->first] = abs(primDistance - std::distance(allChannels.begin(), it));
				}
			}
			// find the closest station
			auto minName = std::min_element(nameDistance.begin(), nameDistance.end(), [](const auto& nd1, const auto& nd2) {
				return nd1.second < nd2.second;
				});
			if (minName != nameDistance.end()) {
				for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
					if (minName->first == chnl.GetName() && FrequencyIsSame(FrequencyFromMHz(chnl.GetFrequency()), *frequency)) {
						PLOGD << "frequency match is found nearest prim, callsign: " << chnl.GetName();
						return chnl;
					}
				}
			}
		}
		else { // prim not set, use the first frequency match
			for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
				if (FrequencyIsSame(FrequencyFromMHz(chnl.GetFrequency()), *frequency)) {
					PLOGD << "frequency match is found without prim, callsign: " << chnl.GetName();
					return chnl;
				}
			}
		}
	}
	PLOGD << "not found";
	return EuroScopePlugIn::CGrountToAirChannel();
}

auto CRDFPlugin::UpdateChannel(const std::optional<std::string>& callsign, const std::optional<RDFCommon::chnl_state>& channelState) -> void
{
	// note: EuroScope channels allow duplication in channel name, but name <-> frequency pair is unique.
	if (channelState) {
		PLOGD << callsign.value_or("NULL") << " - " << channelState->frequency;
		auto chnl = SelectGroundToAirChannel(callsign, channelState->frequency);
		if (chnl.IsValid()) {
			ToggleChannel(chnl, channelState->rx, channelState->tx);
		}
	}
	else { // doesn't specify channel or frequency, deactivate all channels
		PLOGD << "deactivating all";
		for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
			ToggleChannel(chnl, false, false); // check for prim/atis will be done inside
		}
	}
}

auto CRDFPlugin::ToggleChannel(EuroScopePlugIn::CGrountToAirChannel Channel, const std::optional<bool>& rx, const std::optional<bool>& tx) -> void
{
	if (!Channel.IsValid()) {
		PLOGD << "invalid CGrountToAirChannel, skipping";
		return;
	}
	else if (Channel.GetIsAtis() || Channel.GetIsPrimary()) {
		PLOGD << "skipping, atis: " << Channel.GetIsAtis() << " prim: " << Channel.GetIsPrimary();
		return;
	}
	if (rx && *rx != Channel.GetIsTextReceiveOn()) {
		Channel.ToggleTextReceive();
		std::string logMsg = std::format("RX toggle: {} frequency: {} ", Channel.GetName(), std::to_string(Channel.GetFrequency()));
		PLOGI << logMsg;
		DisplayMessageDebug(logMsg);
	}
	if (tx && *tx != Channel.GetIsTextTransmitOn()) {
		Channel.ToggleTextTransmit();
		std::string logMsg = std::format("TX toggle: {} frequency: {} ", Channel.GetName(), std::to_string(Channel.GetFrequency()));
		PLOGI << logMsg;
		DisplayMessageDebug(logMsg);
	}
}

auto CRDFPlugin::GetDrawStations(void) -> RDFCommon::callsign_position
{
	std::shared_lock tlock(mtxTransmission);
	return curTransmission.empty() && GetAsyncKeyState(VK_MBUTTON) ? preTransmission : curTransmission;
}

auto CRDFPlugin::TrackAudioMessageHandler(const ix::WebSocketMessagePtr& msg) -> void
{
	try {
		if (msg->type == ix::WebSocketMessageType::Message) {
			PLOGV << "WS MSG: " << msg->str;
			auto data = nlohmann::json::parse(msg->str);
			std::string msgType = data["type"];
			nlohmann::json msgValue = data["value"];
			if (msgType == "kRxBegin") {
				TrackAudioTransmissionHandler(msgValue, false);
			}
			else if (msgType == "kRxEnd") {
				TrackAudioTransmissionHandler(msgValue, true);
			}
			else if (msgType == "kStationStateUpdate" && modeTrackAudio > 0) { // only handle with sync on
				TrackAudioStationStateUpdateHandler(msgValue);
			}
			else if (msgType == "kStationStates" && modeTrackAudio > 0) {// only handle with sync on
				TrackAudioStationStatesHandler(msgValue);
			}
		}
		else if (msg->type == ix::WebSocketMessageType::Open) {
			// check for TrackAudio presense
			PLOGI << "WS OPEN";
			httplib::Client cli(std::format("http://{}", addressTrackAudio));
			cli.set_connection_timeout(TRACKAUDIO_TIMEOUT_SEC);
			if (auto res = cli.Get(TRACKAUDIO_PARAM_VERSION)) {
				if (res->status == 200 && res->body.size()) {
					auto logMsg = std::format("Connected to {} on {}.", res->body, addressTrackAudio);
					PLOGI << logMsg;
					DisplayMessageSilent(logMsg);
				}
			}
		}
		else if (msg->type == ix::WebSocketMessageType::Error) {
			auto logMsg = std::format("WS ERROR! reason: {}, #retries: {}, wait_time: {}, http_status: {}",
				msg->errorInfo.reason, (int)msg->errorInfo.retries, msg->errorInfo.wait_time, msg->errorInfo.http_status);
			PLOGW << logMsg;
			DisplayMessageDebug(logMsg);
		}
		else if (msg->type == ix::WebSocketMessageType::Close) {
			auto logMsg = std::format("WS CLOSE! code: {}, reason: {}", (int)msg->closeInfo.code, msg->closeInfo.reason);
			PLOGI << logMsg;
			DisplayMessageDebug(logMsg);
			DisplayMessageUnread("TrackAudio WebSocket disconnected!");
		}
	}
	catch (std::exception const& e) {
		PLOGE << e.what();
	}
	catch (...) {
		PLOGE << UNKNOWN_ERROR_MSG;
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
	auto logMsg = std::format("RDF plugin is activated on {}.", sDisplayName);
	PLOGI << logMsg;
	DisplayMessageSilent(logMsg);
	std::shared_ptr<CRDFScreen> screen = std::make_shared<CRDFScreen>(shared_from_this(), i);
	vecScreen.push_back(screen);
	return screen.get();
}

auto CRDFPlugin::OnCompileCommand(const char* sCommandLine) -> bool
{
	std::string cmd = sCommandLine;
	std::smatch match; // all regular expressions will ignore cases
	PLOGV << "command: " << cmd;
	try
	{
		std::regex rxReload(R"(^.RDF RELOAD$)", std::regex_constants::icase);
		if (std::regex_match(cmd, match, rxReload)) {
			LoadTrackAudioSettings();
			{
				std::unique_lock lock(mtxDrawSettings);
				pluginDrawSettings.reset(new RDFCommon::draw_settings);
			}
			LoadDrawingSettings(std::nullopt); // restore plugin settings
			for (auto& s : vecScreen) { // reload asr settings
				s->newAsrData.clear();
				LoadDrawingSettings(s);
			}
			return true;
		}
		std::regex rxRefresh(R"(^.RDF REFRESH$)", std::regex_constants::icase);
		if (std::regex_match(cmd, match, rxRefresh)) {
			PLOGD << "refreshing RDF records and station states";
			std::unique_lock tlock(mtxTransmission);
			curTransmission.clear();
			preTransmission.clear();
			tlock.unlock();
			UpdateChannel(std::nullopt, std::nullopt); // deactivate all channels;
			nlohmann::json jmsg;
			jmsg["type"] = "kGetStationStates";
			socketTrackAudio.send(jmsg.dump());
			PLOGD << "kGetStationStates is sent via WS";
			return true;
		}
	}
	catch (std::exception const& e)
	{

		PLOGE << "Error: " << e.what();
		DisplayMessageUnread(std::string("Error: ") + e.what());
	}
	catch (...) {
		PLOGE << UNKNOWN_ERROR_MSG;
		DisplayMessageUnread(UNKNOWN_ERROR_MSG);
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
