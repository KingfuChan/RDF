#pragma once

#include "stdafx.h"
#include <string>
#include <chrono>
#include <mutex>
#include <queue>
#include <map>
#include <random>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <thread>
#include <atomic>
#include "HiddenWindow.h"

using namespace std;
using namespace EuroScopePlugIn;

const string MY_PLUGIN_NAME = "RDF Plugin for Euroscope";
const string MY_PLUGIN_VERSION = "1.3.4";
const string MY_PLUGIN_DEVELOPER = "Kingfu Chan, Claus Hemberg Joergensen";
const string MY_PLUGIN_COPYRIGHT = "Free to be distributed as source code";

typedef struct {
	CPosition position;
	double radius;
} _draw_position;
typedef map<string, _draw_position> callsign_position;

class CRDFPlugin : public EuroScopePlugIn::CPlugIn
{
private:

	int circleRadius;
	int circlePrecision;
	int lowAltitude;
	int highAltitude;
	int lowPrecision;
	int highPrecision;

	random_device randomDevice;
	mt19937 rdGenerator;
	uniform_real_distribution<> disBearing;
	normal_distribution<> disDistance;

	string addressVectorAudio;
	int connectionTimeout, pollInterval, retryInterval;
	// thread controls
	thread* threadVectorAudioMain, * threadVectorAudioTXRX;
	atomic_bool threadMainRunning, threadMainClosed,
		threadTXRXRunning, threadTXRXClosed;
	void VectorAudioMainLoop(void);
	void VectorAudioTXRXLoop(void);

	HWND hiddenWindow = NULL;

	mutex messageLock; // Lock for the message queue
	// Internal message quque
	queue<set<string>> messages;

	// Class for our window
	WNDCLASS windowClass = {
	   NULL,
	   HiddenWindow,
	   NULL,
	   NULL,
	   GetModuleHandle(NULL),
	   NULL,
	   NULL,
	   NULL,
	   NULL,
	   "RDFHiddenWindowClass"
	};

	bool drawController;

	void GetRGB(COLORREF& color, const char* settingValue);
	void LoadSettings(void);
	void ProcessMessageQueue(void);

	inline void DisplayEuroScopeDebugMessage(string msg) {
#ifdef _DEBUG
		DisplayUserMessage("RDF-DEBUG", "", msg.c_str(), true, true, true, false, false);
#endif // _DEBUG
	}

	inline void DisplayEuroScopeMessage(string msg) {
		DisplayUserMessage("Message", "RDF Plugin", msg.c_str(), false, false, false, false, false);
	}

public:
	CRDFPlugin();
	virtual ~CRDFPlugin();
	void ProcessAFVMessage(string message);
	virtual CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated);
	virtual bool OnCompileCommand(const char* sCommandLine);

	callsign_position activeTransmittingPilots;
	callsign_position previousActiveTransmittingPilots;

	COLORREF rdfRGB, rdfConcurrentTransmissionRGB;
	int circleThreshold;

	void AddOffset(CPosition& position, double heading, double distance);

};

