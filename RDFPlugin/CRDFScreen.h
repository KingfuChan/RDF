#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

class CRDFScreen : public EuroScopePlugIn::CRadarScreen
{
private:
	friend class CRDFPlugin;

	int m_ID;

	inline auto GetRDFPlugin(void) -> CRDFPlugin*;
	auto PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool;

public:
	CRDFScreen(const int& ID);
	~CRDFScreen(void);

	virtual auto OnAsrContentLoaded(bool Loaded) -> void;
	virtual auto OnAsrContentToBeClosed(void) -> void;
	virtual auto OnRefresh(HDC hDC, int Phase) -> void;
	virtual auto OnCompileCommand(const char* sCommandLine) -> bool;

};
