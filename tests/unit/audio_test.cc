#include "audio/audio.h"
#include "audio/module.h"
#include "engine/actor.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/props_engine.h"
#include "engine/package.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
    std::vector<uint8_t> resourceBytes(const std::string& path, const char* className, const char* objectName)
    {
        ut::Package package(path);
        int32_t ref = package.findExport(className, objectName);
        REQUIRE(ref != 0);
        ut::Reader reader = package.load(ref).native();
        reader.index();
        reader.u32();
        int32_t size = reader.index();
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        reader.bytes(bytes.data(), bytes.size());
        return bytes;
    }

    void loadSound(ut::USound& sound, const std::string& package, const char* name)
    {
        sound.bytes = resourceBytes(OPENUT_GAME_DATA "/Sounds/" + package + ".uax", "Sound", name);
    }

    std::vector<uint8_t> sampleWave(const std::vector<int16_t>& samples, uint32_t rate)
    {
        uint32_t frames = static_cast<uint32_t>(samples.size());
        std::vector<uint8_t> bytes;
        auto u32 = [&bytes](uint32_t v) {
            for (int i = 0; i < 4; i++) {
                bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
            }
        };
        auto u16 = [&bytes](uint16_t v) {
            bytes.push_back(static_cast<uint8_t>(v));
            bytes.push_back(static_cast<uint8_t>(v >> 8));
        };
        bytes.insert(bytes.end(), { 'R', 'I', 'F', 'F' });
        u32(36 + frames * 2);
        bytes.insert(bytes.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
        u32(16);
        u16(1);
        u16(1);
        u32(rate);
        u32(rate * 2);
        u16(2);
        u16(16);
        bytes.insert(bytes.end(), { 'd', 'a', 't', 'a' });
        u32(frames * 2);
        for (int16_t sample : samples) {
            u16(static_cast<uint16_t>(sample));
        }
        return bytes;
    }

    std::vector<uint8_t> loopWave(const std::vector<int16_t>& samples, uint32_t rate, uint32_t start, uint32_t last)
    {
        std::vector<uint8_t> bytes = sampleWave(samples, rate);
        std::vector<uint32_t> chunk(15, 0);
        chunk[7] = 1;
        chunk[11] = start;
        chunk[12] = last;
        bytes.insert(bytes.end(), { 's', 'm', 'p', 'l', 60, 0, 0, 0 });
        for (uint32_t value : chunk) {
            for (int i = 0; i < 4; i++) {
                bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
        }
        uint32_t riff = static_cast<uint32_t>(bytes.size() - 8);
        for (int i = 0; i < 4; i++) {
            bytes[4 + static_cast<size_t>(i)] = static_cast<uint8_t>(riff >> (8 * i));
        }
        return bytes;
    }

    std::vector<uint8_t> constantWave(int16_t value, uint32_t rate, uint32_t frames)
    {
        return sampleWave(std::vector<int16_t>(frames, value), rate);
    }

    void renderFrames(size_t frames)
    {
        std::vector<float> buffer(4096 * 2);
        while (frames > 0) {
            size_t chunk = std::min<size_t>(frames, 4096);
            ut::audio::render(buffer.data(), chunk);
            frames -= chunk;
        }
    }

    std::shared_ptr<const ut::audio::ItModule> menuMusic()
    {
        std::vector<uint8_t> bytes = resourceBytes(OPENUT_GAME_DATA "/Music/utmenu23.umx", "Music", "utmenu23");
        auto module = std::make_shared<ut::audio::ItModule>();
        REQUIRE(module->load(bytes.data(), bytes.size()));
        return module;
    }

    struct TrackerCell {
        int channel = 0;
        int note = -1;
        int instrument = 0;
        int volume = -1;
        int command = 0;
        int param = 0;
    };

    struct TrackerSample {
        std::vector<int16_t> data;
        uint8_t flags = 0x13;
        uint32_t loopBegin = 0;
        uint32_t loopEnd = 0;
        uint32_t sustainBegin = 0;
        uint32_t sustainEnd = 0;
        uint8_t globalVolume = 64;
        uint8_t volume = 64;
    };

    std::vector<uint8_t> trackerModule(const std::vector<std::vector<TrackerCell>>& rows, const TrackerSample& sample)
    {
        std::vector<uint8_t> bytes(0xC0, 0);
        auto put16 = [&bytes](size_t at, uint32_t v) {
            bytes[at] = static_cast<uint8_t>(v);
            bytes[at + 1] = static_cast<uint8_t>(v >> 8);
        };
        auto put32 = [&bytes](size_t at, uint32_t v) {
            for (size_t i = 0; i < 4; i++) {
                bytes[at + i] = static_cast<uint8_t>(v >> (8 * i));
            }
        };
        std::memcpy(bytes.data(), "IMPM", 4);
        put16(0x20, 2);
        put16(0x22, 0);
        put16(0x24, 1);
        put16(0x26, 1);
        put16(0x28, 0x217);
        put16(0x2A, 0x200);
        put16(0x2C, 0x09);
        bytes[0x30] = 128;
        bytes[0x31] = 48;
        bytes[0x32] = 6;
        bytes[0x33] = 125;
        bytes[0x34] = 128;
        for (size_t i = 0; i < 64; i++) {
            bytes[0x40 + i] = 32;
            bytes[0x80 + i] = 64;
        }
        bytes.push_back(0);
        bytes.push_back(255);
        size_t sampleTable = bytes.size();
        bytes.resize(bytes.size() + 8, 0);
        size_t sampleHeader = bytes.size();
        bytes.resize(bytes.size() + 0x50, 0);
        std::memcpy(bytes.data() + sampleHeader, "IMPS", 4);
        bytes[sampleHeader + 0x11] = sample.globalVolume;
        bytes[sampleHeader + 0x12] = sample.flags;
        bytes[sampleHeader + 0x13] = sample.volume;
        bytes[sampleHeader + 0x2E] = 1;
        bytes[sampleHeader + 0x2F] = 32;
        put32(sampleHeader + 0x30, static_cast<uint32_t>(sample.data.size()));
        put32(sampleHeader + 0x34, sample.loopBegin);
        put32(sampleHeader + 0x38, sample.loopEnd);
        put32(sampleHeader + 0x3C, 44100);
        put32(sampleHeader + 0x40, sample.sustainBegin);
        put32(sampleHeader + 0x44, sample.sustainEnd);
        size_t pattern = bytes.size();
        bytes.resize(bytes.size() + 8, 0);
        for (const std::vector<TrackerCell>& row : rows) {
            for (const TrackerCell& cell : row) {
                uint8_t mask = static_cast<uint8_t>((cell.note >= 0 ? 1 : 0) | (cell.instrument > 0 ? 2 : 0) | (cell.volume >= 0 ? 4 : 0) | (cell.command > 0 ? 8 : 0));
                bytes.push_back(static_cast<uint8_t>((cell.channel + 1) | 0x80));
                bytes.push_back(mask);
                if (cell.note >= 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.note));
                }
                if (cell.instrument > 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.instrument));
                }
                if (cell.volume >= 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.volume));
                }
                if (cell.command > 0) {
                    bytes.push_back(static_cast<uint8_t>(cell.command));
                    bytes.push_back(static_cast<uint8_t>(cell.param));
                }
            }
            bytes.push_back(0);
        }
        for (size_t i = rows.size(); i < 64; i++) {
            bytes.push_back(0);
        }
        put16(pattern, static_cast<uint32_t>(bytes.size() - pattern - 8));
        put16(pattern + 2, 64);
        size_t data = bytes.size();
        for (int16_t value : sample.data) {
            bytes.push_back(static_cast<uint8_t>(value));
            bytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8));
        }
        put32(sampleTable, static_cast<uint32_t>(sampleHeader));
        put32(sampleTable + 4, static_cast<uint32_t>(pattern));
        put32(sampleHeader + 0x48, static_cast<uint32_t>(data));
        return bytes;
    }

    std::shared_ptr<const ut::audio::ItModule> trackerSong(const std::vector<std::vector<TrackerCell>>& rows, const TrackerSample& sample)
    {
        std::vector<uint8_t> bytes = trackerModule(rows, sample);
        auto module = std::make_shared<ut::audio::ItModule>();
        REQUIRE(module->load(bytes.data(), bytes.size()));
        return module;
    }

    TrackerSample constantSample(int16_t value)
    {
        TrackerSample sample;
        sample.data.assign(64, value);
        sample.loopEnd = 64;
        return sample;
    }

    std::vector<float> renderSong(std::shared_ptr<const ut::audio::ItModule> module, size_t frames)
    {
        ut::audio::ModulePlayer player(module, 44100);
        std::vector<float> out(frames * 2);
        player.render(out.data(), frames);
        return out;
    }
}

TEST_CASE("garbage bytes do not decode")
{
    std::vector<uint8_t> bytes(64, 0x42);
    CHECK_FALSE(ut::audio::decodeBytes(bytes));
}

TEST_CASE("Galaxy pans by the view space angle over seven eighths of the range and attenuates linearly")
{
    ut::Vector listener;
    ut::Rotator facingX;
    ut::audio::Placement right = ut::audio::place(listener, facingX, { 0, 100, 0 }, 400.0f);
    CHECK(right.pan == 30718);
    CHECK(right.attenuation == Catch::Approx(0.75f));

    ut::audio::Placement left = ut::audio::place(listener, facingX, { 0, -100, 0 }, 400.0f);
    CHECK(left.pan == 2047);

    ut::audio::Placement front = ut::audio::place(listener, facingX, { 100, 0, 0 }, 200.0f);
    CHECK(front.pan == 16383);
    CHECK(front.attenuation == Catch::Approx(0.5f));

    ut::audio::Placement behind = ut::audio::place(listener, facingX, { -100, 0, 0 }, 400.0f);
    CHECK(behind.pan == 16383);

    ut::audio::Placement close = ut::audio::place(listener, facingX, { 0, 10, 0 }, 400.0f);
    CHECK(close.pan == 19966);

    ut::audio::Placement outside = ut::audio::place(listener, facingX, { 500, 0, 0 }, 200.0f);
    CHECK(outside.attenuation == 0.0f);

    ut::Rotator facingY;
    facingY.yaw = 16384;
    CHECK(ut::audio::place(listener, facingY, { 0, 100, 0 }, 400.0f).pan == 16383);
    CHECK(ut::audio::place(listener, facingY, { -100, 0, 0 }, 400.0f).pan == 30718);
}

TEST_CASE("Galaxy master volume takes the louder of sound and speech volume on a 127 scale")
{
    CHECK(ut::audio::masterVolume(255, 255) == 127);
    CHECK(ut::audio::masterVolume(204, 255) == 126);
    CHECK(ut::audio::masterVolume(255, 204) == 127);
    CHECK(ut::audio::masterVolume(0, 0) == 0);
}

TEST_CASE("Galaxy voice volume scales by category, obstruction and attenuation with a floor of 128")
{
    CHECK(ut::audio::voiceVolume(0.75f, 0.0f, 204, 255, false, 1.0f) == 19660);
    CHECK(ut::audio::voiceVolume(0.75f, 0.0f, 204, 255, true, 1.0f) == 24575);
    CHECK(ut::audio::voiceVolume(1.0f, 0.5f, 255, 255, false, 1.0f) == 10813);
    CHECK(ut::audio::voiceVolume(1.0f, 0.25f, 255, 255, false, 1.0f) == 16383);
    CHECK(ut::audio::voiceVolume(0.0f, 0.0f, 255, 255, false, 1.0f) == 128);
    CHECK(ut::audio::voiceVolume(0.75f, 0.0f, 204, 255, true, 2.0f) == 32767);
}

TEST_CASE("Galaxy channel gains use the square root pan table and the fixed point volume chain")
{
    ut::audio::Gains center = ut::audio::channelGains(32767, 16383, 127, 128);
    CHECK(center.left == Catch::Approx(22450.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
    CHECK(center.right == Catch::Approx(22449.0 * 32767.0 / 1073741824.0).epsilon(1e-6));

    ut::audio::Gains send = ut::audio::channelGains(32767, 16383, 127, 127);
    CHECK(send.left == Catch::Approx(22274.0 * 32767.0 / 1073741824.0).epsilon(1e-6));

    ut::audio::Gains panned = ut::audio::channelGains(19660, 30718, 126, 128);
    CHECK(panned.left == Catch::Approx(4688.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
    CHECK(panned.right == Catch::Approx(18154.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
}

TEST_CASE("Galaxy Doppler shifts ambient pitch by the approach speed over DopplerSpeed")
{
    ut::Vector listener;
    ut::Vector source { 1000, 0, 0 };
    CHECK(ut::audio::dopplerShift({ 650, 0, 0 }, source, listener, 6500.0f) == Catch::Approx(0.9f));
    CHECK(ut::audio::dopplerShift({ -650, 0, 0 }, source, listener, 6500.0f) == Catch::Approx(1.1f));
    CHECK(ut::audio::dopplerShift({ -13000, 0, 0 }, source, listener, 6500.0f) == 2.0f);
    CHECK(ut::audio::dopplerShift({ 13000, 0, 0 }, source, listener, 6500.0f) == 0.5f);
    CHECK(ut::audio::dopplerShift({ 0, 650, 0 }, source, listener, 6500.0f) == 1.0f);

    CHECK(ut::audio::voiceStep(44100, 1.0f, 1.0f, 44100) == 65536);
    CHECK(ut::audio::voiceStep(11025, 1.0f, 1.0f, 44100) == 16384);
    CHECK(ut::audio::voiceStep(22050, 1.0f, 0.9f, 44100) == 29489);
    CHECK(ut::audio::voiceStep(22050, 1.0f, 1.1f, 44100) == 36044);
}

TEST_CASE("a centered full volume voice starts at the next update at the Galaxy constant power level", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::audio::setVolumes(1.0f, 1.0f, 1.0f);
    ut::USound tone;
    tone.bytes = constantWave(16384, 44100, 8000);
    int32_t id = ut::audio::playSound(&tone, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
    REQUIRE(id != 0);
    std::vector<float> buffer(4000 * 2);
    ut::audio::render(buffer.data(), 4000);
    CHECK(ut::audio::isPlaying(id));
    CHECK(*std::max_element(buffer.begin(), buffer.end()) == 0.0f);
    ut::audio::update({}, {}, 0.0f);
    ut::audio::render(buffer.data(), 4000);
    CHECK(buffer[0] == Catch::Approx(0.5 * 22450.0 * 32767.0 / 1073741824.0).epsilon(1e-6));
    CHECK(buffer[3999 * 2] == Catch::Approx(0.5 * 22450.0 * 32767.0 / 1073741824.0).epsilon(1e-5));
    CHECK(buffer[3999 * 2 + 1] == Catch::Approx(0.5 * 22449.0 * 32767.0 / 1073741824.0).epsilon(1e-5));
    ut::audio::shutdown();
}

TEST_CASE("voices advance in 16.16 fixed point with 15 bit interpolation weights and no pitch clamp", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::audio::setVolumes(1.0f, 1.0f, 1.0f);
    ut::USound ramp;
    ramp.bytes = sampleWave({ 0, 20000, 0, 0 }, 22050);
    REQUIRE(ut::audio::playSound(&ramp, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) != 0);
    ut::audio::update({}, {}, 0.0f);
    std::vector<float> buffer(8 * 2);
    ut::audio::render(buffer.data(), 8);
    const double unit = 22450.0 / 35184372088832.0;
    CHECK(buffer[0] == 0.0f);
    CHECK(buffer[2] == Catch::Approx(20000.0 * 16384.0 * unit).epsilon(1e-7));
    CHECK(buffer[4] == Catch::Approx(20000.0 * 32767.0 * unit).epsilon(1e-7));
    CHECK(buffer[6] == Catch::Approx(20000.0 * 16383.0 * unit).epsilon(1e-7));
    ut::audio::shutdown();

    REQUIRE(ut::audio::initializeOffline(44100));
    ut::USound tone;
    tone.bytes = constantWave(1000, 22050, 4000);
    int32_t fast = ut::audio::playSound(&tone, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 8.0f, false);
    REQUIRE(fast != 0);
    CHECK(ut::audio::voices().front().step == 4.0);
    ut::audio::shutdown();
}

TEST_CASE("the interpolation at a loop end reads the raw next sample before wrapping", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::audio::setVolumes(1.0f, 1.0f, 1.0f);
    ut::USound looped;
    looped.bytes = loopWave({ 0, 0, 20000, 0 }, 22050, 0, 1);
    REQUIRE(ut::audio::playSound(&looped, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false) != 0);
    ut::audio::update({}, {}, 0.0f);
    std::vector<float> buffer(8 * 2);
    ut::audio::render(buffer.data(), 8);
    const double unit = 22450.0 / 35184372088832.0;
    CHECK(buffer[2 * 2] == 0.0f);
    CHECK(buffer[3 * 2] == Catch::Approx(20000.0 * 16384.0 * unit).epsilon(1e-7));
    CHECK(buffer[4 * 2] == 0.0f);
    CHECK(buffer[7 * 2] == Catch::Approx(20000.0 * 16384.0 * unit).epsilon(1e-7));
    ut::audio::shutdown();
}

TEST_CASE("the reverb network is three allpass stages with a crossed half gain feedback")
{
    ut::audio::ReverbSettings settings;
    settings.volume = 1.0f;
    settings.cutoff = 0.0f;
    settings.delay = { 441.5f / 44100.0f, 13230.5f / 44100.0f, 882.5f / 44100.0f, 13230.5f / 44100.0f, 1764.5f / 44100.0f, 13230.5f / 44100.0f };
    settings.gain = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    ut::audio::Reverb reverb;
    reverb.configure(settings, 44100);
    REQUIRE(reverb.active());
    std::vector<float> send(4000 * 2, 0.0f);
    send[0] = 1.0f;
    std::vector<float> out(4000 * 2, 0.0f);
    reverb.process(send.data(), out.data(), 4000);
    auto left = [&](size_t n) {
        return out[n * 2];
    };
    CHECK(left(0) == Catch::Approx(-0.0625).margin(1e-7));
    CHECK(left(441) == Catch::Approx(0.09375).margin(1e-7));
    CHECK(left(882) == Catch::Approx(0.09375).margin(1e-7));
    CHECK(left(1323) == Catch::Approx(-0.140625).margin(1e-7));
    CHECK(left(1764) == Catch::Approx(0.09375).margin(1e-7));
    CHECK(left(2205) == Catch::Approx(-0.140625).margin(1e-7));
    CHECK(left(2646) == Catch::Approx(-0.140625).margin(1e-7));
    CHECK(left(3087) == Catch::Approx(0.2109375).margin(1e-7));
    CHECK(left(1) == 0.0f);
    CHECK(left(100) == 0.0f);
    CHECK(left(2) == Catch::Approx(-0.000244140625).margin(1e-9));
    CHECK(out[1] == 0.0f);
    CHECK(out[3] == Catch::Approx(0.00390625).margin(1e-9));

    settings.volume = 0.0f;
    reverb.configure(settings, 44100);
    CHECK_FALSE(reverb.active());
}

TEST_CASE("the reverb cutoff lowpasses the recirculated delay line")
{
    ut::audio::ReverbSettings settings;
    settings.volume = 1.0f;
    settings.cutoff = static_cast<float>(std::acos(0.5) * 44100.0 / 6.282);
    settings.delay = { 441.5f / 44100.0f, 13230.5f / 44100.0f, 1500.5f / 44100.0f, 13230.5f / 44100.0f, 3000.5f / 44100.0f, 13230.5f / 44100.0f };
    settings.gain = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    ut::audio::Reverb reverb;
    reverb.configure(settings, 44100);
    std::vector<float> send(1000 * 2, 0.0f);
    send[0] = 1.0f;
    std::vector<float> out(1000 * 2, 0.0f);
    reverb.process(send.data(), out.data(), 1000);
    const double k = std::sqrt(1.25) - 0.5;
    CHECK(out[441 * 2] == Catch::Approx(0.09375).margin(1e-7));
    CHECK(out[882 * 2] == Catch::Approx(0.046875 * k).margin(1e-5));
    CHECK(out[883 * 2] == Catch::Approx(0.046875 * k * (1.0 - k)).margin(1e-5));
}

TEST_CASE("module player starts at the requested song section", "[data]")
{
    auto module = menuMusic();
    ut::audio::ModulePlayer player(module, 44100);
    player.setPosition(2);
    CHECK(player.order() == 2);
    CHECK(player.row() == 0);
    player.setPosition(15);
    CHECK(player.order() == 0);
}

TEST_CASE("ambient sound radius follows the actor sound radius")
{
    CHECK(ut::audio::ambientRadius(0) == 25.0f);
    CHECK(ut::audio::ambientRadius(32) == 825.0f);
    CHECK(ut::audio::ambientRadius(255) == 6400.0f);
}

TEST_CASE("a looped sound keeps playing after a one shot sound has ended", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::USound looped;
    loadSound(looped, "AmbCity", "Ambh01a");
    ut::USound oneShot;
    loadSound(oneShot, "AmbCity", "traffic2");
    float loopedLength = ut::audio::duration(&looped);
    REQUIRE(ut::audio::duration(&oneShot) < loopedLength);
    int32_t loopedId = ut::audio::playSound(&looped, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
    int32_t oneShotId = ut::audio::playSound(&oneShot, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
    REQUIRE(loopedId != 0);
    REQUIRE(oneShotId != 0);
    ut::audio::update({}, {}, 0.0f);
    renderFrames(static_cast<size_t>(loopedLength * 2.0f * 44100.0f));
    CHECK(ut::audio::isPlaying(loopedId));
    CHECK_FALSE(ut::audio::isPlaying(oneShotId));
    ut::audio::shutdown();
}

TEST_CASE("a sound on an occupied actor slot replaces the earlier one unless it is no override", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::USound sound;
    loadSound(sound, "AmbCity", "Ambh01a");
    ut::UObject actor;
    const int32_t slotMisc = 1;
    int32_t first = ut::audio::playSound(&sound, &actor, slotMisc, {}, 1.0f, 1000.0f, 1.0f, true);
    int32_t second = ut::audio::playSound(&sound, &actor, slotMisc, {}, 1.0f, 1000.0f, 1.0f, true);
    REQUIRE(first != 0);
    REQUIRE(second != 0);
    CHECK_FALSE(ut::audio::isPlaying(first));
    CHECK(ut::audio::isPlaying(second));

    int32_t blocked = ut::audio::playSound(&sound, &actor, slotMisc, {}, 1.0f, 1000.0f, 1.0f, true, true);
    CHECK(blocked == 0);
    CHECK(ut::audio::isPlaying(second));

    int32_t a = ut::audio::playSound(&sound, &actor, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, true);
    int32_t b = ut::audio::playSound(&sound, &actor, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, true);
    CHECK(ut::audio::isPlaying(a));
    CHECK(ut::audio::isPlaying(b));
    CHECK(ut::audio::isPlaying(second));
    ut::audio::shutdown();
}

TEST_CASE("the voice limit steals the quietest sound and rejects quieter ones", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::USound sound;
    loadSound(sound, "AmbCity", "Ambh01a");
    std::vector<int32_t> distant;
    for (int i = 0; i < 16; i++) {
        int32_t id = ut::audio::playSound(&sound, nullptr, ut::audio::SlotNone, { 500, 0, 0 }, 1.0f, 1000.0f, 1.0f, true);
        REQUIRE(id != 0);
        distant.push_back(id);
    }
    CHECK(ut::audio::voices().size() == 16);
    CHECK(ut::audio::voices().front().priority == Catch::Approx(0.5f));

    int32_t near = ut::audio::playSound(&sound, nullptr, ut::audio::SlotNone, { 100, 0, 0 }, 1.0f, 1000.0f, 1.0f, true);
    CHECK(near != 0);
    CHECK(ut::audio::isPlaying(near));
    size_t stillPlaying = static_cast<size_t>(std::count_if(distant.begin(), distant.end(), ut::audio::isPlaying));
    CHECK(stillPlaying == 15);
    CHECK(ut::audio::voices().size() == 16);

    int32_t far = ut::audio::playSound(&sound, nullptr, ut::audio::SlotNone, { 900, 0, 0 }, 1.0f, 1000.0f, 1.0f, true);
    CHECK(far == 0);
    CHECK(ut::audio::voices().size() == 16);
    ut::audio::shutdown();
}

TEST_CASE("a fast fade drops three per second and switches two latencies after silence", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::UMusic music;
    music.bytes = resourceBytes(OPENUT_GAME_DATA "/Music/Botmca9.umx", "Music", "Botmca9");
    const uint8_t instant = 1;
    const uint8_t fade = 3;
    const uint8_t fastFade = 4;
    ut::audio::playMusic(&music, 0, instant);
    ut::audio::MusicStatus status = ut::audio::musicStatus();
    CHECK(status.music == &music);
    CHECK(status.section == 0);
    CHECK(status.order == 0);
    CHECK_FALSE(status.pending);

    renderFrames(4410);
    ut::audio::playMusic(&music, 3, fastFade);
    status = ut::audio::musicStatus();
    CHECK(status.section == 0);
    CHECK(status.pending);

    renderFrames(7000);
    status = ut::audio::musicStatus();
    CHECK(status.pending);
    CHECK(status.order == 0);
    CHECK(status.fade == Catch::Approx(0.5238f).margin(0.002f));

    renderFrames(8000);
    status = ut::audio::musicStatus();
    CHECK(status.pending);
    CHECK(status.order == 0);
    CHECK(status.fade == Catch::Approx(-0.0204f).margin(0.002f));

    renderFrames(4000);
    status = ut::audio::musicStatus();
    CHECK_FALSE(status.pending);
    CHECK(status.section == 3);
    CHECK(status.order == 3);
    CHECK(status.row == 0);
    CHECK(status.fade == 1.0f);

    ut::audio::playMusic(&music, 255, fade);
    CHECK(ut::audio::musicStatus().order == 3);
    CHECK(ut::audio::musicStatus().pending);
    renderFrames(44100 + 4096);
    status = ut::audio::musicStatus();
    CHECK(status.music == &music);
    CHECK(status.section == 255);
    CHECK(status.order == -1);
    CHECK_FALSE(status.pending);
    ut::audio::shutdown();
}

TEST_CASE("Galaxy ignores the impulse tracker mix volume", "[data]")
{
    auto module = menuMusic();
    auto louder = std::make_shared<ut::audio::ItModule>(*module);
    louder->mixVolume = 96;
    ut::audio::ModulePlayer a(module, 44100);
    ut::audio::ModulePlayer b(louder, 44100);
    std::vector<float> first(44100 * 2);
    std::vector<float> second(44100 * 2);
    a.render(first.data(), 44100);
    b.render(second.data(), 44100);
    CHECK(first == second);
    float peak = 0;
    for (float value : first) {
        peak = std::max(peak, std::fabs(value));
    }
    CHECK(peak > 0.01f);
}

TEST_CASE("Galaxy music voices run the fixed point gain chain through the square root pan table")
{
    std::vector<float> column = renderSong(trackerSong({ { { 0, 60, 1, 64 } } }, constantSample(16384)), 4096);
    CHECK(column[4000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(column[4000 * 2 + 1] == Catch::Approx(0.5 * 13809.0 / 32768.0).epsilon(1e-5));
    std::vector<float> preset = renderSong(trackerSong({ { { 0, 60, 1 } } }, constantSample(16384)), 4096);
    CHECK(preset[4000 * 2] == Catch::Approx(0.5 * 13917.0 / 32768.0).epsilon(1e-5));
    CHECK(preset[4000 * 2 + 1] == Catch::Approx(0.5 * 13917.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("Galaxy drops channel volume, panbrello and the sample global volume")
{
    std::vector<float> plain = renderSong(trackerSong({ { { 0, 60, 1, 64 } }, {} }, constantSample(16384)), 12000);
    TrackerSample quiet = constantSample(16384);
    quiet.globalVolume = 16;
    std::vector<float> dropped = renderSong(trackerSong({ { { 0, 60, 1, 64, 13, 0x20 } }, { { 0, -1, 0, -1, 25, 0x44 } } }, quiet), 12000);
    CHECK(dropped == plain);
    CHECK(plain[4000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("Galaxy ignores sustain loops and a note off cuts the sample at once")
{
    TrackerSample sustained;
    sustained.data.assign(2000, 16384);
    sustained.flags = 0x23;
    sustained.sustainBegin = 0;
    sustained.sustainEnd = 2000;
    std::vector<float> once = renderSong(trackerSong({ { { 0, 60, 1, 64 } } }, sustained), 4000);
    CHECK(once[1000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(once[3000 * 2] == 0.0f);
    std::vector<float> released = renderSong(trackerSong({ { { 0, 60, 1, 64 } }, { { 0, 255 } } }, constantSample(16384)), 6000);
    CHECK(released[5000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(released[5292 * 2 + 200] == 0.0f);
}

TEST_CASE("S9x centres the channel and inverts its right side for Galaxy surround")
{
    std::vector<float> out = renderSong(trackerSong({ { { 0, 60, 1, 64, 24, 0x00 } }, { { 0, -1, 0, -1, 19, 0x91 } } }, constantSample(16384)), 9000);
    CHECK(out[4000 * 2] == Catch::Approx(0.5 * 19528.0 / 32768.0).epsilon(1e-5));
    CHECK(out[4000 * 2 + 1] == 0.0f);
    CHECK(out[8800 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(out[8800 * 2 + 1] == Catch::Approx(-0.5 * 13809.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("Q keeps the volume and retriggers on every tick divisible by its interval")
{
    TrackerSample ramp;
    for (int i = 0; i < 2048; i++) {
        ramp.data.push_back(static_cast<int16_t>(i * 16));
    }
    ramp.flags = 0x03;
    std::vector<float> out = renderSong(trackerSong({ { { 0, 60, 1, 64, 17, 0x32 } } }, ramp), 4000);
    CHECK(out[1500 * 2] == Catch::Approx(1500.0 * 16.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(out[1774 * 2] == Catch::Approx(10.0 * 16.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-5));
    CHECK(out[3538 * 2] == Catch::Approx(10.0 * 16.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-5));
}

TEST_CASE("vibrato bends by the Galaxy sine table times depth in 256ths of a semitone")
{
    TrackerSample ramp;
    for (int i = 0; i < 4000; i++) {
        ramp.data.push_back(static_cast<int16_t>(i * 8));
    }
    ramp.flags = 0x03;
    std::vector<float> out = renderSong(trackerSong({ { { 0, 60, 1, 64, 8, 0x48 } } }, ramp), 2000);
    double position = 882.0 + 882.0 * std::pow(2.0, 97.0 / 3072.0);
    CHECK(out[1764 * 2] == Catch::Approx(position * 8.0 / 32768.0 * 13808.0 / 32768.0).epsilon(1e-4));
}

TEST_CASE("the loader resolves effect memory per channel and drops what Galaxy drops")
{
    auto module = trackerSong({ { { 0, 60, 1, 32, 15, 0x10 } }, { { 0, -1, 0, -1, 4, 0x00 } }, { { 0, -1, 0, -1, 25, 0x44 } }, { { 0, -1, 0, -1, 1, 0x40 } }, { { 0, -1, 0, -1, 17, 0x32 } } }, constantSample(16384));
    const std::vector<ut::audio::ItCell>& cells = module->patterns[0].cells;
    CHECK(cells[64].command == 4);
    CHECK(cells[64].param == 0x10);
    CHECK(cells[2 * 64].command == 0);
    CHECK(cells[3 * 64].param == 0x20);
    CHECK(cells[4 * 64].param == 0x02);
}

TEST_CASE("the music volume is squared into the Galaxy master level", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::UMusic music;
    music.bytes = trackerModule({ { { 0, 60, 1, 64 } } }, constantSample(16384));
    ut::audio::setVolumes(1.0f, 0.5f, 1.0f);
    ut::audio::playMusic(&music, 0, 1);
    std::vector<float> out(4096 * 2);
    ut::audio::render(out.data(), 4096);
    CHECK(out[4000 * 2] == Catch::Approx(0.5 * 13808.0 / 32768.0 * 3969.0 / 16129.0).epsilon(1e-5));
    ut::audio::setVolumes(1.0f, 1.0f, 1.0f);
    ut::audio::shutdown();
}

namespace {
    void put16(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        bytes[offset] = static_cast<uint8_t>(value);
        bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    }

    void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        put16(bytes, offset, value & 0xffff);
        put16(bytes, offset + 2, value >> 16);
    }

    std::vector<uint8_t> singleCycleS3m(uint8_t note)
    {
        std::vector<uint8_t> bytes(0x130 + 2 + 5 + 64, 0);
        std::memcpy(bytes.data(), "cycle", 5);
        bytes[0x1C] = 0x1A;
        bytes[0x1D] = 16;
        put16(bytes, 0x20, 2);
        put16(bytes, 0x22, 1);
        put16(bytes, 0x24, 1);
        put16(bytes, 0x28, 0x1320);
        put16(bytes, 0x2A, 2);
        std::memcpy(bytes.data() + 0x2C, "SCRM", 4);
        bytes[0x30] = 64;
        bytes[0x31] = 6;
        bytes[0x32] = 125;
        bytes[0x33] = 0xB0;
        bytes[0x40] = 0;
        std::fill(bytes.begin() + 0x41, bytes.begin() + 0x60, uint8_t { 255 });
        bytes[0x60] = 0;
        bytes[0x61] = 255;
        put16(bytes, 0x62, 0x07);
        put16(bytes, 0x64, 0x13);
        bytes[0x70] = 1;
        put16(bytes, 0x70 + 0x0E, 0x0C);
        put32(bytes, 0x70 + 0x10, 100);
        put32(bytes, 0x70 + 0x18, 100);
        bytes[0x70 + 0x1C] = 64;
        bytes[0x70 + 0x1F] = 1;
        put32(bytes, 0x70 + 0x20, 44100);
        std::memcpy(bytes.data() + 0x70 + 0x4C, "SCRS", 4);
        for (size_t i = 0; i < 100; i++) {
            bytes[0xC0 + i] = static_cast<uint8_t>(128 + std::lround(100.0 * std::sin(2.0 * 3.14159265358979 * static_cast<double>(i) / 100.0)));
        }
        size_t pattern = 0x130;
        put16(bytes, pattern, static_cast<uint32_t>(bytes.size() - pattern));
        bytes[pattern + 2] = 0x60;
        bytes[pattern + 3] = note;
        bytes[pattern + 4] = 1;
        bytes[pattern + 5] = 64;
        return bytes;
    }

    std::vector<uint8_t> singleCycleXm(uint8_t note)
    {
        std::vector<uint8_t> bytes(336, 0);
        std::memcpy(bytes.data(), "Extended Module: cycle", 22);
        bytes[37] = 0x1A;
        put16(bytes, 58, 0x0104);
        put32(bytes, 60, 276);
        put16(bytes, 64, 1);
        put16(bytes, 68, 2);
        put16(bytes, 70, 1);
        put16(bytes, 72, 1);
        put16(bytes, 74, 1);
        put16(bytes, 76, 6);
        put16(bytes, 78, 125);
        std::vector<uint8_t> rows { 0x83, note, 1, 0x80 };
        for (int row = 1; row < 64; row++) {
            rows.push_back(0x80);
            rows.push_back(0x80);
        }
        std::vector<uint8_t> header(9, 0);
        header[0] = 9;
        header[5] = 64;
        header[7] = static_cast<uint8_t>(rows.size());
        header[8] = static_cast<uint8_t>(rows.size() >> 8);
        bytes.insert(bytes.end(), header.begin(), header.end());
        bytes.insert(bytes.end(), rows.begin(), rows.end());
        size_t instrument = bytes.size();
        bytes.resize(instrument + 263 + 40, 0);
        put32(bytes, instrument, 263);
        put16(bytes, instrument + 27, 1);
        put32(bytes, instrument + 29, 40);
        size_t sample = instrument + 263;
        put32(bytes, sample, 100);
        put32(bytes, sample + 8, 100);
        bytes[sample + 12] = 64;
        bytes[sample + 14] = 1;
        bytes[sample + 15] = 128;
        int8_t previous = 0;
        for (size_t i = 0; i < 100; i++) {
            int8_t value = static_cast<int8_t>(std::lround(100.0 * std::sin(2.0 * 3.14159265358979 * static_cast<double>(i) / 100.0)));
            bytes.push_back(static_cast<uint8_t>(static_cast<int8_t>(value - previous)));
            previous = value;
        }
        return bytes;
    }

    double measuredFrequency(const std::vector<uint8_t>& bytes, double seconds)
    {
        auto module = std::make_shared<ut::audio::ItModule>();
        REQUIRE(module->load(bytes.data(), bytes.size()));
        ut::audio::ModulePlayer player(module, 44100);
        size_t frames = static_cast<size_t>(seconds * 44100.0);
        std::vector<float> stereo(frames * 2);
        player.render(stereo.data(), frames);
        size_t rising = 0;
        for (size_t i = 4410; i + 1 < frames; i++) {
            float a = stereo[i * 2] + stereo[i * 2 + 1];
            float b = stereo[i * 2 + 2] + stereo[i * 2 + 3];
            rising += a < 0.0f && b >= 0.0f ? 1 : 0;
        }
        return static_cast<double>(rising) / (seconds - 0.1);
    }
}

TEST_CASE("an S3M C-4 plays its sample at the C2 speed and C-5 an octave higher")
{
    CHECK(measuredFrequency(singleCycleS3m(0x40), 1.1) == Catch::Approx(441.0).margin(2.0));
    CHECK(measuredFrequency(singleCycleS3m(0x50), 1.1) == Catch::Approx(882.0).margin(2.0));
}

TEST_CASE("an XM C-4 plays a delta coded sample at 8363 Hz and C-5 an octave higher")
{
    std::vector<uint8_t> bytes = singleCycleXm(49);
    ut::audio::ItModule module;
    REQUIRE(module.load(bytes.data(), bytes.size()));
    REQUIRE(module.samples.size() == 1);
    REQUIRE(module.samples[0].data.size() == 100);
    CHECK(module.samples[0].data[0] == 0.0f);
    CHECK(module.samples[0].data[25] == 100.0f / 128.0f);
    CHECK(module.samples[0].data[75] == -100.0f / 128.0f);
    CHECK(measuredFrequency(bytes, 2.1) == Catch::Approx(83.63).margin(1.0));
    CHECK(measuredFrequency(singleCycleXm(61), 2.1) == Catch::Approx(167.26).margin(1.0));
}

TEST_CASE("the S3M and XM songs shipped with the game load and play", "[data]")
{
    ut::audio::ItModule mech;
    std::vector<uint8_t> s3m = resourceBytes(OPENUT_GAME_DATA "/Music/Mech8.umx", "Music", "Mech8");
    REQUIRE(mech.load(s3m.data(), s3m.size()));
    CHECK(mech.title == "mechanism eight - necros/");
    CHECK(mech.orders.size() == 79);
    CHECK(mech.patterns.size() == 94);
    CHECK(mech.samples.size() == 31);
    CHECK(mech.speed == 3);
    CHECK(mech.tempo == 116);
    CHECK((mech.channelPan[15] & 0x80) == 0);
    CHECK((mech.channelPan[16] & 0x80) != 0);

    ut::audio::ItModule fire;
    std::vector<uint8_t> xm = resourceBytes(OPENUT_GAME_DATA "/Music/firebr.umx", "Music", "firebr");
    REQUIRE(fire.load(xm.data(), xm.size()));
    CHECK(fire.orders.size() == 50);
    CHECK(fire.instruments.size() == 99);
    CHECK(fire.speed == 6);
    CHECK(fire.tempo == 135);
    CHECK((fire.channelPan[17] & 0x80) == 0);
    CHECK((fire.channelPan[18] & 0x80) != 0);

    for (const std::vector<uint8_t>* bytes : { &s3m, &xm }) {
        auto module = std::make_shared<ut::audio::ItModule>();
        REQUIRE(module->load(bytes->data(), bytes->size()));
        ut::audio::ModulePlayer player(module, 44100);
        std::vector<float> stereo(44100 * 2 * 4);
        player.render(stereo.data(), 44100 * 4);
        float peak = 0;
        for (float value : stereo) {
            peak = std::max(peak, std::fabs(value));
        }
        CHECK(peak > 0.1f);
    }
}

namespace {
    void listenAt(ut::World& world, const ut::Vector& location)
    {
        ut::UObject* player = world.player();
        REQUIRE(player != nullptr);
        ut::actor::Location(player) = location;
        ut::playerpawn::ViewTarget(player) = nullptr;
    }

    ut::UObject* ambientSource(ut::World& world)
    {
        for (ut::UObject* actor : world.actors) {
            if (actor && actor->data && !actor->destroyed && ut::actor::AmbientSound(actor)) {
                return actor;
            }
        }
        return nullptr;
    }
}

TEST_CASE("PCM wav sound decodes with header format and length", "[data]")
{
    std::vector<uint8_t> bytes = resourceBytes(OPENUT_GAME_DATA "/Sounds/AmbCity.uax", "Sound", "traffic2");
    auto pcm = ut::audio::decodeBytes(bytes);
    REQUIRE(pcm);
    CHECK(pcm->sampleRate == 11025);
    CHECK(pcm->channels == 1);
    CHECK(pcm->frames() == 52992);
    CHECK(pcm->samples[0] == Catch::Approx(8.0 / 128.0).margin(1e-7));
    CHECK(pcm->duration() == Catch::Approx(52992.0 / 11025.0).margin(1e-6));
}

TEST_CASE("wav loop points come from the smpl chunk", "[data]")
{
    auto looped = ut::audio::decodeBytes(resourceBytes(OPENUT_GAME_DATA "/Sounds/AmbCity.uax", "Sound", "Ambh01a"));
    REQUIRE(looped);
    CHECK(looped->frames() == 106491);
    CHECK(looped->looped);
    CHECK(looped->loopStart == 0);
    CHECK(looped->loopEnd == 106491);
    auto oneShot = ut::audio::decodeBytes(resourceBytes(OPENUT_GAME_DATA "/Sounds/AmbCity.uax", "Sound", "traffic2"));
    REQUIRE(oneShot);
    CHECK_FALSE(oneShot->looped);
}

TEST_CASE("ambient light sources scale their ambient sound by the flicker and strobe light functions", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::audio::setVolumes(1.0f, 1.0f, 1.0f);
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Deck16]["));
        ut::UObject* player = world.player();
        REQUIRE(player != nullptr);
        ut::UObject* source = ambientSource(world);
        REQUIRE(source != nullptr);
        ut::Vector listener = ut::actor::Location(source) + ut::Vector { 10, 0, 0 };
        listenAt(world, listener);
        ut::actor::SoundVolume(source) = 128;
        ut::actor::SoundRadius(source) = 64;
        ut::actor::LightBrightness(source) = 255;
        auto level = [&]() {
            ut::audio::update(listener, {}, 0.0f);
            for (const ut::audio::VoiceStatus& voice : ut::audio::voices()) {
                if (voice.actor == source) {
                    return voice.volume;
                }
            }
            return 0;
        };
        const uint8_t steady = 1;
        const uint8_t flicker = 4;
        const uint8_t strobe = 5;
        ut::actor::LightType(source) = steady;
        int32_t full = level();
        REQUIRE(full > 1000);
        ut::audio::setLightRandom([](int32_t) {
            return 0.75f;
        });
        ut::actor::LightType(source) = flicker;
        CHECK(level() == Catch::Approx(full * 0.75).margin(1.0));
        ut::audio::setLightRandom([](int32_t) {
            return 0.4f;
        });
        CHECK(level() == 128);
        ut::actor::LightType(source) = strobe;
        std::vector<int32_t> levels;
        for (int i = 0; i < 4; i++) {
            ut::levelinfo::TimeSeconds(world.levelInfo) += 0.1f;
            levels.push_back(level());
        }
        CHECK(std::min(levels[0], levels[1]) == 128);
        CHECK(std::max(levels[0], levels[1]) == full);
        CHECK(levels[2] == levels[0]);
        CHECK(levels[3] == levels[1]);
        ut::audio::setLightRandom(nullptr);
        ut::actor::LightType(source) = 0;
        ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    }
    ut::audio::shutdown();
}

TEST_CASE("the view actor zone selects the Galaxy reverb and a tail follows the dry sound", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Peak"));
    REQUIRE(ut::audio::initializeOffline(44100));
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Peak"));
        ut::UObject* player = world.player();
        REQUIRE(player != nullptr);
        ut::UObject* reverbZone = nullptr;
        for (ut::UObject* actor : world.actors) {
            if (actor && actor->data && actor->name == ut::Name("ZoneInfo2")) {
                reverbZone = actor;
            }
        }
        REQUIRE(reverbZone != nullptr);
        ut::audio::setVolumes(1.0f, 0.0f, 1.0f);
        for (ut::UObject* actor : world.actors) {
            if (actor && actor->data) {
                ut::actor::AmbientSound(actor) = nullptr;
            }
        }
        REQUIRE(world.farMoveActor(player, ut::actor::Location(reverbZone), false, true));
        REQUIRE(ut::region(player).zone == reverbZone);
        ut::USound tone;
        tone.bytes = constantWave(8000, 44100, 2000);
        auto tailPeak = [&]() {
            int32_t id = ut::audio::playSound(&tone, nullptr, ut::audio::SlotNone, {}, 1.0f, 1000.0f, 1.0f, false);
            REQUIRE(id != 0);
            ut::audio::update(ut::actor::Location(player), {}, 0.0f);
            std::vector<float> buffer(4410 * 2);
            ut::audio::render(buffer.data(), 2100);
            CHECK_FALSE(ut::audio::isPlaying(id));
            ut::audio::render(buffer.data(), 4410);
            float peak = 0;
            for (float sample : buffer) {
                peak = std::max(peak, std::fabs(sample));
            }
            return peak;
        };
        ut::audio::update(ut::actor::Location(player), {}, 0.0f);
        ut::audio::ReverbSettings settings = ut::audio::reverbSettings();
        CHECK(settings.volume == Catch::Approx(100.0 / 255.0).epsilon(1e-6));
        CHECK(settings.cutoff == 6000.0f);
        CHECK(settings.delay[0] == Catch::Approx(0.040).epsilon(1e-6));
        CHECK(settings.delay[1] == Catch::Approx(0.068).epsilon(1e-6));
        CHECK(settings.delay[2] == 0.001f);
        CHECK(settings.delay[5] == 0.001f);
        CHECK(settings.gain[0] == Catch::Approx(50.0 / 255.0).epsilon(1e-6));
        CHECK(settings.gain[1] == Catch::Approx(70.0 / 255.0).epsilon(1e-6));
        CHECK(settings.gain[2] == 0.001f);
        CHECK(tailPeak() > 0.01f);

        ut::zoneinfo::bReverbZone(reverbZone) = 0;
        ut::audio::update(ut::actor::Location(player), {}, 0.0f);
        CHECK(ut::audio::reverbSettings().volume == 0.0f);
        CHECK(tailPeak() == 0.0f);
        ut::objects::unloadPackage(ut::Name("DM-Peak"));
    }
    ut::audio::shutdown();
}

TEST_CASE("an obstructed sound fades to a third over half a second and recovers when clear", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    REQUIRE(ut::audio::initializeOffline(44100));
    ut::audio::setVolumes(1.0f, 1.0f, 1.0f);
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Deck16]["));
        ut::UObject* source = nullptr;
        for (ut::UObject* actor : world.actors) {
            if (actor && actor->data && !actor->destroyed && actor->isA(ut::Name("Inventory"))) {
                source = actor;
                break;
            }
        }
        REQUIRE(source != nullptr);
        listenAt(world, ut::actor::Location(source) + ut::Vector { 0, 0, 30 });
        ut::UObject* player = world.player();
        ut::Vector eye = ut::actor::Location(player);
        eye.z += ut::pawn::EyeHeight(player);
        ut::actor::Location(source) = { eye.x, eye.y, eye.z - 4000.0f };
        REQUIRE_FALSE(world.fastLineCheck(eye, ut::actor::Location(source)));
        ut::USound sound;
        loadSound(sound, "AmbCity", "Ambh01a");
        const int32_t slotMisc = 1;
        int32_t id = ut::audio::playSound(&sound, source, slotMisc, ut::actor::Location(source), 1.0f, 100000.0f, 1.0f, true);
        REQUIRE(id != 0);
        auto status = [id]() {
            for (const ut::audio::VoiceStatus& voice : ut::audio::voices()) {
                if (voice.id == id) {
                    return voice;
                }
            }
            return ut::audio::VoiceStatus {};
        };
        ut::audio::update(eye, {}, 0.2f);
        CHECK(status().obstruction == Catch::Approx(0.2f));
        for (int i = 0; i < 3; i++) {
            ut::audio::update(eye, {}, 0.2f);
        }
        CHECK(status().obstruction == 0.5f);
        CHECK(status().volume == 10380);

        ut::actor::Location(source) = { eye.x + 10.0f, eye.y, eye.z };
        REQUIRE(world.fastLineCheck(eye, ut::actor::Location(source)));
        ut::audio::update(eye, {}, 0.2f);
        CHECK(status().obstruction == Catch::Approx(0.3f));
        ut::audio::update(eye, {}, 0.5f);
        CHECK(status().obstruction == 0.0f);
        ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    }
    ut::audio::shutdown();
}

TEST_CASE("ambient sounds stop while the game is paused and moving ambient sources are Doppler shifted", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    REQUIRE(ut::audio::initializeOffline(44100));
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Deck16]["));
        ut::UObject* source = ambientSource(world);
        REQUIRE(source != nullptr);
        ut::Vector listener = ut::actor::Location(source) + ut::Vector { 100, 0, 0 };
        listenAt(world, listener);
        auto ambient = []() {
            std::vector<ut::audio::VoiceStatus> result;
            for (const ut::audio::VoiceStatus& voice : ut::audio::voices()) {
                if (voice.ambient) {
                    result.push_back(voice);
                }
            }
            return result;
        };
        ut::audio::update(listener, {}, 0.0f);
        std::vector<ut::audio::VoiceStatus> playing = ambient();
        REQUIRE_FALSE(playing.empty());

        ut::levelinfo::Pauser(world.levelInfo) = "Player";
        ut::audio::update(listener, {}, 0.0f);
        CHECK(ambient().empty());
        ut::levelinfo::Pauser(world.levelInfo).clear();
        ut::audio::update(listener, {}, 0.0f);
        playing = ambient();
        REQUIRE_FALSE(playing.empty());

        ut::UObject* mover = const_cast<ut::UObject*>(playing.front().actor);
        double still = playing.front().step;
        ut::Vector away = ut::actor::Location(mover) - listener;
        ut::actor::Velocity(mover) = away.normal() * 650.0f;
        ut::audio::update(listener, {}, 0.0f);
        double receding = 0;
        for (const ut::audio::VoiceStatus& voice : ambient()) {
            if (voice.actor == mover) {
                receding = voice.step;
            }
        }
        CHECK(receding / still == Catch::Approx(1.0 - 650.0 / 9000.0).epsilon(1e-3));
        ut::actor::Velocity(mover) = {};
        ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    }
    ut::audio::shutdown();
}

TEST_CASE("level music starts at login with the level's song and section", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    REQUIRE(ut::audio::initializeOffline(44100));
    {
        ut::World world;
        REQUIRE(world.loadMap("DM-Deck16]["));
        ut::UObject* player = world.player();
        REQUIRE(player != nullptr);
        ut::audio::update(ut::actor::Location(player), {}, 0.0f);
        ut::audio::MusicStatus status = ut::audio::musicStatus();
        REQUIRE(status.music != nullptr);
        CHECK(status.music == ut::levelinfo::Song(world.levelInfo));
        CHECK(status.section == ut::levelinfo::SongSection(world.levelInfo));
        ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    }
    ut::audio::shutdown();
}
