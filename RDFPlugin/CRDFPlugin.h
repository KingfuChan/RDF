#pragma once

#include "stdafx.h"
#include "HiddenWindow.h"

const std::string MY_PLUGIN_NAME = "RDF Plugin for Euroscope";
const std::string MY_PLUGIN_VERSION = "1.3.5";
const std::string MY_PLUGIN_DEVELOPER = "Kingfu Chan, Claus Hemberg Joergensen";
const std::string MY_PLUGIN_COPYRIGHT = "Free to be distributed as source code";

typedef struct {
	EuroScopePlugIn::CPosition position;
	double radius;
} _draw_position;
typedef std::map<std::string, _draw_position> callsign_position;

class CRDFPlugin : public EuroScopePlugIn::CPlugIn
{
private:

	int circleRadius;
	int circlePrecision;
	int lowAltitude;
	int highAltitude;
	int lowPrecision;
	int highPrecision;

	std::random_device randomDevice;
	std::mt19937 rdGenerator;
	std::uniform_real_distribution<> disBearing;
	std::normal_distribution<> disDistance;

	std::string addressVectorAudio;
	int connectionTimeout, pollInterval, retryInterval;
	// thread controls
	std::unique_ptr<std::thread> threadVectorAudioMain, threadVectorAudioTXRX;
	std::atomic_bool threadMainRunning, threadMainClosed,
		threadTXRXRunning, threadTXRXClosed;
	void VectorAudioMainLoop(void);
	void VectorAudioTXRXLoop(void);

	HWND hiddenWindowRDF = NULL;
	HWND hiddenWindowAFV = NULL;

	std::mutex messageLock; // Lock for the message queue
	// Internal message quque
	std::queue<std::set<std::string>> messages;

	// Class for our window
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

	bool drawController;

	void GetRGB(COLORREF& color, const char* settingValue);
	void LoadSettings(void);
	void ProcessRDFQueue(void);

	void UpdateVectorAudioChannels(std::string line, bool mode_tx);
	void ToggleChannels(EuroScopePlugIn::CGrountToAirChannel Channel, int tx = -1, int rx = -1);

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

	friend class CRDFScreen;

public:
	CRDFPlugin();
	virtual ~CRDFPlugin();
	void HiddenWndProcessRDFMessage(std::string message);
	void HiddenWndProcessAFVMessage(std::string message);
	virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated);
	virtual bool OnCompileCommand(const char* sCommandLine);

	callsign_position activeTransmittingPilots;
	callsign_position previousActiveTransmittingPilots;

	COLORREF rdfRGB, rdfConcurrentTransmissionRGB;
	int circleThreshold;

	void AddOffset(EuroScopePlugIn::CPosition& position, double heading, double distance);

};
