#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/net.h"
#include "engine/world.h"
#include "game_process.h"

#include <catch_amalgamated.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
    struct SentMessage {
        uint32_t connection = 0;
        std::vector<uint8_t> data;
    };

    class RecordingTransport : public ut::net::Transport {
    public:
        void send(uint32_t connection, const std::vector<uint8_t>& data) override
        {
            sent.push_back({ connection, data });
        }

        void close(uint32_t connection) override
        {
            closed.push_back(connection);
        }

        std::vector<ut::net::Event> poll() override
        {
            std::vector<ut::net::Event> result;
            result.swap(pending);
            return result;
        }

        std::string address(uint32_t) override
        {
            return "10.1.2.3:4567";
        }

        int ping(uint32_t) override
        {
            return 42;
        }

        int loss(uint32_t) override
        {
            return 7;
        }

        void connect(uint32_t connection)
        {
            ut::net::Event event;
            event.kind = ut::net::Event::Kind::Connected;
            event.connection = connection;
            pending.push_back(event);
        }

        void disconnect(uint32_t connection)
        {
            ut::net::Event event;
            event.kind = ut::net::Event::Kind::Disconnected;
            event.connection = connection;
            pending.push_back(event);
        }

        void receive(uint32_t connection, std::vector<uint8_t> data)
        {
            ut::net::Event event;
            event.kind = ut::net::Event::Kind::Message;
            event.connection = connection;
            event.data = std::move(data);
            pending.push_back(event);
        }

        std::vector<SentMessage> sent;
        std::vector<uint32_t> closed;
        std::vector<ut::net::Event> pending;
    };

    std::vector<uint8_t> joinMessage(const std::string& options)
    {
        std::vector<uint8_t> message { 3, static_cast<uint8_t>(options.size()) };
        message.insert(message.end(), options.begin(), options.end());
        return message;
    }

    struct Peer {
        explicit Peer(uint32_t id)
            : connection(id)
        {
        }

        void say(RecordingTransport& transport, std::vector<uint8_t> message)
        {
            REQUIRE(endpoint.queueReliable(std::move(message)));
            flush(transport);
        }

        void flush(RecordingTransport& transport)
        {
            time += 0.05;
            endpoint.update(time, 0.05, 1000000);
            for (std::vector<uint8_t>& packet : endpoint.flush()) {
                transport.receive(connection, std::move(packet));
            }
        }

        std::vector<ut::net::Endpoint::Received> collect(RecordingTransport& transport)
        {
            std::vector<ut::net::Endpoint::Received> packets;
            for (; consumed < transport.sent.size(); consumed++) {
                const SentMessage& sent = transport.sent[consumed];
                if (sent.connection != connection) {
                    continue;
                }
                largest = std::max(largest, sent.data.size());
                ut::net::Endpoint::Received received;
                REQUIRE(endpoint.receive(sent.data.data(), sent.data.size(), received));
                messages.insert(messages.end(), received.messages.begin(), received.messages.end());
                packets.push_back(std::move(received));
            }
            return packets;
        }

        void acceptAll(const std::vector<ut::net::Endpoint::Received>& packets)
        {
            for (const ut::net::Endpoint::Received& packet : packets) {
                if (packet.fresh) {
                    endpoint.accept(packet.sequence);
                    items.insert(items.end(), packet.items.begin(), packet.items.end());
                }
            }
        }

        uint32_t connection;
        ut::net::Endpoint endpoint;
        double time = 0;
        size_t consumed = 0;
        size_t largest = 0;
        std::vector<std::vector<uint8_t>> messages;
        std::vector<std::vector<uint8_t>> items;
    };

    std::vector<ut::UObject*> playerPawns(ut::World& world)
    {
        std::vector<ut::UObject*> pawns;
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && ut::isPlayerPawn(a)) {
                pawns.push_back(a);
            }
        }
        return pawns;
    }

    size_t countType(const std::vector<std::vector<uint8_t>>& messages, uint8_t type)
    {
        size_t count = 0;
        for (const std::vector<uint8_t>& message : messages) {
            if (!message.empty() && message[0] == type) {
                count++;
            }
        }
        return count;
    }
}

TEST_CASE("packets use little endian fields and seven bit variable length integers")
{
    ut::net::Packet packet;
    packet.u8(7);
    packet.varint(300);
    packet.i32(-2);
    packet.f32(1.0f);
    packet.str("ab");
    std::vector<uint8_t> expected { 7, 0xAC, 0x02, 0xFE, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x80, 0x3F, 2, 'a', 'b' };
    REQUIRE(packet.data == expected);

    ut::net::PacketReader reader(packet.data.data(), packet.data.size());
    CHECK(reader.u8() == 7);
    CHECK(reader.varint() == 300);
    CHECK(reader.i32() == -2);
    CHECK(reader.f32() == 1.0f);
    CHECK(reader.str() == "ab");
    CHECK(reader.atEnd());
    CHECK_FALSE(reader.failed);
}

TEST_CASE("reading past the end of a packet fails instead of returning stale bytes")
{
    std::vector<uint8_t> truncated { 5, 'a', 'b' };
    ut::net::PacketReader text(truncated.data(), truncated.size());
    CHECK(text.str().empty());
    CHECK(text.failed);

    std::vector<uint8_t> shortInt { 1, 2, 3 };
    ut::net::PacketReader number(shortInt.data(), shortInt.size());
    CHECK(number.i32() == 0);
    CHECK(number.failed);

    std::vector<uint8_t> endless { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
    ut::net::PacketReader varint(endless.data(), endless.size());
    CHECK(varint.varint() == 0);
    CHECK(varint.failed);
}

TEST_CASE("server welcomes a client, logs it in on join and removes its pawn on disconnect", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        ut::objects::unloadPackage(ut::Name("EpicCustomModels"));
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        CHECK(ut::objects::findLinker(ut::Name("EpicCustomModels")) != nullptr);
        CHECK(world.player() == nullptr);
        CHECK(playerPawns(world).empty());
        CHECK(*world.levelInfo->propertyAddress(ut::Name("NetMode")) == 1);

        Peer peer(9);
        transport.connect(9);
        peer.say(transport, { 1, 2 });
        world.tick(0.05f);
        driver.tick(0.05f);
        peer.acceptAll(peer.collect(transport));
        REQUIRE(peer.messages.size() == 1);
        std::vector<uint8_t> welcome { 2, 11, 'D', 'M', '-', 'D', 'e', 'c', 'k', '1', '6', ']', '[' };
        CHECK(peer.messages[0] == welcome);
        CHECK(peer.items.empty());

        peer.say(transport, joinMessage("?Name=Tester"));
        world.tick(0.05f);
        driver.tick(0.05f);
        peer.acceptAll(peer.collect(transport));
        std::vector<ut::UObject*> pawns = playerPawns(world);
        REQUIRE(pawns.size() == 1);
        ut::UObject* pawn = pawns[0];
        CHECK(ut::playerreplicationinfo::PlayerName(ut::pawn::PlayerReplicationInfo(pawn)) == "Tester");
        CHECK(ut::actor::Role(pawn) == 4);
        CHECK(ut::actor::RemoteRole(pawn) == 3);
        CHECK(ut::vm::stateName(pawn) == ut::Name("PlayerWaiting"));
        REQUIRE(ut::playerpawn::Player(pawn) != nullptr);
        CHECK(ut::playerpawn::Player(pawn)->cls->name == ut::Name("NetConnection"));
        CHECK(driver.playerAddress(pawn) == "10.1.2.3:4567");
        CHECK(ut::console::execute(pawn, "GETPING") == "42");
        CHECK(ut::console::execute(pawn, "GETLOSS") == "7");
        CHECK(countType(peer.messages, 6) > 0);
        CHECK(countType(peer.messages, 4) == 0);

        transport.disconnect(9);
        world.tick(0.05f);
        driver.tick(0.05f);
        CHECK(playerPawns(world).empty());
        CHECK(driver.connectionCount() == 0);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("server rejects a client that speaks another protocol version", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        Peer peer(4);
        transport.connect(4);
        peer.say(transport, { 1, 99 });
        peer.say(transport, joinMessage("?Name=Intruder"));
        world.tick(0.05f);
        driver.tick(0.05f);
        peer.collect(transport);
        REQUIRE(peer.messages.size() == 1);
        CHECK(peer.messages[0][0] == 4);
        CHECK(std::string(peer.messages[0].begin() + 2, peer.messages[0].end()) == "Incompatible game version");
        CHECK(playerPawns(world).empty());
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("client level keeps only static actors and runs no game", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_Client;
        ut::net::Driver driver(transport, false);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16]["));
        CHECK(world.gameInfo == nullptr);
        CHECK(world.player() == nullptr);
        CHECK(*world.levelInfo->propertyAddress(ut::Name("NetMode")) == 3);
        size_t movable = 0;
        size_t authoritative = 0;
        for (size_t i = 2; i < world.actors.size(); i++) {
            ut::UObject* a = world.actors[i];
            if (!a || a->destroyed) {
                continue;
            }
            if (!ut::actor::bStatic(a) && !ut::actor::bNoDelete(a)) {
                movable++;
            }
            if (ut::actor::Role(a) == 4 && ut::actor::RemoteRole(a) != 0) {
                authoritative++;
            }
        }
        CHECK(world.actors.size() > 100);
        CHECK(movable == 0);
        CHECK(authoritative == 0);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

namespace {
    std::map<uint32_t, std::string> definitions(const Peer& peer)
    {
        std::map<uint32_t, std::string> paths;
        for (const std::vector<uint8_t>& message : peer.messages) {
            ut::net::PacketReader in(message.data(), message.size());
            if (in.u8() != 5) {
                continue;
            }
            uint32_t id = in.varint();
            in.u8();
            paths[id] = in.str();
        }
        return paths;
    }

    std::vector<std::pair<uint32_t, std::string>> openedChannels(const Peer& peer)
    {
        std::map<uint32_t, std::string> paths = definitions(peer);
        std::vector<std::pair<uint32_t, std::string>> opened;
        for (const std::vector<uint8_t>& message : peer.messages) {
            ut::net::PacketReader in(message.data(), message.size());
            if (in.u8() != 6) {
                continue;
            }
            uint32_t channel = in.varint();
            uint8_t flags = in.u8();
            if ((flags & 1) && !(flags & 2) && in.u8() == 1) {
                opened.emplace_back(channel, paths[in.varint()]);
            }
        }
        return opened;
    }

    uint32_t channelOf(const Peer& peer, const std::string& path)
    {
        for (const auto& [channel, opened] : openedChannels(peer)) {
            if (opened == path) {
                return channel;
            }
        }
        return 0;
    }

    bool updates(const std::vector<uint8_t>& item, uint32_t channel)
    {
        ut::net::PacketReader in(item.data(), item.size());
        return in.u8() == 6 && in.varint() == channel;
    }

    void joinPeer(ut::World& world, ut::net::Driver& driver, RecordingTransport& transport, Peer& peer, const std::string& name)
    {
        transport.connect(peer.connection);
        peer.say(transport, { 1, 2 });
        world.tick(0.05f);
        driver.tick(0.05f);
        peer.acceptAll(peer.collect(transport));
        peer.say(transport, joinMessage("?Name=" + name));
        world.tick(0.05f);
        driver.tick(0.05f);
        peer.acceptAll(peer.collect(transport));
        peer.flush(transport);
    }

    void play(ut::World& world, ut::net::Driver& driver, RecordingTransport& transport, std::vector<Peer*> peers, int ticks)
    {
        for (int i = 0; i < ticks; i++) {
            world.tick(0.05f);
            driver.tick(0.05f);
            for (Peer* peer : peers) {
                peer->acceptAll(peer->collect(transport));
                peer->flush(transport);
            }
        }
    }

    ut::UObject* pawnNamed(ut::World& world, const std::string& name)
    {
        for (ut::UObject* pawn : playerPawns(world)) {
            if (ut::playerreplicationinfo::PlayerName(ut::pawn::PlayerReplicationInfo(pawn)) == name) {
                return pawn;
            }
        }
        return nullptr;
    }
}

TEST_CASE("state from a lost packet is resent even when it no longer changes", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        Peer peer(9);
        joinPeer(world, driver, transport, peer, "Tester");
        play(world, driver, transport, { &peer }, 20);
        ut::UObject* pawn = pawnNamed(world, "Tester");
        REQUIRE(pawn != nullptr);
        ut::UObject* info = world.spawn(ut::objects::findClass("UnrealShare.TeamInfo"), pawn, ut::Name(), ut::actor::Location(pawn), ut::Rotator(), nullptr, true);
        REQUIRE(info != nullptr);
        play(world, driver, transport, { &peer }, 10);
        uint32_t channel = channelOf(peer, "UnrealShare.TeamInfo");
        REQUIRE(channel != 0);

        auto step = [&](bool refuse, size_t& refused) {
            world.tick(0.05f);
            driver.tick(0.05f);
            size_t seen = 0;
            for (const ut::net::Endpoint::Received& packet : peer.collect(transport)) {
                bool carries = false;
                for (const std::vector<uint8_t>& item : packet.items) {
                    carries = carries || updates(item, channel);
                }
                if (carries && refuse) {
                    refused++;
                    continue;
                }
                if (packet.fresh) {
                    peer.endpoint.accept(packet.sequence);
                    seen += carries ? 1 : 0;
                }
            }
            peer.flush(transport);
            return seen;
        };
        size_t refused = 0;
        size_t quiet = 0;
        for (int i = 0; i < 20; i++) {
            quiet += step(false, refused);
        }
        REQUIRE(quiet == 0);

        ut::actor::DrawScale(info) = 3.0f;

        SECTION("a delivered update is sent once")
        {
            size_t first = 0;
            for (int i = 0; i < 6 && first == 0; i++) {
                first += step(false, refused);
            }
            REQUIRE(first == 1);
            size_t later = 0;
            for (int i = 0; i < 40; i++) {
                later += step(false, refused);
            }
            CHECK(later == 0);
        }

        SECTION("a refused update is sent again")
        {
            size_t lostRun = 0;
            for (int i = 0; i < 6 && refused == 0; i++) {
                lostRun += step(true, refused);
            }
            REQUIRE(lostRun == 0);
            REQUIRE(refused == 1);
            size_t later = 0;
            for (int i = 0; i < 40; i++) {
                later += step(false, refused);
            }
            CHECK(later == 1);
        }
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("a full server keeps every client inside its bandwidth budget", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.rate = 2600;
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        const int clients = 32;
        const int bots = 8;
        *reinterpret_cast<int32_t*>(world.gameInfo->propertyAddress(ut::Name("MaxPlayers"))) = clients + bots;
        *reinterpret_cast<int32_t*>(world.gameInfo->propertyAddress(ut::Name("MinPlayers"))) = clients + bots;
        std::vector<std::unique_ptr<Peer>> peers;
        std::vector<Peer*> all;
        for (int i = 0; i < clients; i++) {
            peers.push_back(std::make_unique<Peer>(static_cast<uint32_t>(100 + i)));
            all.push_back(peers.back().get());
            joinPeer(world, driver, transport, *peers.back(), "Client" + std::to_string(i));
        }
        REQUIRE(driver.connectionCount() == clients);
        const int ticks = 600;
        double serverSeconds = 0;
        double slowest = 0;
        for (int i = 0; i < ticks; i++) {
            auto before = std::chrono::steady_clock::now();
            world.tick(0.05f);
            driver.tick(0.05f);
            double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
            serverSeconds += spent;
            slowest = std::max(slowest, spent);
            for (Peer* peer : all) {
                peer->acceptAll(peer->collect(transport));
                peer->flush(transport);
            }
        }
        std::printf("net scale: %d clients and %d bots, %.2f ms per server tick, slowest %.2f ms\n", clients, bots, serverSeconds * 1000.0 / ticks, slowest * 1000.0);
        REQUIRE(driver.connectionCount() == clients);
        double seconds = 0.05 * (ticks + 2 * clients);
        std::map<uint32_t, uint64_t> sentBefore;
        for (const ut::net::ConnectionStats& stats : driver.stats()) {
            CHECK(static_cast<double>(stats.bytesSent) <= driver.rate * seconds + 2.0 * ut::net::MaxPacketSize);
            CHECK(stats.queuedMessages < 64);
            sentBefore[stats.connection] = stats.bytesSent;
        }
        size_t botPawns = 0;
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && ut::isPawn(a) && !ut::isPlayerPawn(a)) {
                botPawns++;
            }
        }
        CHECK(botPawns == bots);
        for (Peer* peer : all) {
            INFO("client " << peer->connection);
            CHECK(peer->largest <= ut::net::MaxPacketSize);
            size_t players = 0;
            for (const auto& [channel, path] : openedChannels(*peer)) {
                if (path.find("ReplicationInfo") != std::string::npos && path.find("GameReplicationInfo") == std::string::npos) {
                    players++;
                }
            }
            CHECK(players == clients + bots);
        }

        driver.rate = 400;
        const int saturated = 300;
        play(world, driver, transport, all, saturated);
        double busiest = 0;
        for (const ut::net::ConnectionStats& stats : driver.stats()) {
            INFO("client " << stats.connection);
            double spent = static_cast<double>(stats.bytesSent - sentBefore[stats.connection]);
            CHECK(spent <= driver.rate * 0.05 * saturated + 2.0 * ut::net::MaxPacketSize);
            busiest = std::max(busiest, spent);
        }
        CHECK(busiest >= driver.rate * 0.05 * saturated * 0.8);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("garbage from one client never disturbs the server or the other clients", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        Peer alice(1);
        Peer mallory(2);
        joinPeer(world, driver, transport, alice, "Alice");
        joinPeer(world, driver, transport, mallory, "Mallory");
        play(world, driver, transport, { &alice, &mallory }, 20);
        std::vector<uint32_t> channels;
        for (const auto& [channel, path] : openedChannels(mallory)) {
            channels.push_back(channel);
        }
        REQUIRE_FALSE(channels.empty());
        std::mt19937 random(1234);
        std::uniform_int_distribution<int> byte(0, 255);
        std::uniform_int_distribution<int> length(0, 48);
        std::uniform_int_distribution<size_t> pick(0, channels.size() - 1);
        for (int i = 0; i < 3000; i++) {
            ut::net::Packet message;
            int kind = i % 3;
            if (kind == 0) {
                message.u8(static_cast<uint8_t>(byte(random) % 10));
            } else {
                message.u8(kind == 1 ? 8 : 6);
                message.varint(channels[pick(random)]);
                message.varint(static_cast<uint32_t>(byte(random) % 80));
            }
            int size = length(random);
            for (int j = 0; j < size; j++) {
                message.u8(static_cast<uint8_t>(byte(random)));
            }
            uint16_t sequence = 0;
            if (i % 2 == 0) {
                mallory.endpoint.queueReliable(message.data);
            } else {
                mallory.endpoint.add(message.data, sequence);
            }
            if (i % 20 == 0) {
                mallory.flush(transport);
                world.tick(0.05f);
                driver.tick(0.05f);
                alice.acceptAll(alice.collect(transport));
                alice.flush(transport);
                mallory.acceptAll(mallory.collect(transport));
            }
        }
        REQUIRE(pawnNamed(world, "Alice") != nullptr);
        for (int i = 0; i < 200 && driver.connectionCount() > 1; i++) {
            std::vector<uint8_t> datagram(static_cast<size_t>(length(random)) + 4);
            for (uint8_t& value : datagram) {
                value = static_cast<uint8_t>(byte(random));
            }
            transport.receive(2, datagram);
            world.tick(0.05f);
            driver.tick(0.05f);
            alice.acceptAll(alice.collect(transport));
            alice.flush(transport);
        }
        CHECK(driver.connectionCount() == 1);
        CHECK(pawnNamed(world, "Mallory") == nullptr);
        ut::UObject* survivor = pawnNamed(world, "Alice");
        REQUIRE(survivor != nullptr);
        CHECK(driver.remotePlayers() == std::vector<ut::UObject*> { survivor });
        size_t before = alice.messages.size() + alice.items.size();
        play(world, driver, transport, { &alice }, 40);
        CHECK(alice.messages.size() + alice.items.size() > before);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("a client replaying a recorded server stream moves lifts and players to where the server had them", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    std::vector<std::vector<std::vector<uint8_t>>> ticks;
    std::string moverName;
    ut::Vector moverStart;
    ut::Vector moverEnd;
    std::map<std::string, ut::Vector> botEnds;
    {
        RecordingTransport transport;
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        *reinterpret_cast<int32_t*>(world.gameInfo->propertyAddress(ut::Name("MinPlayers"))) = 3;
        Peer peer(9);
        size_t recorded = 0;
        auto record = [&](int count) {
            for (int i = 0; i < count; i++) {
                world.tick(0.05f);
                driver.tick(0.05f);
                std::vector<std::vector<uint8_t>> packets;
                for (; recorded < transport.sent.size(); recorded++) {
                    if (transport.sent[recorded].connection == peer.connection) {
                        packets.push_back(transport.sent[recorded].data);
                    }
                }
                ticks.push_back(std::move(packets));
                peer.acceptAll(peer.collect(transport));
                peer.flush(transport);
            }
        };
        transport.connect(peer.connection);
        peer.say(transport, { 1, 2 });
        record(1);
        peer.say(transport, joinMessage("?Name=Watcher"));
        record(300);
        ut::UObject* mover = nullptr;
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && ut::isMover(a) && !ut::actor::bInterpolating(a) && ut::mover::KeyNum(a) == 0) {
                mover = a;
                break;
            }
        }
        REQUIRE(mover != nullptr);
        moverName = mover->name.str();
        moverStart = ut::actor::Location(mover);
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && ut::isPawn(a) && !ut::isPlayerPawn(a)) {
                ut::actor::bAlwaysRelevant(a) = 1;
            }
        }
        ut::vm::Call(mover, ut::Name("DoOpen")).run();
        record(40);
        moverEnd = ut::actor::Location(mover);
        REQUIRE((moverEnd - moverStart).size() > 10.0f);
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && ut::isPawn(a) && !ut::isPlayerPawn(a)) {
                botEnds[ut::playerreplicationinfo::PlayerName(ut::pawn::PlayerReplicationInfo(a))] = ut::actor::Location(a);
            }
        }
        REQUIRE(botEnds.size() == 2);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    {
        RecordingTransport transport;
        ut::World world;
        world.netMode = ut::NM_Client;
        ut::net::Driver driver(transport, false);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16]["));
        transport.connect(9);
        driver.tick(0.05f);
        ut::UObject* mover = nullptr;
        for (ut::UObject* a : world.actors) {
            if (a && a->name.str() == moverName) {
                mover = a;
            }
        }
        REQUIRE(mover != nullptr);
        REQUIRE(ut::actor::Location(mover) == moverStart);
        for (size_t i = 0; i < ticks.size(); i++) {
            for (const std::vector<uint8_t>& packet : ticks[i]) {
                transport.receive(9, packet);
            }
            driver.tick(0.05f);
            if (i == 0) {
                REQUIRE(driver.welcomeUrl == "DM-Deck16][");
                driver.welcomeUrl.clear();
                driver.join("?Name=Watcher");
            }
            world.tick(0.05f);
        }
        CHECK_FALSE(driver.closed);
        CHECK((ut::actor::Location(mover) - moverEnd).size() < 2.0f);
        size_t matched = 0;
        for (ut::UObject* a : world.actors) {
            if (!a || a->destroyed || !ut::isPawn(a) || ut::isPlayerPawn(a) || !ut::pawn::PlayerReplicationInfo(a)) {
                continue;
            }
            auto found = botEnds.find(ut::playerreplicationinfo::PlayerName(ut::pawn::PlayerReplicationInfo(a)));
            REQUIRE(found != botEnds.end());
            INFO("bot " << found->first);
            CHECK((ut::actor::Location(a) - found->second).size() < 40.0f);
            matched++;
        }
        CHECK(matched == 2);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("a server refuses logins without the password, caps connections and drops clients that never join", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        *reinterpret_cast<std::string*>(world.gameInfo->propertyAddress(ut::Name("GamePassword"))) = "secret";
        Peer refused(1);
        joinPeer(world, driver, transport, refused, "Refused");
        REQUIRE(countType(refused.messages, 4) == 1);
        CHECK(playerPawns(world).empty());

        for (uint32_t i = 0; i < ut::net::MaxConnections + 5; i++) {
            transport.connect(1000 + i);
        }
        driver.tick(0.05f);
        CHECK(driver.connectionCount() == ut::net::MaxConnections);
        CHECK(transport.closed.size() == 6);

        for (int i = 0; i < static_cast<int>(ut::net::PendingTimeout / 0.5) + 2; i++) {
            driver.tick(0.5f);
        }
        CHECK(driver.connectionCount() == 0);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("a client flooding the server is throttled to its inbound budget", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        Peer flooder(3);
        joinPeer(world, driver, transport, flooder, "Flooder");
        play(world, driver, transport, { &flooder }, 40);
        const size_t packets = 400;
        for (size_t i = 0; i < packets; i++) {
            uint16_t sequence = 0;
            REQUIRE(flooder.endpoint.add(std::vector<uint8_t>(1000, 0), sequence));
            flooder.flush(transport);
        }
        world.tick(0.05f);
        driver.tick(0.05f);
        std::vector<ut::net::ConnectionStats> stats = driver.stats();
        REQUIRE(stats.size() == 1);
        CHECK(stats[0].packetsDropped >= packets - ut::net::MaxIncomingRate / 1000);
        CHECK(stats[0].packetsDropped <= packets);
        CHECK(static_cast<double>(stats[0].bytesReceived) <= ut::net::MaxIncomingRate * (1.0 + 0.05 * 43));
        play(world, driver, transport, { &flooder }, 20);
        CHECK(driver.connectionCount() == 1);
        CHECK(pawnNamed(world, "Flooder") != nullptr);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

TEST_CASE("garbage from the server never crashes a client", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_Client;
        ut::net::Driver driver(transport, false);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16]["));
        Peer server(1);
        transport.connect(1);
        driver.tick(0.05f);
        driver.join("?Name=Victim");
        driver.tick(0.05f);
        server.collect(transport);
        REQUIRE(countType(server.messages, 1) == 1);
        REQUIRE(countType(server.messages, 3) == 1);

        ut::net::Packet define;
        define.u8(5);
        define.varint(1);
        define.u8(1);
        define.str("UnrealShare.TeamInfo");
        server.endpoint.queueReliable(define.data);
        std::mt19937 random(99);
        std::uniform_int_distribution<int> byte(0, 255);
        std::uniform_int_distribution<int> length(0, 48);
        std::uniform_real_distribution<float> coordinate(-4000.0f, 4000.0f);
        for (uint32_t i = 0; i < 3000 && !driver.closed; i++) {
            ut::net::Packet message;
            int kind = static_cast<int>(i % 4);
            if (kind == 0) {
                message.u8(6);
                message.varint(1000 + i);
                message.u8(1);
                message.u8(1);
                message.varint(1);
                message.vector({ coordinate(random), coordinate(random), coordinate(random) });
                message.rotator({ byte(random) << 8, byte(random) << 8, 0 });
            } else if (kind == 1) {
                message.u8(6);
                message.varint(1000 + i - 1);
                message.u8(0);
            } else if (kind == 2) {
                message.u8(8);
                message.varint(1000 + i - 2);
            } else {
                uint8_t type = static_cast<uint8_t>(byte(random) % 10);
                message.u8(type == 2 || type == 4 ? 7 : type);
            }
            int size = length(random);
            for (int j = 0; j < size; j++) {
                message.u8(static_cast<uint8_t>(byte(random)));
            }
            uint16_t sequence = 0;
            if (kind == 0 || i % 2 == 0) {
                server.endpoint.queueReliable(message.data);
            } else {
                server.endpoint.add(message.data, sequence);
            }
            if (i % 20 == 0) {
                server.flush(transport);
                driver.tick(0.05f);
                world.tick(0.05f);
                server.acceptAll(server.collect(transport));
            }
        }
        CHECK_FALSE(driver.closed);
        ut::net::Packet open;
        open.u8(6);
        open.varint(999999);
        open.u8(1);
        open.u8(1);
        open.varint(1);
        open.vector({ 1.0f, 2.0f, 3.0f });
        open.rotator({});
        open.varint(0);
        REQUIRE(server.endpoint.queueReliable(open.data));
        ut::UObject* spawned = nullptr;
        for (int round = 0; round < 400 && !spawned; round++) {
            server.flush(transport);
            driver.tick(0.05f);
            world.tick(0.05f);
            server.acceptAll(server.collect(transport));
            for (ut::UObject* a : world.actors) {
                if (a && !a->destroyed && a->cls->name == ut::Name("TeamInfo") && ut::actor::Location(a).x == 1.0f && ut::actor::Location(a).z == 3.0f) {
                    spawned = a;
                }
            }
        }
        CHECK(spawned != nullptr);
        CHECK(server.endpoint.queuedMessages() == 0);
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}

#ifndef _WIN32
namespace {
    std::string queryServer(ut::World& world, int port, const std::string& request)
    {
        int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in target {};
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        target.sin_port = htons(static_cast<uint16_t>(port));
        sendto(handle, request.data(), request.size(), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
        std::string reply;
        for (int attempt = 0; attempt < 40 && reply.empty(); attempt++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            world.tick(0.05f);
            char buffer[2048];
            ssize_t count = recv(handle, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (count > 0) {
                reply.assign(buffer, static_cast<size_t>(count));
            }
        }
        close(handle);
        return reply;
    }

    ut::UObject* firstActor(ut::World& world, const char* className)
    {
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && a->isA(ut::Name(className))) {
                return a;
            }
        }
        return nullptr;
    }
}

TEST_CASE("a hosted level answers LAN beacon and status queries", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        world.netPort = 7777;
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        ut::UObject* beacon = firstActor(world, "UdpBeacon");
        ut::UObject* query = firstActor(world, "UdpServerQuery");
        REQUIRE(beacon != nullptr);
        REQUIRE(query != nullptr);
        CHECK(firstActor(world, "UdpServerUplink") == nullptr);
        int beaconPort = *reinterpret_cast<int32_t*>(beacon->propertyAddress(ut::Name("Port")));
        int queryPort = *reinterpret_cast<int32_t*>(query->propertyAddress(ut::Name("Port")));
        REQUIRE(beaconPort >= 8777);
        REQUIRE(queryPort > 0);

        CHECK(queryServer(world, beaconPort, "REPORTQUERY") == "ut " + std::to_string(queryPort));
        std::string info = queryServer(world, queryPort, "\\info\\");
        CHECK(info.find("\\mapname\\DM-Deck16][") != std::string::npos);
        CHECK(info.find("\\hostport\\7777") != std::string::npos);
        CHECK(info.find("\\numplayers\\0") != std::string::npos);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}
#endif

#if defined(OPENUT_PATH) && !defined(_WIN32)
namespace {
    using testprocess::eventually;
    using testprocess::GameProcess;
    using testprocess::ScratchDirectory;

    bool anyContains(const std::vector<std::string>& lines, const std::string& text)
    {
        for (const std::string& line : lines) {
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    bool pawnPosition(GameProcess& process, const std::string& name, float& x, float& y)
    {
        std::string marker = "name " + name + " role ";
        for (const std::string& line : process.ask("players", "pawn ")) {
            size_t at = line.find(marker);
            int role = 0;
            float z = 0;
            if (at != std::string::npos && std::sscanf(line.c_str() + at + marker.size(), "%d at %f %f %f", &role, &x, &y, &z) == 4) {
                return true;
            }
        }
        return false;
    }

    void playMatch(const std::string& environment)
    {
        ScratchDirectory scratch;
        const std::filesystem::path& directory = scratch.path;
        std::string port = std::to_string(20000 + getpid() % 20000);
        {
            GameProcess server("--server \"DM-Deck16][?game=Botpack.DeathMatchPlus\" --port " + port, directory / "server.log", environment);
            REQUIRE(eventually([&]() {
                return server.output().find("server running") != std::string::npos;
            },
                120));
            GameProcess alice("\"127.0.0.1:" + port + "?Name=Alice\" --headless", directory / "alice.log", environment);
            GameProcess bob("\"127.0.0.1:" + port + "?Name=Bob\" --headless", directory / "bob.log", environment);
            REQUIRE(eventually([&]() {
                return anyContains(alice.ask("players", "pawn "), "name Alice role 3") && anyContains(bob.ask("players", "pawn "), "name Bob role 3");
            },
                120));
            REQUIRE(anyContains(server.ask("players", "pawn "), "name Alice role 4"));
            REQUIRE(eventually([&]() {
                for (GameProcess* client : { &alice, &bob }) {
                    client->send("press LeftMouse");
                    client->send("release LeftMouse");
                }
                std::vector<std::string> pawns = server.ask("players", "pawn ");
                return anyContains(pawns, "name Alice role 4") && anyContains(pawns, "state PlayerWalking") && !anyContains(pawns, "state PlayerWaiting");
            },
                90));

            REQUIRE_FALSE(server.ask("place Alice PlayerStart 0 60", "placed").empty());
            REQUIRE_FALSE(server.ask("place Bob PlayerStart 0 200", "placed").empty());
            float startX = 0;
            float startY = 0;
            REQUIRE(eventually([&]() {
                float x = 0;
                float y = 0;
                return pawnPosition(server, "Alice", startX, startY) && pawnPosition(alice, "Alice", x, y) && std::abs(x - startX) + std::abs(y - startY) < 1.0f;
            }));
            alice.send("press Up");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            alice.send("release Up");
            float serverX = 0;
            float serverY = 0;
            float ownX = 0;
            float ownY = 0;
            float seenX = 0;
            float seenY = 0;
            bool converged = eventually([&]() {
                if (!pawnPosition(server, "Alice", serverX, serverY) || !pawnPosition(alice, "Alice", ownX, ownY) || !pawnPosition(bob, "Alice", seenX, seenY)) {
                    return false;
                }
                bool moved = std::abs(serverX - startX) + std::abs(serverY - startY) > 20.0f;
                return moved && std::abs(serverX - ownX) + std::abs(serverY - ownY) < 3.0f && std::abs(serverX - seenX) + std::abs(serverY - seenY) < 3.0f;
            });
            INFO("start " << startX << " " << startY << " server " << serverX << " " << serverY << " own " << ownX << " " << ownY << " seen " << seenX << " " << seenY);
            CHECK(converged);
        }
    }
}

TEST_CASE("two clients play on a dedicated server over the network", "[data][net]")
{
    playMatch("");
}

TEST_CASE("two clients play over a link that loses, delays, reorders and duplicates packets", "[data][net]")
{
    playMatch("UT_NET_LOSS=10 UT_NET_LAG=60 UT_NET_REORDER=10 UT_NET_DUP=5");
}

TEST_CASE("reconnect after disconnect joins the last server again, as the multiplayer menu does", "[data][net]")
{
    ScratchDirectory scratch;
    std::filesystem::create_directories(scratch.path / "openut");
    std::ifstream shipped(std::filesystem::path(OPENUT_GAME_DATA) / "System" / "UnrealTournament.ini");
    std::ofstream capped(scratch.path / "openut" / "UnrealTournament.ini");
    for (std::string line; std::getline(shipped, line);) {
        capped << line << "\n";
        if (line.rfind("[WinDrv.WindowsClient]", 0) == 0) {
            capped << "FrameRateLimit=1\n";
        }
    }
    capped.close();
    std::string port = std::to_string(20000 + getpid() % 20000);
    GameProcess server("--server \"DM-Deck16][?game=Botpack.DeathMatchPlus\" --port " + port, scratch.path / "server.log", "");
    REQUIRE(eventually([&]() {
        return server.output().find("server running") != std::string::npos;
    },
        120));
    GameProcess alice("\"unreal://127.0.0.1:" + port + "?Name=Alice\"", scratch.path / "alice.log", "SDL_VIDEODRIVER=offscreen");
    REQUIRE(eventually([&]() {
        return anyContains(server.ask("players", "pawn "), "name Alice");
    },
        120));

    alice.send("exec disconnect");
    REQUIRE(eventually([&]() {
        return !alice.ask("exec mapname", "CityIntro").empty() && !anyContains(server.ask("players", "pawn "), "name Alice");
    },
        60));

    alice.send("exec reconnect");
    REQUIRE(eventually([&]() {
        return !alice.ask("exec mapname", "DM-Deck16][").empty() && anyContains(server.ask("players", "pawn "), "name Alice");
    },
        120));
}
#endif

TEST_CASE("NaN and infinite floats a client sends to server functions arrive as zero and never reach the world", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    RecordingTransport transport;
    {
        ut::World world;
        world.netMode = ut::NM_DedicatedServer;
        ut::net::Driver driver(transport, true);
        driver.setWorld(&world);
        REQUIRE(world.loadMap("DM-Deck16][?game=Botpack.DeathMatchPlus"));
        Peer mallory(2);
        joinPeer(world, driver, transport, mallory, "Mallory");
        play(world, driver, transport, { &mallory }, 20);
        ut::UObject* pawn = pawnNamed(world, "Mallory");
        REQUIRE(pawn != nullptr);
        std::vector<uint32_t> pawnChannels;
        for (const auto& [channel, path] : openedChannels(mallory)) {
            if (path == pawn->cls->pathName()) {
                pawnChannels.push_back(channel);
            }
        }
        REQUIRE_FALSE(pawnChannels.empty());
        const float hostile[] = { std::nanf(""), INFINITY, -INFINITY };
        for (float value : hostile) {
            for (uint32_t channel : pawnChannels) {
                for (uint32_t slot = 0; slot < 160; slot++) {
                    ut::net::Packet move;
                    move.u8(8);
                    move.varint(channel);
                    move.varint(slot);
                    move.f32(value);
                    for (int i = 0; i < 6; i++) {
                        move.signedVarint(0);
                    }
                    for (int i = 0; i < 9; i++) {
                        move.u8(0);
                    }
                    move.signedVarint(0);
                    move.u8(0);
                    move.signedVarint(0);
                    uint16_t sequence = 0;
                    mallory.endpoint.add(move.data, sequence);
                    if (slot % 8 == 0) {
                        mallory.flush(transport);
                        world.tick(0.05f);
                        driver.tick(0.05f);
                        mallory.acceptAll(mallory.collect(transport));
                    }
                }
            }
        }
        play(world, driver, transport, { &mallory }, 20);
        REQUIRE(pawnNamed(world, "Mallory") == pawn);
        CHECK(std::isfinite(*reinterpret_cast<float*>(pawn->propertyAddress(ut::Name("CurrentTimeStamp")))));
        for (ut::UObject* a : world.actors) {
            if (!a || a->destroyed) {
                continue;
            }
            INFO(a->name.str());
            const ut::Vector& location = ut::actor::Location(a);
            const ut::Vector& velocity = ut::actor::Velocity(a);
            CHECK(std::isfinite(location.x));
            CHECK(std::isfinite(location.y));
            CHECK(std::isfinite(location.z));
            CHECK(std::isfinite(velocity.x));
            CHECK(std::isfinite(velocity.y));
            CHECK(std::isfinite(velocity.z));
        }
        driver.setWorld(nullptr);
    }
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
}
