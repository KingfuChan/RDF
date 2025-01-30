#pragma once

#include "stdafx.h"
#include "RDFCommon.h"

auto RDFCommon::GetRGB(COLORREF& color, const std::string& settingValue) -> void
{
	PLOGV << settingValue;
	std::regex rxRGB(R"(^(\d{1,3}):(\d{1,3}):(\d{1,3})$)");
	std::smatch match;
	if (std::regex_match(settingValue, match, rxRGB)) {
		UINT r = std::stoi(match[1].str());
		UINT g = std::stoi(match[2].str());
		UINT b = std::stoi(match[3].str());
		if (r <= 255 && g <= 255 && b <= 255) {
			color = RGB(r, g, b);
		}
	}
}

auto RDFCommon::AddOffset(EuroScopePlugIn::CPosition& position, const double& heading, const double& distance) -> void
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
