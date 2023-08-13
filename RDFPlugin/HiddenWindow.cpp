#include "stdafx.h"
#include "HiddenWindow.h"
#include "CRDFPlugin.h"


CRDFPlugin* rdfPlugin;

LRESULT CALLBACK HiddenWindowRDF(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		rdfPlugin = reinterpret_cast<CRDFPlugin*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		return TRUE;
	}
	case WM_COPYDATA: {
		COPYDATASTRUCT* data = reinterpret_cast<COPYDATASTRUCT*>(lParam);

		if (data != nullptr && data->dwData == 666 && data->lpData != nullptr && rdfPlugin != nullptr) {
			rdfPlugin->ProcessRDFMessage(reinterpret_cast<const char*>(data->lpData));
		}
		return TRUE;
	}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK HiddenWindowAFV(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		rdfPlugin = reinterpret_cast<CRDFPlugin*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		return TRUE;
	}
	case WM_COPYDATA: {
		COPYDATASTRUCT* data = reinterpret_cast<COPYDATASTRUCT*>(lParam);

		if (data != nullptr && data->dwData == 666 && data->lpData != nullptr && rdfPlugin != nullptr) {
			rdfPlugin->ProcessAFVMessage(reinterpret_cast<const char*>(data->lpData));
		}
		return TRUE;
	}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}


