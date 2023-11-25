#pragma once

#include "stdafx.h"
#include <map>
#include "CRDFPlugin.h"

class CRDFScreen : public EuroScopePlugIn::CRadarScreen
{
private:
	CRDFPlugin* rdfPlugin;

	bool PlaneIsVisible(POINT p, RECT radarArea);

public:
	CRDFScreen(CRDFPlugin* plugin);
	virtual ~CRDFScreen();

	virtual void OnAsrContentToBeClosed(void);
	virtual void OnRefresh(HDC hDC, int Phase);

};

