// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <afxwin.h>

// reference additional headers your program requires here

// string
#include <string>
#include <regex>
#include <sstream>
// thread
#include <mutex>
#include <shared_mutex>
#include <atomic>
// container
#include <vector>
#include <set>
#include <queue>
#include <map>
// others
#include <random>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <optional>
// networking
#include <httplib.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ws2_32.lib")
// external
#include <nlohmann/json.hpp>
#include <EuroScopePlugIn.h>
#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>
