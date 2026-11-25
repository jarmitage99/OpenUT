#include "engine/net.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {
    struct Link {
        struct Flight {
            double arrival = 0;
            std::vector<uint8_t> data;
        };

        explicit Link(uint32_t seed)
            : random(seed)
        {
        }

        void send(double now, const std::vector<uint8_t>& data)
        {
            std::uniform_real_distribution<double> chance(0.0, 1.0);
            if (chance(random) < loss) {
                return;
            }
            std::uniform_real_distribution<double> delay(minDelay, maxDelay);
            flights.push_back({ now + delay(random), data });
            if (chance(random) < duplicate) {
                flights.push_back({ now + delay(random), data });
            }
        }

        std::vector<std::vector<uint8_t>> arrive(double now)
        {
            std::stable_sort(flights.begin(), flights.end(), [](const Flight& a, const Flight& b) {
                return a.arrival < b.arrival;
            });
            std::vector<std::vector<uint8_t>> landed;
            size_t count = 0;
            while (count < flights.size() && flights[count].arrival <= now) {
                landed.push_back(flights[count].data);
                count++;
            }
            flights.erase(flights.begin(), flights.begin() + static_cast<std::ptrdiff_t>(count));
            return landed;
        }

        std::mt19937 random;
        double loss = 0;
        double duplicate = 0;
        double minDelay = 0.05;
        double maxDelay = 0.05;
        std::vector<Flight> flights;
    };

    struct Side {
        ut::net::Endpoint endpoint;
        std::vector<std::vector<uint8_t>> messages;
        std::vector<std::vector<uint8_t>> items;
        size_t staleItems = 0;
    };

    void deliver(Side& side, Link& link, double now)
    {
        for (const std::vector<uint8_t>& packet : link.arrive(now)) {
            ut::net::Endpoint::Received received;
            REQUIRE(side.endpoint.receive(packet.data(), packet.size(), received));
            side.messages.insert(side.messages.end(), received.messages.begin(), received.messages.end());
            if (received.fresh) {
                side.endpoint.accept(received.sequence);
                side.items.insert(side.items.end(), received.items.begin(), received.items.end());
            }
        }
    }

    void transmit(Side& side, Link& link, double now)
    {
        for (const std::vector<uint8_t>& packet : side.endpoint.flush()) {
            link.send(now, packet);
        }
    }

    std::vector<uint8_t> numbered(uint32_t number, size_t padding)
    {
        std::vector<uint8_t> data { static_cast<uint8_t>(number), static_cast<uint8_t>(number >> 8), static_cast<uint8_t>(number >> 16), static_cast<uint8_t>(number >> 24) };
        data.resize(4 + padding, static_cast<uint8_t>(number * 7));
        return data;
    }

    uint32_t numberOf(const std::vector<uint8_t>& data)
    {
        return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
    }

    std::vector<uint8_t> header(uint16_t sequence, uint16_t expected, uint8_t flags)
    {
        return { static_cast<uint8_t>(sequence), static_cast<uint8_t>(sequence >> 8), 0, 0, 0, 0, 0, 0, static_cast<uint8_t>(expected), static_cast<uint8_t>(expected >> 8), flags };
    }
}

TEST_CASE("reliable messages arrive once and in order through loss, duplication and reordering")
{
    for (uint32_t seed : { 1u, 2u, 3u }) {
        INFO("seed " << seed);
        Link there(seed);
        Link back(seed + 100);
        for (Link* link : { &there, &back }) {
            link->loss = 0.25;
            link->duplicate = 0.1;
            link->minDelay = 0.02;
            link->maxDelay = 0.2;
        }
        Side sender;
        Side receiver;
        std::mt19937 sizes(seed);
        std::uniform_int_distribution<size_t> padding(0, 300);
        std::vector<std::vector<uint8_t>> expected;
        const uint32_t total = 3000;
        double now = 0;
        const double step = 1.0 / 60.0;
        uint32_t tick = 0;
        while (receiver.messages.size() < total && now < 600) {
            now += step;
            tick++;
            sender.endpoint.update(now, step, 64000);
            receiver.endpoint.update(now, step, 64000);
            for (int i = 0; i < 2 && expected.size() < total; i++) {
                expected.push_back(numbered(static_cast<uint32_t>(expected.size()), padding(sizes)));
                REQUIRE(sender.endpoint.queueReliable(expected.back()));
            }
            uint16_t sequence = 0;
            if (sender.endpoint.ready()) {
                sender.endpoint.add(numbered(tick, 8), sequence);
            }
            transmit(sender, there, now);
            transmit(receiver, back, now);
            deliver(receiver, there, now);
            deliver(sender, back, now);
        }
        REQUIRE(receiver.messages.size() == total);
        CHECK(receiver.messages == expected);
        REQUIRE(receiver.items.size() > 100);
        for (size_t i = 1; i < receiver.items.size(); i++) {
            REQUIRE(numberOf(receiver.items[i]) > numberOf(receiver.items[i - 1]));
        }
        CHECK(sender.endpoint.packetsLost > 0);
        CHECK(sender.endpoint.messagesResent > 0);
    }
}

TEST_CASE("sequence numbers and message ids wrap around without losing order")
{
    Link there(7);
    Link back(8);
    Side sender;
    Side receiver;
    double now = 0;
    const double step = 1.0 / 60.0;
    const uint32_t total = 70000;
    for (uint32_t tick = 0; tick < total; tick++) {
        now += step;
        sender.endpoint.update(now, step, 64000);
        receiver.endpoint.update(now, step, 64000);
        REQUIRE(sender.endpoint.queueReliable(numbered(tick, 0)));
        uint16_t sequence = 0;
        REQUIRE(sender.endpoint.add(numbered(tick, 0), sequence));
        transmit(sender, there, now);
        transmit(receiver, back, now);
        deliver(receiver, there, now);
        deliver(sender, back, now);
    }
    for (int i = 0; i < 30; i++) {
        now += step;
        sender.endpoint.update(now, step, 64000);
        receiver.endpoint.update(now, step, 64000);
        transmit(sender, there, now);
        transmit(receiver, back, now);
        deliver(receiver, there, now);
        deliver(sender, back, now);
    }
    REQUIRE(receiver.messages.size() == total);
    REQUIRE(receiver.items.size() == total);
    for (uint32_t i = 0; i < total; i++) {
        REQUIRE(numberOf(receiver.messages[i]) == i);
        REQUIRE(numberOf(receiver.items[i]) == i);
    }
    CHECK(sender.endpoint.packetsLost == 0);
    CHECK(sender.endpoint.messagesResent == 0);
    CHECK(sender.endpoint.queuedMessages() == 0);
}

TEST_CASE("packets the receiver did not accept are reported lost and the rest acknowledged")
{
    ut::net::Endpoint sender;
    ut::net::Endpoint receiver;
    std::vector<std::vector<uint8_t>> packets;
    for (int i = 0; i < 3; i++) {
        sender.update(i * 0.05, 0.05, 64000);
        uint16_t sequence = 0;
        REQUIRE(sender.add({ static_cast<uint8_t>(i) }, sequence));
        CHECK(sequence == i);
        std::vector<std::vector<uint8_t>> sent = sender.flush();
        REQUIRE(sent.size() == 1);
        packets.push_back(sent[0]);
    }
    for (int i = 0; i < 3; i++) {
        ut::net::Endpoint::Received received;
        REQUIRE(receiver.receive(packets[static_cast<size_t>(i)].data(), packets[static_cast<size_t>(i)].size(), received));
        CHECK(received.fresh);
        CHECK(received.sequence == i);
        if (i != 1) {
            receiver.accept(received.sequence);
        }
    }
    receiver.update(0.2, 0.05, 64000);
    std::vector<std::vector<uint8_t>> acks = receiver.flush();
    REQUIRE(acks.size() == 1);
    sender.update(0.2, 0.05, 64000);
    ut::net::Endpoint::Received received;
    REQUIRE(sender.receive(acks[0].data(), acks[0].size(), received));
    std::sort(sender.acked.begin(), sender.acked.end());
    CHECK(sender.acked == std::vector<uint16_t> { 0, 2 });
    CHECK(sender.lost == std::vector<uint16_t> { 1 });
    CHECK(sender.packetsLost == 1);
}

TEST_CASE("a packet that arrives after a newer one keeps its reliable messages but drops its state")
{
    ut::net::Endpoint sender;
    ut::net::Endpoint receiver;
    sender.update(0, 0.05, 64000);
    REQUIRE(sender.queueReliable({ 42 }));
    uint16_t sequence = 0;
    REQUIRE(sender.add({ 1 }, sequence));
    std::vector<std::vector<uint8_t>> first = sender.flush();
    sender.update(0.05, 0.05, 64000);
    REQUIRE(sender.add({ 2 }, sequence));
    std::vector<std::vector<uint8_t>> second = sender.flush();
    REQUIRE(first.size() == 1);
    REQUIRE(second.size() == 1);

    ut::net::Endpoint::Received received;
    REQUIRE(receiver.receive(second[0].data(), second[0].size(), received));
    CHECK(received.fresh);
    CHECK(received.items == std::vector<std::vector<uint8_t>> { { 2 } });
    CHECK(received.messages.empty());

    REQUIRE(receiver.receive(first[0].data(), first[0].size(), received));
    CHECK_FALSE(received.fresh);
    CHECK(received.items.empty());
    CHECK(received.messages == std::vector<std::vector<uint8_t>> { { 42 } });

    REQUIRE(receiver.receive(first[0].data(), first[0].size(), received));
    CHECK(received.messages.empty());
}

TEST_CASE("malformed packets are rejected before anything is delivered")
{
    ut::net::Endpoint receiver;
    ut::net::Endpoint::Received received;
    std::vector<uint8_t> valid = header(0, 0, 0);
    CHECK(receiver.receive(valid.data(), valid.size(), received));

    std::vector<std::vector<uint8_t>> broken;
    broken.push_back({ 1, 2, 3 });
    std::vector<uint8_t> unknownKind = header(1, 0, 0);
    unknownKind.push_back(9);
    broken.push_back(unknownKind);
    std::vector<uint8_t> overrun = header(2, 0, 0);
    overrun.insert(overrun.end(), { 2, 5, 'a' });
    broken.push_back(overrun);
    std::vector<uint8_t> huge = header(3, 0, 0);
    huge.insert(huge.end(), { 2, 0xff, 0xff, 0xff, 0xff, 0x0f });
    broken.push_back(huge);
    std::vector<uint8_t> truncatedId = header(4, 0, 0);
    truncatedId.insert(truncatedId.end(), { 1, 7 });
    broken.push_back(truncatedId);
    broken.push_back(header(5, 5, 0));
    broken.push_back(header(6, 0, 0x80));
    std::vector<uint8_t> goodThenBad = header(7, 0, 0);
    goodThenBad.insert(goodThenBad.end(), { 1, 0, 0, 1, 'x', 9 });
    broken.push_back(goodThenBad);
    for (const std::vector<uint8_t>& packet : broken) {
        CHECK_FALSE(receiver.receive(packet.data(), packet.size(), received));
        CHECK(received.messages.empty());
        CHECK(received.items.empty());
    }

    std::vector<uint8_t> good = header(8, 0, 0);
    good.insert(good.end(), { 1, 0, 0, 1, 'x' });
    REQUIRE(receiver.receive(good.data(), good.size(), received));
    CHECK(received.messages == std::vector<std::vector<uint8_t>> { { 'x' } });
}

TEST_CASE("an endpoint never sends more than its byte budget")
{
    ut::net::Endpoint sender;
    const int32_t rate = 20000;
    const double step = 0.02;
    double now = 0;
    size_t largest = 0;
    for (int tick = 0; tick < 500; tick++) {
        now += step;
        sender.update(now, step, rate);
        uint16_t sequence = 0;
        while (sender.ready()) {
            REQUIRE(sender.add(std::vector<uint8_t>(100, 1), sequence));
        }
        for (const std::vector<uint8_t>& packet : sender.flush()) {
            largest = std::max(largest, packet.size());
        }
    }
    CHECK(sender.bytesSent <= static_cast<uint64_t>(rate * 10) + 2 * ut::net::MaxPacketSize);
    CHECK(sender.bytesSent >= static_cast<uint64_t>(rate * 9));
    CHECK(largest <= ut::net::MaxPacketSize);
}

TEST_CASE("round trip time is measured and paces retransmission")
{
    Link there(3);
    Link back(4);
    there.minDelay = there.maxDelay = 0.1;
    back.minDelay = back.maxDelay = 0.1;
    Side sender;
    Side receiver;
    double now = 0;
    const double step = 1.0 / 60.0;
    for (int tick = 0; tick < 300; tick++) {
        now += step;
        sender.endpoint.update(now, step, 64000);
        receiver.endpoint.update(now, step, 64000);
        REQUIRE(sender.endpoint.queueReliable(numbered(static_cast<uint32_t>(tick), 0)));
        transmit(sender, there, now);
        transmit(receiver, back, now);
        deliver(receiver, there, now);
        deliver(sender, back, now);
    }
    CHECK(sender.endpoint.rtt() > 0.19);
    CHECK(sender.endpoint.rtt() < 0.23);
    CHECK(sender.endpoint.messagesResent == 0);
    CHECK(receiver.messages.size() > 270);
}

TEST_CASE("the reliable queue refuses messages once it is full")
{
    ut::net::Endpoint sender;
    for (size_t i = 0; i < ut::net::MaxQueuedMessages; i++) {
        REQUIRE(sender.queueReliable({ 1 }));
    }
    CHECK_FALSE(sender.queueReliable({ 1 }));
    CHECK_FALSE(sender.queueReliable(std::vector<uint8_t>(ut::net::MaxMessageSize + 1, 0)));
}

TEST_CASE("sequence comparison handles wraparound")
{
    CHECK(ut::net::newer(1, 0));
    CHECK_FALSE(ut::net::newer(0, 1));
    CHECK_FALSE(ut::net::newer(5, 5));
    CHECK(ut::net::newer(0, 65535));
    CHECK(ut::net::newer(10, 65530));
    CHECK_FALSE(ut::net::newer(65530, 10));
    CHECK(ut::net::newer(32767, 0));
    CHECK_FALSE(ut::net::newer(32768, 0));
}

TEST_CASE("unreliable items never overtake reliable messages still waiting for budget")
{
    ut::net::Endpoint sender;
    ut::net::Endpoint receiver;
    double now = 0;
    sender.update(now, 0.05, 2000);
    for (uint8_t i = 0; i < 5; i++) {
        REQUIRE(sender.queueReliable(std::vector<uint8_t>(1000, i)));
    }
    size_t messagesWhenItemArrived = 0;
    bool itemArrived = false;
    bool refusedFirst = false;
    for (int tick = 0; tick < 100 && !itemArrived; tick++) {
        uint16_t sequence = 0;
        bool added = sender.add({ 7 }, sequence);
        if (tick == 0) {
            refusedFirst = !added;
        }
        size_t messages = 0;
        for (const std::vector<uint8_t>& packet : sender.flush()) {
            ut::net::Endpoint::Received received;
            REQUIRE(receiver.receive(packet.data(), packet.size(), received));
            receiver.accept(received.sequence);
            messages += received.messages.size();
            if (!received.items.empty()) {
                itemArrived = true;
            }
        }
        messagesWhenItemArrived += messages;
        now += 0.05;
        receiver.update(now, 0.05, 2000);
        for (const std::vector<uint8_t>& packet : receiver.flush()) {
            ut::net::Endpoint::Received received;
            REQUIRE(sender.receive(packet.data(), packet.size(), received));
        }
        sender.update(now, 0.05, 2000);
    }
    CHECK(refusedFirst);
    REQUIRE(itemArrived);
    CHECK(messagesWhenItemArrived == 5);
}
