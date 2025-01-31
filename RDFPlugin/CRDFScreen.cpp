#pragma once

#include "stdafx.h"
#include "CRDFScreen.h"

CRDFScreen::CRDFScreen(std::weak_ptr<CRDFPlugin> plugin, const int& ID)
{
	m_Plugin = plugin;
	m_ID = ID;
	m_Opened = true;
	PLOGD << "created, ID: " << m_ID;
}

CRDFScreen::~CRDFScreen()
{
	PLOGD << "screen destroyed, ID: " << m_ID;
}

auto CRDFScreen::OnAsrContentLoaded(bool Loaded) -> void
{
	m_DrawSettings = std::make_shared<RDFCommon::draw_settings>();
	m_Plugin.lock()->LoadDrawingSettings(shared_from_this());
	PLOGD << "ASR settings loaded, ID: " << m_ID;
}

auto CRDFScreen::OnAsrContentToBeClosed(void) -> void
{
	m_Opened = false; // should not delete this to avoid crash
	PLOGD << "screen closed, ID " << m_ID;
}

auto CRDFScreen::OnRefresh(HDC hDC, int Phase) -> void
{
	std::unique_lock dlock(m_Plugin.lock()->mtxDrawSettings); // prevent accidental modification
	if (Phase == EuroScopePlugIn::REFRESH_PHASE_BACK_BITMAP) {
		PLOGV << "updating vidScreen: " << m_ID;
		auto ptr = m_Plugin.lock();
		ptr->screenDrawSettings = m_DrawSettings;
		return;
	}
	if (Phase != EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS) return;

	RDFCommon::callsign_position drawPosition = m_Plugin.lock()->GetDrawStations();
	if (drawPosition.empty()) {
		return;
	}

	const RDFCommon::draw_settings params = *m_Plugin.lock()->screenDrawSettings;
	dlock.unlock();

	HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
	COLORREF penColor = drawPosition.size() > 1 ? params.rdfConcurRGB : params.rdfRGB;
	HPEN hPen = CreatePen(PS_SOLID, 1, penColor);
	HGDIOBJ oldPen = SelectObject(hDC, hPen);

	for (auto& callsignPos : drawPosition) {
		POINT pPos = ConvertCoordFromPositionToPixel(callsignPos.second.position);
		if (PlaneIsVisible(pPos, GetRadarArea())) {
			double drawR = callsignPos.second.radius;
			// deal with drawing radius when threshold enabled
			if (params.circleThreshold >= 0) {
				EuroScopePlugIn::CPosition posLD, posRU;
				GetDisplayArea(&posLD, &posRU);
				POINT pLD = ConvertCoordFromPositionToPixel(posLD);
				POINT pRU = ConvertCoordFromPositionToPixel(posRU);
				double dst = sqrt(pow(pRU.x - pLD.x, 2) + pow(pRU.y - pLD.y, 2));
				drawR = drawR * dst / posLD.DistanceTo(posRU);
			}
			if (drawR >= (double)params.circleThreshold) {
				// draw circle
				if (params.circleThreshold >= 0) {
					// using position as boundary xy
					EuroScopePlugIn::CPosition pl = callsignPos.second.position;
					RDFCommon::AddOffset(pl, 270, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pt = callsignPos.second.position;
					RDFCommon::AddOffset(pt, 0, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pr = callsignPos.second.position;
					RDFCommon::AddOffset(pr, 90, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pb = callsignPos.second.position;
					RDFCommon::AddOffset(pb, 180, callsignPos.second.radius);
					Ellipse(hDC,
						ConvertCoordFromPositionToPixel(pl).x,
						ConvertCoordFromPositionToPixel(pt).y,
						ConvertCoordFromPositionToPixel(pr).x,
						ConvertCoordFromPositionToPixel(pb).y
					);
				}
				else {
					// using pixel as boundary xy
					Ellipse(hDC, pPos.x - (int)round(drawR), pPos.y - (int)round(drawR), pPos.x + (int)round(drawR), pPos.y + (int)round(drawR));
				}
				continue;
			}
		}
		// draw line
		POINT oldPoint;
		MoveToEx(hDC, (GetRadarArea().right - GetRadarArea().left) / 2, (GetRadarArea().bottom - GetRadarArea().top) / 2, &oldPoint);
		LineTo(hDC, pPos.x, pPos.y);
		MoveToEx(hDC, oldPoint.x, oldPoint.y, NULL);
	}

	SelectObject(hDC, oldBrush);
	SelectObject(hDC, oldPen);
	DeleteObject(hPen);
}

auto CRDFScreen::OnCompileCommand(const char* sCommandLine) -> bool
{

	// pass screenID = -1 to use plugin settings, otherwise use ASR settings
	// deals with settings available for asr
	auto pluginPtr = m_Plugin.lock();
	auto SaveSetting = [&](const auto& varName, const auto& varDescr, const auto& val) -> void {
		SaveDataToAsr(varName, varDescr, val);
		auto logMsg = std::format("Add ASR configurations to be saved, {}: {}", varName, val);
		PLOGI << logMsg;
		pluginPtr->DisplayMessageSilent(logMsg);
		};
	try
	{
		std::string command = sCommandLine;
		PLOGV << "command: " << command;
		std::smatch match;
		std::regex rxRGB(R"(^.RDF (RGB|CTRGB) (\S+)$)", std::regex_constants::icase);
		if (regex_match(command, match, rxRGB)) {
			auto bufferMode = match[1].str();
			auto bufferRGB = match[2].str();
			std::transform(bufferMode.begin(), bufferMode.end(), bufferMode.begin(), ::toupper);
			if (bufferMode == "RGB") {
				COLORREF prevRGB = m_DrawSettings->rdfRGB;
				RDFCommon::GetRGB(m_DrawSettings->rdfRGB, bufferRGB);
				if (m_DrawSettings->rdfRGB != prevRGB) {
					SaveSetting(SETTING_RGB, "RGB", bufferRGB.c_str());
					return true;
				}
			}
			else {
				COLORREF prevRGB = m_DrawSettings->rdfConcurRGB;
				RDFCommon::GetRGB(m_DrawSettings->rdfConcurRGB, bufferRGB);
				if (m_DrawSettings->rdfConcurRGB != prevRGB) {
					SaveSetting(SETTING_CONCURRENT_RGB, "Concurrent RGB", bufferRGB.c_str());
					return true;
				}
			}
		}
		// no need for regex
		std::string cmd = command;
		std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
		int bufferRadius;
		if (sscanf_s(cmd.c_str(), ".RDF RADIUS %d", &bufferRadius) == 1) {
			if (bufferRadius > 0) {
				m_DrawSettings->circleRadius = bufferRadius;
				SaveSetting(SETTING_CIRCLE_RADIUS, "Radius", std::to_string(m_DrawSettings->circleRadius).c_str());
				return true;
			}
		}
		int bufferThreshold;
		if (sscanf_s(cmd.c_str(), ".RDF THRESHOLD %d", &bufferThreshold) == 1) {
			m_DrawSettings->circleThreshold = bufferThreshold;
			SaveSetting(SETTING_THRESHOLD, "Threshold", std::to_string(m_DrawSettings->circleThreshold).c_str());
			return true;
		}
		int bufferAltitude;
		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE L%d", &bufferAltitude) == 1) {
			m_DrawSettings->lowAltitude = bufferAltitude;
			SaveSetting(SETTING_LOW_ALTITUDE, "Altitude (low)", std::to_string(m_DrawSettings->lowAltitude).c_str());
			return true;
		}
		if (sscanf_s(cmd.c_str(), ".RDF ALTITUDE H%d", &bufferAltitude) == 1) {
			m_DrawSettings->highAltitude = bufferAltitude;
			SaveSetting(SETTING_HIGH_ALTITUDE, "Altitude (high)", std::to_string(m_DrawSettings->highAltitude).c_str());
			return true;
		}
		int bufferPrecision;
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION L%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				m_DrawSettings->lowPrecision = bufferPrecision;
				SaveSetting(SETTING_LOW_PRECISION, "Precision (low)", std::to_string(m_DrawSettings->lowPrecision).c_str());
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION H%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				m_DrawSettings->highPrecision = bufferPrecision;
				SaveSetting(SETTING_HIGH_PRECISION, "Precision (high)", std::to_string(m_DrawSettings->highPrecision).c_str());
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), ".RDF PRECISION %d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				m_DrawSettings->circlePrecision = bufferPrecision;
				SaveSetting(SETTING_PRECISION, "Precision", std::to_string(m_DrawSettings->circlePrecision).c_str());
				return true;
			}
		}
		int bufferCtrl;
		if (sscanf_s(cmd.c_str(), ".RDF CONTROLLER %d", &bufferCtrl) == 1) {
			m_DrawSettings->drawController = bufferCtrl;
			SaveSetting(SETTING_DRAW_CONTROLLERS, "Draw controllers", std::to_string(bufferCtrl).c_str());
			return true;
		}
	}
	catch (std::exception const& e)
	{
		PLOGE << "Error: " << e.what();
		pluginPtr->DisplayMessageUnread(std::string("Error: ") + e.what());
	}
	catch (...) {
		PLOGE << UNKNOWN_ERROR_MSG;
		pluginPtr->DisplayMessageUnread(UNKNOWN_ERROR_MSG);
	}
	return false;
}

auto CRDFScreen::PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool
{
	return p.x >= radarArea.left && p.x <= radarArea.right && p.y >= radarArea.top && p.y <= radarArea.bottom;
}
