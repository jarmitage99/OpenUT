#include "audio/module.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ut::audio {
    namespace {
        constexpr uint8_t CmdSpeed = 1;
        constexpr uint8_t CmdJump = 2;
        constexpr uint8_t CmdBreak = 3;
        constexpr uint8_t CmdVolumeSlide = 4;
        constexpr uint8_t CmdPitchDown = 5;
        constexpr uint8_t CmdPitchUp = 6;
        constexpr uint8_t CmdPortamento = 7;
        constexpr uint8_t CmdVibrato = 8;
        constexpr uint8_t CmdTremor = 9;
        constexpr uint8_t CmdVibratoSlide = 11;
        constexpr uint8_t CmdPortamentoSlide = 12;
        constexpr uint8_t CmdChannelVolume = 13;
        constexpr uint8_t CmdChannelVolumeSlide = 14;
        constexpr uint8_t CmdOffset = 15;
        constexpr uint8_t CmdPanSlide = 16;
        constexpr uint8_t CmdRetrigger = 17;
        constexpr uint8_t CmdSpecial = 19;
        constexpr uint8_t CmdTempo = 20;
        constexpr uint8_t CmdFineVibrato = 21;
        constexpr uint8_t CmdGlobalVolume = 22;
        constexpr uint8_t CmdGlobalVolumeSlide = 23;
        constexpr uint8_t CmdPan = 24;

        constexpr uint8_t SampleExists = 0x01;
        constexpr uint8_t Sample16Bit = 0x02;
        constexpr uint8_t SampleStereo = 0x04;
        constexpr uint8_t SampleCompressed = 0x08;
        constexpr uint8_t SampleLoop = 0x10;
        constexpr uint8_t SampleSustain = 0x20;
        constexpr uint8_t SamplePingPong = 0x40;

        constexpr uint8_t EnvelopeEnabled = 0x01;
        constexpr uint8_t EnvelopeLoop = 0x02;
        constexpr uint8_t EnvelopeSustain = 0x04;

        constexpr int32_t MaxPan = 0x7fff;
        constexpr int32_t MaxVolume = 0x7fff;

        int32_t waveValue(uint8_t wave, int32_t index)
        {
            switch (wave & 3) {
            case 1:
                return static_cast<int32_t>(std::lround(255.0 - static_cast<double>(index) * 510.0 / 63.0));
            case 2:
                return index < 32 ? 255 : -255;
            default:
                return static_cast<int32_t>(255.0 * std::sin(2.0 * 3.14159265358979 * static_cast<double>(index) / 64.0));
            }
        }

        const std::array<int16_t, 32768>& panTable()
        {
            static const std::array<int16_t, 32768> table = [] {
                std::array<int16_t, 32768> values {};
                for (size_t i = 0; i < values.size(); i++) {
                    values[i] = static_cast<int16_t>(std::sqrt(static_cast<long double>(i) * static_cast<long double>(1.0 / 32767.0)) * 32767.0L);
                }
                return values;
            }();
            return table;
        }

        constexpr int32_t SurroundPan = 0x8000;

        int32_t headerPan(uint8_t pan)
        {
            return pan <= 64 ? std::min(pan * 2, 127) << 8 : 0xa000;
        }

        int32_t commandPan(uint8_t param)
        {
            if (param < 0x80) {
                return param << 8;
            }
            if (param == 0x80) {
                return 0x7f << 8;
            }
            int32_t shifted = param - 4;
            if (shifted >= 0x80 && shifted <= 0xc0) {
                return (std::min((shifted & 0x7f) * 2, 0x7f) | 0x80) << 8;
            }
            return 0xc0 << 8;
        }

        bool sharesMemory(uint8_t command)
        {
            return command == CmdSpeed || command == CmdVolumeSlide || command == CmdPitchDown || command == CmdPitchUp || command == CmdVibratoSlide || command == CmdPortamentoSlide;
        }

        void convertCommand(ItCell& cell)
        {
            uint8_t command = cell.command;
            uint8_t high = cell.param >> 4;
            bool dropped = command == CmdTremor || command == CmdChannelVolume || command == CmdChannelVolumeSlide || command == CmdPanSlide || command == CmdFineVibrato || command == CmdGlobalVolume || command == CmdGlobalVolumeSlide || command > CmdPan;
            if (command == CmdSpecial && (high == 0x2 || high == 0x5 || high == 0x6 || high == 0x7 || high == 0xA)) {
                dropped = true;
            }
            if (dropped) {
                cell.command = 0;
                cell.param = 0;
            } else if (command == CmdSpeed) {
                cell.param = std::min<uint8_t>(cell.param, 0x20);
            } else if (command == CmdRetrigger) {
                cell.param = static_cast<uint8_t>(cell.param & 0x0f);
            } else if (command == CmdTempo && cell.param < 0x20) {
                cell.command = CmdVibrato;
            }
        }

        int32_t songLength(const std::vector<uint8_t>& orders)
        {
            size_t length = std::min<size_t>(orders.size(), 255);
            for (size_t i = 0; i < length; i++) {
                if (orders[i] == 255) {
                    return static_cast<int32_t>(i);
                }
            }
            return static_cast<int32_t>(length);
        }

        uint16_t u16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        uint32_t u32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        std::string text(const uint8_t* p, size_t length)
        {
            std::string result(reinterpret_cast<const char*>(p), length);
            result.resize(std::strlen(result.c_str()));
            return result;
        }

        void readEnvelope(const uint8_t* p, ItEnvelope& envelope)
        {
            envelope.flags = p[0];
            uint8_t count = std::min<uint8_t>(p[1], 10);
            envelope.loopBegin = p[2];
            envelope.loopEnd = p[3];
            envelope.sustainBegin = p[4];
            envelope.sustainEnd = p[5];
            for (uint8_t i = 0; i < count; i++) {
                const uint8_t* node = p + 6 + i * 3;
                envelope.nodes.emplace_back(static_cast<int8_t>(node[0]), u16(node + 1));
            }
            if (envelope.nodes.empty()) {
                envelope.flags = 0;
            }
        }

        constexpr uint8_t CmdArpeggio = 10;
        constexpr uint8_t CmdTremolo = 18;
        constexpr int16_t S3mNoteOffset = 12;
        constexpr int16_t XmNoteOffset = 11;

        void finishCell(ItCell& cell, uint8_t& memory, bool hasCommand, bool cut)
        {
            if (hasCommand && !sharesMemory(cell.command)) {
                memory = cell.param;
            }
            if (sharesMemory(cell.command)) {
                if (cell.param == 0) {
                    cell.param = memory;
                }
                memory = cell.param;
            }
            convertCommand(cell);
            if (cell.volume > 64) {
                cell.volume = 255;
            }
            if (cut) {
                cell.volume = 0;
            }
        }

        void decodeSample(ItSample& sample, const uint8_t* source, size_t length, bool wide, bool isSigned, size_t channelsInFile, bool delta)
        {
            sample.data.resize(length);
            int32_t previous[2] = {};
            for (size_t s = 0; s < length; s++) {
                float value = 0;
                for (size_t c = 0; c < channelsInFile; c++) {
                    size_t index = c * length + s;
                    int32_t v = 0;
                    if (wide) {
                        uint16_t raw = u16(source + index * 2);
                        v = isSigned ? static_cast<int16_t>(raw) : static_cast<int32_t>(raw) - 32768;
                        if (delta) {
                            previous[c] = static_cast<int16_t>(previous[c] + v);
                            v = previous[c];
                        }
                        value += static_cast<float>(v) / 32768.0f;
                    } else {
                        uint8_t raw = source[index];
                        v = isSigned ? static_cast<int8_t>(raw) : static_cast<int32_t>(raw) - 128;
                        if (delta) {
                            previous[c] = static_cast<int8_t>(previous[c] + v);
                            v = previous[c];
                        }
                        value += static_cast<float>(v) / 128.0f;
                    }
                }
                sample.data[s] = value / static_cast<float>(channelsInFile);
            }
        }

        bool loadS3m(ItModule& module, const uint8_t* bytes, size_t size)
        {
            if (size < 0x60) {
                return false;
            }
            module.title = text(bytes, 28);
            uint16_t orderCount = u16(bytes + 0x20);
            uint16_t sampleCount = u16(bytes + 0x22);
            uint16_t patternCount = u16(bytes + 0x24);
            bool isSigned = u16(bytes + 0x2A) == 1;
            module.flags = 0;
            module.globalVolume = static_cast<uint8_t>(std::min(bytes[0x30] * 2, 128));
            module.speed = bytes[0x31] ? bytes[0x31] : 6;
            module.tempo = bytes[0x32] >= 32 ? bytes[0x32] : 125;
            module.mixVolume = bytes[0x33] & 0x7f;
            bool stereo = (bytes[0x33] & 0x80) != 0;
            size_t tables = 0x60 + orderCount + (static_cast<size_t>(sampleCount) + patternCount) * 2;
            bool panTable = bytes[0x35] == 252;
            if (tables + (panTable ? 32 : 0) > size) {
                return false;
            }
            module.orders.assign(bytes + 0x60, bytes + 0x60 + orderCount);
            const uint8_t* samplePointers = bytes + 0x60 + orderCount;
            const uint8_t* patternPointers = samplePointers + sampleCount * 2;
            for (size_t channel = 0; channel < 64; channel++) {
                module.channelVolume[channel] = 64;
                uint8_t setting = channel < 32 ? bytes[0x40 + channel] : 255;
                if (setting >= 16) {
                    module.channelPan[channel] = 0x80 | 32;
                    continue;
                }
                int32_t pan = stereo ? (setting < 8 ? 3 : 12) : 8;
                if (panTable && (bytes[tables + channel] & 0x20)) {
                    pan = bytes[tables + channel] & 0x0f;
                }
                module.channelPan[channel] = static_cast<uint8_t>(std::min(pan * 4 + 2, 64));
            }

            module.samples.resize(sampleCount);
            for (size_t i = 0; i < sampleCount; i++) {
                size_t offset = static_cast<size_t>(u16(samplePointers + i * 2)) * 16;
                if (offset + 0x50 > size) {
                    continue;
                }
                const uint8_t* p = bytes + offset;
                ItSample& sample = module.samples[i];
                sample.name = text(p + 0x30, 28);
                sample.flags = 0;
                if (p[0] != 1 || p[0x1E] != 0) {
                    continue;
                }
                size_t pointer = ((static_cast<size_t>(p[0x0D]) << 16) | u16(p + 0x0E)) * 16;
                uint32_t length = u32(p + 0x10);
                sample.loopBegin = u32(p + 0x14);
                sample.loopEnd = u32(p + 0x18);
                sample.volume = std::min<uint8_t>(p[0x1C], 64);
                uint8_t sampleFlags = p[0x1F];
                sample.c5speed = u32(p + 0x20);
                bool wide = (sampleFlags & 4) != 0;
                size_t channelsInFile = (sampleFlags & 2) ? 2 : 1;
                size_t needed = static_cast<size_t>(length) * (wide ? 2 : 1) * channelsInFile;
                if (length == 0 || pointer + needed > size) {
                    continue;
                }
                sample.flags = static_cast<uint8_t>(SampleExists | (wide ? Sample16Bit : 0));
                decodeSample(sample, bytes + pointer, length, wide, isSigned, channelsInFile, false);
                sample.loopEnd = std::min(sample.loopEnd, length);
                if ((sampleFlags & 1) && sample.loopBegin < sample.loopEnd) {
                    sample.flags = static_cast<uint8_t>(sample.flags | SampleLoop);
                }
            }

            module.patterns.resize(patternCount);
            uint8_t memory[64] = {};
            for (size_t i = 0; i < patternCount; i++) {
                ItPattern& pattern = module.patterns[i];
                pattern.rows = 64;
                pattern.cells.assign(64 * 64, ItCell {});
                size_t offset = static_cast<size_t>(u16(patternPointers + i * 2)) * 16;
                if (offset == 0 || offset + 2 > size) {
                    continue;
                }
                const uint8_t* p = bytes + offset + 2;
                const uint8_t* end = std::min(bytes + offset + u16(bytes + offset), bytes + size);
                uint16_t row = 0;
                while (p < end && row < 64) {
                    uint8_t what = *p++;
                    if (what == 0) {
                        row++;
                        continue;
                    }
                    size_t channel = what & 31;
                    ItCell& cell = pattern.cells[static_cast<size_t>(row) * 64 + channel];
                    bool cut = false;
                    bool hasCommand = false;
                    if ((what & 0x20) && p + 1 < end) {
                        uint8_t note = *p++;
                        cell.instrument = *p++;
                        if (note == 254) {
                            cut = true;
                        } else if (note < 254) {
                            cell.note = static_cast<int16_t>(std::min((note >> 4) * 12 + (note & 0x0f) + S3mNoteOffset, 119));
                        }
                    }
                    if ((what & 0x40) && p < end) {
                        cell.volume = *p++;
                    }
                    if ((what & 0x80) && p + 1 < end) {
                        cell.command = *p++;
                        cell.param = *p++;
                        hasCommand = true;
                    }
                    finishCell(cell, memory[channel], hasCommand, cut);
                }
            }
            return !module.orders.empty();
        }

        void readXmEnvelope(const uint8_t* points, uint8_t count, uint8_t sustain, uint8_t loopBegin, uint8_t loopEnd, uint8_t type, int32_t center, ItEnvelope& envelope)
        {
            count = std::min<uint8_t>(count, 12);
            for (uint8_t i = 0; i < count; i++) {
                int32_t value = std::clamp(static_cast<int32_t>(u16(points + i * 4 + 2)), 0, 64) - center;
                envelope.nodes.emplace_back(static_cast<int8_t>(value), u16(points + i * 4));
            }
            envelope.flags = static_cast<uint8_t>(((type & 1) ? EnvelopeEnabled : 0) | ((type & 2) ? EnvelopeSustain : 0) | ((type & 4) ? EnvelopeLoop : 0));
            envelope.sustainBegin = sustain;
            envelope.sustainEnd = sustain;
            envelope.loopBegin = loopBegin;
            envelope.loopEnd = loopEnd;
            if (envelope.nodes.empty()) {
                envelope.flags = 0;
            }
        }

        struct XmMemory {
            uint8_t volumeSlide = 0;
            uint8_t portaUp = 0;
            uint8_t portaDown = 0;
            uint8_t finePortaUp = 0;
            uint8_t finePortaDown = 0;
            uint8_t fineVolumeUp = 0;
            uint8_t fineVolumeDown = 0;
        };

        uint8_t remember(uint8_t param, uint8_t& memory)
        {
            if (param != 0) {
                memory = param;
            }
            return memory;
        }

        uint8_t volumeSlideParam(uint8_t param)
        {
            return (param & 0xf0) ? static_cast<uint8_t>(param & 0xf0) : param;
        }

        void convertXmEffect(ItCell& cell, uint8_t effect, uint8_t param, XmMemory& memory)
        {
            uint8_t high = param >> 4;
            uint8_t low = param & 0x0f;
            cell.command = 0;
            cell.param = 0;
            switch (effect) {
            case 0x0:
                if (param != 0) {
                    cell.command = CmdArpeggio;
                    cell.param = param;
                }
                break;
            case 0x1:
                cell.command = CmdPitchUp;
                cell.param = std::min<uint8_t>(remember(param, memory.portaUp), 0xdf);
                break;
            case 0x2:
                cell.command = CmdPitchDown;
                cell.param = std::min<uint8_t>(remember(param, memory.portaDown), 0xdf);
                break;
            case 0x3:
                cell.command = CmdPortamento;
                cell.param = param;
                break;
            case 0x4:
                cell.command = CmdVibrato;
                cell.param = param;
                break;
            case 0x5:
                cell.command = CmdPortamentoSlide;
                cell.param = volumeSlideParam(remember(param, memory.volumeSlide));
                break;
            case 0x6:
                cell.command = CmdVibratoSlide;
                cell.param = volumeSlideParam(remember(param, memory.volumeSlide));
                break;
            case 0x7:
                cell.command = CmdTremolo;
                cell.param = param;
                break;
            case 0x8:
                cell.command = CmdPan;
                cell.param = static_cast<uint8_t>(param >> 1);
                break;
            case 0x9:
                cell.command = CmdOffset;
                cell.param = param;
                break;
            case 0xA:
                cell.command = CmdVolumeSlide;
                cell.param = volumeSlideParam(remember(param, memory.volumeSlide));
                break;
            case 0xB:
                cell.command = CmdJump;
                cell.param = param;
                break;
            case 0xC:
                if (cell.volume == 255) {
                    cell.volume = std::min<uint8_t>(param, 64);
                }
                break;
            case 0xD:
                cell.command = CmdBreak;
                cell.param = param;
                break;
            case 0xE:
                switch (high) {
                case 0x1:
                    cell.command = CmdPitchUp;
                    cell.param = static_cast<uint8_t>(0xf0 | (remember(low, memory.finePortaUp) & 0x0f));
                    break;
                case 0x2:
                    cell.command = CmdPitchDown;
                    cell.param = static_cast<uint8_t>(0xf0 | (remember(low, memory.finePortaDown) & 0x0f));
                    break;
                case 0x4:
                    cell.command = CmdSpecial;
                    cell.param = static_cast<uint8_t>(0x30 | low);
                    break;
                case 0x6:
                    cell.command = CmdSpecial;
                    cell.param = static_cast<uint8_t>(0xB0 | low);
                    break;
                case 0x8:
                    cell.command = CmdSpecial;
                    cell.param = static_cast<uint8_t>(0x80 | low);
                    break;
                case 0x9:
                    cell.command = CmdRetrigger;
                    cell.param = low;
                    break;
                case 0xA:
                    cell.command = CmdVolumeSlide;
                    cell.param = static_cast<uint8_t>(((remember(low, memory.fineVolumeUp) & 0x0f) << 4) | 0x0f);
                    break;
                case 0xB:
                    cell.command = CmdVolumeSlide;
                    cell.param = static_cast<uint8_t>(0xf0 | (remember(low, memory.fineVolumeDown) & 0x0f));
                    break;
                case 0xC:
                case 0xD:
                case 0xE:
                    cell.command = CmdSpecial;
                    cell.param = param;
                    break;
                default:
                    break;
                }
                break;
            case 0xF:
                if (param != 0) {
                    cell.command = param < 0x20 ? CmdSpeed : CmdTempo;
                    cell.param = param;
                }
                break;
            case 0x10:
                cell.command = CmdGlobalVolume;
                cell.param = static_cast<uint8_t>(std::min(param * 2, 128));
                break;
            case 0x11:
                cell.command = CmdGlobalVolumeSlide;
                cell.param = param;
                break;
            case 0x14:
                if (param == 0 && cell.note == NoteNone) {
                    cell.note = NoteOff;
                }
                break;
            case 0x19:
                cell.command = CmdPanSlide;
                cell.param = param;
                break;
            case 0x1B:
                cell.command = CmdRetrigger;
                cell.param = param;
                break;
            case 0x1D:
                cell.command = CmdTremor;
                cell.param = param;
                break;
            case 0x21:
                if (high == 1 || high == 2) {
                    cell.command = high == 1 ? CmdPitchUp : CmdPitchDown;
                    cell.param = static_cast<uint8_t>(0xe0 | low);
                }
                break;
            default:
                break;
            }
        }

        void convertXmVolumeColumn(ItCell& cell, uint8_t volume)
        {
            uint8_t low = volume & 0x0f;
            if (volume >= 0x10 && volume <= 0x50) {
                cell.volume = static_cast<uint8_t>(volume - 0x10);
                return;
            }
            if (cell.command != 0) {
                return;
            }
            switch (volume >> 4) {
            case 0x6:
                cell.command = CmdVolumeSlide;
                cell.param = low;
                break;
            case 0x7:
                cell.command = CmdVolumeSlide;
                cell.param = static_cast<uint8_t>(low << 4);
                break;
            case 0x8:
                cell.command = CmdVolumeSlide;
                cell.param = static_cast<uint8_t>(0xf0 | low);
                break;
            case 0x9:
                cell.command = CmdVolumeSlide;
                cell.param = static_cast<uint8_t>((low << 4) | 0x0f);
                break;
            case 0xB:
                cell.command = CmdVibrato;
                cell.param = low;
                break;
            case 0xC:
                cell.command = CmdPan;
                cell.param = static_cast<uint8_t>(low * 8 + 4);
                break;
            case 0xF:
                cell.command = CmdPortamento;
                cell.param = static_cast<uint8_t>(low << 4);
                break;
            default:
                break;
            }
        }

        bool loadXm(ItModule& module, const uint8_t* bytes, size_t size)
        {
            if (size < 80 + 256 || u16(bytes + 58) < 0x0104) {
                return false;
            }
            module.title = text(bytes + 17, 20);
            size_t headerSize = u32(bytes + 60);
            uint16_t songLength = std::min<uint16_t>(u16(bytes + 64), 256);
            uint16_t channelCount = std::min<uint16_t>(u16(bytes + 68), 64);
            uint16_t patternCount = u16(bytes + 70);
            uint16_t instrumentCount = u16(bytes + 72);
            module.flags = 0x04;
            module.globalVolume = 128;
            module.speed = static_cast<uint8_t>(std::clamp<uint16_t>(u16(bytes + 76), 1, 31));
            module.tempo = static_cast<uint8_t>(std::clamp<uint16_t>(u16(bytes + 78), 32, 255));
            module.orders.assign(bytes + 80, bytes + 80 + songLength);
            for (size_t channel = 0; channel < 64; channel++) {
                module.channelVolume[channel] = 64;
                module.channelPan[channel] = static_cast<uint8_t>(channel < channelCount ? 32 : 0x80 | 32);
            }

            size_t offset = 60 + headerSize;
            uint16_t usedPatterns = patternCount;
            for (uint8_t order : module.orders) {
                usedPatterns = std::max<uint16_t>(usedPatterns, static_cast<uint16_t>(order + 1));
            }
            module.patterns.resize(usedPatterns);
            for (ItPattern& pattern : module.patterns) {
                pattern.rows = 64;
                pattern.cells.assign(64 * 64, ItCell {});
            }
            XmMemory memory[64];
            for (size_t i = 0; i < patternCount; i++) {
                if (offset + 9 > size) {
                    return false;
                }
                size_t patternHeader = u32(bytes + offset);
                uint16_t rows = std::max<uint16_t>(u16(bytes + offset + 5), 1);
                uint16_t packed = u16(bytes + offset + 7);
                ItPattern& pattern = module.patterns[i];
                pattern.rows = rows;
                pattern.cells.assign(static_cast<size_t>(rows) * 64, ItCell {});
                const uint8_t* p = bytes + offset + patternHeader;
                const uint8_t* end = std::min(p + packed, bytes + size);
                for (uint16_t row = 0; row < rows && p < end; row++) {
                    for (uint16_t channel = 0; channel < channelCount && p < end; channel++) {
                        uint8_t note = 0;
                        uint8_t instrument = 0;
                        uint8_t volume = 0;
                        uint8_t effect = 0;
                        uint8_t param = 0;
                        uint8_t first = *p++;
                        uint8_t mask = (first & 0x80) ? first : 0x1f;
                        if (!(first & 0x80)) {
                            note = first;
                            mask = 0x1e;
                        }
                        if ((mask & 0x01) && p < end) {
                            note = *p++;
                        }
                        if ((mask & 0x02) && p < end) {
                            instrument = *p++;
                        }
                        if ((mask & 0x04) && p < end) {
                            volume = *p++;
                        }
                        if ((mask & 0x08) && p < end) {
                            effect = *p++;
                        }
                        if ((mask & 0x10) && p < end) {
                            param = *p++;
                        }
                        ItCell& cell = pattern.cells[static_cast<size_t>(row) * 64 + channel];
                        if (note == 97) {
                            cell.note = NoteOff;
                        } else if (note >= 1 && note <= 96) {
                            cell.note = static_cast<int16_t>(note + XmNoteOffset);
                        }
                        cell.instrument = instrument;
                        convertXmEffect(cell, effect, param, memory[channel]);
                        convertXmVolumeColumn(cell, volume);
                        convertCommand(cell);
                        if (cell.volume > 64) {
                            cell.volume = 255;
                        }
                    }
                }
                offset += patternHeader + packed;
            }

            module.instruments.resize(instrumentCount);
            for (size_t i = 0; i < instrumentCount; i++) {
                if (offset + 29 > size) {
                    return false;
                }
                const uint8_t* p = bytes + offset;
                size_t instrumentSize = u32(p);
                uint16_t sampleCount = u16(p + 27);
                ItInstrument& instrument = module.instruments[i];
                instrument.defaultPan = 0x80;
                for (size_t n = 0; n < 120; n++) {
                    instrument.noteMap[n] = static_cast<uint8_t>(n);
                }
                if (sampleCount == 0) {
                    offset += instrumentSize;
                    continue;
                }
                if (offset + 241 > size) {
                    return false;
                }
                size_t sampleHeaderSize = u32(p + 29);
                size_t base = module.samples.size();
                for (size_t n = 0; n < 120; n++) {
                    int32_t key = static_cast<int32_t>(n) - XmNoteOffset - 1;
                    uint8_t mapped = key >= 0 && key < 96 ? p[33 + key] : 0;
                    instrument.sampleMap[n] = mapped < sampleCount ? static_cast<uint8_t>(std::min<size_t>(base + mapped + 1, 255)) : 0;
                }
                readXmEnvelope(p + 129, p[225], p[227], p[228], p[229], p[233], 0, instrument.volumeEnvelope);
                readXmEnvelope(p + 177, p[226], p[230], p[231], p[232], p[234], 32, instrument.panEnvelope);
                uint16_t fadeOut = u16(p + 239);
                instrument.fadeOut = static_cast<uint16_t>((fadeOut + 63) / 64);
                offset += instrumentSize;
                std::vector<size_t> lengths;
                for (size_t s = 0; s < sampleCount; s++) {
                    if (offset + 40 > size) {
                        return false;
                    }
                    const uint8_t* h = bytes + offset;
                    ItSample sample;
                    uint8_t type = h[14];
                    bool wide = (type & 0x10) != 0;
                    size_t unit = wide ? 2 : 1;
                    uint32_t length = u32(h);
                    uint32_t loopStart = u32(h + 4) / static_cast<uint32_t>(unit);
                    uint32_t loopLength = u32(h + 8) / static_cast<uint32_t>(unit);
                    sample.volume = std::min<uint8_t>(h[12], 64);
                    double transpose = static_cast<double>(static_cast<int8_t>(h[16])) + static_cast<double>(static_cast<int8_t>(h[13])) / 128.0;
                    sample.c5speed = static_cast<uint32_t>(std::lround(8363.0 * std::pow(2.0, transpose / 12.0)));
                    sample.defaultPan = static_cast<uint8_t>(0x80 | (h[15] * 64 / 255));
                    sample.name = text(h + 18, 22);
                    sample.flags = wide ? Sample16Bit : 0;
                    sample.loopBegin = loopStart;
                    sample.loopEnd = loopStart + loopLength;
                    if ((type & 3) != 0 && loopLength > 0) {
                        sample.flags = static_cast<uint8_t>(sample.flags | SampleLoop | ((type & 3) == 2 ? SamplePingPong : 0));
                    }
                    lengths.push_back(length);
                    module.samples.push_back(sample);
                    offset += sampleHeaderSize;
                }
                for (size_t s = 0; s < sampleCount; s++) {
                    ItSample& sample = module.samples[base + s];
                    size_t length = lengths[s];
                    if (offset + length > size) {
                        return false;
                    }
                    bool wide = (sample.flags & Sample16Bit) != 0;
                    size_t frames = wide ? length / 2 : length;
                    if (frames > 0) {
                        decodeSample(sample, bytes + offset, frames, wide, true, 1, true);
                        sample.flags = static_cast<uint8_t>(sample.flags | SampleExists);
                        sample.loopEnd = std::min<uint32_t>(sample.loopEnd, static_cast<uint32_t>(frames));
                        if (sample.loopBegin >= sample.loopEnd) {
                            sample.flags = static_cast<uint8_t>(sample.flags & ~(SampleLoop | SamplePingPong));
                        }
                    }
                    offset += length;
                }
            }
            return !module.orders.empty();
        }
    }

    bool ItModule::useInstruments() const
    {
        return (flags & 0x04) != 0 && !instruments.empty();
    }

    bool ItModule::load(const uint8_t* bytes, size_t size)
    {
        if (size >= 0x30 && std::memcmp(bytes + 0x2C, "SCRM", 4) == 0) {
            return loadS3m(*this, bytes, size);
        }
        if (size >= 17 && std::memcmp(bytes, "Extended Module: ", 17) == 0) {
            return loadXm(*this, bytes, size);
        }
        if (size < 0xC0 || std::memcmp(bytes, "IMPM", 4) != 0) {
            return false;
        }
        title = text(bytes + 4, 26);
        uint16_t orderCount = u16(bytes + 0x20);
        uint16_t instrumentCount = u16(bytes + 0x22);
        uint16_t sampleCount = u16(bytes + 0x24);
        uint16_t patternCount = u16(bytes + 0x26);
        uint16_t compatible = u16(bytes + 0x2A);
        flags = u16(bytes + 0x2C);
        globalVolume = bytes[0x30];
        mixVolume = bytes[0x31];
        speed = bytes[0x32];
        tempo = bytes[0x33];
        separation = bytes[0x34];
        std::memcpy(channelPan.data(), bytes + 0x40, 64);
        std::memcpy(channelVolume.data(), bytes + 0x80, 64);
        size_t tables = 0xC0 + orderCount + (static_cast<size_t>(instrumentCount) + sampleCount + patternCount) * 4;
        if (tables > size) {
            return false;
        }
        orders.assign(bytes + 0xC0, bytes + 0xC0 + orderCount);
        const uint8_t* instrumentOffsets = bytes + 0xC0 + orderCount;
        const uint8_t* sampleOffsets = instrumentOffsets + instrumentCount * 4;
        const uint8_t* patternOffsets = sampleOffsets + sampleCount * 4;

        instruments.resize(instrumentCount);
        for (size_t i = 0; i < instrumentCount; i++) {
            uint32_t offset = u32(instrumentOffsets + i * 4);
            if (compatible < 0x200 || offset + 0x226 > size) {
                continue;
            }
            const uint8_t* p = bytes + offset;
            ItInstrument& instrument = instruments[i];
            instrument.newNoteAction = p[0x11];
            instrument.fadeOut = u16(p + 0x14);
            instrument.globalVolume = p[0x18];
            instrument.defaultPan = p[0x19];
            for (size_t n = 0; n < 120; n++) {
                instrument.noteMap[n] = p[0x40 + n * 2];
                instrument.sampleMap[n] = p[0x41 + n * 2];
            }
            readEnvelope(p + 0x130, instrument.volumeEnvelope);
            readEnvelope(p + 0x182, instrument.panEnvelope);
            readEnvelope(p + 0x1D4, instrument.pitchEnvelope);
        }

        samples.resize(sampleCount);
        for (size_t i = 0; i < sampleCount; i++) {
            uint32_t offset = u32(sampleOffsets + i * 4);
            if (offset + 0x50 > size) {
                continue;
            }
            const uint8_t* p = bytes + offset;
            ItSample& sample = samples[i];
            sample.globalVolume = p[0x11];
            sample.flags = p[0x12];
            sample.volume = p[0x13];
            sample.name = text(p + 0x14, 26);
            uint8_t convert = p[0x2E];
            sample.defaultPan = p[0x2F];
            uint32_t length = u32(p + 0x30);
            sample.loopBegin = u32(p + 0x34);
            sample.loopEnd = u32(p + 0x38);
            sample.c5speed = u32(p + 0x3C);
            sample.sustainBegin = u32(p + 0x40);
            sample.sustainEnd = u32(p + 0x44);
            uint32_t pointer = u32(p + 0x48);
            sample.vibratoSpeed = p[0x4C];
            sample.vibratoDepth = p[0x4D];
            sample.vibratoRate = p[0x4E];
            sample.vibratoType = p[0x4F];
            if (!(sample.flags & SampleExists) || (sample.flags & SampleCompressed) || length == 0) {
                sample.flags = static_cast<uint8_t>(sample.flags & ~SampleExists);
                continue;
            }
            bool wide = (sample.flags & Sample16Bit) != 0;
            bool isSigned = (convert & 1) != 0;
            size_t channelsInFile = (sample.flags & SampleStereo) ? 2 : 1;
            size_t bytesPerSample = wide ? 2 : 1;
            size_t needed = static_cast<size_t>(length) * bytesPerSample * channelsInFile;
            if (pointer + needed > size) {
                sample.flags = static_cast<uint8_t>(sample.flags & ~SampleExists);
                continue;
            }
            sample.data.resize(length);
            const uint8_t* source = bytes + pointer;
            for (size_t s = 0; s < length; s++) {
                float value = 0;
                for (size_t c = 0; c < channelsInFile; c++) {
                    size_t index = c * length + s;
                    float component;
                    if (wide) {
                        uint16_t raw = u16(source + index * 2);
                        int32_t v = isSigned ? static_cast<int16_t>(raw) : static_cast<int32_t>(raw) - 32768;
                        component = static_cast<float>(v) / 32768.0f;
                    } else {
                        uint8_t raw = source[index];
                        int32_t v = isSigned ? static_cast<int8_t>(raw) : static_cast<int32_t>(raw) - 128;
                        component = static_cast<float>(v) / 128.0f;
                    }
                    value += component;
                }
                sample.data[s] = value / static_cast<float>(channelsInFile);
            }
            sample.loopEnd = std::min(sample.loopEnd, length);
            sample.sustainEnd = std::min(sample.sustainEnd, length);
            if (sample.loopBegin >= sample.loopEnd) {
                sample.flags = static_cast<uint8_t>(sample.flags & ~SampleLoop);
            }
            if (sample.sustainBegin >= sample.sustainEnd) {
                sample.flags = static_cast<uint8_t>(sample.flags & ~SampleSustain);
            }
        }

        patterns.resize(patternCount);
        uint8_t masks[64] = {};
        ItCell last[64];
        uint8_t memory[64] = {};
        for (size_t i = 0; i < patternCount; i++) {
            uint32_t offset = u32(patternOffsets + i * 4);
            ItPattern& pattern = patterns[i];
            if (offset == 0 || offset + 8 > size) {
                pattern.rows = 64;
                pattern.cells.assign(64 * 64, ItCell {});
                continue;
            }
            uint16_t length = u16(bytes + offset);
            pattern.rows = u16(bytes + offset + 2);
            pattern.cells.assign(static_cast<size_t>(pattern.rows) * 64, ItCell {});
            const uint8_t* p = bytes + offset + 8;
            const uint8_t* end = std::min(p + length, bytes + size);
            uint16_t row = 0;
            while (p < end && row < pattern.rows) {
                uint8_t channelVariable = *p++;
                if (channelVariable == 0) {
                    row++;
                    continue;
                }
                uint8_t channel = static_cast<uint8_t>((channelVariable - 1) & 63);
                if ((channelVariable & 0x80) && p < end) {
                    masks[channel] = *p++;
                }
                uint8_t mask = masks[channel];
                ItCell& cell = pattern.cells[static_cast<size_t>(row) * 64 + channel];
                bool cut = false;
                if ((mask & 0x01) && p < end) {
                    uint8_t note = *p++;
                    cut = note == 254;
                    last[channel].note = note < 120 || note == 255 ? static_cast<int16_t>(note) : (cut ? NoteNone : static_cast<int16_t>(253));
                    cell.note = last[channel].note;
                }
                if ((mask & 0x02) && p < end) {
                    last[channel].instrument = *p++;
                    cell.instrument = last[channel].instrument;
                }
                if ((mask & 0x04) && p < end) {
                    last[channel].volume = *p++;
                    cell.volume = last[channel].volume;
                }
                if ((mask & 0x08) && p + 1 < end) {
                    last[channel].command = *p++;
                    cell.command = last[channel].command;
                    cell.param = *p++;
                    if (!sharesMemory(cell.command)) {
                        memory[channel] = cell.param;
                    }
                }
                if (mask & 0x10) {
                    cell.note = last[channel].note;
                }
                if (mask & 0x20) {
                    cell.instrument = last[channel].instrument;
                }
                if (mask & 0x40) {
                    cell.volume = last[channel].volume;
                }
                if (mask & 0x80) {
                    cell.command = last[channel].command;
                    cell.param = memory[channel];
                }
                if (sharesMemory(cell.command)) {
                    if (cell.param == 0) {
                        cell.param = memory[channel];
                    }
                    memory[channel] = cell.param;
                }
                convertCommand(cell);
                if (cell.volume > 64) {
                    cell.volume = 255;
                }
                if (cut) {
                    cell.volume = 0;
                }
            }
        }
        return !orders.empty();
    }

    ModulePlayer::ModulePlayer(std::shared_ptr<const ItModule> module, uint32_t sampleRate)
        : song(std::move(module))
        , rate(sampleRate)
        , channels(64)
    {
        setPosition(0);
    }

    int32_t ModulePlayer::order() const
    {
        return currentOrder;
    }

    int32_t ModulePlayer::row() const
    {
        return currentRow;
    }

    void ModulePlayer::setPosition(int32_t newOrder)
    {
        for (Channel& channel : channels) {
            channel = Channel {};
        }
        resetSong();
        restart(newOrder);
    }

    void ModulePlayer::resetSong()
    {
        speed = std::max<int32_t>(song->speed, 1);
        tempo = std::max<int32_t>(song->tempo, 32);
        for (size_t i = 0; i < channels.size(); i++) {
            channels[i].pan = headerPan(song->channelPan[i]);
        }
    }

    void ModulePlayer::restart(int32_t newOrder)
    {
        for (Channel& channel : channels) {
            channel.active = false;
            channel.loopRow = 0;
            channel.loopCount = 0;
        }
        currentOrder = std::max(newOrder, 0);
        currentRow = 0;
        tickIndex = 0;
        patternDelay = 0;
        rowTicks = speed;
        breakRow = -1;
        jumpOrder = -1;
        loopTarget = -1;
        tickRemaining = 0;
        normalizeOrder();
    }

    void ModulePlayer::normalizeOrder()
    {
        const std::vector<uint8_t>& orders = song->orders;
        for (size_t guard = 0; guard <= orders.size() + 1; guard++) {
            if (currentOrder < 0 || static_cast<size_t>(currentOrder) >= orders.size() || orders[static_cast<size_t>(currentOrder)] == 255) {
                currentOrder = 0;
            }
            if (currentOrder == 0) {
                resetSong();
            }
            uint8_t pattern = orders[static_cast<size_t>(currentOrder)];
            if (pattern != 254 && pattern != 255 && pattern < song->patterns.size()) {
                return;
            }
            currentOrder++;
        }
        currentOrder = 0;
    }

    const ItPattern* ModulePlayer::currentPattern() const
    {
        if (static_cast<size_t>(currentOrder) >= song->orders.size()) {
            return nullptr;
        }
        uint8_t index = song->orders[static_cast<size_t>(currentOrder)];
        if (index >= song->patterns.size()) {
            return nullptr;
        }
        return &song->patterns[index];
    }

    double ModulePlayer::noteFrequency(const ItSample* sample, int32_t note) const
    {
        double c5 = sample ? static_cast<double>(sample->c5speed) : 8363.0;
        return c5 * std::pow(2.0, static_cast<double>(note - 60) / 12.0);
    }

    void ModulePlayer::slidePitch(Channel& channel, int32_t units)
    {
        channel.frequency *= std::pow(2.0, static_cast<double>(units) / 768.0);
        channel.frequency = std::clamp(channel.frequency, 1.0, 1000000.0);
    }

    void ModulePlayer::tonePortamento(Channel& channel, int32_t speedValue)
    {
        if (channel.targetFrequency <= 0 || channel.frequency <= 0) {
            return;
        }
        double factor = std::pow(2.0, static_cast<double>(speedValue * 4) / 768.0);
        if (channel.frequency < channel.targetFrequency) {
            channel.frequency = std::min(channel.frequency * factor, channel.targetFrequency);
        } else if (channel.frequency > channel.targetFrequency) {
            channel.frequency = std::max(channel.frequency / factor, channel.targetFrequency);
        }
    }

    void ModulePlayer::volumeSlide(Channel& channel, uint8_t param, bool firstTick)
    {
        int32_t up = param >> 4;
        int32_t down = param & 0x0f;
        if (down == 0x0f && up != 0) {
            if (firstTick) {
                channel.volume += up << 9;
            }
        } else if (up == 0x0f && down != 0) {
            if (firstTick) {
                channel.volume -= down << 9;
            }
        } else if (!firstTick) {
            if (down == 0) {
                channel.volume += up << 9;
            } else {
                channel.volume -= down << 9;
            }
        }
        channel.volume = std::clamp(channel.volume, 0, MaxVolume);
    }

    void ModulePlayer::retrigger(Channel& channel)
    {
        int32_t interval = channel.param & 0x0f;
        if (interval != 0 && tickIndex % interval == 0) {
            channel.position = 0;
            channel.direction = 1;
        }
    }

    void ModulePlayer::triggerNote(Channel& channel, const ItCell& cell)
    {
        bool usesInstruments = song->useInstruments();
        if (cell.instrument > 0) {
            channel.instrumentIndex = cell.instrument;
        }
        if (cell.note >= 0 && cell.note < 120) {
            const ItSample* sample = nullptr;
            const ItInstrument* instrument = nullptr;
            int32_t note = cell.note;
            if (usesInstruments) {
                if (channel.instrumentIndex > 0 && channel.instrumentIndex <= song->instruments.size()) {
                    instrument = &song->instruments[channel.instrumentIndex - 1];
                    uint8_t sampleNumber = instrument->sampleMap[static_cast<size_t>(cell.note)];
                    note = instrument->noteMap[static_cast<size_t>(cell.note)];
                    if (sampleNumber > 0 && sampleNumber <= song->samples.size()) {
                        sample = &song->samples[sampleNumber - 1];
                    }
                }
            } else if (channel.instrumentIndex > 0 && channel.instrumentIndex <= song->samples.size()) {
                sample = &song->samples[channel.instrumentIndex - 1];
            }
            if (cell.command == CmdPortamento) {
                channel.targetFrequency = noteFrequency(channel.sample ? channel.sample : sample, note);
            } else if (sample && (sample->flags & SampleExists)) {
                channel.lastNote = static_cast<int16_t>(note);
                channel.sample = sample;
                channel.instrument = instrument;
                channel.frequency = noteFrequency(sample, note);
                channel.targetFrequency = channel.frequency;
                channel.position = 0;
                channel.direction = 1;
                channel.active = true;
                channel.keyOff = false;
                channel.fading = false;
                channel.fade = MaxVolume;
                channel.envelopeTick[0] = 0;
                channel.envelopeTick[1] = 0;
                channel.envelopeTick[2] = 0;
                if (!(channel.vibratoWave & 4)) {
                    channel.vibratoPosition = 0;
                }
                if (sample->defaultPan & 0x80) {
                    channel.pan = std::min((sample->defaultPan & 0x7f) << 9, MaxPan);
                } else if (instrument && !(instrument->defaultPan & 0x80)) {
                    channel.pan = std::min(instrument->defaultPan << 9, MaxPan);
                }
            }
        } else if (cell.note >= 120) {
            channel.keyOff = true;
            if (!channel.instrument || !(channel.instrument->volumeEnvelope.flags & EnvelopeEnabled)) {
                channel.active = false;
            } else {
                channel.fading = true;
            }
        }
        if (cell.instrument > 0 && channel.sample) {
            channel.volume = std::min(channel.sample->volume << 9, MaxVolume);
        }
    }

    void ModulePlayer::volumeColumnTickZero(Channel& channel)
    {
        uint8_t v = channel.volumeCommand;
        if (v <= 64) {
            channel.volume = std::min(v * 2, 127) << 8;
        }
    }

    void ModulePlayer::tickZeroEffects(Channel& channel)
    {
        uint8_t param = channel.param;
        uint8_t high = param >> 4;
        uint8_t low = param & 0x0f;
        switch (channel.command) {
        case CmdSpeed:
            if (param > 0) {
                speed = param;
            }
            break;
        case CmdJump:
            if (param <= songLength(song->orders)) {
                jumpOrder = param;
            }
            break;
        case CmdBreak:
            breakRow = high * 10 + low;
            break;
        case CmdVolumeSlide:
            volumeSlide(channel, param, true);
            break;
        case CmdPitchDown:
        case CmdPitchUp: {
            int32_t sign = channel.command == CmdPitchUp ? 1 : -1;
            if (high == 0xf) {
                slidePitch(channel, sign * low * 4);
            } else if (high == 0xe) {
                slidePitch(channel, sign * ((low + 2) >> 2) * 4);
            }
            break;
        }
        case CmdPortamento:
            if (param != 0) {
                channel.portaMemory = param;
            }
            break;
        case CmdVibrato:
            if (high != 0) {
                channel.vibratoSpeed = high;
            }
            if (low != 0) {
                channel.vibratoDepth = low;
            }
            break;
        case CmdOffset:
            if (param != 0) {
                channel.offsetMemory = param;
            }
            if (channel.sample && !channel.sample->data.empty()) {
                channel.position = std::min(static_cast<double>(channel.offsetMemory) * 256.0, static_cast<double>(channel.sample->data.size() - 1));
            }
            break;
        case CmdRetrigger:
            retrigger(channel);
            break;
        case CmdSpecial:
            switch (high) {
            case 0x3:
                channel.vibratoWave = low;
                break;
            case 0x8:
                channel.pan = (low << 11) + 0x400;
                break;
            case 0x9:
                channel.pan = commandPan(0xa4);
                break;
            case 0xB:
                if (low == 0) {
                    channel.loopRow = currentRow;
                } else if (channel.loopCount == 0) {
                    channel.loopCount = low;
                    loopTarget = channel.loopRow;
                } else if (--channel.loopCount > 0) {
                    loopTarget = channel.loopRow;
                }
                break;
            case 0xC:
                channel.noteCutTick = low + 1;
                break;
            case 0xE:
                if (patternDelay == 0) {
                    patternDelay = low;
                }
                break;
            default:
                break;
            }
            break;
        case CmdTempo:
            if (param == 0x20) {
                speed = param;
            } else {
                tempo = param;
            }
            break;
        case CmdPan:
            channel.pan = commandPan(param);
            break;
        default:
            break;
        }
    }

    void ModulePlayer::tickEffects(Channel& channel)
    {
        if (channel.noteDelayTick == tickIndex) {
            channel.noteDelayTick = -1;
            triggerNote(channel, channel.delayed);
            channel.volumeCommand = channel.delayed.volume;
            volumeColumnTickZero(channel);
        }
        if (channel.noteCutTick >= 0 && tickIndex >= channel.noteCutTick) {
            channel.volume = 0;
        }
        switch (channel.command) {
        case CmdVolumeSlide:
            volumeSlide(channel, channel.param, false);
            break;
        case CmdPitchDown:
        case CmdPitchUp:
            if (channel.param < 0xe0) {
                int32_t sign = channel.command == CmdPitchUp ? 1 : -1;
                slidePitch(channel, sign * channel.param * 4);
            }
            break;
        case CmdPortamento:
            tonePortamento(channel, channel.portaMemory);
            break;
        case CmdVibrato:
            channel.vibratoPosition = static_cast<uint8_t>(channel.vibratoPosition + channel.vibratoSpeed * 4);
            channel.vibratoOffset = (waveValue(channel.vibratoWave, channel.vibratoPosition >> 2) * (channel.vibratoDepth << 5)) >> 8;
            break;
        case CmdRetrigger:
            retrigger(channel);
            break;
        default:
            break;
        }
    }

    void ModulePlayer::processCell(Channel& channel, const ItCell& cell)
    {
        channel.command = cell.command;
        channel.param = cell.param;
        channel.volumeCommand = cell.volume;
        channel.vibratoOffset = 0;
        channel.noteCutTick = -1;
        channel.noteDelayTick = -1;
        if (cell.command == CmdSpecial && (cell.param >> 4) == 0xD && (cell.param & 0x0f) > 0) {
            channel.noteDelayTick = cell.param & 0x0f;
            channel.delayed = cell;
            return;
        }
        triggerNote(channel, cell);
        volumeColumnTickZero(channel);
        tickZeroEffects(channel);
    }

    void ModulePlayer::processRow()
    {
        const ItPattern* pattern = currentPattern();
        breakRow = -1;
        jumpOrder = -1;
        loopTarget = -1;
        if (!pattern || currentRow >= pattern->rows) {
            return;
        }
        for (size_t i = 0; i < channels.size(); i++) {
            if (song->channelPan[i] & 0x80) {
                continue;
            }
            processCell(channels[i], pattern->cells[static_cast<size_t>(currentRow) * 64 + i]);
        }
    }

    void ModulePlayer::advanceRow()
    {
        const ItPattern* pattern = currentPattern();
        if (loopTarget >= 0) {
            currentRow = loopTarget;
            loopTarget = -1;
            return;
        }
        if (jumpOrder >= 0 || breakRow >= 0) {
            if (jumpOrder >= 0) {
                currentOrder = jumpOrder;
            } else {
                currentOrder++;
            }
            normalizeOrder();
            const ItPattern* next = currentPattern();
            currentRow = breakRow >= 0 ? breakRow : 0;
            if (next && currentRow >= next->rows) {
                currentRow = 0;
            }
            for (Channel& channel : channels) {
                channel.loopRow = 0;
                channel.loopCount = 0;
            }
            return;
        }
        currentRow++;
        if (!pattern || currentRow >= pattern->rows) {
            currentRow = 0;
            currentOrder++;
            normalizeOrder();
            for (Channel& channel : channels) {
                channel.loopRow = 0;
                channel.loopCount = 0;
            }
        }
    }

    int32_t ModulePlayer::envelopeValue(const ItEnvelope& envelope, Channel& channel, int index, int32_t fallback, int32_t scale)
    {
        if (!(envelope.flags & EnvelopeEnabled) || envelope.nodes.empty()) {
            return fallback;
        }
        int32_t& tickValue = channel.envelopeTick[index];
        const auto& nodes = envelope.nodes;
        auto scaled = [scale](int32_t node) {
            return std::min(node * scale, MaxVolume);
        };
        int32_t value = scaled(nodes.back().first);
        for (size_t i = 0; i + 1 < nodes.size(); i++) {
            int32_t t0 = nodes[i].second;
            int32_t t1 = nodes[i + 1].second;
            if (tickValue >= t0 && tickValue <= t1) {
                if (t1 == t0) {
                    value = scaled(nodes[i + 1].first);
                } else {
                    value = scaled(nodes[i].first) + (scaled(nodes[i + 1].first) - scaled(nodes[i].first)) * (tickValue - t0) / (t1 - t0);
                }
                break;
            }
        }
        if (tickValue < nodes.front().second) {
            value = scaled(nodes.front().first);
        }
        tickValue++;
        bool sustained = (envelope.flags & EnvelopeSustain) && !channel.keyOff;
        if (sustained && envelope.sustainEnd < nodes.size() && envelope.sustainBegin < nodes.size()) {
            if (tickValue > nodes[envelope.sustainEnd].second) {
                tickValue = nodes[envelope.sustainBegin].second;
            }
        } else if ((envelope.flags & EnvelopeLoop) && envelope.loopEnd < nodes.size() && envelope.loopBegin < nodes.size()) {
            if (tickValue > nodes[envelope.loopEnd].second) {
                tickValue = nodes[envelope.loopBegin].second;
            }
        } else if (tickValue > nodes.back().second) {
            tickValue = nodes.back().second + 1;
        }
        return value;
    }

    void ModulePlayer::updateOutput(Channel& channel)
    {
        if (!channel.active || !channel.sample) {
            channel.leftGain = 0;
            channel.rightGain = 0;
            return;
        }
        int32_t level = (0x4e20 * (127 * 127 * 2)) >> 15;
        level = (level * MaxVolume) >> 15;
        level = (level * MaxVolume) >> 15;
        int32_t pan = channel.pan & MaxPan;
        double frequency = channel.frequency * std::pow(2.0, static_cast<double>(channel.vibratoOffset) / 3072.0);
        int32_t envelope = MaxVolume;
        if (const ItInstrument* instrument = channel.instrument) {
            envelope = envelopeValue(instrument->volumeEnvelope, channel, 0, MaxVolume, 512);
            int32_t panEnvelope = envelopeValue(instrument->panEnvelope, channel, 1, 0, 1024);
            pan += (((0x4000 - std::abs(pan - 0x4000)) * panEnvelope) >> 15) * MaxPan >> 15;
            int32_t pitch = envelopeValue(instrument->pitchEnvelope, channel, 2, 0, 1);
            if (!(instrument->pitchEnvelope.flags & 0x80)) {
                frequency *= std::pow(2.0, static_cast<double>(pitch) / 24.0);
            }
            if (channel.fading) {
                int32_t step = std::min(instrument->fadeOut << 5, MaxVolume);
                if (channel.fade < step) {
                    channel.fade = 0;
                    channel.active = false;
                } else {
                    channel.fade -= step;
                }
            }
        }
        level = (level * envelope) >> 15;
        level = (level * channel.fade) >> 15;
        level = (level * channel.volume) >> 15;
        pan = std::clamp(pan, 0, MaxPan);
        const std::array<int16_t, 32768>& table = panTable();
        int32_t left = (table[static_cast<size_t>(MaxPan - pan)] * level) >> 15;
        int32_t right = (table[static_cast<size_t>(pan)] * level) >> 15;
        channel.outputFrequency = frequency;
        channel.leftGain = static_cast<float>(left) / 32768.0f;
        channel.rightGain = static_cast<float>((channel.pan & SurroundPan) ? -right : right) / 32768.0f;
    }

    void ModulePlayer::tick()
    {
        if (tickIndex == 0) {
            patternDelay = 0;
            processRow();
            rowTicks = speed * (1 + patternDelay);
        } else {
            for (size_t i = 0; i < channels.size(); i++) {
                if (song->channelPan[i] & 0x80) {
                    continue;
                }
                tickEffects(channels[i]);
            }
        }
        for (Channel& channel : channels) {
            updateOutput(channel);
        }
        tickIndex++;
        if (tickIndex >= rowTicks) {
            tickIndex = 0;
            advanceRow();
        }
    }

    void ModulePlayer::mixChannel(Channel& channel, float* stereo, size_t frames)
    {
        const ItSample* sample = channel.sample;
        if (!sample || sample->data.empty()) {
            return;
        }
        double step = channel.outputFrequency / static_cast<double>(rate);
        const float* data = sample->data.data();
        double length = static_cast<double>(sample->data.size());
        bool loop = (sample->flags & SampleLoop) != 0;
        double loopBegin = static_cast<double>(sample->loopBegin);
        double loopEnd = static_cast<double>(sample->loopEnd);
        bool pingPong = (sample->flags & SamplePingPong) != 0;
        float ramp = 1.0f / 64.0f;
        for (size_t i = 0; i < frames; i++) {
            if (!channel.active) {
                break;
            }
            size_t index = static_cast<size_t>(channel.position);
            double fraction = channel.position - static_cast<double>(index);
            float a = data[std::min(index, sample->data.size() - 1)];
            size_t nextIndex = index + 1;
            if (loop && static_cast<double>(nextIndex) >= loopEnd) {
                nextIndex = static_cast<size_t>(loopBegin);
            }
            float b = data[std::min(nextIndex, sample->data.size() - 1)];
            float value = a + (b - a) * static_cast<float>(fraction);
            channel.currentLeft += (channel.leftGain - channel.currentLeft) * ramp;
            channel.currentRight += (channel.rightGain - channel.currentRight) * ramp;
            stereo[i * 2] += value * channel.currentLeft;
            stereo[i * 2 + 1] += value * channel.currentRight;
            channel.position += step * channel.direction;
            if (loop) {
                if (pingPong) {
                    if (channel.direction > 0 && channel.position >= loopEnd) {
                        channel.position = std::max(loopEnd - (channel.position - loopEnd) - 1.0, loopBegin);
                        channel.direction = -1;
                    } else if (channel.direction < 0 && channel.position < loopBegin) {
                        channel.position = std::min(loopBegin + (loopBegin - channel.position), loopEnd - 1.0);
                        channel.direction = 1;
                    }
                } else if (channel.position >= loopEnd) {
                    double span = loopEnd - loopBegin;
                    channel.position = loopBegin + std::fmod(channel.position - loopBegin, span);
                }
            } else if (channel.position >= length) {
                channel.active = false;
            }
        }
    }

    void ModulePlayer::render(float* stereo, size_t frames)
    {
        std::fill(stereo, stereo + frames * 2, 0.0f);
        size_t done = 0;
        while (done < frames) {
            if (tickRemaining <= 0) {
                tick();
                tickRemaining += static_cast<double>(rate) * 2.5 / static_cast<double>(tempo);
            }
            size_t chunk = std::min<size_t>(frames - done, static_cast<size_t>(std::ceil(tickRemaining)));
            if (chunk == 0) {
                chunk = 1;
            }
            for (Channel& channel : channels) {
                if (channel.active) {
                    mixChannel(channel, stereo + done * 2, chunk);
                }
            }
            done += chunk;
            tickRemaining -= static_cast<double>(chunk);
        }
    }
}
