// dllmain.cpp

#include "stdafx.h"
#include "CRDFPlugin.h"

// Interface for EuroScope plugin loading
std::shared_ptr<CRDFPlugin> pMyPlugIn;

void __declspec (dllexport)
EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
	pMyPlugIn = std::make_shared<CRDFPlugin>();
	*ppPlugInInstance = pMyPlugIn.get();
}

void __declspec (dllexport)
EuroScopePlugInExit(void)
{
	pMyPlugIn.reset();
}