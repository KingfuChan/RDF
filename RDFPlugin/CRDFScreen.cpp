#include "stdafx.h"
#include "CRDFScreen.h"

CRDFScreen::CRDFScreen(CRDFPlugin* plugin)
{
	this->rdfPlugin = plugin;
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
	if (Phase != REFRESH_PHASE_AFTER_TAGS) return;
	callsign_position drawPosition = rdfPlugin->activeTransmittingPilots;
	if (drawPosition.empty()) {
		drawPosition = rdfPlugin->previousActiveTransmittingPilots;
		if (drawPosition.empty() || !(GetKeyState(VK_MBUTTON) == -127 || GetKeyState(VK_MBUTTON) == -128)) {
			return;
		}
	}

	HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
	COLORREF penColor = drawPosition.size() > 1 ? rdfPlugin->rdfConcurrentTransmissionRGB : rdfPlugin->rdfRGB;
	HPEN hPen = CreatePen(PS_SOLID, 1, penColor);
	HGDIOBJ oldPen = SelectObject(hDC, hPen);

	for (auto& callsignPos : drawPosition) {
		POINT pPos = ConvertCoordFromPositionToPixel(callsignPos.second.position);
		if (PlaneIsVisible(pPos, GetRadarArea())) {
			double drawR = callsignPos.second.radius;
			// deal with drawing radius when threshold enabled
			if (rdfPlugin->circleThreshold >= 0) {
				CPosition posLD, posRU;
				GetDisplayArea(&posLD, &posRU);
				POINT pLD = ConvertCoordFromPositionToPixel(posLD);
				POINT pRU = ConvertCoordFromPositionToPixel(posRU);
				double dst = sqrt(pow(pRU.x - pLD.x, 2) + pow(pRU.y - pLD.y, 2));
				drawR = drawR * dst / posLD.DistanceTo(posRU);
			}
			if (drawR >= (double)rdfPlugin->circleThreshold) {
				// draw circle
				Ellipse(hDC, pPos.x - round(drawR), pPos.y - round(drawR), pPos.x + round(drawR), pPos.y + round(drawR));
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

bool CRDFScreen::PlaneIsVisible(POINT p, RECT radarArea)
{
	return p.x >= radarArea.left && p.x <= radarArea.right && p.y >= radarArea.top && p.y <= radarArea.bottom;
}



