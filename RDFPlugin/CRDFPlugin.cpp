#include "stdafx.h"
#include "CRDFPlugin.h"
#include "CRDFScreen.h"

using namespace std;

const double pi = 3.141592653589793;
const double EarthRadius = 6371.393 / 1.852; // nautical miles

CRDFPlugin::CRDFPlugin()
	: EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
		MY_PLUGIN_NAME.c_str(),
		MY_PLUGIN_VERSION.c_str(),
		MY_PLUGIN_DEVELOPER.c_str(),
		MY_PLUGIN_COPYRIGHT.c_str())
{
	DisplayEuroScopeMessage(string("Version " + MY_PLUGIN_VERSION + " loaded"));

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

	useVectorAudio = false;
	VectorAudioVersion = async(std::launch::async, &CRDFPlugin::GetVectorAudioInfo, this, "/*");

	circlePrecision = 0;
	this->rdGenerator = mt19937(this->randomDevice());
	this->disUniform = uniform_real_distribution<>(0, 180);
	this->disNormal = normal_distribution<>(0, 1);

}


CRDFPlugin::~CRDFPlugin()
{
	if (this->hiddenWindow != NULL) {
		DestroyWindow(this->hiddenWindow);
	}
}

/*
	Process the message queue
*/
void CRDFPlugin::OnTimer(int counter)
{
	// check vector audio status
	if (VectorAudioVersion.valid() && VectorAudioVersion.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
		try {
			string res = VectorAudioVersion.get();
			if (!useVectorAudio && res.size()) {
				DisplayEuroScopeMessage(string("Connected to ") + res);
				useVectorAudio = true;
			}
		}
		catch (const std::exception& exc) {
			if (useVectorAudio) {
				DisplayEuroScopeMessage(string("Disconnected from vector audio: ") + exc.what());
			}
			useVectorAudio = false;
		}
	}
	else if (!useVectorAudio && !(counter % 5)) { // refresh every 5 seconds
		VectorAudioVersion = async(std::launch::async, &CRDFPlugin::GetVectorAudioInfo, this, "/*");
	}

	std::lock_guard<std::mutex> lock(this->messageLock);

	// Process VectorAudio message
	if (VectorAudioTransmission.valid() && VectorAudioTransmission.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
		try {
			string res = VectorAudioTransmission.get();
#ifdef _DEBUG
			DisplayEuroScopeDebugMessage(string("VectorAudio message: ") + res);
#endif // _DEBUG
			if (!res.size()) {
				this->messages.push(set<string>());
			}
			else {
				set<string> strings;
				istringstream f(res);
				string s;
				while (getline(f, s, ',')) {
					strings.insert(s);
				}
				this->messages.push(strings);
			}
		}
		catch (const std::exception& exc) {
#ifdef _DEBUG
			DisplayEuroScopeDebugMessage(exc.what());
#endif // _DEBUG
			if (useVectorAudio) {
				DisplayEuroScopeMessage(string("Disconnected from vector audio: ") + exc.what());
			}
			useVectorAudio = false;
		}
	}

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
			else {
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

	// GET VectorAudio
	if (useVectorAudio) {
		VectorAudioTransmission = async(std::launch::async, &CRDFPlugin::GetVectorAudioInfo, this, "/transmitting");
	}
}

void CRDFPlugin::AddMessageToQueue(std::string message)
{
	std::lock_guard<std::mutex> lock(this->messageLock);
	if (message.size()) {
#ifdef _DEBUG
		DisplayEuroScopeDebugMessage(string("AFV message: ") + message);
#endif // _DEBUG
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

string CRDFPlugin::GetVectorAudioInfo(string param)
{
	// need to use try-catch in every .get()
	httplib::Client cli("http://10.211.55.2:49080");
	cli.set_connection_timeout(0, 300000); // 300,000 usec = 300 millisecond
	if (auto res = cli.Get(param)) {
		if (res->status == 200) {
			return res->body;
		}
		else {
			auto err = res.error();
			throw runtime_error("HTTP error: " + httplib::to_string(err));
		}
	}
	throw runtime_error("Not connected");
}

COLORREF CRDFPlugin::GetRGB(const char* settingValue)
{
	string circleRGB = settingValue;

	size_t firstColonIndex = circleRGB.find(':');
	if (firstColonIndex != string::npos)
	{
		size_t secondColonIndex = circleRGB.find(':', firstColonIndex + 1);
		if (secondColonIndex != string::npos)
		{
			string redString = circleRGB.substr(0, firstColonIndex);
			string greenString = circleRGB.substr(firstColonIndex + 1, secondColonIndex - firstColonIndex - 1);
			string blueString = circleRGB.substr(secondColonIndex + 1, circleRGB.size() - secondColonIndex - 1);
#ifdef _DEBUG
			DisplayEuroScopeDebugMessage(string("R: ") + redString + string(" G: ") + greenString + string(" B: ") + blueString);
#endif

			if (!redString.empty() && !greenString.empty() && !blueString.empty())
			{
				return RGB(std::stoi(redString), std::stoi(greenString), std::stoi(blueString));
			}
		}
	}
}

CPosition CRDFPlugin::AddRandomOffset(CPosition pos)
{
	double distance = disNormal(rdGenerator) * (double)circlePrecision / 2.0;
	double angle = disUniform(rdGenerator);
	double startLat = pos.m_Latitude / 180.0 * pi;
	double startLong = pos.m_Longitude / 180.0 * pi;
	double lat2 = asin(sin(startLat) * cos(distance / EarthRadius) + cos(startLat) * sin(distance / EarthRadius) * cos(angle));
	double lon2 = startLong + atan2(sin(angle) * sin(distance / EarthRadius) * cos(startLat), cos(distance / EarthRadius) - sin(startLat) * sin(lat2));

	CPosition posnew;
	posnew.m_Latitude = lat2 / pi * 180.0;
	posnew.m_Longitude = lon2 / pi * 180.0;

#ifdef _DEBUG
	string lats1 = to_string(pos.m_Latitude);
	string lons1 = to_string(pos.m_Longitude);
	string lats2 = to_string(posnew.m_Latitude);
	string lons2 = to_string(posnew.m_Longitude);
	OutputDebugString((lons1 + "," + lats1 + "\t" + lons2 + "," + lats2 + "\n").c_str());
#endif // _DEBUG

	return posnew;
}

CRadarScreen* CRDFPlugin::OnRadarScreenCreated(const char* sDisplayName,
	bool NeedRadarContent,
	bool GeoReferenced,
	bool CanBeSaved,
	bool CanBeCreated)
{
	DisplayEuroScopeMessage(string("Radio Direction Finder plugin activated on ") + sDisplayName);

	COLORREF rdfRGB = RGB(255, 255, 255);	// Default: white
	COLORREF rdfConcurrentTransmissionRGB = RGB(255, 0, 0);	// Default: red
	int circleRadius = 20;

	try
	{
		const char* cstrRGB = GetDataFromSettings("RGB");
		if (cstrRGB != NULL)
		{
			rdfRGB = GetRGB(cstrRGB);
		}

		cstrRGB = GetDataFromSettings("ConcurrentTransmissionRGB");
		if (cstrRGB != NULL)
		{
			rdfConcurrentTransmissionRGB = GetRGB(cstrRGB);
		}

		const char* cstrRadius = GetDataFromSettings("Radius");
		if (cstrRadius != NULL)
		{
			int parsedRadius = atoi(cstrRadius);
			if (parsedRadius > 0) {
				circleRadius = parsedRadius;

#ifdef _DEBUG
				DisplayEuroScopeDebugMessage(string("Radius: ") + to_string(circleRadius));
#endif
			}
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

	return new CRDFScreen(this, rdfRGB, rdfConcurrentTransmissionRGB, circleRadius);
}
