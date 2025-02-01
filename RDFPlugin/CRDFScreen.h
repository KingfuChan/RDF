#pragma once

#ifndef CRDFSCREEN_H
#define CRDFSCREEN_H

#include "stdafx.h"
#include "CRDFPlugin.h"

class CRDFScreen : public EuroScopePlugIn::CRadarScreen, public std::enable_shared_from_this<CRDFScreen>
{
private:
	friend class CRDFPlugin;

	std::weak_ptr<CRDFPlugin> m_Plugin;
	int m_ID;

	auto PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool;

public:
	CRDFScreen(std::weak_ptr<CRDFPlugin> plugin, const int& ID);
	~CRDFScreen(void);

	bool m_Opened;

	virtual auto OnAsrContentToBeClosed(void) -> void;
	virtual auto OnRefresh(HDC hDC, int Phase) -> void;
	virtual auto OnCompileCommand(const char* sCommandLine) -> bool;

};

#endif // !CRDFSCREEN_H
