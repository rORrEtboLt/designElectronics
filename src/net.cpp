// designElectronics — minimal localhost TCP helpers (see net.h).
#include "net.h"

#ifdef DE_HAVE_NET
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cstring>

namespace net {

bool init() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
        done = true;
    }
#endif
    return true;
}

static sockaddr_in loopback(int port) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // never reachable from other machines
    return a;
}

Sock listenLocal(int& port) {
    if (!init()) return kInvalid;
    auto s = socket(AF_INET, SOCK_STREAM, 0);
    if ((Sock)s == kInvalid) return kInvalid;
    sockaddr_in a = loopback(0);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 4) != 0) { closeSock((Sock)s); return kInvalid; }
    socklen_t len = sizeof a;
    getsockname(s, (sockaddr*)&a, &len);
    port = ntohs(a.sin_port);
    return (Sock)s;
}

Sock acceptClient(Sock listener, int timeoutMs) {
#ifdef _WIN32
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET((SOCKET)listener, &fds);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select(0, &fds, nullptr, nullptr, &tv) <= 0) return kInvalid;
#else
    pollfd p{(int)listener, POLLIN, 0};
    if (poll(&p, 1, timeoutMs) <= 0) return kInvalid;
#endif
    auto c = accept((decltype(socket(0, 0, 0)))listener, nullptr, nullptr);
    return (Sock)c;
}

Sock connectLocal(int port) {
    if (!init()) return kInvalid;
    auto s = socket(AF_INET, SOCK_STREAM, 0);
    if ((Sock)s == kInvalid) return kInvalid;
    sockaddr_in a = loopback(port);
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) { closeSock((Sock)s); return kInvalid; }
    return (Sock)s;
}

bool sendAll(Sock s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        auto n = send((decltype(socket(0, 0, 0)))s, data.data() + off, (int)(data.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

bool readLine(Sock s, std::string& buffer, std::string& line) {
    for (;;) {
        size_t nl = buffer.find('\n');
        if (nl != std::string::npos) {
            line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            return true;
        }
        char tmp[4096];
        auto n = recv((decltype(socket(0, 0, 0)))s, tmp, sizeof tmp, 0);
        if (n <= 0) return false;
        buffer.append(tmp, (size_t)n);
    }
}

void closeSock(Sock s) {
    if (s == kInvalid) return;
#ifdef _WIN32
    closesocket((SOCKET)s);
#else
    close((int)s);
#endif
}

}  // namespace net
#endif
