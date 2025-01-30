#pragma once

#ifndef CRDFPLUGIN_H
#define CRDFPLUGIN_H

#include "stdafx.h"
#include "HiddenWindow.h"
#include "RDFCommon.h"
#include "CRDFScreen.h"

class CRDFPlugin : public EuroScopePlugIn::CPlugIn, public std::enable_shared_from_this<CRDFPlugin>
{
private:
	friend class CRDFScreen;

	// screen controls and drawing params
	std::shared_mutex mtxDrawSettings;
	std::vector<std::shared_ptr<CRDFScreen>> vecScreen; // index is screen ID (incremental int)
	std::shared_ptr<RDFCommon::draw_settings> pluginDrawSettings; // only reset by command
	std::shared_ptr<RDFCommon::draw_settings> screenDrawSettings; // updated by CRDFScreen

	// drawing records
	std::shared_mutex mtxTransmission;
	RDFCommon::callsign_position curTransmission;
	RDFCommon::callsign_position preTransmission;

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
	auto LoadTrackAudioSettings(void) -> void;
	auto LoadDrawingSettings(std::optional<std::shared_ptr<CRDFScreen>> screenPtr) -> void;

	// functional things 
	auto GenerateDrawPosition(std::string callsign) -> RDFCommon::draw_position;
	auto TrackAudioTransmissionHandler(const nlohmann::json& data, const bool& rxEnd) -> void;
	auto TrackAudioStationStatesHandler(const nlohmann::json& data) -> void;
	auto TrackAudioStationStateUpdateHandler(const nlohmann::json& data) -> void;
	auto SelectGroundToAirChannel(const std::optional<std::string>& callsign, const std::optional<int>& frequency) -> EuroScopePlugIn::CGrountToAirChannel;
	auto UpdateChannel(const std::optional<std::string>& callsign, const std::optional<RDFCommon::chnl_state>& channelState) -> void;
	auto ToggleChannel(EuroScopePlugIn::CGrountToAirChannel Channel, const std::optional<bool>& rx, const std::optional<bool>& tx) -> void;

	// messages
	inline auto DisplayMessageDebug(const std::string& msg) -> void {
#ifdef _DEBUG
		DisplayUserMessage("RDF-DEBUG", "", msg.c_str(), true, true, true, false, false);
#endif // _DEBUG
	}
	inline auto DisplayMessageSilent(const std::string& msg) -> void {
		DisplayUserMessage("Message", "RDF Plugin", msg.c_str(), false, false, false, false, false);
	}
	inline auto DisplayMessageUnread(const std::string& msg) -> void {
		DisplayUserMessage("Message", "RDF Plugin", msg.c_str(), true, true, true, false, false);
	}

public:
	CRDFPlugin();
	~CRDFPlugin();
	auto GetDrawStations(void) -> RDFCommon::callsign_position;
	auto HiddenWndProcessRDFMessage(const std::string& message) -> void;
	auto HiddenWndProcessAFVMessage(const std::string& message) -> void;
	virtual auto OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) -> EuroScopePlugIn::CRadarScreen*;
	virtual auto OnCompileCommand(const char* sCommandLine) -> bool;
	virtual auto OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) -> void;
};

#endif // !CRDFPLUGIN_H
