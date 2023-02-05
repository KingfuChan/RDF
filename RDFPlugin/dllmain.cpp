// dllmain.cpp

#include "stdafx.h"
#include "CRDFPlugin.h"

// Interface for EuroScope plugin loading
CRDFPlugin* pMyPlugIn = nullptr;

void __declspec (dllexport)
EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
	*ppPlugInInstance = pMyPlugIn = new CRDFPlugin;
}

void __declspec (dllexport)
EuroScopePlugInExit(void)
{
	delete pMyPlugIn;
}