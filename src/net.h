// designElectronics — minimal localhost TCP helpers (desktop only) used to
// connect an AI agent's MCP process to the running app.
#pragma once
#include <string>

#if !defined(__EMSCRIPTEN__) && !defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_ANDROID) && !defined(__ANDROID__)
#define DE_HAVE_NET 1
#endif

namespace net {

#ifdef DE_HAVE_NET
// A connected or listening socket handle (-1 = invalid).
using Sock = long long;
constexpr Sock kInvalid = -1;

bool init();                                  // WSAStartup on Windows, no-op elsewhere
Sock listenLocal(int& port);                  // bind 127.0.0.1:0, returns the chosen port
Sock acceptClient(Sock listener, int timeoutMs);  // kInvalid on timeout/error
Sock connectLocal(int port);
bool sendAll(Sock s, const std::string& data);
// Read one '\n'-terminated line (without the newline). False when the peer closed.
bool readLine(Sock s, std::string& buffer, std::string& line);
void closeSock(Sock s);
#endif

}  // namespace net
