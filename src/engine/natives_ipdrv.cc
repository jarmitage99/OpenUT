#include "core/vm.h"
#include "engine/ipdrv.h"
#include "engine/props_engine.h"
#include "engine/world.h"

#ifndef __EMSCRIPTEN__

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ut {
    namespace {
#ifdef _WIN32
        using SocketHandle = SOCKET;
        using AddressLength = int;
        using DataLength = int;
#else
        using SocketHandle = int;
        using AddressLength = socklen_t;
        using DataLength = size_t;
#endif

        struct IpAddr {
            int32_t addr = 0;
            int32_t port = 0;
        };

        struct Link {
            UObject* actor = nullptr;
            SocketHandle socket = 0;
        };

        struct Resolution {
            UObject* actor = nullptr;
            IpAddr address;
            bool found = false;
        };

        enum LinkState : uint8_t {
            STATE_Initialized,
            STATE_Ready,
            STATE_Listening,
            STATE_Connecting,
            STATE_Connected,
            STATE_ListenClosePending,
            STATE_ConnectClosePending,
        };

        constexpr size_t TcpReadPerTick = 16384;
        constexpr size_t AcceptedReadLimit = 1 << 20;
        constexpr int AcceptsPerTick = 8;
        constexpr double CloseTimeout = 10.0;
        constexpr double ReplyBurst = 16384.0;
        constexpr double ReplyRate = 4096.0;

        struct TcpLink {
            UObject* actor = nullptr;
            SocketHandle socket = 0;
            bool open = false;
            bool accepted = false;
            int32_t port = 0;
            size_t received = 0;
            double closeBy = 0;
            std::string outgoing;
            std::string partialLine;
        };

        struct Allowance {
            double bytes = ReplyBurst;
            double updated = 0;
        };

        std::vector<Link> links;
        std::vector<Resolution> resolutions;
        std::vector<std::unique_ptr<TcpLink>> tcpLinks;
        std::unordered_map<uint32_t, Allowance> allowances;
        double lastPrune = 0;

        double now()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        void startSockets()
        {
#ifdef _WIN32
            static bool started = false;
            if (!started) {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
                started = true;
            }
#endif
        }

        void closeSocket(SocketHandle handle)
        {
#ifdef _WIN32
            closesocket(handle);
#else
            close(handle);
#endif
        }

        void makeNonBlocking(SocketHandle handle)
        {
#ifdef _WIN32
            u_long nonBlocking = 1;
            ioctlsocket(handle, static_cast<long>(FIONBIO), &nonBlocking);
#else
            fcntl(handle, F_SETFL, fcntl(handle, F_GETFL, 0) | O_NONBLOCK);
#endif
#ifdef SO_NOSIGPIPE
            int enabled = 1;
            setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        }

        bool wouldBlock()
        {
#ifdef _WIN32
            int error = WSAGetLastError();
            return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
            return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR;
#endif
        }

        int connectProgress(SocketHandle handle)
        {
#ifdef _WIN32
            fd_set writable;
            fd_set failed;
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            FD_SET(handle, &writable);
            FD_SET(handle, &failed);
            timeval immediate {};
            if (select(0, nullptr, &writable, &failed, &immediate) <= 0) {
                return 0;
            }
#else
            pollfd entry { handle, POLLOUT, 0 };
            if (poll(&entry, 1, 0) <= 0) {
                return 0;
            }
#endif
            int error = 0;
            AddressLength length = sizeof(error);
            if (getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0) {
                return -1;
            }
            return 1;
        }

        bool allowReply(uint32_t host, size_t size)
        {
            if (host == INADDR_BROADCAST) {
                return true;
            }
            double time = now();
            auto [entry, added] = allowances.try_emplace(host, Allowance { ReplyBurst, time });
            Allowance& allowance = entry->second;
            allowance.bytes = std::min(ReplyBurst, allowance.bytes + (time - allowance.updated) * ReplyRate);
            allowance.updated = time;
            if (allowance.bytes < static_cast<double>(size)) {
                return false;
            }
            allowance.bytes -= static_cast<double>(size);
            return true;
        }

        void pruneAllowances()
        {
            double time = now();
            if (time - lastPrune < 1.0) {
                return;
            }
            lastPrune = time;
            std::erase_if(allowances, [&](const auto& entry) {
                return entry.second.bytes + (time - entry.second.updated) * ReplyRate >= ReplyBurst;
            });
        }

        sockaddr_in socketAddress(uint32_t address, int32_t port)
        {
            sockaddr_in target {};
            target.sin_family = AF_INET;
            target.sin_addr.s_addr = htonl(address);
            target.sin_port = htons(static_cast<uint16_t>(port));
            return target;
        }

        bool parseAddress(const std::string& text, int32_t& address)
        {
            in_addr parsed {};
            if (inet_pton(AF_INET, text.c_str(), &parsed) != 1) {
                return false;
            }
            address = static_cast<int32_t>(ntohl(parsed.s_addr));
            return true;
        }

        bool isLocalAddress(uint32_t host)
        {
            static std::unordered_map<uint32_t, bool> known;
            auto found = known.find(host);
            if (found != known.end()) {
                return found->second;
            }
            SocketHandle probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in local = socketAddress(host, 0);
            bool own = bind(probe, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
            closeSocket(probe);
            known[host] = own;
            return own;
        }

        Link* linkOf(UObject* actor)
        {
            for (Link& link : links) {
                if (link.actor == actor) {
                    return &link;
                }
            }
            return nullptr;
        }

        void nativeBindPort(UObject* self, Frame& stack, void* result)
        {
            int32_t port = vm::getOpt<int32_t>(stack, 0);
            bool useNext = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            vm::ret<int32_t>(result, 0);
            if (linkOf(self)) {
                return;
            }
            startSockets();
            SocketHandle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            int enabled = 1;
            setsockopt(handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
            makeNonBlocking(handle);
            sockaddr_in local = socketAddress(INADDR_ANY, port);
            bool bound = bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
            for (int attempt = 1; !bound && useNext && attempt < 100; attempt++) {
                local = socketAddress(INADDR_ANY, port + attempt);
                bound = bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
            }
            AddressLength length = sizeof(local);
            if (!bound || getsockname(handle, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
                closeSocket(handle);
                return;
            }
            links.push_back({ self, handle });
            int32_t boundPort = ntohs(local.sin_port);
            *reinterpret_cast<int32_t*>(self->propertyAddress(Name("Port"))) = boundPort;
            vm::ret<int32_t>(result, boundPort);
        }

        void nativeSendText(UObject* self, Frame& stack, void* result)
        {
            IpAddr address = vm::get<IpAddr>(stack);
            std::string text = vm::get<std::string>(stack);
            vm::finish(stack);
            Link* link = linkOf(self);
            if (!link) {
                vm::retBool(result, false);
                return;
            }
            uint32_t host = static_cast<uint32_t>(address.addr);
            if (!allowReply(host, text.size())) {
                vm::retBool(result, false);
                return;
            }
            sockaddr_in target = socketAddress(host, address.port);
            auto sent = sendto(link->socket, text.data(), static_cast<DataLength>(text.size()), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
            if (host == INADDR_BROADCAST) {
                target = socketAddress(INADDR_LOOPBACK, address.port);
                sent = std::max(sent, sendto(link->socket, text.data(), static_cast<DataLength>(text.size()), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target)));
            }
            vm::retBool(result, sent >= 0);
        }

        void nativeResolve(UObject* self, Frame& stack, void*)
        {
            std::string domain = vm::get<std::string>(stack);
            vm::finish(stack);
            Resolution resolution;
            resolution.actor = self;
            resolution.found = parseAddress(domain, resolution.address.addr);
            if (!resolution.found) {
                addrinfo hints {};
                hints.ai_family = AF_INET;
                addrinfo* found = nullptr;
                if (getaddrinfo(domain.c_str(), nullptr, &hints, &found) == 0 && found) {
                    resolution.address.addr = static_cast<int32_t>(ntohl(reinterpret_cast<const sockaddr_in*>(found->ai_addr)->sin_addr.s_addr));
                    resolution.found = true;
                    freeaddrinfo(found);
                }
            }
            resolutions.push_back(resolution);
        }

        void nativeIpAddrToString(UObject*, Frame& stack, void* result)
        {
            IpAddr address = vm::get<IpAddr>(stack);
            vm::finish(stack);
            uint32_t host = static_cast<uint32_t>(address.addr);
            vm::ret<std::string>(result, std::to_string(host >> 24) + "." + std::to_string((host >> 16) & 255) + "." + std::to_string((host >> 8) & 255) + "." + std::to_string(host & 255) + ":" + std::to_string(address.port));
        }

        void nativeStringToIpAddr(UObject*, Frame& stack, void* result)
        {
            std::string text = vm::get<std::string>(stack);
            IpAddr* address = vm::getRef<IpAddr>(stack);
            vm::finish(stack);
            vm::retBool(result, parseAddress(text, address->addr));
        }

        void nativeGetLocalIP(UObject*, Frame& stack, void*)
        {
            IpAddr* address = vm::getRef<IpAddr>(stack);
            vm::finish(stack);
            startSockets();
            address->addr = static_cast<int32_t>(INADDR_LOOPBACK);
            address->port = 0;
            SocketHandle probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in remote = socketAddress(0xC0000201u, 9);
            sockaddr_in local {};
            AddressLength length = sizeof(local);
            if (connect(probe, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0 && getsockname(probe, reinterpret_cast<sockaddr*>(&local), &length) == 0 && local.sin_addr.s_addr != 0) {
                address->addr = static_cast<int32_t>(ntohl(local.sin_addr.s_addr));
            }
            closeSocket(probe);
        }

        TcpLink* tcpOf(UObject* actor)
        {
            for (const std::unique_ptr<TcpLink>& link : tcpLinks) {
                if (link->actor == actor) {
                    return link.get();
                }
            }
            return nullptr;
        }

        TcpLink& tcpFor(UObject* actor)
        {
            TcpLink* link = tcpOf(actor);
            if (link) {
                return *link;
            }
            tcpLinks.push_back(std::make_unique<TcpLink>());
            tcpLinks.back()->actor = actor;
            return *tcpLinks.back();
        }

        uint8_t& linkState(UObject* actor)
        {
            return *actor->propertyAddress(Name("LinkState"));
        }

        void dropSocket(TcpLink& link)
        {
            if (link.open) {
                closeSocket(link.socket);
            }
            link.open = false;
            link.outgoing.clear();
            link.partialLine.clear();
        }

        void finishClose(TcpLink& link)
        {
            dropSocket(link);
            linkState(link.actor) = STATE_Initialized;
            vm::Call(link.actor, Name("Closed")).run();
        }

        bool bindTcp(TcpLink& link, int32_t port, bool useNext)
        {
            startSockets();
            SocketHandle handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifndef _WIN32
            int enabled = 1;
            setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
            makeNonBlocking(handle);
            sockaddr_in local = socketAddress(INADDR_ANY, port);
            bool bound = bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
            for (int attempt = 1; !bound && useNext && attempt < 100; attempt++) {
                local = socketAddress(INADDR_ANY, port + attempt);
                bound = bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
            }
            AddressLength length = sizeof(local);
            if (!bound || getsockname(handle, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
                closeSocket(handle);
                return false;
            }
            link.socket = handle;
            link.open = true;
            link.port = ntohs(local.sin_port);
            *reinterpret_cast<int32_t*>(link.actor->propertyAddress(Name("Port"))) = link.port;
            linkState(link.actor) = STATE_Ready;
            return true;
        }

        void nativeTcpBindPort(UObject* self, Frame& stack, void* result)
        {
            int32_t port = vm::getOpt<int32_t>(stack, 0);
            bool useNext = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            TcpLink& link = tcpFor(self);
            if (link.open) {
                vm::ret<int32_t>(result, 0);
                return;
            }
            vm::ret<int32_t>(result, bindTcp(link, port, useNext) ? link.port : 0);
        }

        void nativeListen(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            TcpLink* link = tcpOf(self);
            if (!link || link->accepted || (!link->open && !bindTcp(*link, link->port, false))) {
                vm::retBool(result, false);
                return;
            }
            if (linkState(self) == STATE_Listening) {
                vm::retBool(result, true);
                return;
            }
            if (linkState(self) != STATE_Ready || listen(link->socket, 16) != 0) {
                vm::retBool(result, false);
                return;
            }
            linkState(self) = STATE_Listening;
            vm::retBool(result, true);
        }

        void nativeOpen(UObject* self, Frame& stack, void* result)
        {
            IpAddr address = vm::get<IpAddr>(stack);
            vm::finish(stack);
            TcpLink& link = tcpFor(self);
            if (!link.open && !bindTcp(link, 0, false)) {
                vm::retBool(result, false);
                return;
            }
            if (linkState(self) != STATE_Ready) {
                vm::retBool(result, false);
                return;
            }
            sockaddr_in target = socketAddress(static_cast<uint32_t>(address.addr), address.port);
            if (connect(link.socket, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0 && !wouldBlock()) {
                dropSocket(link);
                linkState(self) = STATE_Initialized;
                vm::retBool(result, false);
                return;
            }
            std::memcpy(self->propertyAddress(Name("RemoteAddr")), &address, sizeof(address));
            linkState(self) = STATE_Connecting;
            vm::retBool(result, true);
        }

        void nativeClose(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            TcpLink* link = tcpOf(self);
            if (!link || !link->open) {
                vm::retBool(result, false);
                return;
            }
            uint8_t& state = linkState(self);
            if (state == STATE_Listening) {
                dropSocket(*link);
                state = STATE_Ready;
            } else if (state == STATE_Connected) {
                state = STATE_ConnectClosePending;
                link->closeBy = now() + CloseTimeout;
            } else if (state == STATE_Connecting) {
                finishClose(*link);
            }
            vm::retBool(result, true);
        }

        void nativeIsConnected(UObject* self, Frame& stack, void* result)
        {
            vm::finish(stack);
            vm::retBool(result, linkState(self) == STATE_Connected);
        }

        int32_t queueSend(UObject* self, std::string data)
        {
            TcpLink* link = tcpOf(self);
            if (!link || !link->open || linkState(self) != STATE_Connected) {
                return 0;
            }
            link->outgoing += data;
            return static_cast<int32_t>(data.size());
        }

        void nativeTcpSendText(UObject* self, Frame& stack, void* result)
        {
            std::string text = vm::get<std::string>(stack);
            vm::finish(stack);
            if (*self->propertyAddress(Name("LinkMode")) == 1) {
                text += "\r\n";
            }
            vm::ret<int32_t>(result, queueSend(self, text));
        }

        struct ByteBlock {
            uint8_t bytes[255];
        };

        void nativeTcpSendBinary(UObject* self, Frame& stack, void* result)
        {
            int32_t count = vm::get<int32_t>(stack);
            ByteBlock* block = vm::getRef<ByteBlock>(stack);
            vm::finish(stack);
            count = std::clamp(count, 0, 255);
            vm::ret<int32_t>(result, queueSend(self, std::string(reinterpret_cast<const char*>(block->bytes), static_cast<size_t>(count))));
        }

        void flush(TcpLink& link)
        {
            while (!link.outgoing.empty()) {
#ifdef MSG_NOSIGNAL
                int flags = MSG_NOSIGNAL;
#else
                int flags = 0;
#endif
                auto sent = send(link.socket, link.outgoing.data(), static_cast<DataLength>(link.outgoing.size()), flags);
                if (sent <= 0) {
                    if (sent < 0 && !wouldBlock()) {
                        link.outgoing.clear();
                    }
                    return;
                }
                link.outgoing.erase(0, static_cast<size_t>(sent));
            }
        }

        void deliver(TcpLink& link, const char* data, size_t size)
        {
            uint8_t mode = *link.actor->propertyAddress(Name("LinkMode"));
            if (mode == 2) {
                for (size_t at = 0; at < size && link.open && !link.actor->destroyed; at += 255) {
                    vm::Call call(link.actor, Name("ReceivedBinary"));
                    if (!call.valid()) {
                        return;
                    }
                    int32_t count = static_cast<int32_t>(std::min<size_t>(255, size - at));
                    *reinterpret_cast<int32_t*>(call.parameter(0)) = count;
                    std::memcpy(call.parameter(1), data + at, static_cast<size_t>(count));
                    call.run();
                }
                return;
            }
            if (mode == 1) {
                link.partialLine.append(data, size);
                size_t end = link.partialLine.find('\n');
                while (end != std::string::npos && link.open && !link.actor->destroyed) {
                    std::string line = link.partialLine.substr(0, end);
                    link.partialLine.erase(0, end + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    vm::Call(link.actor, Name("ReceivedLine")).s(line).run();
                    end = link.partialLine.find('\n');
                }
                return;
            }
            vm::Call(link.actor, Name("ReceivedText")).s(std::string(data, size)).run();
        }

        void acceptConnections(TcpLink& listener)
        {
            UClass* acceptClass = *reinterpret_cast<UClass**>(listener.actor->propertyAddress(Name("AcceptClass")));
            for (int count = 0; count < AcceptsPerTick && listener.open && linkState(listener.actor) == STATE_Listening && !listener.actor->destroyed; count++) {
                sockaddr_in source {};
                AddressLength length = sizeof(source);
                SocketHandle handle = accept(listener.socket, reinterpret_cast<sockaddr*>(&source), &length);
#ifdef _WIN32
                if (handle == INVALID_SOCKET) {
                    return;
                }
#else
                if (handle < 0) {
                    return;
                }
#endif
                makeNonBlocking(handle);
                IpAddr remote { static_cast<int32_t>(ntohl(source.sin_addr.s_addr)), ntohs(source.sin_port) };
                UObject* child = listener.actor;
                if (acceptClass) {
                    World* world = World::current;
                    child = world ? world->spawn(acceptClass, listener.actor, Name(), actor::Location(listener.actor), {}, nullptr, true) : nullptr;
                    if (!child || child->destroyed || !child->isA(Name("TcpLink"))) {
                        closeSocket(handle);
                        continue;
                    }
                }
                TcpLink& link = tcpFor(child);
                dropSocket(link);
                link.socket = handle;
                link.open = true;
                link.accepted = true;
                link.port = listener.port;
                link.received = 0;
                *reinterpret_cast<int32_t*>(child->propertyAddress(Name("Port"))) = listener.port;
                std::memcpy(child->propertyAddress(Name("RemoteAddr")), &remote, sizeof(remote));
                linkState(child) = STATE_Connected;
                vm::Call(child, Name("Accepted")).run();
            }
        }

        void pollTcp(TcpLink& link)
        {
            uint8_t state = linkState(link.actor);
            if (state == STATE_Listening) {
                acceptConnections(link);
                return;
            }
            if (state == STATE_Connecting) {
                int progress = connectProgress(link.socket);
                if (progress > 0) {
                    linkState(link.actor) = STATE_Connected;
                    vm::Call(link.actor, Name("Opened")).run();
                } else if (progress < 0) {
                    finishClose(link);
                }
                return;
            }
            if (state != STATE_Connected && state != STATE_ConnectClosePending) {
                return;
            }
            flush(link);
            if (state == STATE_ConnectClosePending) {
                if (link.outgoing.empty() || now() > link.closeBy) {
                    finishClose(link);
                }
                return;
            }
            char buffer[4096];
            size_t taken = 0;
            while (taken < TcpReadPerTick && link.open && !link.actor->destroyed && linkState(link.actor) == STATE_Connected) {
                auto count = recv(link.socket, buffer, sizeof(buffer), 0);
                if (count < 0 && wouldBlock()) {
                    return;
                }
                if (count <= 0) {
                    finishClose(link);
                    return;
                }
                taken += static_cast<size_t>(count);
                link.received += static_cast<size_t>(count);
                if (link.accepted && link.received > AcceptedReadLimit) {
                    finishClose(link);
                    return;
                }
                deliver(link, buffer, static_cast<size_t>(count));
            }
        }

        void receive(const Link& link)
        {
            static const Name names[] = { Name("ReceivedText"), Name("ReceivedLine"), Name("ReceivedBinary") };
            for (int packet = 0; packet < 32 && !link.actor->destroyed; packet++) {
                char buffer[4096];
                sockaddr_in source {};
                AddressLength length = sizeof(source);
                auto count = recvfrom(link.socket, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&source), &length);
                if (count <= 0) {
                    return;
                }
                uint8_t mode = std::min<uint8_t>(*link.actor->propertyAddress(Name("LinkMode")), 2);
                vm::Call call(link.actor, names[mode]);
                if (!call.valid()) {
                    continue;
                }
                uint32_t host = ntohl(source.sin_addr.s_addr);
                if (isLocalAddress(host)) {
                    host = INADDR_LOOPBACK;
                }
                IpAddr address { static_cast<int32_t>(host), ntohs(source.sin_port) };
                std::memcpy(call.parameter(0), &address, sizeof(address));
                if (mode == 2) {
                    int32_t size = std::min<int32_t>(static_cast<int32_t>(count), 255);
                    *reinterpret_cast<int32_t*>(call.parameter(1)) = size;
                    std::memcpy(call.parameter(2), buffer, static_cast<size_t>(size));
                } else {
                    *reinterpret_cast<std::string*>(call.parameter(1)) = std::string(buffer, static_cast<size_t>(count));
                }
                call.run();
            }
        }
    }

    void pollLinks()
    {
        std::erase_if(links, [](const Link& link) {
            if (!link.actor->destroyed) {
                return false;
            }
            closeSocket(link.socket);
            return true;
        });
        std::erase_if(tcpLinks, [](const std::unique_ptr<TcpLink>& link) {
            if (!link->actor->destroyed) {
                return false;
            }
            dropSocket(*link);
            return true;
        });
        pruneAllowances();
        std::vector<Resolution> resolved;
        resolved.swap(resolutions);
        for (const Resolution& resolution : resolved) {
            if (resolution.actor->destroyed) {
                continue;
            }
            vm::Call call(resolution.actor, Name(resolution.found ? "Resolved" : "ResolveFailed"));
            if (call.valid() && resolution.found) {
                std::memcpy(call.parameter(0), &resolution.address, sizeof(resolution.address));
            }
            call.run();
        }
        for (const Link& link : std::vector<Link>(links)) {
            receive(link);
        }
        std::vector<UObject*> actors;
        for (const std::unique_ptr<TcpLink>& link : tcpLinks) {
            actors.push_back(link->actor);
        }
        for (UObject* actor : actors) {
            TcpLink* link = tcpOf(actor);
            if (link && link->open && !actor->destroyed) {
                pollTcp(*link);
            }
        }
    }

    void closeLinks()
    {
        for (const Link& link : links) {
            closeSocket(link.socket);
        }
        links.clear();
        resolutions.clear();
        for (const std::unique_ptr<TcpLink>& link : tcpLinks) {
            dropSocket(*link);
        }
        tcpLinks.clear();
        allowances.clear();
    }

    void registerIpDrvNatives()
    {
        vm::registerNative("UdpLink", "BindPort", nativeBindPort);
        vm::registerNative("UdpLink", "SendText", nativeSendText);
        vm::registerNative("InternetLink", "Resolve", nativeResolve);
        vm::registerNative("InternetLink", "IpAddrToString", nativeIpAddrToString);
        vm::registerNative("InternetLink", "StringToIpAddr", nativeStringToIpAddr);
        vm::registerNative("InternetLink", "GetLocalIP", nativeGetLocalIP);
        vm::registerNative("TcpLink", "BindPort", nativeTcpBindPort);
        vm::registerNative("TcpLink", "Listen", nativeListen);
        vm::registerNative("TcpLink", "Open", nativeOpen);
        vm::registerNative("TcpLink", "Close", nativeClose);
        vm::registerNative("TcpLink", "IsConnected", nativeIsConnected);
        vm::registerNative("TcpLink", "SendText", nativeTcpSendText);
        vm::registerNative("TcpLink", "SendBinary", nativeTcpSendBinary);
    }
}

#else

namespace ut {
    void pollLinks()
    {
    }

    void closeLinks()
    {
    }

    void registerIpDrvNatives()
    {
    }
}

#endif
