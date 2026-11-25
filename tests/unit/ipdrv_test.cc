#include "compiler/script_compiler.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/net.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#ifndef _WIN32

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
    const char* const Login = "Authorization: Basic dGVzdGVyOnNlY3JldA==\r\n";

    ut::UObject* firstActor(ut::World& world, const char* className)
    {
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && a->isA(ut::Name(className))) {
                return a;
            }
        }
        return nullptr;
    }

    void setDefault(const char* className, const char* property, const std::string& value)
    {
        ut::UClass* cls = ut::objects::findClass(className);
        REQUIRE(cls != nullptr);
        ut::objects::ensureLoaded(cls);
        ut::UProperty* found = cls->findProperty(ut::Name(property));
        REQUIRE(found != nullptr);
        REQUIRE(found->fromText(cls->defaultObject->data + found->offset, value));
    }

    int32_t intProperty(ut::UObject* object, const char* name)
    {
        return *reinterpret_cast<int32_t*>(object->propertyAddress(ut::Name(name)));
    }

    sockaddr_in loopback(int port)
    {
        sockaddr_in target {};
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        target.sin_port = htons(static_cast<uint16_t>(port));
        return target;
    }

    int connectTo(int port)
    {
        int handle = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in target = loopback(port);
        if (connect(handle, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0) {
            close(handle);
            return -1;
        }
        return handle;
    }

    bool readUntilClosed(ut::World& world, int handle, std::string& reply)
    {
        for (int attempt = 0; attempt < 600; attempt++) {
            world.tick(0.02f);
            char buffer[8192];
            ssize_t count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
            while (count > 0) {
                reply.append(buffer, static_cast<size_t>(count));
                count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
            }
            if (count == 0) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

    std::string exchange(ut::World& world, int port, const std::string& request)
    {
        int handle = connectTo(port);
        REQUIRE(handle >= 0);
        REQUIRE(send(handle, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));
        std::string reply;
        CHECK(readUntilClosed(world, handle, reply));
        close(handle);
        return reply;
    }

    std::string get(ut::World& world, int port, const std::string& path, const std::string& headers)
    {
        return exchange(world, port, "GET " + path + " HTTP/1.0\r\n" + headers + "\r\n");
    }

    std::string bodyOf(const std::string& reply)
    {
        size_t split = reply.find("\r\n\r\n");
        return split == std::string::npos ? std::string() : reply.substr(split + 4);
    }

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    int freePort()
    {
        int handle = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in local = loopback(0);
        REQUIRE(bind(handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
        socklen_t length = sizeof(local);
        REQUIRE(getsockname(handle, reinterpret_cast<sockaddr*>(&local), &length) == 0);
        close(handle);
        return ntohs(local.sin_port);
    }

    struct WebHost {
        explicit WebHost(int maxConnections = 30)
        {
            ut::engine::initialize(OPENUT_GAME_DATA);
            ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
            setDefault("UWeb.WebServer", "bEnabled", "True");
            int wanted = freePort();
            setDefault("UWeb.WebServer", "ListenPort", std::to_string(wanted));
            setDefault("UWeb.WebServer", "MaxConnections", std::to_string(maxConnections));
            setDefault("UTServerAdmin.UTServerAdmin", "AdminUsername", "tester");
            setDefault("UTServerAdmin.UTServerAdmin", "AdminPassword", "secret");
            world.netMode = ut::NM_DedicatedServer;
            world.netPort = 7777;
            REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
            server = firstActor(world, "WebServer");
            REQUIRE(server != nullptr);
            port = intProperty(server, "Port");
            REQUIRE(port == wanted);
        }

        ~WebHost()
        {
            setDefault("UWeb.WebServer", "bEnabled", "False");
            setDefault("UWeb.WebServer", "ListenPort", "80");
            setDefault("UWeb.WebServer", "MaxConnections", "30");
        }

        ut::World world;
        ut::UObject* server = nullptr;
        int port = 0;
    };
}

TEST_CASE("the web admin asks for a login and then serves its pages, forms and images", "[data]")
{
    WebHost host;
    ut::World& world = host.world;
    int port = host.port;

    std::string root = get(world, port, "/", "");
    CHECK(root.rfind("HTTP/1.1 302 Document Moved\r\n", 0) == 0);
    std::string location = ":" + std::to_string(port) + "/ServerAdmin/\r\n";
    CHECK(root.find("Location: http://") != std::string::npos);
    CHECK(root.find(location) != std::string::npos);
    CHECK(root.find("Location: http://0.0.0.0") == std::string::npos);

    std::string refused = get(world, port, "/ServerAdmin/", "");
    CHECK(refused.rfind("HTTP/1.1 401 Unauthorized\r\n", 0) == 0);
    CHECK(refused.find("WWW-authenticate: basic realm=") != std::string::npos);
    std::string wrong = get(world, port, "/ServerAdmin/", "Authorization: Basic dGVzdGVyOndyb25n\r\n");
    CHECK(wrong.rfind("HTTP/1.1 401 Unauthorized\r\n", 0) == 0);

    std::string frames = get(world, port, "/ServerAdmin/", Login);
    CHECK(frames.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(frames.find("Content-Type: text/html\r\n") != std::string::npos);
    CHECK(frames.find("<frameset") != std::string::npos);

    std::string game = bodyOf(get(world, port, "/ServerAdmin/current_game", Login));
    CHECK(game.find("<option value=\"DM-Deck16][.unr\" selected>DM-Deck16][</option>") != std::string::npos);
    CHECK(game.find("<option value=\"BotPack.DeathMatchPlus\" selected>") != std::string::npos);
    CHECK(game.find("width=\"100%\"") != std::string::npos);
    CHECK(game.find("%MapSelect%") == std::string::npos);
    CHECK(game.find("%%") == std::string::npos);

    std::string form = "SendText=say+web+hello%21&Send=Send";
    std::string post = "POST /ServerAdmin/current_console HTTP/1.0\r\n" + std::string(Login) + "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " + std::to_string(form.size()) + "\r\n\r\n" + form;
    CHECK(exchange(world, port, post).rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    std::string log = bodyOf(get(world, port, "/ServerAdmin/current_console_log", Login));
    CHECK(log.find("Admin: web hello!") != std::string::npos);

    std::string image = get(world, port, "/images/logo.gif", Login);
    CHECK(image.find("Content-Type: image/gif\r\n") != std::string::npos);
    std::string logo = readFile(std::filesystem::path(OPENUT_GAME_DATA) / "Web" / "images" / "logo.gif");
    REQUIRE(logo.size() > 1000);
    CHECK(bodyOf(image) == logo);

    CHECK(bodyOf(get(world, port, "/images/../../Web/images/logo.gif", Login)).empty());
    CHECK(bodyOf(get(world, port, "/images/nothere.gif", Login)).empty());
}

TEST_CASE("the web server drops a client that sends a megabyte without ending its request", "[data]")
{
    WebHost host;
    ut::World& world = host.world;
    std::signal(SIGPIPE, SIG_IGN);
    int handle = connectTo(host.port);
    REQUIRE(handle >= 0);
    std::string flood(64 * 1024, 'a');
    size_t sent = 0;
    bool closed = false;
    for (int attempt = 0; attempt < 2000 && !closed; attempt++) {
        ssize_t count = send(handle, flood.data(), flood.size(), MSG_DONTWAIT);
        if (count > 0) {
            sent += static_cast<size_t>(count);
        }
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            closed = true;
        }
        world.tick(0.02f);
        char buffer[256];
        if (recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT) == 0) {
            closed = true;
        }
    }
    close(handle);
    CHECK(closed);
    CHECK(sent > 1024 * 1024);
    CHECK(sent < 8 * 1024 * 1024);
    CHECK(get(world, host.port, "/ServerAdmin/", Login).rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
}

TEST_CASE("the web server stops listening at its connection limit and listens again when a client leaves", "[data]")
{
    WebHost host(1);
    ut::World& world = host.world;
    int first = connectTo(host.port);
    int second = connectTo(host.port);
    REQUIRE(first >= 0);
    REQUIRE(second >= 0);
    for (int i = 0; i < 10; i++) {
        world.tick(0.02f);
    }
    CHECK(intProperty(host.server, "ConnectionCount") == 2);
    CHECK(connectTo(host.port) == -1);
    close(first);
    close(second);
    for (int i = 0; i < 10; i++) {
        world.tick(0.02f);
    }
    CHECK(intProperty(host.server, "ConnectionCount") == 0);
    CHECK(get(world, host.port, "/ServerAdmin/", Login).rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
}

namespace {
    const std::string tcpProbeSource = "class TcpProbe extends TcpLink;\r\n"
                                       "\r\n"
                                       "var string Text;\r\n"
                                       "var string Lines;\r\n"
                                       "var int Bytes;\r\n"
                                       "var int LastByte;\r\n"
                                       "var bool bOpened;\r\n"
                                       "var bool bClosed;\r\n"
                                       "\r\n"
                                       "function Go(int TargetPort)\r\n{\r\n    local IpAddr A;\r\n    StringToIpAddr(\"127.0.0.1\", A);\r\n    A.Port = TargetPort;\r\n    Open(A);\r\n}\r\n"
                                       "\r\n"
                                       "event Opened()\r\n{\r\n    bOpened = true;\r\n    SendText(\"hello\");\r\n}\r\n"
                                       "\r\n"
                                       "event ReceivedText(string T)\r\n{\r\n    Text = Text $ T;\r\n}\r\n"
                                       "\r\n"
                                       "event ReceivedLine(string L)\r\n{\r\n    Lines = Lines $ \"[\" $ L $ \"]\";\r\n}\r\n"
                                       "\r\n"
                                       "event ReceivedBinary(int Count, byte B[255])\r\n{\r\n    Bytes += Count;\r\n    LastByte = B[Count - 1];\r\n}\r\n"
                                       "\r\n"
                                       "event Closed()\r\n{\r\n    bClosed = true;\r\n}\r\n";

    std::string takeAll(ut::World& world, int handle, size_t wanted)
    {
        std::string data;
        for (int attempt = 0; attempt < 200 && data.size() < wanted; attempt++) {
            world.tick(0.02f);
            char buffer[512];
            ssize_t count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (count > 0) {
                data.append(buffer, static_cast<size_t>(count));
            }
        }
        return data;
    }

    void settle(ut::World& world)
    {
        for (int i = 0; i < 5; i++) {
            world.tick(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

TEST_CASE("a script TCP link opens a connection, reads text, lines and bytes and hears the close", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    ut::World world;
    world.netMode = ut::NM_DedicatedServer;
    world.netPort = 7777;
    REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
    std::string log;
    ut::compiler::PackageCompiler compiler(world.level->outermost(), [&](const std::string& line) {
        log += line + "\n";
    });
    ut::UClass* probeClass = compiler.importClass("TcpProbe.uc", tcpProbeSource);
    REQUIRE(probeClass != nullptr);
    bool compiled = compiler.compile();
    INFO(log);
    REQUIRE(compiled);
    ut::UObject* probe = world.spawn(probeClass, nullptr, ut::Name(), {}, {}, nullptr, true);
    REQUIRE(probe != nullptr);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in local = loopback(0);
    REQUIRE(bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0);
    socklen_t length = sizeof(local);
    REQUIRE(getsockname(listener, reinterpret_cast<sockaddr*>(&local), &length) == 0);
    REQUIRE(listen(listener, 1) == 0);
    fcntl(listener, F_SETFL, fcntl(listener, F_GETFL, 0) | O_NONBLOCK);
    ut::vm::Call(probe, ut::Name("Go")).i(ntohs(local.sin_port)).run();
    CHECK(*probe->propertyAddress(ut::Name("LinkState")) == 3);
    int peer = -1;
    for (int attempt = 0; attempt < 200 && peer < 0; attempt++) {
        world.tick(0.02f);
        peer = accept(listener, nullptr, nullptr);
    }
    REQUIRE(peer >= 0);
    close(listener);
    CHECK(takeAll(world, peer, 5) == "hello");
    CHECK(*reinterpret_cast<uint32_t*>(probe->propertyAddress(ut::Name("bOpened"))) != 0);
    CHECK(*probe->propertyAddress(ut::Name("LinkState")) == 4);

    std::string text = "abc\r\ndef";
    send(peer, text.data(), text.size(), 0);
    settle(world);
    CHECK(*reinterpret_cast<std::string*>(probe->propertyAddress(ut::Name("Text"))) == text);

    *probe->propertyAddress(ut::Name("LinkMode")) = 1;
    std::string lines = "one\r\ntwo\npart";
    send(peer, lines.data(), lines.size(), 0);
    settle(world);
    CHECK(*reinterpret_cast<std::string*>(probe->propertyAddress(ut::Name("Lines"))) == "[one][two]");
    send(peer, "ial\n", 4, 0);
    settle(world);
    CHECK(*reinterpret_cast<std::string*>(probe->propertyAddress(ut::Name("Lines"))) == "[one][two][partial]");
    ut::vm::Call(probe, ut::Name("SendText")).s("ready").run();
    CHECK(takeAll(world, peer, 7) == "ready\r\n");

    *probe->propertyAddress(ut::Name("LinkMode")) = 2;
    std::string bytes(600, '\x07');
    bytes.back() = '\x2A';
    send(peer, bytes.data(), bytes.size(), 0);
    settle(world);
    CHECK(intProperty(probe, "Bytes") == 600);
    CHECK(intProperty(probe, "LastByte") == 42);

    close(peer);
    settle(world);
    CHECK(*reinterpret_cast<uint32_t*>(probe->propertyAddress(ut::Name("bClosed"))) != 0);
    CHECK(*probe->propertyAddress(ut::Name("LinkState")) == 0);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("the query port stops answering an address that asks for more than its reply allowance", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        world.netPort = 7777;
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        ut::UObject* query = firstActor(world, "UdpServerQuery");
        REQUIRE(query != nullptr);
        int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in target = loopback(intProperty(query, "Port"));
        auto started = std::chrono::steady_clock::now();
        size_t replies = 0;
        size_t replyBytes = 0;
        size_t largest = 0;
        const size_t queries = 400;
        for (size_t i = 0; i < queries; i++) {
            std::string request = "\\status\\";
            sendto(handle, request.data(), request.size(), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
            if (i % 20 == 19) {
                world.tick(0.02f);
            }
            char buffer[4096];
            ssize_t count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
            while (count > 0) {
                replies++;
                replyBytes += static_cast<size_t>(count);
                largest = std::max(largest, static_cast<size_t>(count));
                count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
            }
        }
        world.tick(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        char buffer[4096];
        ssize_t count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
        while (count > 0) {
            replies++;
            replyBytes += static_cast<size_t>(count);
            count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
        }
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        close(handle);
        REQUIRE(largest > 0);
        CHECK(replyBytes > 8000);
        CHECK(static_cast<double>(replyBytes) <= 16384.0 + 4096.0 * elapsed + static_cast<double>(largest));
        CHECK(replies < queries);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

#endif
