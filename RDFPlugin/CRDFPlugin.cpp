#include "stdafx.h"
#include "CRDFPlugin.h"
#include "CRDFScreen.h"

using namespace std;

#define VECTORAUDIO_PARAM_VERSION	"/*"
#define VECTORAUDIO_PARAM_TRANSMIT	"/transmitting"
#define VECTORAUDIO_PARAM_TX		"/tx"
#define VECTORAUDIO_PARAM_RX		"/rx"

#define SETTING_VECTORAUDIO_ADDRESS "VectorAudioAddress"
#define SETTING_VECTORAUDIO_TIMEOUT "VectorAudioTimeout"
#define SETTING_VECTORAUDIO_POLL_INTERVAL "VectorAudioPollInterval"
#define SETTING_VECTORAUDIO_RETRY_INTERVAL "VectorAudioRetryInterval"
#define SETTING_RGB "RGB"
#define SETTING_CONCURRENT_RGB "ConcurrentTransmissionRGB"
#define SETTING_CIRCLE_RADIUS "Radius"
#define SETTING_THRESHOLD "Threshold"
#define SETTING_PRECISION "Precision"
#define SETTING_LOW_ALTITUDE "LowAltitude"
#define SETTING_HIGH_ALTITUDE "HighAltitude"
#define SETTING_LOW_PRECISION "LowPrecision"
#define SETTING_HIGH_PRECISION "HighPrecision"
#define SETTING_DRAW_CONTROLLERS "DrawControllers"

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
		MY_PLUGIN_NAME.c_str(),
		MY_PLUGIN_VERSION.c_str(),
		MY_PLUGIN_DEVELOPER.c_str(),
		MY_PLUGIN_COPYRIGHT.c_str())
{
	// RDF window
	RegisterClass(&this->windowClassRDF);
	this->hiddenWindowRDF = CreateWindow(
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
		DisplayEuroScopeMessage("Unable to open communications for RDF");
	}

	// AFV bridge window
	RegisterClass(&this->windowClassAFV);
	this->hiddenWindowAFV = CreateWindow(
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
		DisplayEuroScopeMessage("Unable to open communications for AFV bridge");
	}

	LoadSettings();

	this->rdGenerator = mt19937(this->randomDevice());
	this->disBearing = uniform_real_distribution<>(0.0, 360.0);
	this->disDistance = normal_distribution<>(0, 1.0);

	DisplayEuroScopeMessage(string("Version " + MY_PLUGIN_VERSION + " loaded"));

	// detach thread for VectorAudio
	threadVectorAudioMain = new thread(&CRDFPlugin::VectorAudioMainLoop, this);
	threadVectorAudioMain->detach();
	threadVectorAudioTXRX = new thread(&CRDFPlugin::VectorAudioTXRXLoop, this);
	threadVectorAudioTXRX->detach();

}

CRDFPlugin::~CRDFPlugin()
{
	// close detached thread
	threadMainRunning = false;
	threadTXRXRunning = false;

	if (this->hiddenWindowRDF != NULL) {
		DestroyWindow(this->hiddenWindowRDF);
	}
	UnregisterClass("RDFHiddenWindowClass", NULL);

	if (this->hiddenWindowAFV != NULL) {
		DestroyWindow(this->hiddenWindowAFV);
	}
	UnregisterClass("AfvBridgeHiddenWindowClass", NULL);

	threadMainClosed.wait(false);
	threadTXRXClosed.wait(false);
}

void CRDFPlugin::HiddenWndProcessRDFMessage(std::string message)
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
	ProcessRDFQueue();
}

void CRDFPlugin::HiddenWndProcessAFVMessage(string message)
{
	// functions as AFV bridge
	if (!message.size()) return;
	DisplayEuroScopeDebugMessage(string("AFV message: ") + message);
	// TODO: set TX/RX
	// format: xxx.xxx:True:False + xxx.xx0:True:False

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
	connectionTimeout = 300; // milliseconds, range: [100, 1000]
	pollInterval = 200; // milliseconds, range: [100, +inf)
	retryInterval = 5; // seconds, range: [1, +inf)

	rdfRGB = RGB(255, 255, 255);	// Default: white
	rdfConcurrentTransmissionRGB = RGB(255, 0, 0);	// Default: red

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
			DisplayEuroScopeDebugMessage(string("Address: ") + addressVectorAudio);
		}

		const char* cstrTimeout = GetDataFromSettings(SETTING_VECTORAUDIO_TIMEOUT);
		if (cstrTimeout != NULL)
		{
			int parsedTimeout = atoi(cstrTimeout);
			if (parsedTimeout >= 100 && parsedTimeout <= 1000) {
				connectionTimeout = parsedTimeout;
				DisplayEuroScopeDebugMessage(string("Timeout: ") + to_string(connectionTimeout));
			}
		}

		const char* cstrPollInterval = GetDataFromSettings(SETTING_VECTORAUDIO_POLL_INTERVAL);
		if (cstrPollInterval != NULL)
		{
			int parsedInterval = atoi(cstrPollInterval);
			if (parsedInterval >= 100) {
				pollInterval = parsedInterval;
				DisplayEuroScopeDebugMessage(string("Poll interval: ") + to_string(pollInterval));
			}
		}

		const char* cstrRetryInterval = GetDataFromSettings(SETTING_VECTORAUDIO_RETRY_INTERVAL);
		if (cstrRetryInterval != NULL)
		{
			int parsedInterval = atoi(cstrRetryInterval);
			if (parsedInterval >= 1) {
				retryInterval = parsedInterval;
				DisplayEuroScopeDebugMessage(string("Retry interval: ") + to_string(retryInterval));
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
			GetRGB(rdfConcurrentTransmissionRGB, cstrRGB);
		}

		const char* cstrRadius = GetDataFromSettings(SETTING_CIRCLE_RADIUS);
		if (cstrRadius != NULL)
		{
			int parsedRadius = atoi(cstrRadius);
			if (parsedRadius > 0) {
				circleRadius = parsedRadius;
				DisplayEuroScopeDebugMessage(string("Radius: ") + to_string(circleRadius));
			}
		}

		const char* cstrThreshold = GetDataFromSettings(SETTING_THRESHOLD);
		if (cstrThreshold != NULL)
		{
			circleThreshold = atoi(cstrThreshold);
			DisplayEuroScopeDebugMessage(string("Threshold: ") + to_string(circleThreshold));
		}

		const char* cstrPrecision = GetDataFromSettings(SETTING_PRECISION);
		if (cstrPrecision != NULL)
		{
			int parsedPrecision = atoi(cstrPrecision);
			if (parsedPrecision >= 0) {
				circlePrecision = parsedPrecision;
				DisplayEuroScopeDebugMessage(string("Precision: ") + to_string(circlePrecision));
			}
		}

		const char* cstrLowAlt = GetDataFromSettings(SETTING_LOW_ALTITUDE);
		if (cstrLowAlt != NULL)
		{
			int parsedAlt = atoi(cstrLowAlt);
			lowAltitude = parsedAlt;
			DisplayEuroScopeDebugMessage(string("Low Altitude: ") + to_string(lowAltitude));
		}

		const char* cstrHighAlt = GetDataFromSettings(SETTING_HIGH_ALTITUDE);
		if (cstrHighAlt != NULL)
		{
			int parsedAlt = atoi(cstrHighAlt);
			if (parsedAlt > 0) {
				highAltitude = parsedAlt;
				DisplayEuroScopeDebugMessage(string("High Altitude: ") + to_string(highAltitude));
			}
		}

		const char* cstrLowPrecision = GetDataFromSettings(SETTING_LOW_PRECISION);
		if (cstrLowPrecision != NULL)
		{
			int parsedPrecision = atoi(cstrLowPrecision);
			if (parsedPrecision >= 0) {
				lowPrecision = parsedPrecision;
				DisplayEuroScopeDebugMessage(string("Low Precision: ") + to_string(lowPrecision));
			}
		}

		const char* cstrHighPrecision = GetDataFromSettings(SETTING_HIGH_PRECISION);
		if (cstrHighPrecision != NULL)
		{
			int parsedPrecision = atoi(cstrHighPrecision);
			if (parsedPrecision >= 0) {
				highPrecision = parsedPrecision;
				DisplayEuroScopeDebugMessage(string("High Precision: ") + to_string(highPrecision));
			}
		}

		const char* cstrController = GetDataFromSettings(SETTING_DRAW_CONTROLLERS);
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

void CRDFPlugin::ProcessRDFQueue(void)
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
			auto controller = ControllerSelect(callsign.c_str());
			if (!radarTarget.IsValid() && controller.IsValid() && callsign.back() >= 'A' && callsign.back() <= 'Z') {
				// dump last character and find callsign again
				string callsign_dump = callsign.substr(0, callsign.size() - 1);
				radarTarget = RadarTargetSelect(callsign_dump.c_str());
			}
			if (radarTarget.IsValid()) {
				CPosition pos = radarTarget.GetPosition().GetPosition();
				int alt = radarTarget.GetPosition().GetPressureAltitude();
				if (alt >= lowAltitude) { // need to draw, see Schematic in LoadSettings
					CPosition posnew = pos;
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
						AddOffset(posnew, bearing, distance);
					}
					activeTransmittingPilots[callsign] = { posnew, radius };
				}
			}
			else if (drawController && controller.IsValid()) {
				CPosition pos = controller.GetPosition();
				activeTransmittingPilots[callsign] = { pos, (double)lowPrecision };
			}
		}

		if (!activeTransmittingPilots.empty()) {
			previousActiveTransmittingPilots = activeTransmittingPilots;
		}
	}
}

void CRDFPlugin::UpdateVectorAudioChannels(string line, bool mode_tx)
{
	// parse message and returns number of total toggles
	map<string, int> channelFreq;
	istringstream ssLine(line);
	string strChnl;
	int freqMe = FrequencyConvert(ControllerMyself().GetPrimaryFrequency());
	while (getline(ssLine, strChnl, ',')) {
		size_t colon = strChnl.find(':');
		string channel = strChnl.substr(0, colon);
		try {
			int frequency = FrequencyConvert(stod(strChnl.substr(colon + 1)));
			if (!FrequencyCompare(freqMe, frequency)) {
				channelFreq.insert({ channel, frequency });
			}
		}
		catch (...) {
			DisplayEuroScopeDebugMessage("Error when parsing frequencies: " + strChnl);
			continue;
		}
	}

	for (auto chnl = GroundToArChannelSelectFirst(); chnl.IsValid(); chnl = GroundToArChannelSelectNext(chnl)) {
		if (chnl.GetIsPrimary() || chnl.GetIsAtis()) { // make sure primary and ATIS are not affected
			continue;
		}
		string chName = chnl.GetName();
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
				size_t posc = chName.find('_');
				if (itc->first.substr(0, posi) == chName.substr(0, posc)) {
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

void CRDFPlugin::ToggleChannels(CGrountToAirChannel Channel, int tx, int rx)
{
	// pass tx/rx = -1 to skip
	if (tx >= 0 && tx != (int)Channel.GetIsTextTransmitOn()) {
		Channel.ToggleTextTransmit();
	}
	if (rx >= 0 && rx != (int)Channel.GetIsTextReceiveOn()) {
		Channel.ToggleTextReceive();
	}
}

void CRDFPlugin::AddOffset(CPosition& position, double heading, double distance)
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

void CRDFPlugin::VectorAudioMainLoop(void)
{
	threadMainRunning = true;
	threadMainClosed = false;
	bool getTransmit = false;
	while (true) {
		for (int sleepRemain = getTransmit ? pollInterval : retryInterval * 1000; sleepRemain > 0;) {
			if (threadMainRunning) {
				int sleepThis = min(sleepRemain, pollInterval);
				this_thread::sleep_for(chrono::milliseconds(sleepThis));
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
				ProcessRDFQueue();
				continue;
			}
			DisplayEuroScopeDebugMessage("HTTP error on MAIN: " + httplib::to_string(res.error()));
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

void CRDFPlugin::VectorAudioTXRXLoop(void)
{
	threadTXRXRunning = true;
	threadTXRXClosed = false;
	while (true) {
		for (int sleepRemain = retryInterval * 1000; sleepRemain > 0;) {
			if (threadTXRXRunning) {
				int sleepThis = min(sleepRemain, pollInterval);
				this_thread::sleep_for(chrono::milliseconds(sleepThis));
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
		if (auto res = cli.Get(VECTORAUDIO_PARAM_TX)) {
			if (res->status == 200) {
				DisplayEuroScopeDebugMessage(string("VectorAudio message on TX: ") + res->body);
				UpdateVectorAudioChannels(res->body, true);
			}
			else {
				DisplayEuroScopeDebugMessage("HTTP error on TX: " + httplib::to_string(res.error()));
			}
		}
		if (auto res = cli.Get(VECTORAUDIO_PARAM_RX)) {
			if (res->status == 200) {
				DisplayEuroScopeDebugMessage(string("VectorAudio message on RX: ") + res->body);
				UpdateVectorAudioChannels(res->body, false);
			}
			else {
				DisplayEuroScopeDebugMessage("HTTP error on RX: " + httplib::to_string(res.error()));
			}
		}
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
		if (sscanf_s(cmd.c_str(), ".RDF ADDRESS %s", bufferAddr, sizeof(bufferAddr)) == 1) {
			addressVectorAudio = string(bufferAddr);
			DisplayEuroScopeMessage(string("Address: ") + addressVectorAudio);
			SaveDataToSettings(SETTING_VECTORAUDIO_ADDRESS, "VectorAudio address", addressVectorAudio.c_str());
			return true;
		}

		int bufferTimeout;
		if (sscanf_s(cmd.c_str(), ".RDF TIMEOUT %d", &bufferTimeout) == 1) {
			if (bufferTimeout >= 100 && bufferTimeout <= 1000) {
				connectionTimeout = bufferTimeout;
				DisplayEuroScopeMessage(string("Timeout: ") + to_string(connectionTimeout));
				SaveDataToSettings(SETTING_VECTORAUDIO_TIMEOUT, "VectorAudio timeout", to_string(connectionTimeout).c_str());
				return true;
			}
		}

		int bufferPollInterval;
		if (sscanf_s(cmd.c_str(), ".RDF POLL %d", &bufferPollInterval) == 1) {
			if (bufferPollInterval >= 100) {
				pollInterval = bufferPollInterval;
				DisplayEuroScopeMessage(string("Poll interval: ") + to_string(bufferPollInterval));
				SaveDataToSettings(SETTING_VECTORAUDIO_POLL_INTERVAL, "VectorAudio poll interval", to_string(pollInterval).c_str());
				return true;
			}
		}

		int bufferRetryInterval;
		if (sscanf_s(cmd.c_str(), ".RDF RETRY %d", &bufferRetryInterval) == 1) {
			if (bufferRetryInterval >= 1) {
				retryInterval = bufferRetryInterval;
				DisplayEuroScopeMessage(string("Retry interval: ") + to_string(retryInterval));
				SaveDataToSettings(SETTING_VECTORAUDIO_RETRY_INTERVAL, "VectorAudio retry interval", to_string(retryInterval).c_str());
				return true;
			}
		}

		char bufferRGB[15] = { 0 };
		if (sscanf_s(cmd.c_str(), ".RDF RGB %s", bufferRGB, sizeof(bufferRGB)) == 1) {
			COLORREF prevRGB = rdfRGB;
			GetRGB(rdfRGB, bufferRGB);
			if (rdfRGB != prevRGB) {
				SaveDataToSettings(SETTING_RGB, "RGB", bufferRGB);
				DisplayEuroScopeMessage((string("RGB: ") + bufferRGB).c_str());
				return true;
			}
		}
		else if (sscanf_s(cmd.c_str(), ".RDF CTRGB %s", bufferRGB, sizeof(bufferRGB)) == 1) {
			COLORREF prevRGB = rdfConcurrentTransmissionRGB;
			GetRGB(rdfConcurrentTransmissionRGB, bufferRGB);
			if (rdfConcurrentTransmissionRGB != prevRGB) {
				SaveDataToSettings(SETTING_CONCURRENT_RGB, "Concurrent RGB", bufferRGB);
				DisplayEuroScopeMessage((string("Concurrent RGB: ") + bufferRGB).c_str());
				return true;
			}
		}

		int bufferRadius;
		if (sscanf_s(cmd.c_str(), ".RDF RADIUS %d", &bufferRadius) == 1) {
			if (bufferRadius > 0) {
				circleRadius = bufferRadius;
				DisplayEuroScopeMessage(string("Radius: ") + to_string(circleRadius));
				SaveDataToSettings(SETTING_CIRCLE_RADIUS, "Radius", to_string(circleRadius).c_str());
				return true;
			}
		}

		if (sscanf_s(cmd.c_str(), ".RDF THRESHOLD %d", &circleThreshold) == 1) {
			DisplayEuroScopeMessage(string("Threshold: ") + to_string(circleThreshold));
			SaveDataToSettings(SETTING_THRESHOLD, "Threshold", to_string(circleThreshold).c_str());
			return true;
		}

		int bufferPrecision;
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION %d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				circlePrecision = bufferPrecision;
				DisplayEuroScopeMessage(string("Precision: ") + to_string(circlePrecision));
				SaveDataToSettings(SETTING_PRECISION, "Precision", to_string(circlePrecision).c_str());
				return true;
			}
		}

		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE L%d", &lowAltitude) == 1) {
			DisplayEuroScopeMessage(string("Altitude (low): ") + to_string(lowAltitude));
			SaveDataToSettings(SETTING_LOW_ALTITUDE, "Altitude (low)", to_string(lowAltitude).c_str());
			return true;
		}

		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE H%d", &highAltitude) == 1) {
			DisplayEuroScopeMessage(string("Altitude (high): ") + to_string(highAltitude));
			SaveDataToSettings(SETTING_HIGH_ALTITUDE, "Altitude (high)", to_string(highAltitude).c_str());
			return true;
		}

		if (sscanf_s(cmd.c_str(), ".RDF PRECISION L%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				lowPrecision = bufferPrecision;
				DisplayEuroScopeMessage(string("Precision (low): ") + to_string(lowPrecision));
				SaveDataToSettings(SETTING_LOW_PRECISION, "Precision (low)", to_string(lowPrecision).c_str());
				return true;
			}
		}

		if (sscanf_s(cmd.c_str(), ".RDF PRECISION H%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				highPrecision = bufferPrecision;
				DisplayEuroScopeMessage(string("Precision (high): ") + to_string(highPrecision));
				SaveDataToSettings(SETTING_HIGH_PRECISION, "Precision (high)", to_string(highPrecision).c_str());
				return true;
			}
		}

		int bufferCtrl;
		if (sscanf_s(cmd.c_str(), ".RDF CONTROLLER %d", &bufferCtrl) == 1) {
			drawController = bufferCtrl;
			DisplayEuroScopeMessage(string("Draw controllers: ") + to_string(drawController));
			SaveDataToSettings(SETTING_DRAW_CONTROLLERS, "Draw controllers", to_string(bufferCtrl).c_str());
			return true;
		}

	}
	catch (const std::exception& e)
	{
		DisplayEuroScopeDebugMessage(e.what());
	}
	return false;
}
