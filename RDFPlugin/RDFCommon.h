#pragma once

#ifndef RDFCOMMON_H
#define RDFCOMMON_H

#include "stdafx.h"

// Plugin info
constexpr auto MY_PLUGIN_NAME = "RDF Plugin for Euroscope";
constexpr auto MY_PLUGIN_VERSION = "1.4.2";
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
constexpr auto TAG_ITEM_TYPE_RDF_STATE = 1001; // RDF state

// Constants
constexpr auto UNKNOWN_ERROR_MSG = "Unknown error!";
constexpr auto FREQUENCY_REDUNDANT = 199999; // kHz
constexpr auto pi = 3.141592653589793;
constexpr auto EarthRadius = 3438.0; // nautical miles, referred to internal CEuroScopeCoord
inline static constexpr auto GEOM_RAD_FROM_DEG(const double& deg) -> double { return deg * pi / 180.0; };
inline static constexpr auto GEOM_DEG_FROM_RAD(const double& rad) -> double { return rad / pi * 180.0; };

// Inline functions
inline static auto FrequencyFromMHz(const double& freq) -> int { return (int)round(freq * 1000.0); };
inline static auto FrequencyFromHz(const double& freq) -> int { return (int)round(freq / 1000.0); };
inline static auto FrequencyIsSame(const auto& freq1, const auto& freq2) -> bool { return abs(freq1 - freq2) <= 10; }; // return true if same frequency, frequency in kHz

namespace RDFCommon {

	// General functions
	auto GetRGB(COLORREF& color, const std::string& settingValue) -> void;
	auto AddOffset(EuroScopePlugIn::CPosition& position, const double& heading, const double& distance) -> void;

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
		}
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

}

#endif // !RDFCOMMON_H
