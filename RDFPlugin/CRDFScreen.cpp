#pragma once

#include "stdafx.h"
#include "CRDFScreen.h"

CRDFScreen::CRDFScreen(const int& ID)
{
	m_ID = ID;
}

CRDFScreen::~CRDFScreen()
{
}

auto CRDFScreen::OnAsrContentLoaded(bool Loaded) -> void
{
	if (Loaded) { // ASR load finished
		GetRDFPlugin()->LoadDrawingSettings(m_ID);
	}
}

auto CRDFScreen::OnAsrContentToBeSaved(void) -> void
{
	for (auto& s : newAsrData) {
		SaveDataToAsr(s.first.c_str(), s.second.descr.c_str(), s.second.value.c_str());
	}
}

auto CRDFScreen::OnAsrContentToBeClosed(void) -> void
{
}

auto CRDFScreen::OnRefresh(HDC hDC, int Phase) -> void
{
	if (Phase == EuroScopePlugIn::REFRESH_PHASE_BACK_BITMAP) {
		GetRDFPlugin()->vidScreen = m_ID;
		return;
	}
	if (Phase != EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS) return;

	callsign_position drawPosition = GetRDFPlugin()->GetDrawStations();
	if (drawPosition.empty()) {
		return;
	}

	draw_settings params = GetRDFPlugin()->GetDrawingParam();
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
					AddOffset(pl, 270, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pt = callsignPos.second.position;
					AddOffset(pt, 0, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pr = callsignPos.second.position;
					AddOffset(pr, 90, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pb = callsignPos.second.position;
					AddOffset(pb, 180, callsignPos.second.radius);
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
	return GetRDFPlugin()->ProcessDrawingCommand(sCommandLine, m_ID);
}

auto CRDFScreen::GetRDFPlugin(void) -> CRDFPlugin*
{
	return static_cast<CRDFPlugin*>(GetPlugIn());
}

auto CRDFScreen::PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool
{
	return p.x >= radarArea.left && p.x <= radarArea.right && p.y >= radarArea.top && p.y <= radarArea.bottom;
}

auto CRDFScreen::AddAsrDataToBeSaved(const std::string& name, const std::string& description, const std::string& value) -> void
{
	newAsrData[name] = { description, value };
}
