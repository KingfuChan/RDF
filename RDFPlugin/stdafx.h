// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>

// reference additional headers your program requires here

// string
#include <string>
#include <regex>
#include <sstream>
// thread
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>
// container
#include <set>
#include <queue>
#include <map>
// others
#include <chrono>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <memory>
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
