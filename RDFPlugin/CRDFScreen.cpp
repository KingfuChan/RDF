#pragma once

#include "stdafx.h"
#include "CRDFScreen.h"

CRDFScreen::CRDFScreen(CRDFPlugin* plugin)
{
	rdfPlugin = plugin;
}


CRDFScreen::~CRDFScreen()
{
}

void CRDFScreen::OnAsrContentToBeClosed(void)
{
	delete this;
}

void CRDFScreen::OnRefresh(HDC hDC, int Phase)
{
	if (Phase != EuroScopePlugIn::REFRESH_PHASE_AFTER_TAGS) return;
	//rdfPlugin->DisplayDebugMessage("refresh triggered");
	callsign_position drawPosition = rdfPlugin->activeStations;
	if (drawPosition.empty()) {
		if (GetAsyncKeyState(VK_MBUTTON)) {
			drawPosition = rdfPlugin->previousStations;
			if (drawPosition.empty()) {
				return;
			}
		}
		else {
			return;
		}
	}

	HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
	COLORREF penColor = drawPosition.size() > 1 ? rdfPlugin->rdfConcurRGB : rdfPlugin->rdfRGB;
	HPEN hPen = CreatePen(PS_SOLID, 1, penColor);
	HGDIOBJ oldPen = SelectObject(hDC, hPen);

	for (auto& callsignPos : drawPosition) {
		POINT pPos = ConvertCoordFromPositionToPixel(callsignPos.second.position);
		if (PlaneIsVisible(pPos, GetRadarArea())) {
			double drawR = callsignPos.second.radius;
			// deal with drawing radius when threshold enabled
			if (rdfPlugin->circleThreshold >= 0) {
				EuroScopePlugIn::CPosition posLD, posRU;
				GetDisplayArea(&posLD, &posRU);
				POINT pLD = ConvertCoordFromPositionToPixel(posLD);
				POINT pRU = ConvertCoordFromPositionToPixel(posRU);
				double dst = sqrt(pow(pRU.x - pLD.x, 2) + pow(pRU.y - pLD.y, 2));
				drawR = drawR * dst / posLD.DistanceTo(posRU);
			}
			if (drawR >= (double)rdfPlugin->circleThreshold) {
				// draw circle
				if (rdfPlugin->circleThreshold >= 0) {
					// using position as boundary xy
					EuroScopePlugIn::CPosition pl = callsignPos.second.position;
					rdfPlugin->AddOffset(pl, 270, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pt = callsignPos.second.position;
					rdfPlugin->AddOffset(pt, 0, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pr = callsignPos.second.position;
					rdfPlugin->AddOffset(pr, 90, callsignPos.second.radius);
					EuroScopePlugIn::CPosition pb = callsignPos.second.position;
					rdfPlugin->AddOffset(pb, 180, callsignPos.second.radius);
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

bool CRDFScreen::OnCompileCommand(const char* sCommandLine)
{
	return rdfPlugin->ParseSharedSettings(sCommandLine, this);
}

bool CRDFScreen::PlaneIsVisible(POINT p, RECT radarArea)
{
	return p.x >= radarArea.left && p.x <= radarArea.right && p.y >= radarArea.top && p.y <= radarArea.bottom;
}

void CRDFScreen::LoadAsrSettings(void)
{
}
