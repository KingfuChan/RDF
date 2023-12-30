#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

class CRDFScreen : public EuroScopePlugIn::CRadarScreen
{
private:
	friend class CRDFPlugin;

	CRDFPlugin* rdfPlugin;

	auto PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool;
	auto LoadAsrSettings(void) -> void;

public:
	CRDFScreen(CRDFPlugin* plugin);
	~CRDFScreen();

	virtual auto OnAsrContentToBeClosed(void) -> void;
	virtual auto OnRefresh(HDC hDC, int Phase) -> void;
	virtual auto OnCompileCommand(const char* sCommandLine) -> bool;

};
