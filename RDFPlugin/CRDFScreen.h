#pragma once

#include "stdafx.h"
#include "CRDFPlugin.h"

typedef struct _asr_to_save {
	std::string descr;
	std::string value;
} asr_to_save;

class CRDFScreen : public EuroScopePlugIn::CRadarScreen
{
private:
	friend class CRDFPlugin;

	int m_ID;
	std::map<std::string, asr_to_save> newAsrData; // sVariableName -> asr_to_save

	inline auto GetRDFPlugin(void) -> CRDFPlugin*;
	auto PlaneIsVisible(const POINT& p, const RECT& radarArea) -> bool;

public:
	CRDFScreen(const int& ID);
	~CRDFScreen(void);

	virtual auto OnAsrContentLoaded(bool Loaded) -> void;
	virtual auto OnAsrContentToBeSaved(void) -> void;
	virtual auto OnAsrContentToBeClosed(void) -> void;
	virtual auto OnRefresh(HDC hDC, int Phase) -> void;
	virtual auto OnCompileCommand(const char* sCommandLine) -> bool;

	auto AddAsrDataToBeSaved(const std::string& name, const std::string& description, const std::string& value) -> void;

};
