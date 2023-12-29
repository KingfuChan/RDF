#pragma once

#include "stdafx.h"
#include "HiddenWindow.h"
#include "CRDFScreen.h"

const std::string MY_PLUGIN_NAME = "RDF Plugin for Euroscope";
const std::string MY_PLUGIN_VERSION = "1.3.5";
const std::string MY_PLUGIN_DEVELOPER = "Kingfu Chan, Claus Hemberg Joergensen";
const std::string MY_PLUGIN_COPYRIGHT = "Free to be distributed as source code";

constexpr auto VECTORAUDIO_PARAM_VERSION = "/*";
constexpr auto VECTORAUDIO_PARAM_TRANSMIT = "/transmitting";
constexpr auto VECTORAUDIO_PARAM_TX = "/tx";
constexpr auto VECTORAUDIO_PARAM_RX = "/rx";

constexpr auto SETTING_VECTORAUDIO_ADDRESS = "VectorAudioAddress";
constexpr auto SETTING_VECTORAUDIO_TIMEOUT = "VectorAudioTimeout";
constexpr auto SETTING_VECTORAUDIO_POLL_INTERVAL = "VectorAudioPollInterval";
constexpr auto SETTING_VECTORAUDIO_RETRY_INTERVAL = "VectorAudioRetryInterval";
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

typedef struct {
	EuroScopePlugIn::CPosition position;
	double radius;
} _draw_position;
typedef std::map<std::string, _draw_position> callsign_position;

class CRDFPlugin : public EuroScopePlugIn::CPlugIn
{
private:
	friend class CRDFScreen;

	// drawing params
	COLORREF rdfRGB, rdfConcurRGB;
	int circleRadius;
	int circlePrecision;
	int circleThreshold;
	int lowAltitude;
	int highAltitude;
	int lowPrecision;
	int highPrecision;
	bool drawController;
	// drawing records
	callsign_position activeStations;
	callsign_position previousStations;

	// offset params
	std::random_device randomDevice;
	std::mt19937 rdGenerator;
	std::uniform_real_distribution<> disBearing;
	std::normal_distribution<> disDistance;

	// VectorAudio controls
	std::string addressVectorAudio;
	int connectionTimeout, pollInterval, retryInterval;
	// thread controls
	std::unique_ptr<std::thread> threadVectorAudioMain, threadVectorAudioTXRX;
	std::atomic_bool threadMainRunning, threadMainClosed,
		threadTXRXRunning, threadTXRXClosed;
	void VectorAudioMainLoop(void);
	void VectorAudioTXRXLoop(void);

	// AFV standalone client controls
	HWND hiddenWindowRDF = NULL;
	HWND hiddenWindowAFV = NULL;
	std::mutex messageLock; // Lock for the message queue
	// Internal message quque
	std::queue<std::set<std::string>> messages;
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

	void AddOffset(EuroScopePlugIn::CPosition& position, double heading, double distance);
	void GetRGB(COLORREF& color, const char* settingValue);
	void LoadSettings(void);
	bool ParseSharedSettings(const std::string& command, CRDFScreen* screen = nullptr);
	void ProcessRDFQueue(void);

	void UpdateVectorAudioChannels(std::string line, bool mode_tx);
	void ToggleChannels(EuroScopePlugIn::CGrountToAirChannel Channel, int tx = -1, int rx = -1);

	// messages
	inline void DisplayDebugMessage(std::string msg) {
#ifdef _DEBUG
		DisplayUserMessage("RDF-DEBUG", "", msg.c_str(), true, true, true, false, false);
#endif // _DEBUG
	}

	inline void DisplayInfoMessage(std::string msg) {
		DisplayUserMessage("Message", "RDF Plugin", msg.c_str(), false, false, false, false, false);
	}

	inline void DisplayWarnMessage(std::string msg) {
		DisplayUserMessage("Message", "RDF Plugin", msg.c_str(), true, true, true, false, false);
	}

public:
	CRDFPlugin();
	virtual ~CRDFPlugin();
	void HiddenWndProcessRDFMessage(std::string message);
	void HiddenWndProcessAFVMessage(std::string message);
	virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated);
	virtual bool OnCompileCommand(const char* sCommandLine);

};
