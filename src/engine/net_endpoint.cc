#include "engine/net.h"

#include <algorithm>

namespace ut::net {
    namespace {
        constexpr uint8_t ItemReliable = 1;
        constexpr uint8_t ItemUnreliable = 2;
        constexpr uint8_t FlagAcked = 1;
        constexpr size_t HeaderSize = 11;
        constexpr size_t AckBits = 32;
        constexpr uint32_t NoSequence = 0xffffffff;

        void put16(std::vector<uint8_t>& out, uint16_t value)
        {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
        }

        void put32(std::vector<uint8_t>& out, uint32_t value)
        {
            put16(out, static_cast<uint16_t>(value));
            put16(out, static_cast<uint16_t>(value >> 16));
        }

        void putVarint(std::vector<uint8_t>& out, uint32_t value)
        {
            while (value >= 0x80) {
                out.push_back(static_cast<uint8_t>(value | 0x80));
                value >>= 7;
            }
            out.push_back(static_cast<uint8_t>(value));
        }

        size_t varintSize(uint32_t value)
        {
            size_t size = 1;
            while (value >= 0x80) {
                value >>= 7;
                size++;
            }
            return size;
        }

        uint16_t get16(const uint8_t* data)
        {
            return static_cast<uint16_t>(data[0] | (data[1] << 8));
        }

        uint32_t get32(const uint8_t* data)
        {
            return static_cast<uint32_t>(get16(data)) | (static_cast<uint32_t>(get16(data + 2)) << 16);
        }

        bool getVarint(const uint8_t*& cursor, const uint8_t* end, uint32_t& value)
        {
            value = 0;
            for (int shift = 0; shift < 35; shift += 7) {
                if (cursor >= end) {
                    return false;
                }
                uint8_t byte = *cursor++;
                value |= static_cast<uint32_t>(byte & 0x7f) << shift;
                if (!(byte & 0x80)) {
                    return true;
                }
            }
            return false;
        }

        struct Piece {
            bool reliable = false;
            uint16_t id = 0;
            const uint8_t* data = nullptr;
            size_t size = 0;
        };
    }

    bool newer(uint16_t a, uint16_t b)
    {
        return a != b && static_cast<uint16_t>(a - b) < 0x8000;
    }

    Endpoint::Endpoint()
    {
        accepted.fill(NoSequence);
    }

    bool Endpoint::queueReliable(std::vector<uint8_t> message)
    {
        if (message.size() > MaxMessageSize || outgoing.size() >= MaxQueuedMessages) {
            return false;
        }
        Outgoing entry;
        entry.id = nextMessage++;
        entry.data = std::move(message);
        outgoing.push_back(std::move(entry));
        unsentMessages++;
        return true;
    }

    void Endpoint::update(double time, double seconds, int32_t rate)
    {
        now = time;
        double credit = static_cast<double>(rate) * seconds;
        budget = std::min(budget + credit, std::max(2.0 * credit, static_cast<double>(MaxPacketSize)));
        packetsThisTick = 0;
        double timeout = std::max(PacketTimeout, 3.0 * smoothedRtt);
        for (SentPacket& packet : sent) {
            if (packet.pending && now - packet.time > timeout) {
                losePacket(packet);
            }
        }
    }

    bool Endpoint::ready() const
    {
        return budget > 0;
    }

    double Endpoint::rtt() const
    {
        return smoothedRtt;
    }

    size_t Endpoint::queuedMessages() const
    {
        return outgoing.size();
    }

    void Endpoint::open()
    {
        building = true;
        buildingSequence = nextSequence++;
        body.clear();
        bodyMessages.clear();
    }

    void Endpoint::finish()
    {
        if (!building) {
            return;
        }
        std::vector<uint8_t> packet;
        packet.reserve(HeaderSize + body.size());
        put16(packet, buildingSequence);
        put16(packet, newestAccepted);
        uint32_t bits = 0;
        for (size_t i = 1; i <= AckBits; i++) {
            uint16_t sequence = static_cast<uint16_t>(newestAccepted - i);
            if (accepted[sequence % PacketWindow] == sequence) {
                bits |= 1u << (i - 1);
            }
        }
        put32(packet, bits);
        put16(packet, expectedMessage);
        packet.push_back(acceptedAny ? FlagAcked : 0);
        packet.insert(packet.end(), body.begin(), body.end());
        SentPacket& record = sent[buildingSequence % PacketWindow];
        if (record.pending) {
            losePacket(record);
        }
        if (!body.empty()) {
            record.pending = true;
            record.sequence = buildingSequence;
            record.time = now;
            record.messages = bodyMessages;
        }
        size_t total = packet.size() + PacketOverhead;
        budget -= static_cast<double>(total);
        bytesSent += total;
        packetsSent++;
        packetsThisTick++;
        acksOwed = false;
        building = false;
        finished.push_back(std::move(packet));
    }

    void Endpoint::place(Outgoing& message)
    {
        size_t size = 3 + varintSize(static_cast<uint32_t>(message.data.size())) + message.data.size();
        if (!building) {
            open();
        } else if (!body.empty() && HeaderSize + body.size() + size > MaxPacketSize) {
            finish();
            open();
        }
        body.push_back(ItemReliable);
        put16(body, message.id);
        putVarint(body, static_cast<uint32_t>(message.data.size()));
        body.insert(body.end(), message.data.begin(), message.data.end());
        bodyMessages.push_back(message.id);
        if (message.sends > 0) {
            messagesResent++;
        } else {
            unsentMessages--;
        }
        message.sends++;
        message.sentTime = now;
    }

    void Endpoint::fillReliable()
    {
        if (outgoing.empty()) {
            return;
        }
        uint16_t oldest = outgoing.front().id;
        double delay = std::max(MinResendDelay, 2.0 * smoothedRtt);
        for (Outgoing& message : outgoing) {
            if (static_cast<uint16_t>(message.id - oldest) >= ReliableWindow) {
                break;
            }
            if (message.acked || (message.sentTime >= 0 && now - message.sentTime < delay)) {
                continue;
            }
            if (!ready()) {
                break;
            }
            place(message);
        }
    }

    bool Endpoint::add(const std::vector<uint8_t>& item, uint16_t& sequence)
    {
        if (item.size() > MaxMessageSize) {
            return false;
        }
        fillReliable();
        if (unsentMessages > 0) {
            return false;
        }
        size_t size = 1 + varintSize(static_cast<uint32_t>(item.size())) + item.size();
        if (!building) {
            open();
        } else if (!body.empty() && HeaderSize + body.size() + size > MaxPacketSize) {
            finish();
            open();
        }
        body.push_back(ItemUnreliable);
        putVarint(body, static_cast<uint32_t>(item.size()));
        body.insert(body.end(), item.begin(), item.end());
        sequence = buildingSequence;
        return true;
    }

    std::vector<std::vector<uint8_t>> Endpoint::flush()
    {
        fillReliable();
        if (building) {
            finish();
        } else if (acksOwed && packetsThisTick == 0) {
            open();
            finish();
        }
        std::vector<std::vector<uint8_t>> result;
        result.swap(finished);
        return result;
    }

    void Endpoint::ackMessage(uint16_t id)
    {
        if (outgoing.empty()) {
            return;
        }
        uint16_t index = static_cast<uint16_t>(id - outgoing.front().id);
        if (index < outgoing.size()) {
            outgoing[index].acked = true;
        }
        while (!outgoing.empty() && outgoing.front().acked) {
            outgoing.pop_front();
        }
    }

    void Endpoint::ackPacket(SentPacket& packet)
    {
        packet.pending = false;
        for (uint16_t id : packet.messages) {
            ackMessage(id);
        }
        double sample = now - packet.time;
        if (rttMeasured) {
            smoothedRtt += 0.1 * (sample - smoothedRtt);
        } else {
            smoothedRtt = sample;
            rttMeasured = true;
        }
        acked.push_back(packet.sequence);
    }

    void Endpoint::losePacket(SentPacket& packet)
    {
        packet.pending = false;
        if (!outgoing.empty()) {
            for (uint16_t id : packet.messages) {
                uint16_t index = static_cast<uint16_t>(id - outgoing.front().id);
                if (index < outgoing.size() && !outgoing[index].acked) {
                    outgoing[index].sentTime = -1;
                }
            }
        }
        packetsLost++;
        lost.push_back(packet.sequence);
    }

    void Endpoint::deliver(uint16_t id, const uint8_t* data, size_t size, Received& out)
    {
        if (static_cast<uint16_t>(id - expectedMessage) >= ReliableWindow) {
            return;
        }
        Incoming& slot = incoming[id % ReliableWindow];
        if (!slot.present) {
            slot.present = true;
            slot.id = id;
            slot.data.assign(data, data + size);
        }
        while (true) {
            Incoming& next = incoming[expectedMessage % ReliableWindow];
            if (!next.present || next.id != expectedMessage) {
                break;
            }
            out.messages.push_back(std::move(next.data));
            next.data.clear();
            next.present = false;
            expectedMessage++;
        }
    }

    bool Endpoint::receive(const uint8_t* data, size_t size, Received& out)
    {
        out = Received();
        if (size < HeaderSize) {
            return false;
        }
        uint16_t sequence = get16(data);
        uint16_t ack = get16(data + 2);
        uint32_t bits = get32(data + 4);
        uint16_t peerExpected = get16(data + 8);
        uint8_t flags = data[10];
        if ((flags & ~FlagAcked) != 0 || newer(peerExpected, nextMessage)) {
            return false;
        }
        std::vector<Piece> pieces;
        const uint8_t* cursor = data + HeaderSize;
        const uint8_t* end = data + size;
        while (cursor < end) {
            Piece piece;
            uint8_t kind = *cursor++;
            if (kind == ItemReliable) {
                if (end - cursor < 2) {
                    return false;
                }
                piece.reliable = true;
                piece.id = get16(cursor);
                cursor += 2;
            } else if (kind != ItemUnreliable) {
                return false;
            }
            uint32_t length = 0;
            if (!getVarint(cursor, end, length) || length > MaxMessageSize || static_cast<size_t>(end - cursor) < length) {
                return false;
            }
            piece.data = cursor;
            piece.size = length;
            cursor += length;
            pieces.push_back(piece);
        }
        if (flags & FlagAcked) {
            for (size_t i = 0; i <= AckBits; i++) {
                if (i > 0 && !(bits & (1u << (i - 1)))) {
                    continue;
                }
                uint16_t acknowledged = static_cast<uint16_t>(ack - i);
                SentPacket& packet = sent[acknowledged % PacketWindow];
                if (packet.pending && packet.sequence == acknowledged) {
                    ackPacket(packet);
                }
            }
            for (SentPacket& packet : sent) {
                if (!packet.pending || !newer(ack, packet.sequence)) {
                    continue;
                }
                uint16_t distance = static_cast<uint16_t>(ack - packet.sequence);
                if (distance > AckBits || !(bits & (1u << (distance - 1)))) {
                    losePacket(packet);
                }
            }
        }
        while (!outgoing.empty() && newer(peerExpected, outgoing.front().id)) {
            if (outgoing.front().sends == 0) {
                unsentMessages--;
            }
            outgoing.pop_front();
        }
        while (!outgoing.empty() && outgoing.front().acked) {
            outgoing.pop_front();
        }
        bool fresh = !seenAny || newer(sequence, newestSeen);
        if (fresh) {
            newestSeen = sequence;
            seenAny = true;
        }
        acksOwed = acksOwed || !pieces.empty();
        for (const Piece& piece : pieces) {
            if (piece.reliable) {
                deliver(piece.id, piece.data, piece.size, out);
            } else if (fresh) {
                out.items.emplace_back(piece.data, piece.data + piece.size);
            }
        }
        out.sequence = sequence;
        out.fresh = fresh;
        return true;
    }

    void Endpoint::accept(uint16_t sequence)
    {
        accepted[sequence % PacketWindow] = sequence;
        if (!acceptedAny || newer(sequence, newestAccepted)) {
            newestAccepted = sequence;
        }
        acceptedAny = true;
    }
}
