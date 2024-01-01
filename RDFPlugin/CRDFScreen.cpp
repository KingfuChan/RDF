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

auto CRDFScreen::OnAsrContentToBeClosed(void) -> void
{
}

auto CRDFScreen::OnRefresh(HDC hDC, int Phase) -> void
{
	if (Phase == EuroScopePlugIn::REFRESH_PHASE_BACK_BITMAP) {
		GetRDFPlugin()->activeScreenID = m_ID;
		return;
	}
	if (Phase != EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS) return;

	callsign_position drawPosition = GetRDFPlugin()->activeStations;
	if (drawPosition.empty()) {
		if (GetAsyncKeyState(VK_MBUTTON)) {
			drawPosition = GetRDFPlugin()->previousStations;
			if (drawPosition.empty()) {
				return;
			}
		}
		else {
			return;
		}
	}

	draw_settings paramsPtr = *GetRDFPlugin()->GetDrawingParam();
	HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
	COLORREF penColor = drawPosition.size() > 1 ? paramsPtr.rdfConcurRGB : paramsPtr.rdfRGB;
	HPEN hPen = CreatePen(PS_SOLID, 1, penColor);
	HGDIOBJ oldPen = SelectObject(hDC, hPen);

	for (auto& callsignPos : drawPosition) {
		POINT pPos = ConvertCoordFromPositionToPixel(callsignPos.second.position);
		if (PlaneIsVisible(pPos, GetRadarArea())) {
			double drawR = callsignPos.second.radius;
			// deal with drawing radius when threshold enabled
			if (paramsPtr.circleThreshold >= 0) {
				EuroScopePlugIn::CPosition posLD, posRU;
				GetDisplayArea(&posLD, &posRU);
				POINT pLD = ConvertCoordFromPositionToPixel(posLD);
				POINT pRU = ConvertCoordFromPositionToPixel(posRU);
				double dst = sqrt(pow(pRU.x - pLD.x, 2) + pow(pRU.y - pLD.y, 2));
				drawR = drawR * dst / posLD.DistanceTo(posRU);
			}
			if (drawR >= (double)paramsPtr.circleThreshold) {
				// draw circle
				if (paramsPtr.circleThreshold >= 0) {
					// using position as boundary xy
					EuroScopePlugIn::CPosition pl = callsignPos.second.position;
					GetRDFPlugin()->AddOffset(pl, 270, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pt = callsignPos.second.position;
					GetRDFPlugin()->AddOffset(pt, 0, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pr = callsignPos.second.position;
					GetRDFPlugin()->AddOffset(pr, 90, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pb = callsignPos.second.position;
					GetRDFPlugin()->AddOffset(pb, 180, callsignPos.second.radius);
					Ellipse(hDC,
						ConvertCoordFromPositionToPixel(pl).x,
						ConvertCoordFromPositionToPixel(pt).y,
						ConvertCoordFromPositionToPixel(pr).x,
						ConvertCoordFromPositionToPixel(pb).y
					);
				}
				else {
					// using pixel as boundary xy
					Ellipse(hDC, pPos.x - round(drawR), pPos.y - round(drawR), pPos.x + round(drawR), pPos.y + round(drawR));
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
	return GetRDFPlugin()->ParseSharedSettings(sCommandLine, m_ID);
}

auto CRDFScreen::GetRDFPlugin(void) -> CRDFPlugin*
{
	return static_cast<CRDFPlugin*>(GetPlugIn());
}

auto CRDFScreen::PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool
{
	return p.x >= radarArea.left && p.x <= radarArea.right && p.y >= radarArea.top && p.y <= radarArea.bottom;
}
