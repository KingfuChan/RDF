#pragma once

#include "stdafx.h"
#include "HiddenWindow.h"
#include "CRDFScreen.h"

// Plugin info
constexpr auto MY_PLUGIN_NAME = "RDF Plugin for Euroscope";
constexpr auto MY_PLUGIN_VERSION = "1.4.0";
constexpr auto MY_PLUGIN_DEVELOPER = "Kingfu Chan";
constexpr auto MY_PLUGIN_COPYRIGHT = "GPLv3 License, Copyright (c) 2023 Kingfu Chan";
// TrackAudio URLs and parameters
constexpr auto TRACKAUDIO_PARAM_VERSION = "/*";
constexpr auto TRACKAUDIO_PARAM_WS = "/ws";
constexpr auto TRACKAUDIO_TIMEOUT_SEC = 1;
constexpr auto TRACKAUDIO_HEARTBEAT_SEC = 30;
// Global settings
constexpr auto SETTING_ENDPOINT = "Endpoint";
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

typedef struct _draw_position {
	EuroScopePlugIn::CPosition position;
	double radius;
	_draw_position(void) :
		position(),
		radius(0) // invalid value
	{};
	_draw_position(EuroScopePlugIn::CPosition _position, double _radius) :
		position(_position),
		radius(_radius)
	{};
} draw_position;
typedef std::map<std::string, draw_position> callsign_position;
auto AddOffset(EuroScopePlugIn::CPosition& position, const double& heading, const double& distance) -> void;

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

typedef struct _freq_data {
	int frequency = 199999;
	bool tx = false;
} freq_data;
typedef std::map<std::string, freq_data> callsign_frequency;

class CRDFPlugin : public EuroScopePlugIn::CPlugIn
{
private:
	friend class CRDFScreen;

	// screen controls and drawing params
	std::vector<std::shared_ptr<CRDFScreen>> screenVec; // index is screen ID (incremental int)
	std::map<int, std::shared_ptr<draw_settings>> screenSettings; // screeID -> settings, ID=-1 used as plugin setting
	std::atomic_int activeScreenID;
	std::shared_mutex screenLock;
	auto GetDrawingParam(void) -> draw_settings const;

	// drawing records and transmitting frequency records
	std::shared_mutex mtxTransmission, mtxFrequency;
	callsign_position activeStations;
	callsign_position previousStations;
	callsign_frequency activeFrequencies;

	// randoms
	std::random_device randomDevice;
	std::mt19937 rdGenerator;
	std::uniform_real_distribution<> disBearing;
	std::normal_distribution<> disDistance;

	// TrackAudio WebSocket
	std::string addressTrackAudio;
	ix::WebSocket ixTrackAudioSocket;
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
	auto ParseDrawingSettings(const std::string& command, const int& screenID = -1) -> bool;

	// functional things 
	auto GenerateDrawPosition(std::string callsign) -> draw_position;
	auto TrackAudioTransmissionHandler(const nlohmann::json& data, const bool& rxEnd) -> void;
	auto TrackAudioChannelHandler(const nlohmann::json& data) -> void;
	auto UpdateChannels(void) -> void;
	auto ToggleChannels(EuroScopePlugIn::CGrountToAirChannel Channel, const int& tx = -1, const int& rx = -1) -> void;

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
