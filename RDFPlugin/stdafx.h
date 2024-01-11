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
#include <condition_variable>
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
// external
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>
#include <EuroScopePlugIn.h>
