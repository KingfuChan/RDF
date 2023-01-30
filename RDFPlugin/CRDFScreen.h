#pragma once
#include <random>
#include <map>
#include <EuroScopePlugIn.h>
#include "CRDFPlugin.h"

using namespace std;
using namespace EuroScopePlugIn;

class CRDFScreen : public CRadarScreen
{
private:
	CRDFPlugin *rdfPlugin;
	COLORREF rdfColor;
	COLORREF rdfConcurrentTransmissionsColor;
	int circleRadius;

	random_device randomDevice;
	mt19937 rdGenerator;
	uniform_real_distribution<> disUniform;
	normal_distribution<> disNormal;

	map<string, CPosition> activePosition, previousPosition, drawPosition;

	bool PlaneIsVisible(POINT p, RECT radarArea);
	CPosition AddRandomOffset(CPosition pos);
public:
	CRDFScreen(CRDFPlugin *plugin, COLORREF rdfColor, COLORREF rdfConcurrentTransmissionColor, int CircleRadius);
	virtual ~CRDFScreen();

	virtual void    OnAsrContentToBeClosed(void);
	virtual void OnRefresh(HDC hDC, int Phase);

	

};

