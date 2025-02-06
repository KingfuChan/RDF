#pragma once

#include "stdafx.h"
#include "CRDFScreen.h"

CRDFScreen::CRDFScreen(std::weak_ptr<CRDFPlugin> plugin, const int& ID)
{
	m_Plugin = plugin;
	m_ID = ID;
	m_Opened = false;
	PLOGI << "created, ID: " << m_ID;
}

CRDFScreen::~CRDFScreen()
{
	PLOGI << "screen destroyed, ID: " << m_ID;
}

auto CRDFScreen::OnAsrContentLoaded(bool Loaded) -> void
{
	m_Opened = true;
	PLOGI << "content loaded, ID: " << m_ID;
}

auto CRDFScreen::OnAsrContentToBeClosed(void) -> void
{
	m_Opened = false; // should not delete this to avoid crash
	PLOGI << "screen closed, ID " << m_ID;
}

auto CRDFScreen::OnRefresh(HDC hDC, int Phase) -> void
{
	if (!m_Opened) return;
	if (Phase == EuroScopePlugIn::REFRESH_PHASE_BACK_BITMAP) {
		PLOGD << "updating screen, ID: " << m_ID;
		m_Plugin.lock()->LoadDrawingSettings(shared_from_this());
		return;
	}
	if (Phase != EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS) return;

	RDFCommon::callsign_position drawPosition = m_Plugin.lock()->GetDrawStations();
	if (drawPosition.empty()) {
		return;
	}

	PLOGV << "drawing RDF";
	std::shared_lock dlock(m_Plugin.lock()->mtxDrawSettings); // prevent accidental modification
	const RDFCommon::draw_settings params = *m_Plugin.lock()->currentDrawSettings;
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
	PLOGV << "draw complete";
}

auto CRDFScreen::OnCompileCommand(const char* sCommandLine) -> bool
{
	try
	{
		PLOGD << "(ID: " << m_ID << ") " << sCommandLine;
		std::string cmd = sCommandLine;
		std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
		bool asr = false;
		static const std::string COMMAND_ASR = ".RDF ASR ";
		static const std::string COMMAND_PLUGIN = ".RDF ";
		if (cmd.starts_with(COMMAND_ASR)) { // test per-ASR
			cmd = cmd.substr(COMMAND_ASR.size());
			PLOGV << "matching ASR config";
			asr = true;
		}
		else if (cmd.starts_with(COMMAND_PLUGIN)) {
			cmd = cmd.substr(COMMAND_PLUGIN.size());
			PLOGV << "matching plugin config";
		}
		else {
			return false;
		}
		// match config
		std::smatch match;
		std::regex rxDraw(R"(^DRAW (ON|OFF)$)", std::regex_constants::icase);
		if (std::regex_match(cmd, match, rxDraw)) {
			bool mode = match[1].str() == "ON";
			SaveDrawSetting(SETTING_ENABLE_DRAW, "Enable RDF draw", std::to_string(mode), asr);
			return true;
		}
		std::regex rxRGB(R"(^(RGB|CTRGB) (\S+)$)", std::regex_constants::icase);
		if (std::regex_match(cmd, match, rxRGB)) {
			auto bufferMode = match[1].str();
			auto bufferRGB = match[2].str();
			std::transform(bufferMode.begin(), bufferMode.end(), bufferMode.begin(), ::toupper);
			COLORREF _rgb = RGB(0, 0, 0);
			if (RDFCommon::GetRGB(_rgb, bufferRGB)) {
				if (bufferMode == "RGB") {
					SaveDrawSetting(SETTING_RGB, "RGB", bufferRGB, asr);
				}
				else {
					SaveDrawSetting(SETTING_CONCURRENT_RGB, "Concurrent RGB", bufferRGB, asr);
				}
				return true;
			}
		}
		// no need for regex
		int bufferRadius;
		if (sscanf_s(cmd.c_str(), "RADIUS %d", &bufferRadius) == 1) {
			if (bufferRadius > 0) {
				SaveDrawSetting(SETTING_CIRCLE_RADIUS, "Radius", std::to_string(bufferRadius), asr);
				return true;
			}
		}
		int bufferThreshold;
		if (sscanf_s(cmd.c_str(), "THRESHOLD %d", &bufferThreshold) == 1) {
			SaveDrawSetting(SETTING_THRESHOLD, "Threshold", std::to_string(bufferThreshold), asr);
			return true;
		}
		int bufferAltitude;
		if (sscanf_s(cmd.c_str(), "ALTITUDE L%d", &bufferAltitude) == 1) {
			SaveDrawSetting(SETTING_LOW_ALTITUDE, "Altitude (low)", std::to_string(bufferAltitude), asr);
			return true;
		}
		if (sscanf_s(cmd.c_str(), "ALTITUDE H%d", &bufferAltitude) == 1) {
			SaveDrawSetting(SETTING_HIGH_ALTITUDE, "Altitude (high)", std::to_string(bufferAltitude), asr);
			return true;
		}
		int bufferPrecision;
		if (sscanf_s(cmd.c_str(), "PRECISION L%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				SaveDrawSetting(SETTING_LOW_PRECISION, "Precision (low)", std::to_string(bufferPrecision), asr);
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), "PRECISION H%d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				SaveDrawSetting(SETTING_HIGH_PRECISION, "Precision (high)", std::to_string(bufferPrecision), asr);
				return true;
			}
		}
		if (sscanf_s(cmd.c_str(), "PRECISION %d", &bufferPrecision) == 1) {
			if (bufferPrecision >= 0) {
				SaveDrawSetting(SETTING_PRECISION, "Precision", std::to_string(bufferPrecision), asr);
				return true;
			}
		}
		int bufferCtrl;
		if (sscanf_s(cmd.c_str(), "CONTROLLER %d", &bufferCtrl) == 1) {
			SaveDrawSetting(SETTING_DRAW_CONTROLLERS, "Draw controllers", std::to_string(bufferCtrl), asr);
			return true;
		}
	}
	catch (std::exception const& e)
	{
		PLOGE << "Error: " << e.what();
		m_Plugin.lock()->DisplayMessageUnread(std::string("Error: ") + e.what());
	}
	catch (...) {
		PLOGE << UNKNOWN_ERROR_MSG;
		m_Plugin.lock()->DisplayMessageUnread(UNKNOWN_ERROR_MSG);
	}
	return false;
}

auto CRDFScreen::PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool
{
	return p.x >= radarArea.left && p.x <= radarArea.right && p.y >= radarArea.top && p.y <= radarArea.bottom;
}

auto CRDFScreen::SaveDrawSetting(const std::string& varName, const std::string& varDescr, const std::string& val, const bool& useAsr) -> void
{
	std::string logMsg;
	if (useAsr) {
		SaveDataToAsr(varName.c_str(), varDescr.c_str(), val.c_str());
		logMsg = std::format("ASR configurations, {}: {}", varName, val);
	}
	else {
		m_Plugin.lock()->SaveDataToSettings(varName.c_str(), varDescr.c_str(), val.c_str());
		logMsg = std::format("Plugin configurations, {}: {}", varName, val);
	}
	PLOGI << logMsg;
	m_Plugin.lock()->DisplayMessageSilent(logMsg);
}
