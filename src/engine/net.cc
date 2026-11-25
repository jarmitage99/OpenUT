#include "engine/net.h"

#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace ut::net {
    namespace {
        constexpr uint8_t MessageHello = 1;
        constexpr uint8_t MessageWelcome = 2;
        constexpr uint8_t MessageJoin = 3;
        constexpr uint8_t MessageFailure = 4;
        constexpr uint8_t MessageDefine = 5;
        constexpr uint8_t MessageActor = 6;
        constexpr uint8_t MessageClose = 7;
        constexpr uint8_t MessageCall = 8;

        constexpr uint8_t ActorOpen = 1;
        constexpr uint8_t ActorStatic = 2;

        constexpr uint8_t RefNone = 0;
        constexpr uint8_t RefStatic = 1;
        constexpr uint8_t RefActor = 2;

        constexpr double RelevantTimeout = 5.0;
        constexpr double RelevantCheckInterval = 0.3;
        constexpr double SpawnPrioritySeconds = 1.0;

        Driver* active = nullptr;

        bool remoteHook(UObject* context, UFunction* function, uint8_t* locals)
        {
            return active->remoteCall(context, function, locals);
        }

        bool persistent(UObject* a)
        {
            return actor::bStatic(a) || actor::bNoDelete(a);
        }

        bool isActor(UObject* object)
        {
            return object->cls && object->cls->isChildOf(engineClasses().actor);
        }

        UObject* topOwner(UObject* a)
        {
            int guard = 0;
            while (actor::Owner(a) && guard++ < 64) {
                a = actor::Owner(a);
            }
            return a;
        }

        UFunction* rootFunction(UFunction* function)
        {
            while (UFunction* parent = cast<UFunction>(function->superField)) {
                objects::ensureLoaded(parent);
                function = parent;
            }
            return function;
        }

        std::string optionValue(const std::string& options, std::string_view key)
        {
            size_t mark = options.find('?');
            while (mark != std::string::npos) {
                size_t next = options.find('?', mark + 1);
                std::string item = options.substr(mark + 1, next == std::string::npos ? std::string::npos : next - mark - 1);
                size_t equals = item.find('=');
                if (equals != std::string::npos && iequals(item.substr(0, equals), key)) {
                    return item.substr(equals + 1);
                }
                mark = next;
            }
            return {};
        }

        struct ReceiveState {
            Vector location;
            Rotator rotation;
            UObject* base = nullptr;
            uint32_t collideActors = 0;
            float radius = 0;
            float height = 0;
            Plane simAnim;
            Name animSequence;
            Vector simInterpolate;
        };

        int32_t quantize(float value)
        {
            if (!(value > -1048576.0f)) {
                return value < 0 ? -1048576 : 0;
            }
            if (value > 1048576.0f) {
                return 1048576;
            }
            return static_cast<int32_t>(std::lround(value));
        }

        float netPriority(Connection& connection, UObject* a, Channel* channel, const Vector& viewLocation, const Vector& viewDirection, double time)
        {
            double since = channel ? time - channel->lastUpdate : time - connection.waiting.emplace(a, time - SpawnPrioritySeconds).first->second;
            float facing = 0;
            if (actor::bAlwaysRelevant(a)) {
                facing = 0;
            } else if (connection.pawn && pawn::Weapon(connection.pawn) == a) {
                facing = 1;
            } else if (actor::RemoteRole(a) > ROLE_DumbProxy || actor::Physics(a) == PHYS_None) {
                Vector direction = actor::Location(a) - viewLocation;
                float along = viewDirection.dot(direction);
                facing = along < 0 ? -1.0f : viewDirection.dot(direction.normal());
            }
            float weight = actor::NetPriority(a) * static_cast<float>(since);
            if (actor::bAlwaysRelevant(a)) {
                weight *= std::min(actor::NetUpdateFrequency(a) * 0.1f, 1.0f);
            }
            return (3.0f + facing) * weight;
        }

        bool samePlane(const Plane& a, const Plane& b)
        {
            return std::memcmp(&a, &b, sizeof(Plane)) == 0;
        }

        ReceiveState preReceive(UObject* a)
        {
            ReceiveState saved;
            saved.location = actor::Location(a);
            saved.rotation = actor::Rotation(a);
            saved.base = actor::Base(a);
            saved.collideActors = actor::bCollideActors(a);
            saved.radius = actor::CollisionRadius(a);
            saved.height = actor::CollisionHeight(a);
            saved.simAnim = actor::SimAnim(a);
            saved.animSequence = actor::AnimSequence(a);
            if (isMover(a)) {
                saved.simInterpolate = mover::SimInterpolate(a);
            }
            return saved;
        }

        void postReceive(World& world, UObject* a, ReceiveState saved)
        {
            std::swap(actor::Location(a), saved.location);
            std::swap(actor::Rotation(a), saved.rotation);
            std::swap(actor::Base(a), saved.base);
            std::swap(actor::bCollideActors(a), saved.collideActors);
            std::swap(actor::CollisionRadius(a), saved.radius);
            std::swap(actor::CollisionHeight(a), saved.height);
            if (isMover(a) && !(saved.simInterpolate == mover::SimInterpolate(a))) {
                Vector interpolate = mover::SimInterpolate(a);
                mover::OldPos(a) = mover::SimOldPos(a);
                mover::OldRot(a) = { mover::SimOldRotPitch(a), mover::SimOldRotYaw(a), mover::SimOldRotRoll(a) };
                actor::PhysAlpha(a) = interpolate.x * 0.01f;
                actor::PhysRate(a) = interpolate.y * 0.01f;
                int32_t keys = static_cast<int32_t>(interpolate.z);
                mover::KeyNum(a) = static_cast<uint8_t>(keys & 255);
                mover::PrevKeyNum(a) = static_cast<uint8_t>(keys >> 8);
                setPhysics(a, PHYS_MovingBrush, nullptr);
                actor::bInterpolating(a) = 1;
            }
            if (!samePlane(saved.simAnim, actor::SimAnim(a)) || saved.animSequence != actor::AnimSequence(a)) {
                Plane sim = actor::SimAnim(a);
                actor::AnimFrame(a) = sim.x * 0.0001f;
                actor::AnimRate(a) = sim.y * 0.0002f;
                actor::TweenRate(a) = sim.z * 0.001f;
                actor::AnimLast(a) = std::abs(sim.w) * 0.0001f;
                actor::bAnimLoop(a) = sim.w < 0 ? 1 : 0;
                actor::bAnimFinished(a) = 0;
            }
            if (!(saved.location == actor::Location(a))) {
                Vector target = saved.location;
                Vector error = actor::Location(a) - target;
                bool smooth = isPawn(a) && actor::Role(a) == ROLE_SimulatedProxy && actor::Velocity(a).sizeSquared() > 0.0001f && error.sizeSquared() < 10000.0f;
                if (smooth) {
                    float startError = error.sizeSquared();
                    Vector corrected = target;
                    if (startError > 1600.0f) {
                        moveSmooth(a, (target - actor::Location(a)) * 0.35f);
                        if ((actor::Location(a) - target).sizeSquared() > 0.75f * startError) {
                            corrected = target + (actor::Location(a) - target) * 0.5f;
                        } else {
                            corrected = actor::Location(a);
                        }
                    } else {
                        corrected = target + error * 0.15f;
                    }
                    world.farMoveActor(a, corrected, false, true);
                } else {
                    world.farMoveActor(a, target, false, true);
                }
            }
            if (!(saved.rotation == actor::Rotation(a))) {
                CheckResult hit;
                world.moveActor(a, {}, saved.rotation, hit, false, false, false, true);
            }
            if (saved.radius != actor::CollisionRadius(a) || saved.height != actor::CollisionHeight(a)) {
                actor::CollisionRadius(a) = saved.radius;
                actor::CollisionHeight(a) = saved.height;
                world.collisionHash.update(a);
            }
            if (saved.collideActors != actor::bCollideActors(a)) {
                setCollision(a, saved.collideActors != 0, actor::bBlockActors(a) != 0, actor::bBlockPlayers(a) != 0);
            }
            if (saved.base != actor::Base(a)) {
                setBase(a, saved.base, false);
            }
            actor::bJustTeleported(a) = 0;
        }
    }

    void Packet::u8(uint8_t value)
    {
        data.push_back(value);
    }

    void Packet::varint(uint32_t value)
    {
        while (value >= 0x80) {
            data.push_back(static_cast<uint8_t>(value | 0x80));
            value >>= 7;
        }
        data.push_back(static_cast<uint8_t>(value));
    }

    void Packet::signedVarint(int32_t value)
    {
        uint32_t bits = static_cast<uint32_t>(value);
        varint((bits << 1) ^ (value < 0 ? 0xffffffffu : 0u));
    }

    void Packet::i32(int32_t value)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void Packet::f32(float value)
    {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void Packet::str(std::string_view value)
    {
        varint(static_cast<uint32_t>(value.size()));
        data.insert(data.end(), value.begin(), value.end());
    }

    void Packet::vector(const Vector& value)
    {
        f32(value.x);
        f32(value.y);
        f32(value.z);
    }

    void Packet::rotator(const Rotator& value)
    {
        i32(value.pitch);
        i32(value.yaw);
        i32(value.roll);
    }

    PacketReader::PacketReader(const uint8_t* begin, size_t size)
        : cursor(begin)
        , end(begin + size)
    {
    }

    uint8_t PacketReader::u8()
    {
        if (cursor >= end) {
            failed = true;
            return 0;
        }
        return *cursor++;
    }

    uint32_t PacketReader::varint()
    {
        uint32_t value = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            uint8_t byte = u8();
            value |= static_cast<uint32_t>(byte & 0x7f) << shift;
            if (!(byte & 0x80)) {
                return value;
            }
        }
        failed = true;
        return 0;
    }

    int32_t PacketReader::signedVarint()
    {
        uint32_t bits = varint();
        return static_cast<int32_t>((bits >> 1) ^ (0u - (bits & 1u)));
    }

    int32_t PacketReader::i32()
    {
        if (end - cursor < 4) {
            failed = true;
            cursor = end;
            return 0;
        }
        int32_t value;
        std::memcpy(&value, cursor, 4);
        cursor += 4;
        return value;
    }

    float PacketReader::f32()
    {
        if (end - cursor < 4) {
            failed = true;
            cursor = end;
            return 0;
        }
        float value;
        std::memcpy(&value, cursor, 4);
        cursor += 4;
        return value;
    }

    std::string PacketReader::str()
    {
        uint32_t size = varint();
        if (static_cast<size_t>(end - cursor) < size) {
            failed = true;
            cursor = end;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(cursor), size);
        cursor += size;
        return value;
    }

    Vector PacketReader::vector()
    {
        Vector value;
        value.x = f32();
        value.y = f32();
        value.z = f32();
        return value;
    }

    Rotator PacketReader::rotator()
    {
        Rotator value;
        value.pitch = i32();
        value.yaw = i32();
        value.roll = i32();
        return value;
    }

    bool PacketReader::atEnd() const
    {
        return cursor >= end;
    }

    Driver::Driver(Transport& link, bool isServer)
        : server(isServer)
        , transport(link)
    {
        const Ini& ini = config::system();
        if (std::optional<std::string> value = ini.get("IpDrv.TcpNetDriver", "MaxClientRate")) {
            rate = std::max(std::atoi(value->c_str()), 1000);
        }
        if (std::optional<std::string> value = ini.get("IpDrv.TcpNetDriver", "NetServerMaxTickRate")) {
            tickRate = std::max(static_cast<float>(std::atof(value->c_str())), 1.0f);
        }
    }

    Driver::~Driver()
    {
        setWorld(nullptr);
    }

    void Driver::setWorld(World* target)
    {
        for (auto& [id, connection] : connections) {
            for (auto& [object, channel] : connection.channels) {
                channel.cls->destroyValue(channel.shadow);
                delete[] channel.shadow;
            }
            connection.channels.clear();
            connection.temporaries.clear();
            connection.waiting.clear();
            connection.sentObjects.clear();
            connection.receivedObjects.clear();
            connection.pawn = nullptr;
            connection.player = nullptr;
            connection.joined = false;
            connection.welcomed = false;
        }
        netIds.clear();
        actorsById.clear();
        layouts.clear();
        if (world) {
            world->net = nullptr;
        }
        world = target;
        if (!world) {
            if (active == this) {
                active = nullptr;
                vm::remoteHook = nullptr;
            }
            return;
        }
        world->net = this;
        active = this;
        vm::remoteHook = remoteHook;
    }

    size_t Driver::connectionCount() const
    {
        return connections.size();
    }

    std::vector<ConnectionStats> Driver::stats() const
    {
        std::vector<ConnectionStats> result;
        for (const auto& [id, connection] : connections) {
            ConnectionStats entry;
            entry.connection = id;
            entry.pawn = connection.pawn;
            entry.bytesSent = connection.endpoint.bytesSent;
            entry.packetsSent = connection.endpoint.packetsSent;
            entry.packetsLost = connection.endpoint.packetsLost;
            entry.messagesResent = connection.endpoint.messagesResent;
            entry.bytesReceived = connection.bytesReceived;
            entry.packetsDropped = connection.packetsDropped;
            entry.channels = connection.channels.size();
            entry.queuedMessages = connection.endpoint.queuedMessages();
            result.push_back(entry);
        }
        return result;
    }

    std::string Driver::playerAddress(UObject* pawn)
    {
        for (auto& [id, connection] : connections) {
            if (connection.pawn == pawn) {
                return transport.address(id);
            }
        }
        return {};
    }

    std::vector<UObject*> Driver::remotePlayers() const
    {
        std::vector<UObject*> players;
        for (const auto& [id, connection] : connections) {
            if (connection.pawn && !connection.pawn->destroyed) {
                players.push_back(connection.pawn);
            }
        }
        return players;
    }

    int Driver::playerPing(UObject* pawn)
    {
        for (auto& [id, connection] : connections) {
            if (server ? connection.pawn == pawn : world && world->localPlayer == pawn) {
                return transport.ping(id);
            }
        }
        return 0;
    }

    int Driver::playerLoss(UObject* pawn)
    {
        for (auto& [id, connection] : connections) {
            if (server ? connection.pawn == pawn : world && world->localPlayer == pawn) {
                return transport.loss(id);
            }
        }
        return 0;
    }

    void Driver::queue(Connection& connection, const Packet& packet)
    {
        if (!connection.endpoint.queueReliable(packet.data)) {
            connection.failed = true;
        }
    }

    void Driver::transmit(Connection& connection)
    {
        for (const std::vector<uint8_t>& packet : connection.endpoint.flush()) {
            transport.send(connection.id, packet);
        }
    }

    void Driver::retire(Connection& connection)
    {
        Endpoint& endpoint = connection.endpoint;
        for (uint16_t sequence : endpoint.lost) {
            auto found = connection.retirements.find(sequence);
            if (found == connection.retirements.end()) {
                continue;
            }
            for (const Retirement& retirement : found->second) {
                auto known = actorsById.find(retirement.channel);
                if (known == actorsById.end()) {
                    continue;
                }
                auto channel = connection.channels.find(known->second);
                if (channel == connection.channels.end() || retirement.entry >= channel->second.lastSequence.size()) {
                    continue;
                }
                if (channel->second.lastSequence[retirement.entry] == sequence) {
                    channel->second.dirty[retirement.entry] = 1;
                }
            }
            connection.retirements.erase(found);
        }
        for (uint16_t sequence : endpoint.acked) {
            connection.retirements.erase(sequence);
        }
        endpoint.lost.clear();
        endpoint.acked.clear();
    }

    void Driver::receive(Connection& connection, const std::vector<uint8_t>& data)
    {
        if (server && static_cast<double>(data.size()) > connection.inbound) {
            connection.packetsDropped++;
            return;
        }
        connection.inbound -= static_cast<double>(data.size());
        connection.bytesReceived += data.size();
        Endpoint::Received received;
        if (!connection.endpoint.receive(data.data(), data.size(), received)) {
            connection.failed = true;
            return;
        }
        for (const std::vector<uint8_t>& message : received.messages) {
            handle(connection, message);
        }
        if (!received.fresh) {
            return;
        }
        unresolved = false;
        for (const std::vector<uint8_t>& item : received.items) {
            handle(connection, item);
        }
        if (server || !unresolved) {
            connection.endpoint.accept(received.sequence);
        }
    }

    void Driver::welcome(Connection& connection)
    {
        Packet out;
        out.u8(MessageWelcome);
        std::string speed = world->urlOption("Speed");
        out.str(world->url.substr(0, world->url.find('?')) + (speed.empty() ? std::string() : "?Speed=" + speed));
        queue(connection, out);
    }

    void Driver::join(const std::string& options)
    {
        for (auto& [id, connection] : connections) {
            Packet out;
            out.u8(MessageJoin);
            out.str(options);
            queue(connection, out);
            connection.joined = true;
        }
    }

    void Driver::drop(Connection& connection, bool notify)
    {
        if (notify) {
            transport.close(connection.id);
        }
        UObject* pawn = connection.pawn;
        connection.pawn = nullptr;
        for (auto& [object, channel] : connection.channels) {
            channel.cls->destroyValue(channel.shadow);
            delete[] channel.shadow;
        }
        connection.channels.clear();
        uint32_t id = connection.id;
        connections.erase(id);
        if (pawn && !pawn->destroyed && world) {
            world->destroy(pawn);
        }
    }

    void Driver::tick(float deltaSeconds)
    {
        double seconds = static_cast<double>(deltaSeconds);
        time += seconds;
        for (auto& [id, connection] : connections) {
            connection.endpoint.update(time, seconds, rate);
            connection.inbound = std::min(connection.inbound + MaxIncomingRate * seconds, static_cast<double>(MaxIncomingRate));
        }
        for (Event& event : transport.poll()) {
            if (event.kind == Event::Kind::Connected) {
                if (server && connections.size() >= MaxConnections) {
                    transport.close(event.connection);
                    continue;
                }
                Connection& connection = connections[event.connection];
                connection.id = event.connection;
                connection.created = time;
                connection.endpoint.update(time, 0, rate);
                if (!server) {
                    Packet out;
                    out.u8(MessageHello);
                    out.varint(ProtocolVersion);
                    queue(connection, out);
                }
                continue;
            }
            if (event.kind == Event::Kind::Disconnected && !server) {
                closed = true;
                if (failure.empty()) {
                    failure = "Connection to the server was lost";
                }
                continue;
            }
            auto found = connections.find(event.connection);
            if (found == connections.end()) {
                continue;
            }
            if (event.kind == Event::Kind::Disconnected) {
                drop(found->second, false);
                continue;
            }
            receive(found->second, event.data);
        }
        std::vector<uint32_t> failed;
        for (auto& [id, connection] : connections) {
            if (connection.failed || (server && !connection.joined && time - connection.created > PendingTimeout)) {
                failed.push_back(id);
            }
        }
        for (uint32_t id : failed) {
            drop(connections.at(id), true);
            if (!server) {
                closed = true;
                if (failure.empty()) {
                    failure = "Connection to the server failed";
                }
            }
        }
        for (auto& [id, connection] : connections) {
            retire(connection);
        }
        if (world && !closed) {
            replicate();
        }
        for (auto& [id, connection] : connections) {
            transmit(connection);
        }
    }

    void Driver::replicate()
    {
        if (server) {
            for (auto& [id, connection] : connections) {
                if (connection.greeted && !connection.welcomed) {
                    connection.welcomed = true;
                    welcome(connection);
                }
            }
            if (lastReplication >= 0 && time - lastReplication < 1.0 / static_cast<double>(tickRate) - 0.001) {
                return;
            }
            lastReplication = time;
        }
        std::vector<uint32_t> ids;
        for (auto& [id, connection] : connections) {
            if (connection.joined) {
                ids.push_back(id);
            }
        }
        for (uint32_t id : ids) {
            auto found = connections.find(id);
            if (found == connections.end()) {
                continue;
            }
            Connection& connection = found->second;
            if (!server) {
                UObject* pawn = world->localPlayer;
                auto channel = pawn ? connection.channels.find(pawn) : connection.channels.end();
                if (channel != connection.channels.end()) {
                    replicateActor(connection, channel->second);
                }
                continue;
            }
            if (!connection.pawn || connection.pawn->destroyed) {
                drop(connection, true);
                continue;
            }
            replicateConnection(connection);
        }
    }

    void Driver::handle(Connection& connection, const std::vector<uint8_t>& data)
    {
        PacketReader in(data.data(), data.size());
        uint8_t type = in.u8();
        if (server) {
            if (type == MessageHello) {
                handleHello(connection, in);
            } else if (type == MessageJoin && connection.greeted && !connection.joined && world) {
                handleJoin(connection, in);
            } else if (connection.joined && world) {
                if (type == MessageDefine) {
                    handleDefine(connection, in);
                } else if (type == MessageActor) {
                    handleActor(connection, in);
                } else if (type == MessageCall) {
                    handleCall(connection, in);
                }
            }
            return;
        }
        if (type == MessageWelcome) {
            handleWelcome(in);
        } else if (type == MessageFailure) {
            failure = in.str();
            closed = true;
        } else if (connection.joined && world) {
            if (type == MessageDefine) {
                handleDefine(connection, in);
            } else if (type == MessageActor) {
                handleActor(connection, in);
            } else if (type == MessageClose) {
                handleClose(in);
            } else if (type == MessageCall) {
                handleCall(connection, in);
            }
        }
    }

    void Driver::handleHello(Connection& connection, PacketReader& in)
    {
        uint32_t version = in.varint();
        if (in.failed || version != ProtocolVersion) {
            Packet out;
            out.u8(MessageFailure);
            out.str("Incompatible game version");
            queue(connection, out);
            return;
        }
        connection.greeted = true;
    }

    void Driver::handleWelcome(PacketReader& in)
    {
        std::string url = in.str();
        if (in.failed) {
            return;
        }
        welcomeUrl = url;
        for (auto& [id, connection] : connections) {
            connection.joined = false;
        }
    }

    void Driver::handleJoin(Connection& connection, PacketReader& in)
    {
        std::string options = in.str();
        if (in.failed || !world->gameInfo) {
            return;
        }
        auto fail = [&](const std::string& message) {
            Packet out;
            out.u8(MessageFailure);
            out.str(message);
            queue(connection, out);
        };
        vm::Call preLogin(world->gameInfo, Name("PreLogin"));
        if (preLogin.valid()) {
            preLogin.s(options).s(transport.address(connection.id)).s("").s("").run();
            std::string error = *reinterpret_cast<std::string*>(preLogin.parameter(2));
            if (!error.empty()) {
                fail(error);
                return;
            }
        }
        UClass* playerClass = nullptr;
        std::string className = optionValue(options, "Class");
        size_t dot = className.find('.');
        if (dot != std::string::npos && objects::findLinker(Name(className.substr(0, dot)))) {
            playerClass = objects::findClass(className);
        }
        if (!playerClass || !playerClass->isChildOf(engineClasses().playerPawn)) {
            playerClass = cast<UClass>(gameinfo::DefaultPlayerClass(world->gameInfo));
        }
        objects::flushLoads();
        vm::Call login(world->gameInfo, Name("Login"));
        if (!login.valid() || !playerClass) {
            fail("Login failed");
            return;
        }
        login.s("").s(options).s("").obj(playerClass).run();
        UObject* pawn = *reinterpret_cast<UObject**>(login.returnValue());
        if (!pawn) {
            fail(*reinterpret_cast<std::string*>(login.parameter(2)));
            return;
        }
        connection.player = objects::newObject(objects::intrinsicClass(Name("NetConnection")), objects::packageObject(Name("Transient")), Name(), 0);
        connection.pawn = pawn;
        connection.joined = true;
        playerpawn::Player(pawn) = connection.player;
        player::Actor(connection.player) = pawn;
        player::CurrentNetSpeed(connection.player) = rate;
        vm::event(pawn, Name("Possess"));
        actor::Role(pawn) = ROLE_Authority;
        actor::RemoteRole(pawn) = ROLE_AutonomousProxy;
        vm::event(pawn, Name("TravelPreAccept"));
        vm::Call(world->gameInfo, Name("AcceptInventory")).obj(pawn).run();
        vm::event(pawn, Name("TravelPostAccept"));
        vm::Call(world->gameInfo, Name("PostLogin")).obj(pawn).run();
    }

    void Driver::handleDefine(Connection& connection, PacketReader& in)
    {
        uint32_t id = in.varint();
        bool isClass = in.u8() != 0;
        std::string path = in.str();
        if (in.failed || id != connection.receivedObjects.size() + 1 || id > MaxDefinitions) {
            return;
        }
        UObject* object = nullptr;
        size_t dot = path.find('.');
        bool known = dot != std::string::npos && objects::findLinker(Name(path.substr(0, dot)));
        if (!server || known) {
            if (isClass) {
                object = objects::findClass(path);
            } else {
                object = objects::loadObject(path, nullptr);
            }
            objects::flushLoads();
        }
        connection.receivedObjects.push_back(object);
    }

    const ClassLayout& Driver::layout(UClass* cls)
    {
        auto found = layouts.find(cls);
        if (found != layouts.end()) {
            return found->second;
        }
        objects::ensureLoaded(cls);
        cls->linkClass();
        ClassLayout result;
        static const Name animationNames[] = { Name("SimAnim"), Name("AnimMinRate"), Name("SimBlendAnim"), Name("BlendAnimMinRate") };
        for (UProperty* property : cls->properties) {
            if (!(property->propertyFlags & CPF_Net)) {
                continue;
            }
            RepCondition condition;
            condition.owner = cast<UClass>(property->outer);
            condition.offset = property->replicationOffset;
            condition.animation = condition.owner == engineClasses().actor && std::find(std::begin(animationNames), std::end(animationNames), property->name) != std::end(animationNames);
            if (!condition.owner) {
                continue;
            }
            size_t slot = 0;
            while (slot < result.conditions.size() && !(result.conditions[slot].owner == condition.owner && result.conditions[slot].offset == condition.offset && result.conditions[slot].animation == condition.animation)) {
                slot++;
            }
            if (slot == result.conditions.size()) {
                objects::ensureLoaded(condition.owner);
                result.conditions.push_back(condition);
            }
            for (int32_t i = 0; i < property->arrayDim; i++) {
                RepEntry entry;
                entry.property = property;
                entry.offset = property->offset + static_cast<size_t>(i) * property->elementSize();
                entry.condition = slot;
                entry.wireIndex = static_cast<uint32_t>(result.entries.size());
                result.entries.push_back(entry);
            }
        }
        static const Name nameRole("Role");
        static const Name nameRemoteRole("RemoteRole");
        RepEntry* role = nullptr;
        RepEntry* remoteRole = nullptr;
        for (RepEntry& entry : result.entries) {
            if (entry.property->outer == engineClasses().actor && entry.property->name == nameRole) {
                role = &entry;
            } else if (entry.property->outer == engineClasses().actor && entry.property->name == nameRemoteRole) {
                remoteRole = &entry;
            }
        }
        if (role && remoteRole) {
            std::swap(role->wireIndex, remoteRole->wireIndex);
        }
        for (UClass* current = cls; current; current = current->superClass()) {
            objects::ensureLoaded(current);
            for (UField* child = current->children; child; child = child->next) {
                objects::ensureLoaded(child);
                UFunction* function = cast<UFunction>(child);
                if (function && (rootFunction(function)->functionFlags & FUNC_Net) && std::find(result.functions.begin(), result.functions.end(), function->name) == result.functions.end()) {
                    result.functions.push_back(function->name);
                }
            }
        }
        std::sort(result.functions.begin(), result.functions.end(), [](Name a, Name b) {
            return a.str() < b.str();
        });
        return layouts.emplace(cls, std::move(result)).first->second;
    }

    Connection* Driver::connectionOf(UObject* a)
    {
        for (auto& [id, connection] : connections) {
            if (connection.pawn == a) {
                return &connection;
            }
        }
        return nullptr;
    }

    bool Driver::owns(const Connection& connection, UObject* a) const
    {
        UObject* top = topOwner(a);
        if (server) {
            return connection.pawn && top == connection.pawn;
        }
        return world->localPlayer && top == world->localPlayer;
    }

    bool Driver::evaluate(UObject* a, const RepCondition& condition)
    {
        if (condition.animation) {
            return actor::DrawType(a) == DT_Mesh && actor::RemoteRole(a) <= ROLE_SimulatedProxy && actor::Role(a) == ROLE_Authority && (!actor::bNetOwner(a) || !actor::bClientAnim(a));
        }
        if (condition.offset >= condition.owner->script.size()) {
            return false;
        }
        Frame frame;
        frame.object = a;
        frame.node = condition.owner;
        frame.code = condition.owner->script.data() + condition.offset;
        uint32_t value = 0;
        frame.step(a, &value);
        return value != 0;
    }

    bool Driver::mappable(const Connection& connection, UObject* object) const
    {
        if (!object || object->destroyed) {
            return true;
        }
        if (isActor(object) && !persistent(object)) {
            return netIds.count(object) && (!server || connection.channels.count(object));
        }
        return true;
    }

    bool Driver::writeObject(Packet& out, Connection& connection, UObject* object)
    {
        if (!object || object->destroyed) {
            out.u8(RefNone);
            return true;
        }
        if (isActor(object) && !persistent(object)) {
            auto found = netIds.find(object);
            if (found == netIds.end() || (server && !connection.channels.count(object))) {
                out.u8(RefNone);
                return false;
            }
            out.u8(RefActor);
            out.varint(found->second);
            return true;
        }
        bool isClass = cast<UClass>(object) != nullptr;
        if (!object->linker && !isClass) {
            out.u8(RefNone);
            return true;
        }
        auto found = connection.sentObjects.find(object);
        if (found == connection.sentObjects.end()) {
            uint32_t id = static_cast<uint32_t>(connection.sentObjects.size() + 1);
            found = connection.sentObjects.emplace(object, id).first;
            Packet define;
            define.u8(MessageDefine);
            define.varint(id);
            define.u8(isClass ? 1 : 0);
            define.str(object->pathName());
            queue(connection, define);
        }
        out.u8(RefStatic);
        out.varint(found->second);
        return true;
    }

    UObject* Driver::readObject(PacketReader& in, Connection& connection)
    {
        uint8_t kind = in.u8();
        if (kind == RefStatic) {
            uint32_t id = in.varint();
            if (id == 0 || id > connection.receivedObjects.size()) {
                unresolved = true;
                return nullptr;
            }
            return connection.receivedObjects[id - 1];
        }
        if (kind == RefActor) {
            uint32_t id = in.varint();
            auto found = actorsById.find(id);
            if (found == actorsById.end()) {
                unresolved = unresolved || !connection.dead.count(id);
                return nullptr;
            }
            if (found->second->destroyed) {
                return nullptr;
            }
            return found->second;
        }
        return nullptr;
    }

    bool Driver::writeValue(Packet& out, Connection& connection, UProperty* property, const uint8_t* data)
    {
        static const Name nameVector("Vector");
        static const Name nameRotator("Rotator");
        static const Name namePlane("Plane");
        switch (property->type) {
        case PropType::Byte:
            out.u8(*data);
            return true;
        case PropType::Int:
            out.signedVarint(*reinterpret_cast<const int32_t*>(data));
            return true;
        case PropType::Bool:
            out.u8(*reinterpret_cast<const uint32_t*>(data) ? 1 : 0);
            return true;
        case PropType::Float:
            out.f32(*reinterpret_cast<const float*>(data));
            return true;
        case PropType::Name:
            out.str(reinterpret_cast<const Name*>(data)->str());
            return true;
        case PropType::Str:
        case PropType::String:
            out.str(*reinterpret_cast<const std::string*>(data));
            return true;
        case PropType::Object:
        case PropType::Class:
            return writeObject(out, connection, *reinterpret_cast<UObject* const*>(data));
        case PropType::Struct: {
            if (property->structType->name == nameVector) {
                const Vector& value = *reinterpret_cast<const Vector*>(data);
                out.signedVarint(quantize(value.x));
                out.signedVarint(quantize(value.y));
                out.signedVarint(quantize(value.z));
                return true;
            }
            if (property->structType->name == nameRotator) {
                const Rotator& value = *reinterpret_cast<const Rotator*>(data);
                uint8_t parts[3] = { static_cast<uint8_t>(value.pitch >> 8), static_cast<uint8_t>(value.yaw >> 8), static_cast<uint8_t>(value.roll >> 8) };
                uint8_t mask = 0;
                for (int i = 0; i < 3; i++) {
                    if (parts[i]) {
                        mask |= static_cast<uint8_t>(1 << i);
                    }
                }
                out.u8(mask);
                for (int i = 0; i < 3; i++) {
                    if (parts[i]) {
                        out.u8(parts[i]);
                    }
                }
                return true;
            }
            if (property->structType->name == namePlane) {
                const Plane& value = *reinterpret_cast<const Plane*>(data);
                out.signedVarint(quantize(value.x));
                out.signedVarint(quantize(value.y));
                out.signedVarint(quantize(value.z));
                out.signedVarint(quantize(value.w));
                return true;
            }
            bool mapped = true;
            for (UProperty* inner : property->structType->properties) {
                for (int32_t i = 0; i < inner->arrayDim; i++) {
                    mapped = writeValue(out, connection, inner, data + inner->offset + static_cast<size_t>(i) * inner->elementSize()) && mapped;
                }
            }
            return mapped;
        }
        case PropType::Array:
        case PropType::Map:
        case PropType::FixedArray:
        case PropType::Pointer:
            return true;
        }
        return true;
    }

    void Driver::readValue(PacketReader& in, Connection& connection, UProperty* property, uint8_t* data)
    {
        static const Name nameVector("Vector");
        static const Name nameRotator("Rotator");
        static const Name namePlane("Plane");
        switch (property->type) {
        case PropType::Byte:
            *data = in.u8();
            return;
        case PropType::Int:
            *reinterpret_cast<int32_t*>(data) = in.signedVarint();
            return;
        case PropType::Bool:
            *reinterpret_cast<uint32_t*>(data) = in.u8() ? 1 : 0;
            return;
        case PropType::Float: {
            float value = in.f32();
            *reinterpret_cast<float*>(data) = server && !std::isfinite(value) ? 0.0f : value;
            return;
        }
        case PropType::Name: {
            std::string text = in.str();
            *reinterpret_cast<Name*>(data) = server ? Name::find(text) : Name(text);
            return;
        }
        case PropType::Str:
        case PropType::String:
            *reinterpret_cast<std::string*>(data) = in.str();
            return;
        case PropType::Object:
        case PropType::Class: {
            UObject* value = readObject(in, connection);
            if (value && property->objectClass && !value->isA(property->objectClass)) {
                value = nullptr;
            }
            if (value && property->type == PropType::Class) {
                UClass* cls = cast<UClass>(value);
                if (!cls || (property->metaClass && !cls->isChildOf(property->metaClass))) {
                    value = nullptr;
                }
            }
            *reinterpret_cast<UObject**>(data) = value;
            return;
        }
        case PropType::Struct:
            if (property->structType->name == nameVector) {
                Vector& value = *reinterpret_cast<Vector*>(data);
                value.x = static_cast<float>(in.signedVarint());
                value.y = static_cast<float>(in.signedVarint());
                value.z = static_cast<float>(in.signedVarint());
                return;
            }
            if (property->structType->name == nameRotator) {
                Rotator& value = *reinterpret_cast<Rotator*>(data);
                uint8_t mask = in.u8();
                int32_t* parts[3] = { &value.pitch, &value.yaw, &value.roll };
                for (int i = 0; i < 3; i++) {
                    *parts[i] = (mask & (1 << i)) ? static_cast<int32_t>(in.u8()) << 8 : 0;
                }
                return;
            }
            if (property->structType->name == namePlane) {
                Plane& value = *reinterpret_cast<Plane*>(data);
                value.x = static_cast<float>(in.signedVarint());
                value.y = static_cast<float>(in.signedVarint());
                value.z = static_cast<float>(in.signedVarint());
                value.w = static_cast<float>(in.signedVarint());
                return;
            }
            for (UProperty* inner : property->structType->properties) {
                for (int32_t i = 0; i < inner->arrayDim; i++) {
                    readValue(in, connection, inner, data + inner->offset + static_cast<size_t>(i) * inner->elementSize());
                }
            }
            return;
        case PropType::Array:
        case PropType::Map:
        case PropType::FixedArray:
        case PropType::Pointer:
            return;
        }
    }

    Channel& Driver::openChannel(Connection& connection, UObject* a)
    {
        Channel& channel = connection.channels[a];
        channel.actor = a;
        channel.cls = a->cls;
        channel.shadow = new uint8_t[a->cls->size + 16];
        a->cls->initValue(channel.shadow);
        a->cls->copyValue(channel.shadow, a->cls->defaultObject->data);
        for (UProperty* property : a->cls->properties) {
            if (property->propertyFlags & CPF_Config) {
                for (int32_t i = 0; i < property->arrayDim; i++) {
                    property->clear(channel.shadow + property->offset + static_cast<size_t>(i) * property->elementSize());
                }
            }
        }
        auto found = netIds.find(a);
        if (found == netIds.end()) {
            found = netIds.emplace(a, nextNetId++).first;
            actorsById[found->second] = a;
        }
        channel.id = found->second;
        channel.relevantTime = time;
        connection.waiting.erase(a);
        size_t entries = layout(a->cls).entries.size();
        channel.lastSequence.assign(entries, -1);
        channel.dirty.assign(entries, 0);
        return channel;
    }

    void Driver::closeChannel(Connection& connection, UObject* a, bool notify)
    {
        auto found = connection.channels.find(a);
        if (found == connection.channels.end()) {
            return;
        }
        if (notify) {
            Packet out;
            out.u8(MessageClose);
            out.varint(found->second.id);
            queue(connection, out);
        }
        found->second.cls->destroyValue(found->second.shadow);
        delete[] found->second.shadow;
        connection.channels.erase(found);
    }

    void Driver::replicateActor(Connection& connection, Channel& channel)
    {
        UObject* a = channel.actor;
        const ClassLayout& classLayout = layout(a->cls);
        uint8_t savedRemoteRole = actor::RemoteRole(a);
        bool initial = channel.initial;
        actor::bNetInitial(a) = initial ? 1 : 0;
        actor::bNetOwner(a) = owns(connection, a) ? 1 : 0;
        if (server && savedRemoteRole == ROLE_AutonomousProxy && !actor::bNetOwner(a)) {
            actor::RemoteRole(a) = ROLE_SimulatedProxy;
        }
        actor::bSimulatedPawn(a) = isPawn(a) && actor::RemoteRole(a) == ROLE_SimulatedProxy ? 1 : 0;

        Packet out;
        out.u8(MessageActor);
        out.varint(channel.id);
        if (initial) {
            if (persistent(a)) {
                out.u8(ActorOpen | ActorStatic);
                writeObject(out, connection, a);
            } else {
                out.u8(ActorOpen);
                writeObject(out, connection, a->cls);
                out.vector(actor::Location(a));
                out.rotator(actor::Rotation(a));
                *reinterpret_cast<Vector*>(channel.shadow + offsets_actor.Location) = actor::Location(a);
                *reinterpret_cast<Rotator*>(channel.shadow + offsets_actor.Rotation) = actor::Rotation(a);
            }
        } else {
            out.u8(0);
        }

        std::vector<uint8_t> evaluated(classLayout.conditions.size(), 0);
        std::vector<uint32_t> written;
        for (uint32_t index = 0; index < classLayout.entries.size(); index++) {
            const RepEntry& entry = classLayout.entries[index];
            bool dirty = index < channel.dirty.size() && channel.dirty[index];
            uint8_t& state = evaluated[entry.condition];
            if (state == 1) {
                continue;
            }
            const uint8_t* source = a->data + entry.offset;
            uint8_t* recent = channel.shadow + entry.offset;
            bool object = entry.property->type == PropType::Object || entry.property->type == PropType::Class;
            if (object) {
                UObject* value = *reinterpret_cast<UObject* const*>(source);
                if (!mappable(connection, value) || (value && value->destroyed)) {
                    value = nullptr;
                }
                if (value == *reinterpret_cast<UObject**>(recent) && !dirty) {
                    continue;
                }
            } else if (entry.property->equals(source, recent) && !dirty) {
                continue;
            }
            if (state == 0) {
                state = evaluate(a, classLayout.conditions[entry.condition]) ? 2 : 1;
            }
            if (state == 1) {
                continue;
            }
            out.varint(entry.wireIndex + 1);
            bool mapped = writeValue(out, connection, entry.property, source);
            if (mapped) {
                entry.property->copy(recent, source);
            } else {
                entry.property->clear(recent);
            }
            if (dirty) {
                channel.dirty[index] = 0;
            }
            written.push_back(index);
        }
        out.varint(0);

        actor::bNetOwner(a) = 0;
        actor::bNetInitial(a) = 0;
        actor::RemoteRole(a) = savedRemoteRole;
        channel.initial = false;
        channel.lastUpdate = time;
        if (initial) {
            queue(connection, out);
        } else if (!written.empty()) {
            uint16_t sequence = 0;
            if (connection.endpoint.add(out.data, sequence)) {
                std::vector<Retirement>& retirements = connection.retirements[sequence];
                for (uint32_t index : written) {
                    channel.lastSequence[index] = sequence;
                    retirements.push_back({ channel.id, index });
                }
            } else {
                for (uint32_t index : written) {
                    channel.dirty[index] = 1;
                }
            }
        }
        if (initial && server && actor::bNetTemporary(a)) {
            connection.temporaries.insert(a);
            closeChannel(connection, a, false);
        }
    }

    bool Driver::relevant(Connection& connection, UObject* a, UObject* viewer, const Vector& viewLocation)
    {
        UObject* pawn = connection.pawn;
        UObject* top = topOwner(a);
        if (actor::bAlwaysRelevant(a) || top == viewer || top == pawn || a == viewer || a == pawn || actor::Instigator(a) == viewer) {
            return true;
        }
        if (actor::AmbientSound(a)) {
            float radius = 25.0f * static_cast<float>(actor::SoundRadius(a));
            if ((actor::Location(a) - actor::Location(viewer)).sizeSquared() < 0.3f * radius * radius) {
                return true;
            }
        }
        UObject* owner = actor::Owner(a);
        if (owner && isPawn(owner) && pawn::Weapon(owner) == a) {
            return relevant(connection, owner, viewer, viewLocation);
        }
        if ((actor::bHidden(a) || actor::bOnlyOwnerSee(a)) && !actor::bBlockPlayers(a) && !actor::AmbientSound(a)) {
            return false;
        }
        return world->fastLineCheck(actor::Location(a), viewLocation);
    }

    void Driver::replicateConnection(Connection& connection)
    {
        UObject* pawn = connection.pawn;
        UObject* viewer = pawn;
        Vector viewLocation = actor::Location(pawn);
        static const Name namePlayerCalcView("PlayerCalcView");
        vm::Call view(pawn, namePlayerCalcView);
        if (view.valid()) {
            view.obj(pawn).v(viewLocation).r(pawn::ViewRotation(pawn)).run();
            UObject* target = *reinterpret_cast<UObject**>(view.parameter(0));
            if (target && !target->destroyed) {
                viewer = target;
            }
            viewLocation = *reinterpret_cast<Vector*>(view.parameter(1));
        }

        struct Candidate {
            UObject* actor = nullptr;
            float priority = 0;
        };

        Vector viewDirection = rotationVector(pawn::ViewRotation(pawn));
        Vector viewPosition = actor::Location(viewer);
        std::vector<Candidate> candidates;
        for (UObject* a : world->actors) {
            if (!a || a->destroyed || actor::RemoteRole(a) == ROLE_None || connection.temporaries.count(a)) {
                continue;
            }
            if (actor::bStatic(a) && !actor::bAlwaysRelevant(a)) {
                continue;
            }
            auto found = connection.channels.find(a);
            Channel* channel = found == connection.channels.end() ? nullptr : &found->second;
            float frequency = actor::NetUpdateFrequency(a);
            if (channel && frequency > 0 && time - channel->lastUpdate < 1.0 / static_cast<double>(frequency)) {
                continue;
            }
            candidates.push_back({ a, netPriority(connection, a, channel, viewPosition, viewDirection, time) });
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.priority > b.priority;
        });
        for (const Candidate& candidate : candidates) {
            if (!connection.endpoint.ready()) {
                break;
            }
            UObject* a = candidate.actor;
            if (a->destroyed) {
                continue;
            }
            auto found = connection.channels.find(a);
            Channel* channel = found == connection.channels.end() ? nullptr : &found->second;
            bool visible = false;
            if (!channel || time - channel->relevantTime > RelevantCheckInterval) {
                visible = relevant(connection, a, viewer, viewLocation);
            }
            if (visible || (channel && time - channel->relevantTime < RelevantTimeout)) {
                if (!channel) {
                    channel = &openChannel(connection, a);
                }
                if (visible) {
                    channel->relevantTime = time;
                }
                replicateActor(connection, *channel);
            } else if (channel) {
                closeChannel(connection, a, true);
            } else {
                connection.waiting.erase(a);
            }
        }
    }

    void Driver::possess(UObject* pawn)
    {
        UObject* viewport = world->viewport;
        playerpawn::Player(pawn) = viewport;
        player::Actor(viewport) = pawn;
        player::CurrentNetSpeed(viewport) = rate;
        actor::Role(pawn) = ROLE_AutonomousProxy;
        world->localPlayer = pawn;
        vm::event(pawn, Name("Possess"));
        console::resetInput(pawn);
    }

    void Driver::handleActor(Connection& connection, PacketReader& in)
    {
        uint32_t id = in.varint();
        uint8_t flags = in.u8();
        if (in.failed) {
            return;
        }
        auto known = actorsById.find(id);
        UObject* a = known == actorsById.end() ? nullptr : known->second;
        if (server && (!a || !connection.channels.count(a) || (flags & ActorOpen))) {
            return;
        }
        if (!a && !(flags & ActorOpen)) {
            unresolved = unresolved || !connection.dead.count(id);
            return;
        }
        if (flags & ActorOpen) {
            if (flags & ActorStatic) {
                a = readObject(in, connection);
                if (!a || !isActor(a) || a->destroyed) {
                    connection.dead.insert(id);
                    return;
                }
            } else {
                UClass* cls = cast<UClass>(readObject(in, connection));
                Vector location = in.vector();
                Rotator rotation = in.rotator();
                if (in.failed || !cls || !cls->isChildOf(engineClasses().actor)) {
                    connection.dead.insert(id);
                    return;
                }
                a = world->spawn(cls, nullptr, Name(), location, rotation, nullptr, true, nullptr, true);
                if (!a) {
                    connection.dead.insert(id);
                    return;
                }
            }
            actorsById[id] = a;
            netIds[a] = id;
            if (!connection.channels.count(a)) {
                Channel& channel = openChannel(connection, a);
                channel.initial = false;
            }
            if (actor::bNetTemporary(a)) {
                closeChannel(connection, a, false);
                actorsById.erase(id);
                netIds.erase(a);
            }
        }
        if (!a || a->destroyed) {
            return;
        }
        const ClassLayout& classLayout = layout(a->cls);
        auto found = connection.channels.find(a);
        uint8_t* shadow = found == connection.channels.end() ? nullptr : found->second.shadow;
        bool owned = owns(connection, a);
        if (!server) {
            actor::bNetOwner(a) = owned ? 1 : 0;
        }
        ReceiveState saved = preReceive(a);
        std::vector<uint8_t> scratch;
        while (true) {
            uint32_t index = in.varint();
            if (in.failed || index == 0 || index > classLayout.entries.size()) {
                break;
            }
            const RepEntry& entry = classLayout.entries[index - 1];
            bool accept = true;
            if (server) {
                std::swap(actor::Role(a), actor::RemoteRole(a));
                accept = owned && evaluate(a, classLayout.conditions[entry.condition]);
                std::swap(actor::Role(a), actor::RemoteRole(a));
            }
            if (!accept) {
                scratch.assign(entry.property->elementSize() + 16, 0);
                entry.property->construct(scratch.data());
                readValue(in, connection, entry.property, scratch.data());
                entry.property->destroy(scratch.data());
                continue;
            }
            readValue(in, connection, entry.property, a->data + entry.offset);
            if (shadow) {
                entry.property->copy(shadow + entry.offset, a->data + entry.offset);
            }
        }
        postReceive(*world, a, saved);
        if ((flags & ActorOpen) && !(flags & ActorStatic)) {
            vm::event(a, Name("PostNetBeginPlay"));
        }
        if (!server && !world->localPlayer && actor::bNetOwner(a) && isPlayerPawn(a)) {
            possess(a);
        }
    }

    void Driver::handleClose(PacketReader& in)
    {
        uint32_t id = in.varint();
        for (auto& [connectionId, connection] : connections) {
            connection.dead.erase(id);
        }
        auto known = actorsById.find(id);
        if (in.failed || known == actorsById.end()) {
            return;
        }
        UObject* a = known->second;
        for (auto& [connectionId, connection] : connections) {
            closeChannel(connection, a, false);
        }
        actorsById.erase(known);
        netIds.erase(a);
        if (!persistent(a) && !a->destroyed) {
            receiving = true;
            world->destroy(a);
            receiving = false;
        }
    }

    bool Driver::canDestroy(UObject* a) const
    {
        return server || receiving || actor::Role(a) == ROLE_Authority || actor::bNetTemporary(a);
    }

    void Driver::actorDestroyed(UObject* a)
    {
        for (auto& [id, connection] : connections) {
            closeChannel(connection, a, server);
            connection.temporaries.erase(a);
            connection.waiting.erase(a);
            if (connection.pawn == a) {
                connection.pawn = nullptr;
            }
        }
        auto found = netIds.find(a);
        if (found != netIds.end()) {
            actorsById.erase(found->second);
            netIds.erase(found);
        }
    }

    bool Driver::remoteCall(UObject* context, UFunction* function, uint8_t* locals)
    {
        if (!world || !isActor(context) || (function->functionFlags & FUNC_Static) || actor::bDeleteMe(context)) {
            return false;
        }
        uint8_t role = actor::Role(context);
        bool absorb = role <= ROLE_SimulatedProxy && !(function->functionFlags & FUNC_Simulated);
        if (!(function->functionFlags & FUNC_Net)) {
            return absorb;
        }
        Connection* target = nullptr;
        if (role == ROLE_Authority) {
            if (!server) {
                return absorb;
            }
            target = connectionOf(topOwner(context));
            if (!target) {
                return absorb;
            }
        }
        UFunction* root = rootFunction(function);
        RepCondition condition;
        condition.owner = cast<UClass>(root->outer);
        condition.offset = root->replicationOffset;
        if (!condition.owner || !evaluate(context, condition)) {
            return absorb;
        }
        if (!server) {
            if (connections.empty()) {
                return true;
            }
            target = &connections.begin()->second;
        }
        auto found = target->channels.find(context);
        if (found == target->channels.end()) {
            if (!server) {
                return true;
            }
            replicateActor(*target, openChannel(*target, context));
            found = target->channels.find(context);
            if (found == target->channels.end()) {
                return true;
            }
        }
        const ClassLayout& classLayout = layout(context->cls);
        auto slot = std::find(classLayout.functions.begin(), classLayout.functions.end(), function->name);
        if (slot == classLayout.functions.end()) {
            return true;
        }
        Packet out;
        out.u8(MessageCall);
        out.varint(found->second.id);
        out.varint(static_cast<uint32_t>(slot - classLayout.functions.begin()));
        for (UProperty* param : function->params) {
            writeValue(out, *target, param, locals + param->offset);
        }
        if (root->functionFlags & FUNC_NetReliable) {
            queue(*target, out);
        } else if (target->endpoint.ready()) {
            uint16_t sequence = 0;
            target->endpoint.add(out.data, sequence);
        }
        return true;
    }

    void Driver::handleCall(Connection& connection, PacketReader& in)
    {
        uint32_t id = in.varint();
        uint32_t slot = in.varint();
        auto known = actorsById.find(id);
        if (!in.failed && known == actorsById.end() && !connection.dead.count(id)) {
            unresolved = true;
        }
        if (in.failed || known == actorsById.end() || known->second->destroyed) {
            return;
        }
        UObject* a = known->second;
        const ClassLayout& classLayout = layout(a->cls);
        if (slot >= classLayout.functions.size()) {
            return;
        }
        UFunction* function = vm::findFunction(a, classLayout.functions[slot]);
        if (!function) {
            return;
        }
        if (server) {
            if (!owns(connection, a) || !connection.channels.count(a)) {
                return;
            }
            UFunction* root = rootFunction(function);
            RepCondition condition;
            condition.owner = cast<UClass>(root->outer);
            condition.offset = root->replicationOffset;
            std::swap(actor::Role(a), actor::RemoteRole(a));
            bool wanted = condition.owner && evaluate(a, condition);
            std::swap(actor::Role(a), actor::RemoteRole(a));
            if (!wanted) {
                return;
            }
        }
        vm::Call call(a, function);
        if (!call.valid()) {
            return;
        }
        for (size_t i = 0; i < function->params.size(); i++) {
            readValue(in, connection, function->params[i], call.parameter(i));
        }
        if (in.failed) {
            return;
        }
        call.run();
    }
}
