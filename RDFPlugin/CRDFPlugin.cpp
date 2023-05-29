#include "stdafx.h"
#include "CRDFPlugin.h"
#include "CRDFScreen.h"

using namespace std;

#define VECTORAUDIO_PARAM_VERSION	"/*"
#define VECTORAUDIO_PARAM_TRANSMIT	"/transmitting"
const double pi = 3.141592653589793;
const double EarthRadius = 6371.393 / 1.852; // nautical miles

CRDFPlugin::CRDFPlugin()
	: EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
		MY_PLUGIN_NAME.c_str(),
		MY_PLUGIN_VERSION.c_str(),
		MY_PLUGIN_DEVELOPER.c_str(),
		MY_PLUGIN_COPYRIGHT.c_str())
{
	RegisterClass(&this->windowClass);

	this->hiddenWindow = CreateWindow(
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
		DisplayEuroScopeMessage("Unable to open communications for RDF plugin");
	}

	LoadSettings();

	this->rdGenerator = mt19937(this->randomDevice());
	this->disUniform = uniform_real_distribution<>(0, 180);
	this->disNormal = normal_distribution<>(0.0, 1.0);

	DisplayEuroScopeMessage(string("Version " + MY_PLUGIN_VERSION + " loaded"));

	// detach thread for VectorAudio
	VectorAudioTransmission = new thread(&CRDFPlugin::VectorAudioHTTPLoop, this);
	VectorAudioTransmission->detach();

}

CRDFPlugin::~CRDFPlugin()
{
	// close detached thread
	threadRunning = false;
	threadClosed.wait(false);

	if (this->hiddenWindow != NULL) {
		DestroyWindow(this->hiddenWindow);
	}
	UnregisterClass("RDFHiddenWindowClass", NULL);
}

void CRDFPlugin::ProcessAFVMessage(std::string message)
{
	{
		std::lock_guard<std::mutex> lock(this->messageLock);
		if (message.size()) {
			DisplayEuroScopeDebugMessage(string("AFV message: ") + message);
			set<string> strings;
			istringstream f(message);
			string s;
			while (getline(f, s, ':')) {
				strings.insert(s);
			}
			this->messages.push(strings);
		}
		else {
			this->messages.push(set<string>());
		}
	}
	ProcessMessageQueue();
}

void CRDFPlugin::GetRGB(COLORREF& color, const char* settingValue)
{
	unsigned int r, g, b;
	sscanf_s(settingValue, "%u:%u:%u", &r, &g, &b);
	if (r <= 255 && g <= 255 && b <= 255) {
		DisplayEuroScopeDebugMessage(string("R: ") + to_string(r) + string(" G: ") + to_string(g) + string(" B: ") + to_string(b));
		color = RGB(r, g, b);
	}
}

void CRDFPlugin::LoadSettings(void)
{
	addressVectorAudio = "127.0.0.1:49080";
	connectionTimeout = 300;
	pollInterval = 200;
	retryInterval = 5;

	rdfRGB = RGB(255, 255, 255);	// Default: white
	rdfConcurrentTransmissionRGB = RGB(255, 0, 0);	// Default: red
	circleRadius = 20; // Default: 20 nautical miles
	circleThreshold = -1; // Default: -1 (always use pixel)
	circlePrecision = 0; // Default: no offset (nautical miles)
	drawController = false;

	try
	{
		const char* cstrAddrVA = GetDataFromSettings("VectorAudioAddress");
		if (cstrAddrVA != NULL)
		{
			addressVectorAudio = cstrAddrVA;
			DisplayEuroScopeDebugMessage(string("Address: ") + addressVectorAudio);
		}

		const char* cstrTimeout = GetDataFromSettings("VectorAudioTimeout");
		if (cstrTimeout != NULL)
		{
			int parsedTimeout = atoi(cstrTimeout);
			if (parsedTimeout >= 100 && parsedTimeout <= 1000) {
				connectionTimeout = parsedTimeout;
				DisplayEuroScopeDebugMessage(string("Timeout: ") + to_string(connectionTimeout));
			}
		}

		const char* cstrPollInterval = GetDataFromSettings("VectorAudioPollInterval");
		if (cstrPollInterval != NULL)
		{
			int parsedInterval = atoi(cstrPollInterval);
			if (parsedInterval >= 100) {
				pollInterval = parsedInterval;
				DisplayEuroScopeDebugMessage(string("Poll interval: ") + to_string(pollInterval));
			}
		}

		const char* cstrRetryInterval = GetDataFromSettings("VectorAudioRetryInterval");
		if (cstrRetryInterval != NULL)
		{
			int parsedInterval = atoi(cstrRetryInterval);
			if (parsedInterval >= 1) {
				retryInterval = parsedInterval;
				DisplayEuroScopeDebugMessage(string("Retry interval: ") + to_string(retryInterval));
			}
		}

		const char* cstrRGB = GetDataFromSettings("RGB");
		if (cstrRGB != NULL)
		{
			GetRGB(rdfRGB, cstrRGB);
		}

		cstrRGB = GetDataFromSettings("ConcurrentTransmissionRGB");
		if (cstrRGB != NULL)
		{
			GetRGB(rdfConcurrentTransmissionRGB, cstrRGB);
		}

		const char* cstrRadius = GetDataFromSettings("Radius");
		if (cstrRadius != NULL)
		{
			int parsedRadius = atoi(cstrRadius);
			if (parsedRadius > 0) {
				circleRadius = parsedRadius;
				DisplayEuroScopeDebugMessage(string("Radius: ") + to_string(circleRadius));
			}
		}

		const char* cstrThreshold = GetDataFromSettings("Threshold");
		if (cstrThreshold != NULL)
		{
			circleThreshold = atoi(cstrThreshold);
			DisplayEuroScopeDebugMessage(string("Threshold: ") + to_string(circleThreshold));
		}

		const char* cstrPrecision = GetDataFromSettings("Precision");
		if (cstrPrecision != NULL)
		{
			int parsedPrecision = atoi(cstrPrecision);
			if (parsedPrecision > 0) {
				circlePrecision = parsedPrecision;
				DisplayEuroScopeDebugMessage(string("Precision: ") + to_string(circlePrecision));
			}
		}

		const char* cstrController = GetDataFromSettings("DrawControllers");
		if (cstrController != NULL)
		{
			drawController = (bool)atoi(cstrController);
			DisplayEuroScopeDebugMessage(string("Draw controllers and observers: ") + to_string(drawController));
		}
	}
	catch (std::runtime_error const& e)
	{
		DisplayEuroScopeMessage(string("Error: ") + e.what());
	}
	catch (...)
	{
		DisplayEuroScopeMessage(string("Unexpected error: ") + to_string(GetLastError()));
	}

}

void CRDFPlugin::ProcessMessageQueue(void)
{
	std::lock_guard<std::mutex> lock(this->messageLock);
	// Process all incoming messages
	while (this->messages.size() > 0) {
		set<string> amessage = this->messages.front();
		this->messages.pop();

		// remove existing records
		for (auto itr = activeTransmittingPilots.begin(); itr != activeTransmittingPilots.end();) {
			if (amessage.erase(itr->first)) { // remove still transmitting from message
				itr++;
			}
			else {
				// no removal, means stopped transmission, need to also remove from map
				activeTransmittingPilots.erase(itr++);
			}
		}

		// add new active transmitting records
		for (const auto& callsign : amessage) {
			auto radarTarget = RadarTargetSelect(callsign.c_str());
			if (radarTarget.IsValid()) {
				CPosition pos = radarTarget.GetPosition().GetPosition();
				pos = AddRandomOffset(pos);
				activeTransmittingPilots[callsign] = pos;
			}
			else if (drawController) {
				auto controller = ControllerSelect(callsign.c_str());
				if (controller.IsValid()) {
					CPosition pos = controller.GetPosition();
					if (!controller.IsController()) { // for shared cockpit
						pos = AddRandomOffset(pos);
					}
					activeTransmittingPilots[callsign] = pos;
				}
			}
		}

		if (!activeTransmittingPilots.empty()) {
			previousActiveTransmittingPilots = activeTransmittingPilots;
		}
	}
}

CPosition CRDFPlugin::AddRandomOffset(CPosition pos)
{
	double distance = disNormal(rdGenerator) * (double)circlePrecision / 2.0;
	double bearing = disUniform(rdGenerator);
	CPosition posnew;

	double rLat1 = pos.m_Latitude / 180.0 * pi;
	double rLon1 = pos.m_Longitude / 180.0 * pi;
	double rDistance = distance / EarthRadius;
	double rBearing = bearing / 180.0 * pi;
	double rLat2 = asin(sin(rLat1) * cos(rDistance) + cos(rLat1) * sin(rDistance) * cos(rBearing));
	double rLon2 = abs(cos(rLat1)) < 0.000001 ? rLon1 : \
		rLon1 + atan2(sin(rBearing) * sin(rDistance) * cos(rLat1), cos(rDistance) - sin(rLat1) * sin(rLat2));
	posnew.m_Latitude = rLat2 / pi * 180.0;
	posnew.m_Longitude = rLon2 / pi * 180.0;

	return posnew;
}

void CRDFPlugin::VectorAudioHTTPLoop(void)
{
	threadRunning = true;
	threadClosed = false;
	bool getTransmit = false;
	while (true) {
		for (int sleepRemain = getTransmit ? pollInterval : retryInterval * 1000; sleepRemain > 0;) {
			if (threadRunning) {
				int sleepThis = min(sleepRemain, pollInterval);
				this_thread::sleep_for(chrono::milliseconds(sleepThis));
				sleepRemain -= sleepThis;
			}
			else {
				threadClosed = true;
				threadClosed.notify_all();
				return;
			}
		}

		httplib::Client cli("http://" + addressVectorAudio);
		cli.set_connection_timeout(0, connectionTimeout * 1000);
		if (auto res = cli.Get(getTransmit ? VECTORAUDIO_PARAM_TRANSMIT : VECTORAUDIO_PARAM_VERSION)) {
			if (res->status == 200) {
				DisplayEuroScopeDebugMessage(string("VectorAudio message: ") + res->body);
				if (!getTransmit) {
					DisplayEuroScopeMessage("Connected to " + res->body);
					getTransmit = true;
					continue;
				}
				{
					std::lock_guard<std::mutex> lock(this->messageLock);
					if (!res->body.size()) {
						this->messages.push(set<string>());
					}
					else {
						set<string> strings;
						istringstream f(res->body);
						string s;
						while (getline(f, s, ',')) {
							strings.insert(s);
						}
						this->messages.push(strings);
					}
				}
				ProcessMessageQueue();
				continue;
			}
			DisplayEuroScopeDebugMessage("HTTP error: " + httplib::to_string(res.error()));
		}
		else {
			DisplayEuroScopeDebugMessage("Not connected");
		}
		if (getTransmit) {
			DisplayEuroScopeMessage("VectorAudio disconnected");
		}
		getTransmit = false;
	}
}

CRadarScreen* CRDFPlugin::OnRadarScreenCreated(const char* sDisplayName,
	bool NeedRadarContent,
	bool GeoReferenced,
	bool CanBeSaved,
	bool CanBeCreated)
{
	DisplayEuroScopeMessage(string("Radio Direction Finder plugin activated on ") + sDisplayName);

	return new CRDFScreen(this);
}

bool CRDFPlugin::OnCompileCommand(const char* sCommandLine)
{
	string cmd = sCommandLine;
	for (auto& c : cmd) {
		c += c >= 'a' && c <= 'z' ? 'A' - 'a' : 0; // make upper
	}
	try
	{

		if (cmd == ".RDF RELOAD") {
			LoadSettings();
			return true;
		}

		char bufferAddr[128] = { 0 };
		if (sscanf_s(cmd.c_str(), ".RDF ADDRESS %s", bufferAddr, sizeof(bufferAddr))) {
			addressVectorAudio = string(bufferAddr);
			DisplayEuroScopeDebugMessage(string("Address: ") + addressVectorAudio);
			return true;
		}

		int bufferTimeout;
		if (sscanf_s(cmd.c_str(), ".RDF TIMEOUT %d", &bufferTimeout)) {
			if (bufferTimeout >= 100 && bufferTimeout <= 1000) {
				connectionTimeout = bufferTimeout;
				DisplayEuroScopeDebugMessage(string("Timeout: ") + to_string(connectionTimeout));
				return true;
			}
		}

		int bufferPollInterval;
		if (sscanf_s(cmd.c_str(), ".RDF POLL %d", &bufferPollInterval)) {
			if (bufferPollInterval >= 100) {
				pollInterval = bufferPollInterval;
				DisplayEuroScopeDebugMessage(string("Poll interval: ") + to_string(bufferPollInterval));
				return true;
			}
		}

		int bufferRetryInterval;
		if (sscanf_s(cmd.c_str(), ".RDF RETRY %d", &bufferRetryInterval)) {
			if (bufferRetryInterval >= 1) {
				retryInterval = bufferRetryInterval;
				DisplayEuroScopeDebugMessage(string("Retry interval: ") + to_string(retryInterval));
				return true;
			}
		}

		char bufferRGB[15] = { 0 };
		if (sscanf_s(cmd.c_str(), ".RDF RGB %s", bufferRGB, sizeof(bufferRGB))) {
			GetRGB(rdfRGB, bufferRGB);
			return true;
		}
		else if (sscanf_s(cmd.c_str(), ".RDF CTRGB %s", bufferRGB, sizeof(bufferRGB))) {
			GetRGB(rdfConcurrentTransmissionRGB, bufferRGB);
			return true;
		}

		int bufferRadius;
		if (sscanf_s(cmd.c_str(), ".RDF RADIUS %d", &bufferRadius)) {
			if (bufferRadius > 0) {
				circleRadius = bufferRadius;
				DisplayEuroScopeDebugMessage(string("Radius: ") + to_string(circleRadius));
				return true;
			}
		}

		if (sscanf_s(cmd.c_str(), ".RDF THRESHOLD %d", &circleThreshold)) {
			DisplayEuroScopeDebugMessage(string("Threshold: ") + to_string(circleThreshold));
			return true;
		}

		int bufferPrecision;
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION %d", &bufferPrecision)) {
			if (bufferPrecision > 0) {
				circlePrecision = bufferPrecision;
				DisplayEuroScopeDebugMessage(string("Precision: ") + to_string(circlePrecision));
				return true;
			}
		}

		int bufferCtrl;
		if (sscanf_s(cmd.c_str(), ".RDF CONTROLLER %d", &bufferCtrl)) {
			DisplayEuroScopeDebugMessage(string("Draw controllers and observers: ") + to_string(drawController));
			drawController = bufferCtrl;
			return true;
		}

	}
	catch (const std::exception& e)
	{
		DisplayEuroScopeDebugMessage(e.what());
	}
	return false;
}
