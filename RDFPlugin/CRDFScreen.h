#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

class CRDFScreen : public EuroScopePlugIn::CRadarScreen
{
private:
	friend class CRDFPlugin;

	CRDFPlugin* rdfPlugin;

	bool PlaneIsVisible(POINT p, RECT radarArea);
	void LoadAsrSettings(void);

public:
	CRDFScreen(CRDFPlugin* plugin);
	virtual ~CRDFScreen();

	virtual void OnAsrContentToBeClosed(void);
	virtual void OnRefresh(HDC hDC, int Phase);
	virtual bool OnCompileCommand(const char* sCommandLine);

};
