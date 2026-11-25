#pragma once

#include "core/math.h"
#include "core/object.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ut {
    class World;

    constexpr uint8_t ROLE_None = 0;
    constexpr uint8_t ROLE_DumbProxy = 1;
    constexpr uint8_t ROLE_SimulatedProxy = 2;
    constexpr uint8_t ROLE_AutonomousProxy = 3;
    constexpr uint8_t ROLE_Authority = 4;

    constexpr uint8_t NM_Standalone = 0;
    constexpr uint8_t NM_DedicatedServer = 1;
    constexpr uint8_t NM_ListenServer = 2;
    constexpr uint8_t NM_Client = 3;

    namespace net {
        constexpr uint32_t ProtocolVersion = 2;
        constexpr int DefaultPort = 7777;
        constexpr size_t MaxPacketSize = 1200;
        constexpr size_t MaxMessageSize = 65536;
        constexpr size_t PacketOverhead = 28;
        constexpr size_t PacketWindow = 256;
        constexpr uint16_t ReliableWindow = 1024;
        constexpr size_t MaxQueuedMessages = 4096;
        constexpr double PacketTimeout = 1.0;
        constexpr double MinResendDelay = 0.1;

        bool newer(uint16_t a, uint16_t b);

        struct Event {
            enum class Kind : uint8_t {
                Connected,
                Disconnected,
                Message,
            };

            Kind kind = Kind::Message;
            uint32_t connection = 0;
            std::vector<uint8_t> data;
        };

        class Transport {
        public:
            virtual ~Transport() = default;
            virtual void send(uint32_t connection, const std::vector<uint8_t>& data) = 0;
            virtual void close(uint32_t connection) = 0;
            virtual std::vector<Event> poll() = 0;
            virtual std::string address(uint32_t connection) = 0;
            virtual int ping(uint32_t connection) = 0;
            virtual int loss(uint32_t connection) = 0;
        };

        class Packet {
        public:
            void u8(uint8_t value);
            void varint(uint32_t value);
            void signedVarint(int32_t value);
            void i32(int32_t value);
            void f32(float value);
            void str(std::string_view value);
            void vector(const Vector& value);
            void rotator(const Rotator& value);

            std::vector<uint8_t> data;
        };

        class PacketReader {
        public:
            PacketReader(const uint8_t* begin, size_t size);

            uint8_t u8();
            uint32_t varint();
            int32_t signedVarint();
            int32_t i32();
            float f32();
            std::string str();
            Vector vector();
            Rotator rotator();
            bool atEnd() const;

            bool failed = false;

        private:
            const uint8_t* cursor;
            const uint8_t* end;
        };

        class Endpoint {
        public:
            struct Received {
                uint16_t sequence = 0;
                bool fresh = false;
                std::vector<std::vector<uint8_t>> messages;
                std::vector<std::vector<uint8_t>> items;
            };

            Endpoint();

            bool queueReliable(std::vector<uint8_t> message);
            void update(double now, double seconds, int32_t rate);
            bool ready() const;
            bool add(const std::vector<uint8_t>& item, uint16_t& sequence);
            std::vector<std::vector<uint8_t>> flush();
            bool receive(const uint8_t* data, size_t size, Received& out);
            void accept(uint16_t sequence);
            double rtt() const;
            size_t queuedMessages() const;

            std::vector<uint16_t> acked;
            std::vector<uint16_t> lost;
            uint64_t bytesSent = 0;
            uint64_t packetsSent = 0;
            uint64_t packetsLost = 0;
            uint64_t messagesResent = 0;

        private:
            struct Outgoing {
                uint16_t id = 0;
                std::vector<uint8_t> data;
                double sentTime = -1;
                uint32_t sends = 0;
                bool acked = false;
            };

            struct SentPacket {
                bool pending = false;
                uint16_t sequence = 0;
                double time = 0;
                std::vector<uint16_t> messages;
            };

            struct Incoming {
                bool present = false;
                uint16_t id = 0;
                std::vector<uint8_t> data;
            };

            void open();
            void finish();
            void place(Outgoing& message);
            void fillReliable();
            void ackPacket(SentPacket& packet);
            void losePacket(SentPacket& packet);
            void ackMessage(uint16_t id);
            void deliver(uint16_t id, const uint8_t* data, size_t size, Received& out);

            double now = 0;
            double budget = static_cast<double>(MaxPacketSize);
            double smoothedRtt = 0.25;
            bool rttMeasured = false;
            uint16_t nextSequence = 0;
            uint16_t nextMessage = 0;
            uint16_t expectedMessage = 0;
            uint16_t newestSeen = 0;
            bool seenAny = false;
            uint16_t newestAccepted = 0;
            bool acceptedAny = false;
            bool acksOwed = false;
            size_t packetsThisTick = 0;
            size_t unsentMessages = 0;
            bool building = false;
            uint16_t buildingSequence = 0;
            std::vector<uint8_t> body;
            std::vector<uint16_t> bodyMessages;
            std::vector<std::vector<uint8_t>> finished;
            std::deque<Outgoing> outgoing;
            std::array<SentPacket, PacketWindow> sent;
            std::array<uint32_t, PacketWindow> accepted;
            std::array<Incoming, ReliableWindow> incoming;
        };

        constexpr size_t MaxConnections = 128;
        constexpr int32_t MaxIncomingRate = 65536;
        constexpr double PendingTimeout = 30.0;
        constexpr size_t MaxDefinitions = 65536;

        struct RepEntry {
            UProperty* property = nullptr;
            size_t offset = 0;
            size_t condition = 0;
            uint32_t wireIndex = 0;
        };

        struct RepCondition {
            UClass* owner = nullptr;
            uint16_t offset = 0;
            bool animation = false;
        };

        struct ClassLayout {
            std::vector<RepEntry> entries;
            std::vector<RepCondition> conditions;
            std::vector<Name> functions;
        };

        struct Channel {
            UObject* actor = nullptr;
            UClass* cls = nullptr;
            uint32_t id = 0;
            uint8_t* shadow = nullptr;
            bool initial = true;
            double relevantTime = 0;
            double lastUpdate = 0;
            std::vector<int32_t> lastSequence;
            std::vector<uint8_t> dirty;
        };

        struct Retirement {
            uint32_t channel = 0;
            uint32_t entry = 0;
        };

        struct Connection {
            uint32_t id = 0;
            UObject* player = nullptr;
            UObject* pawn = nullptr;
            bool greeted = false;
            bool welcomed = false;
            bool joined = false;
            bool failed = false;
            double created = 0;
            double inbound = static_cast<double>(MaxIncomingRate);
            uint64_t bytesReceived = 0;
            uint64_t packetsDropped = 0;
            Endpoint endpoint;
            std::unordered_map<UObject*, Channel> channels;
            std::unordered_map<UObject*, uint32_t> sentObjects;
            std::vector<UObject*> receivedObjects;
            std::unordered_set<UObject*> temporaries;
            std::unordered_set<uint32_t> dead;
            std::unordered_map<UObject*, double> waiting;
            std::unordered_map<uint16_t, std::vector<Retirement>> retirements;
        };

        struct ConnectionStats {
            uint32_t connection = 0;
            UObject* pawn = nullptr;
            uint64_t bytesSent = 0;
            uint64_t packetsSent = 0;
            uint64_t packetsLost = 0;
            uint64_t messagesResent = 0;
            uint64_t bytesReceived = 0;
            uint64_t packetsDropped = 0;
            size_t channels = 0;
            size_t queuedMessages = 0;
        };

        class Driver {
        public:
            Driver(Transport& link, bool isServer);
            ~Driver();
            Driver(const Driver&) = delete;
            Driver& operator=(const Driver&) = delete;

            void setWorld(World* target);
            void tick(float deltaSeconds);
            void join(const std::string& options);
            bool remoteCall(UObject* context, UFunction* function, uint8_t* locals);
            void actorDestroyed(UObject* actor);
            bool canDestroy(UObject* actor) const;
            std::string playerAddress(UObject* pawn);
            int playerPing(UObject* pawn);
            int playerLoss(UObject* pawn);
            std::vector<UObject*> remotePlayers() const;
            size_t connectionCount() const;
            std::vector<ConnectionStats> stats() const;

            const bool server;
            std::string welcomeUrl;
            std::string failure;
            bool closed = false;
            int32_t rate = 20000;
            float tickRate = 20;

        private:
            const ClassLayout& layout(UClass* cls);
            Connection* connectionOf(UObject* actor);
            bool owns(const Connection& connection, UObject* actor) const;
            void queue(Connection& connection, const Packet& packet);
            void receive(Connection& connection, const std::vector<uint8_t>& data);
            void retire(Connection& connection);
            void transmit(Connection& connection);
            void replicate();
            void handle(Connection& connection, const std::vector<uint8_t>& data);
            void handleHello(Connection& connection, PacketReader& in);
            void handleWelcome(PacketReader& in);
            void handleJoin(Connection& connection, PacketReader& in);
            void handleDefine(Connection& connection, PacketReader& in);
            void handleActor(Connection& connection, PacketReader& in);
            void handleClose(PacketReader& in);
            void handleCall(Connection& connection, PacketReader& in);
            void welcome(Connection& connection);
            void drop(Connection& connection, bool notify);
            void replicateConnection(Connection& connection);
            bool relevant(Connection& connection, UObject* actor, UObject* viewer, const Vector& viewLocation);
            Channel& openChannel(Connection& connection, UObject* actor);
            void closeChannel(Connection& connection, UObject* actor, bool notify);
            void replicateActor(Connection& connection, Channel& channel);
            bool evaluate(UObject* actor, const RepCondition& condition);
            bool mappable(const Connection& connection, UObject* object) const;
            bool writeObject(Packet& out, Connection& connection, UObject* object);
            UObject* readObject(PacketReader& in, Connection& connection);
            bool writeValue(Packet& out, Connection& connection, UProperty* property, const uint8_t* data);
            void readValue(PacketReader& in, Connection& connection, UProperty* property, uint8_t* data);
            void possess(UObject* pawn);

            Transport& transport;
            World* world = nullptr;
            std::unordered_map<uint32_t, Connection> connections;
            std::unordered_map<UClass*, ClassLayout> layouts;
            std::unordered_map<UObject*, uint32_t> netIds;
            std::unordered_map<uint32_t, UObject*> actorsById;
            uint32_t nextNetId = 1;
            double time = 0;
            double lastReplication = -1;
            bool receiving = false;
            bool unresolved = false;
        };
    }
}
