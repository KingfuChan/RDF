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
	std::map<std::string, RDFCommon::asr_to_save> newAsrData; // sVariableName -> asr_to_save

	auto PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool;

public:
	CRDFScreen(std::weak_ptr<CRDFPlugin> plugin, const int& ID);
	~CRDFScreen(void);

	bool m_Opened;
	std::shared_ptr<RDFCommon::draw_settings> m_DrawSettings;

	virtual auto OnAsrContentLoaded(bool Loaded) -> void;
	virtual auto OnAsrContentToBeSaved(void) -> void;
	virtual auto OnAsrContentToBeClosed(void) -> void;
	virtual auto OnRefresh(HDC hDC, int Phase) -> void;
	virtual auto OnCompileCommand(const char* sCommandLine) -> bool;

};

#endif // !CRDFSCREEN_H
