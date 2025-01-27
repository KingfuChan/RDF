#pragma once

#include "stdafx.h"
#include "HiddenWindow.h"
#include "CRDFScreen.h"

// Plugin info
constexpr auto MY_PLUGIN_NAME = "RDF Plugin for Euroscope";
constexpr auto MY_PLUGIN_VERSION = "1.4.1";
constexpr auto MY_PLUGIN_DEVELOPER = "Kingfu Chan";
constexpr auto MY_PLUGIN_COPYRIGHT = "GPLv3 License, Copyright (c) 2023 Kingfu Chan";
// TrackAudio URLs and parameters
constexpr auto TRACKAUDIO_PARAM_VERSION = "/*";
constexpr auto TRACKAUDIO_PARAM_WS = "/ws";
constexpr auto TRACKAUDIO_TIMEOUT_SEC = 1;
constexpr auto TRACKAUDIO_HEARTBEAT_SEC = 30;
// Global settings
constexpr auto SETTING_LOG_LEVEL = "LogLevel"; // see plog::Severity
constexpr auto SETTING_ENDPOINT = "Endpoint";
constexpr auto SETTING_HELPER_MODE = "TrackAudioMode"; // Default: 1 (station sync TA -> RDF)
// Shared settings (ASR specific)
constexpr auto SETTING_RGB = "RGB";
constexpr auto SETTING_CONCURRENT_RGB = "ConcurrentTransmissionRGB";
constexpr auto SETTING_CIRCLE_RADIUS = "Radius";
constexpr auto SETTING_THRESHOLD = "Threshold";
constexpr auto SETTING_PRECISION = "Precision";
constexpr auto SETTING_LOW_ALTITUDE = "LowAltitude";
constexpr auto SETTING_HIGH_ALTITUDE = "HighAltitude";
constexpr auto SETTING_LOW_PRECISION = "LowPrecision";
constexpr auto SETTING_HIGH_PRECISION = "HighPrecision";
constexpr auto SETTING_DRAW_CONTROLLERS = "DrawControllers";
// Tag item type
const int TAG_ITEM_TYPE_RDF_STATE = 1001; // RDF state

// Constants
constexpr auto UNKNOWN_ERROR_MSG = "Unknown error!";
constexpr auto FREQUENCY_REDUNDANT = 199999; // kHz
const double pi = 3.141592653589793;
const double EarthRadius = 3438.0; // nautical miles, referred to internal CEuroScopeCoord
static constexpr auto GEOM_RAD_FROM_DEG(const double& deg) -> double { return deg * pi / 180.0; };
static constexpr auto GEOM_DEG_FROM_RAD(const double& rad) -> double { return rad / pi * 180.0; };

// Inline functions
inline static auto FrequencyFromMHz(const double& freq) -> int {
	return (int)round(freq * 1000.0);
}
inline static auto FrequencyFromHz(const double& freq) -> int {
	return (int)round(freq / 1000.0);
}
inline static auto FrequencyIsSame(const auto& freq1, const auto& freq2) -> bool { // return true if same frequency, frequency in kHz
	return abs(freq1 - freq2) <= 10;
}

// Draw position
typedef struct _draw_position {
	EuroScopePlugIn::CPosition position;
	double radius;
	_draw_position(void) :
		position(),
		radius(0) // invalid value
	{
	};
	_draw_position(EuroScopePlugIn::CPosition _position, double _radius) :
		position(_position),
		radius(_radius)
	{
	};
} draw_position;
typedef std::map<std::string, draw_position> callsign_position;
auto AddOffset(EuroScopePlugIn::CPosition& position, const double& heading, const double& distance) -> void;

// Draw settings
typedef struct _draw_settings {
	COLORREF rdfRGB;
	COLORREF rdfConcurRGB;
	int circleRadius;
	int circlePrecision;
	int circleThreshold;
	int lowAltitude;
	int highAltitude;
	int lowPrecision;
	int highPrecision;
	bool drawController;

	_draw_settings(void) {
		rdfRGB = RGB(255, 255, 255); // Default: white
		rdfConcurRGB = RGB(255, 0, 0); // Default: red
		circleRadius = 20; // Default: 20 (nautical miles or pixel), range: (0, +inf)
		circleThreshold = -1; // Default: -1 (always use pixel)
		circlePrecision = 0; // Default: no offset (nautical miles), range: [0, +inf)
		lowAltitude = 0; // Default: 0 (feet)
		lowPrecision = 0; // Default: 0 (nautical miles), range: [0, +inf)
		highAltitude = 0; // Default: 0 (feet)
		highPrecision = 0; // Default: 0 (nautical miles), range: [0, +inf)
		drawController = false;
	};
} draw_settings;

// Frequency & channel state
typedef struct _freq_state {
	std::optional<std::string> callsign; // can be empty
	bool tx = false;
} freq_state;
typedef struct _es_chnl_state {
	bool isPrim;
	bool isAtis;
	int frequency;
	bool rx;
	bool tx;
	_es_chnl_state(void) {
		isPrim = false;
		isAtis = false;
		frequency = FREQUENCY_REDUNDANT;
		rx = false;
		tx = false;
	}
	_es_chnl_state(EuroScopePlugIn::CGrountToAirChannel channel) {
		isPrim = channel.GetIsPrimary();
		isAtis = channel.GetIsAtis();
		frequency = FrequencyFromMHz(channel.GetFrequency());
		rx = channel.GetIsTextReceiveOn();
		tx = channel.GetIsTextTransmitOn();
	}
} chnl_state;

class CRDFPlugin : public EuroScopePlugIn::CPlugIn
{
private:
	friend class CRDFScreen;

	// screen controls and drawing params
	std::vector<std::shared_ptr<CRDFScreen>> vecScreen; // index is screen ID (incremental int)
	std::map<int, std::shared_ptr<draw_settings>> setScreen; // screeID -> settings, ID=-1 used as plugin setting
	std::atomic_int vidScreen;
	std::shared_mutex mtxScreen;
	auto GetDrawingParam(void) -> draw_settings const;

	// drawing records
	std::shared_mutex mtxTransmission;
	callsign_position curTransmission;
	callsign_position preTransmission;

	// TrackAudio WebSocket
	std::atomic_int modeTrackAudio; // -1: no RDF, 0: no station sync, 1: station sync TA -> RDF, 2: station sync TA <-> RDF
	std::string addressTrackAudio;
	ix::WebSocket socketTrackAudio;
	auto TrackAudioMessageHandler(const ix::WebSocketMessagePtr& msg) -> void;

	// AFV standalone client controls
	HWND hiddenWindowRDF = NULL;
	HWND hiddenWindowAFV = NULL;
	WNDCLASS windowClassRDF = {
	   NULL,
	   HiddenWindowRDF,
	   NULL,
	   NULL,
	   GetModuleHandle(NULL),
	   NULL,
	   NULL,
	   NULL,
	   NULL,
	   "RDFHiddenWindowClass"
	};
	WNDCLASS windowClassAFV = {
	   NULL,
	   HiddenWindowAFV,
	   NULL,
	   NULL,
	   GetModuleHandle(NULL),
	   NULL,
	   NULL,
	   NULL,
	   NULL,
	   "AfvBridgeHiddenWindowClass"
	};

	// settings related functions
	auto GetRGB(COLORREF& color, const std::string& settingValue) -> void;
	auto LoadTrackAudioSettings(void) -> void;
	auto LoadDrawingSettings(const int& screenID = -1) -> void;
	auto ProcessDrawingCommand(const std::string& command, const int& screenID = -1) -> bool;

	// functional things 
	auto GenerateDrawPosition(std::string callsign) -> draw_position;
	auto TrackAudioTransmissionHandler(const nlohmann::json& data, const bool& rxEnd) -> void;
	auto TrackAudioStationStatesHandler(const nlohmann::json& data) -> void;
	auto TrackAudioStationStateUpdateHandler(const nlohmann::json& data) -> void;
	auto SelectGroundToAirChannel(const std::optional<std::string>& callsign, const std::optional<int>& frequency) -> EuroScopePlugIn::CGrountToAirChannel;
	auto UpdateChannel(const std::optional<std::string>& callsign, const std::optional<chnl_state>& channelState) -> void;
	auto ToggleChannel(EuroScopePlugIn::CGrountToAirChannel Channel, const std::optional<bool>& rx, const std::optional<bool>& tx) -> void;

	// messages
	inline auto DisplayDebugMessage(const std::string& msg) -> void {
#ifdef _DEBUG
		DisplayUserMessage("RDF-DEBUG", "", msg.c_str(), true, true, true, false, false);
#endif // _DEBUG
	};
	inline auto DisplayInfoMessage(const std::string& msg) -> void {
		DisplayUserMessage("Message", "RDF Plugin", msg.c_str(), false, false, false, false, false);
	}
	inline auto DisplayWarnMessage(const std::string& msg) -> void {
		DisplayUserMessage("Message", "RDF Plugin", msg.c_str(), true, true, true, false, false);
	}

public:
	CRDFPlugin();
	~CRDFPlugin();
	auto GetDrawStations(void) -> callsign_position;
	auto HiddenWndProcessRDFMessage(const std::string& message) -> void;
	auto HiddenWndProcessAFVMessage(const std::string& message) -> void;
	virtual auto OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) -> EuroScopePlugIn::CRadarScreen*;
	virtual auto OnCompileCommand(const char* sCommandLine) -> bool;
	virtual auto OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) -> void;
};
