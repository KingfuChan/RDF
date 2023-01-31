#pragma once

#include "stdafx.h"
#include <map>
#include "CRDFPlugin.h"

using namespace std;
using namespace EuroScopePlugIn;

class CRDFScreen : public CRadarScreen
{
private:
	CRDFPlugin* rdfPlugin;
	COLORREF rdfColor;
	COLORREF rdfConcurrentTransmissionsColor;
	int circleRadius;

	bool PlaneIsVisible(POINT p, RECT radarArea);

public:
	CRDFScreen(CRDFPlugin* plugin, COLORREF rdfColor, COLORREF rdfConcurrentTransmissionColor, int circleRadius);
	virtual ~CRDFScreen();

	virtual void OnAsrContentToBeClosed(void);
	virtual void OnRefresh(HDC hDC, int Phase);

};

