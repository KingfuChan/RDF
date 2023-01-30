#include "stdafx.h"
#include "CRDFScreen.h"

const double pi = 3.141592653589793;
const double EarthRadius = 6371.393 / 1.852; // nautical miles

CRDFScreen::CRDFScreen(CRDFPlugin* plugin, COLORREF rdfColor, COLORREF rdfConcurrentTransmissionsColor, int CircleRadius)
{
	this->rdfPlugin = plugin;
	this->rdfColor = rdfColor;
	this->rdfConcurrentTransmissionsColor = rdfConcurrentTransmissionsColor;
	this->circleRadius = CircleRadius;
	this->rdGenerator = mt19937(this->randomDevice());
	this->disUniform = uniform_real_distribution<>(0, 180);
	this->disNormal = normal_distribution<>(0, 1);
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
	int activeTransmitCount = rdfPlugin->GetActiveTransmittingPilots().size();
	int previousTransmitCount = rdfPlugin->GetPreviousActiveTransmittingPilots().size();
	bool mouseStroke = GetKeyState(VK_MBUTTON) == -127 || GetKeyState(VK_MBUTTON) == -128;

	// organize drawings
	if (activeTransmitCount) {
		for (auto& callsign : rdfPlugin->GetActiveTransmittingPilots()) {
			if (activePosition.find(callsign) == activePosition.end()) {
				auto radarTarget = rdfPlugin->RadarTargetSelect(callsign.c_str());
				if (radarTarget.IsValid()) {
					CPosition pos = radarTarget.GetPosition().GetPosition();
					pos = AddRandomOffset(pos);
					activePosition[callsign] = pos;
				}
				else {
					auto controller = rdfPlugin->ControllerSelect(callsign.c_str());
					if (controller.IsValid()) {
						CPosition pos = controller.GetPosition();
						if (!controller.IsController()) { // for shared cockpit
							pos = AddRandomOffset(pos);
						}
						activePosition[callsign] = pos;
					}
				}
			}
		}
		previousPosition = activePosition;
		drawPosition = activePosition;
	}
	else if (previousTransmitCount && mouseStroke) {
		activePosition.clear();
		drawPosition = previousPosition;
	}
	else {
		activePosition.clear();
		drawPosition.clear();
		return;
	}

	// drawings
	HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
	COLORREF penColor = activeTransmitCount > 1 || previousTransmitCount > 1 ? rdfConcurrentTransmissionsColor : rdfColor;
	HPEN hPen = CreatePen(PS_SOLID, 1, penColor);
	HGDIOBJ oldPen = SelectObject(hDC, hPen);

	for (auto& callsignPos : drawPosition) {
		POINT p = ConvertCoordFromPositionToPixel(callsignPos.second);
		if (PlaneIsVisible(p, GetRadarArea()))
		{
			CPosition posLD, posRU;
			GetDisplayArea(&posLD, &posRU);
			POINT pLD = ConvertCoordFromPositionToPixel(posLD);
			POINT pRU = ConvertCoordFromPositionToPixel(posRU);
			double dst = sqrt(pow(pRU.x - pLD.x, 2) + pow(pRU.y - pLD.y, 2));
			int drawR = round((double)circleRadius * dst / posLD.DistanceTo(posRU));
			Ellipse(hDC, p.x - drawR, p.y - drawR, p.x + drawR, p.y + drawR);
		}
		else
		{
			// Outside screen - draw line
			POINT oldPoint;
			MoveToEx(hDC, (GetRadarArea().right - GetRadarArea().left) / 2, (GetRadarArea().bottom - GetRadarArea().top) / 2, &oldPoint);
			LineTo(hDC, p.x, p.y);
			MoveToEx(hDC, oldPoint.x, oldPoint.y, NULL);
		}
	}
	SelectObject(hDC, oldBrush);
	SelectObject(hDC, oldPen);
	DeleteObject(hPen);

}

CPosition CRDFScreen::AddRandomOffset(CPosition pos)
{
	// input distance in nautical miles, angle in radian.
	double distance = disNormal(rdGenerator) * (double)circleRadius / 2.0;
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

bool CRDFScreen::PlaneIsVisible(POINT p, RECT radarArea)
{
	return p.x >= radarArea.left && p.x <= radarArea.right && p.y >= radarArea.top && p.y <= radarArea.bottom;
}



