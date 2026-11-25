#include "audio/audio.h"
#include "core/config.h"
#include "core/objects.h"
#include "core/url.h"
#include "core/vm.h"
#include "engine/actor.h"
#include "engine/canvas.h"
#include "engine/canvas_hooks.h"
#include "engine/console.h"
#include "engine/icon.h"
#include "engine/input.h"
#include "engine/net.h"
#include "engine/resources.h"
#include "engine/truetype_fonts.h"
#include "engine/video_options.h"
#include "engine/world.h"
#include "render/lighting.h"
#include "render/scene_renderer.h"
#ifdef UT_NETWORKING
#include "net/gns_transport.h"
#endif

#include <SDL.h>
#include <glad/gl.h>

#ifndef _WIN32
#include <sys/select.h>
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

EM_JS(void, showPageError, (const char* text), { Module.showError(UTF8ToString(text)); });
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
#ifdef __EMSCRIPTEN__
    constexpr bool browser = true;
#else
    constexpr bool browser = false;
#endif

    uint8_t translateKey(SDL_Keycode key)
    {
        if (key >= SDLK_a && key <= SDLK_z) {
            return static_cast<uint8_t>(ut::IK_A + (key - SDLK_a));
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            return static_cast<uint8_t>(ut::IK_0 + (key - SDLK_0));
        }
        if (key >= SDLK_F1 && key <= SDLK_F12) {
            return static_cast<uint8_t>(ut::IK_F1 + (key - SDLK_F1));
        }
        if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
            return static_cast<uint8_t>(ut::IK_NumPad0 + 1 + (key - SDLK_KP_1));
        }
        switch (key) {
        case SDLK_KP_0:
            return ut::IK_NumPad0;
        case SDLK_BACKSPACE:
            return ut::IK_Backspace;
        case SDLK_TAB:
            return ut::IK_Tab;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return ut::IK_Enter;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            return ut::IK_Shift;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return ut::IK_Ctrl;
        case SDLK_LALT:
        case SDLK_RALT:
            return ut::IK_Alt;
        case SDLK_PAUSE:
            return ut::IK_Pause;
        case SDLK_CAPSLOCK:
            return ut::IK_CapsLock;
        case SDLK_ESCAPE:
            return ut::IK_Escape;
        case SDLK_SPACE:
            return ut::IK_Space;
        case SDLK_PAGEUP:
            return ut::IK_PageUp;
        case SDLK_PAGEDOWN:
            return ut::IK_PageDown;
        case SDLK_END:
            return ut::IK_End;
        case SDLK_HOME:
            return ut::IK_Home;
        case SDLK_LEFT:
            return ut::IK_Left;
        case SDLK_UP:
            return ut::IK_Up;
        case SDLK_RIGHT:
            return ut::IK_Right;
        case SDLK_DOWN:
            return ut::IK_Down;
        case SDLK_INSERT:
            return ut::IK_Insert;
        case SDLK_DELETE:
            return ut::IK_Delete;
        case SDLK_KP_MULTIPLY:
            return ut::IK_GreyStar;
        case SDLK_KP_PLUS:
            return ut::IK_GreyPlus;
        case SDLK_KP_MINUS:
            return ut::IK_GreyMinus;
        case SDLK_KP_PERIOD:
            return ut::IK_NumPadPeriod;
        case SDLK_KP_DIVIDE:
            return ut::IK_GreySlash;
        case SDLK_NUMLOCKCLEAR:
            return ut::IK_NumLock;
        case SDLK_SCROLLLOCK:
            return ut::IK_ScrollLock;
        case SDLK_SEMICOLON:
            return ut::IK_Semicolon;
        case SDLK_EQUALS:
            return ut::IK_Equals;
        case SDLK_COMMA:
            return ut::IK_Comma;
        case SDLK_MINUS:
            return ut::IK_Minus;
        case SDLK_PERIOD:
            return ut::IK_Period;
        case SDLK_SLASH:
            return ut::IK_Slash;
        case SDLK_BACKQUOTE:
            return ut::IK_Tilde;
        case SDLK_LEFTBRACKET:
            return ut::IK_LeftBracket;
        case SDLK_BACKSLASH:
            return ut::IK_Backslash;
        case SDLK_RIGHTBRACKET:
            return ut::IK_RightBracket;
        case SDLK_QUOTE:
            return ut::IK_SingleQuote;
        default:
            return ut::IK_None;
        }
    }

    uint8_t translateButton(uint8_t button)
    {
        switch (button) {
        case SDL_BUTTON_LEFT:
            return ut::IK_LeftMouse;
        case SDL_BUTTON_RIGHT:
            return ut::IK_RightMouse;
        case SDL_BUTTON_MIDDLE:
            return ut::IK_MiddleMouse;
        default:
            return ut::IK_None;
        }
    }

    struct Options {
        std::string map;
        std::string screenshot;
        int frames = -1;
        int width = 1280;
        int height = 720;
        int port = 0;
        int fps = 0;
        bool dedicated = false;
        bool headless = false;
        bool control = false;
        bool sizeGiven = false;
        std::vector<std::string> commands;
        std::vector<std::pair<int, std::string>> actions;
    };

    Options parseOptions(int argc, char** argv)
    {
        Options options;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--screenshot" && i + 1 < argc) {
                options.screenshot = argv[++i];
            } else if (arg == "--frames" && i + 1 < argc) {
                options.frames = std::atoi(argv[++i]);
            } else if (arg == "--width" && i + 1 < argc) {
                options.width = std::atoi(argv[++i]);
                options.sizeGiven = true;
            } else if (arg == "--height" && i + 1 < argc) {
                options.height = std::atoi(argv[++i]);
                options.sizeGiven = true;
            } else if (arg == "--port" && i + 1 < argc) {
                options.port = std::atoi(argv[++i]);
            } else if (arg == "--fps" && i + 1 < argc) {
                options.fps = std::atoi(argv[++i]);
            } else if (arg == "--headless") {
                options.headless = true;
            } else if (arg == "--control") {
                options.control = true;
            } else if (arg == "--server" || arg == "-server") {
                options.dedicated = true;
            } else if (arg == "--exec" && i + 1 < argc) {
                options.commands.push_back(argv[++i]);
            } else if (arg == "--at" && i + 1 < argc) {
                std::string spec = argv[++i];
                size_t colon = spec.find(':');
                if (colon != std::string::npos) {
                    options.actions.emplace_back(std::atoi(spec.substr(0, colon).c_str()), spec.substr(colon + 1));
                }
            } else {
                options.map = ut::stripProtocol(arg);
            }
        }
        return options;
    }

    void saveBmp(const std::string& path, const std::vector<uint8_t>& rgba, int width, int height)
    {
        std::vector<uint8_t> pixels = rgba;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
        SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }

    int listenPort(const Options& options)
    {
        if (options.port > 0) {
            return options.port;
        }
        return std::atoi(ut::config::system().get("URL", "Port").value_or(std::to_string(ut::net::DefaultPort)).c_str());
    }

    std::unique_ptr<ut::net::Transport> listenOn(int port)
    {
#ifdef UT_NETWORKING
        return ut::net::listenTransport(port);
#else
        static_cast<void>(port);
        return nullptr;
#endif
    }

    std::unique_ptr<ut::net::Transport> connectTo(const std::string& address)
    {
#ifdef UT_NETWORKING
        return ut::net::connectTransport(address);
#else
        static_cast<void>(address);
        return nullptr;
#endif
    }

    bool hasOption(const std::string& url, std::string_view key)
    {
        size_t mark = url.find('?');
        while (mark != std::string::npos) {
            size_t next = url.find('?', mark + 1);
            std::string item = url.substr(mark + 1, next == std::string::npos ? std::string::npos : next - mark - 1);
            if (ut::iequals(item.substr(0, item.find('=')), key)) {
                return true;
            }
            mark = next;
        }
        return false;
    }

    std::string joinOptionsFor(const std::string& url)
    {
        size_t question = url.find('?');
        std::string joinOptions = question == std::string::npos ? std::string() : url.substr(question);
        for (const auto& [key, value] : ut::config::user().section("DefaultPlayer")) {
            if (!hasOption(joinOptions, key)) {
                joinOptions += "?" + key + "=" + value;
            }
        }
        return joinOptions;
    }

    bool isServerAddress(const std::string& url)
    {
        std::string host = url.substr(0, url.find_first_of("?/"));
        if (host.empty()) {
            return false;
        }
        if (host.find(':') != std::string::npos || ut::iequals(host, "localhost")) {
            return true;
        }
        return host.find('.') != std::string::npos && host.find_first_not_of("0123456789.") == std::string::npos;
    }

    bool isLocalMap(const std::string& url)
    {
        std::string map = std::filesystem::path(url.substr(0, url.find('?'))).stem().string();
        std::string localMap = std::filesystem::path(ut::config::system().get("URL", "LocalMap").value_or("CityIntro.unr")).stem().string();
        return ut::iequals(map, "Entry") || ut::iequals(map, localMap);
    }

    void printPlayers(ut::World& world)
    {
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && ut::isPlayerPawn(a)) {
                ut::Vector location = ut::actor::Location(a);
                ut::UObject* info = ut::pawn::PlayerReplicationInfo(a);
                std::string name = info ? ut::playerreplicationinfo::PlayerName(info) : std::string("?");
                std::printf("pawn %s name %s role %d at %.1f %.1f %.1f health %d state %s\n", a->cls->name.c_str(), name.c_str(), ut::actor::Role(a), static_cast<double>(location.x), static_cast<double>(location.y), static_cast<double>(location.z), ut::pawn::Health(a), ut::vm::stateName(a).c_str());
            }
        }
    }

    ut::UObject* findPlayer(ut::World& world, const std::string& name)
    {
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && ut::isPlayerPawn(a) && ut::pawn::PlayerReplicationInfo(a) && ut::playerreplicationinfo::PlayerName(ut::pawn::PlayerReplicationInfo(a)) == name) {
                return a;
            }
        }
        return nullptr;
    }

    std::vector<ut::UObject*> actorsOfClass(ut::World& world, const std::string& className)
    {
        std::vector<ut::UObject*> found;
        for (ut::UObject* a : world.actors) {
            if (a && !a->destroyed && a->isA(ut::Name(className))) {
                found.push_back(a);
            }
        }
        return found;
    }

    void dumpActors(ut::World& world, const std::string& rest)
    {
        std::istringstream words(rest);
        std::string className;
        words >> className;
        std::vector<std::string> names;
        for (std::string name; words >> name;) {
            names.push_back(name);
        }
        for (ut::UObject* a : actorsOfClass(world, className)) {
            std::string line = "actor " + a->cls->name.str();
            for (const std::string& name : names) {
                ut::UProperty* property = a->cls->findProperty(ut::Name(name));
                if (property) {
                    line += " " + name + "=" + property->toText(a->data + property->offset);
                }
            }
            std::printf("%s\n", line.c_str());
        }
    }

    void facePlayer(ut::UObject* pawn, const ut::Vector& target)
    {
        ut::Vector delta = target - (ut::actor::Location(pawn) + ut::Vector { 0, 0, ut::pawn::BaseEyeHeight(pawn) });
        ut::Rotator facing;
        facing.yaw = static_cast<int32_t>(std::atan2(delta.y, delta.x) * 32768.0f / ut::Pi);
        facing.pitch = static_cast<int32_t>(std::atan2(delta.z, std::sqrt(delta.x * delta.x + delta.y * delta.y)) * 32768.0f / ut::Pi) & 65535;
        ut::pawn::ViewRotation(pawn) = facing;
        ut::actor::Velocity(pawn) = {};
        ut::vm::Call(pawn, ut::Name("ClientSetLocation")).v(ut::actor::Location(pawn)).r(facing).run();
        ut::Vector location = ut::actor::Location(pawn);
        std::printf("placed at %.1f %.1f %.1f\n", static_cast<double>(location.x), static_cast<double>(location.y), static_cast<double>(location.z));
    }

    void placePlayer(ut::World& world, const std::string& rest, bool move)
    {
        std::istringstream words(rest);
        std::string playerName;
        std::string className;
        size_t index = 0;
        float distance = 70.0f;
        float raise = 0.0f;
        words >> playerName >> className >> index >> distance >> raise;
        ut::UObject* pawn = findPlayer(world, playerName);
        std::vector<ut::UObject*> targets = actorsOfClass(world, className);
        if (!pawn || index >= targets.size()) {
            std::printf("place failed\n");
            return;
        }
        ut::Vector target = ut::actor::Location(targets[index]);
        if (!move) {
            facePlayer(pawn, target + ut::Vector { 0, 0, raise });
            return;
        }
        for (int step = 0; step < 24; step++) {
            float angle = static_cast<float>(step % 8) * ut::Pi / 4.0f;
            float lift = static_cast<float>(step / 8) * 40.0f;
            ut::Vector spot = target + ut::Vector { std::cos(angle) * distance, std::sin(angle) * distance, lift };
            if (!world.fastLineCheck(spot, target) || !world.farMoveActor(pawn, spot, true, false)) {
                continue;
            }
            world.farMoveActor(pawn, spot, false, false);
            facePlayer(pawn, target + ut::Vector { 0, 0, raise });
            return;
        }
        std::printf("place failed\n");
    }

    void giveItem(ut::World& world, const std::string& rest)
    {
        std::istringstream words(rest);
        std::string playerName;
        std::string className;
        words >> playerName >> className;
        ut::UObject* pawn = findPlayer(world, playerName);
        ut::UClass* cls = ut::objects::findClass(className);
        ut::UObject* item = pawn && cls ? world.spawn(cls, nullptr, ut::Name(), ut::actor::Location(pawn), {}, nullptr, true) : nullptr;
        if (!item) {
            std::printf("give failed\n");
            return;
        }
        ut::vm::Call(item, ut::Name("Touch")).obj(pawn).run();
        if (!item->destroyed) {
            world.destroy(item);
        }
        std::printf("gave %s %s\n", playerName.c_str(), className.c_str());
    }

    bool worldAction(ut::World& world, const std::string& verb, const std::string& rest)
    {
        if (verb == "players") {
            printPlayers(world);
        } else if (verb == "dump") {
            dumpActors(world, rest);
        } else if (verb == "place") {
            placePlayer(world, rest, true);
        } else if (verb == "face") {
            placePlayer(world, rest, false);
        } else if (verb == "give") {
            giveItem(world, rest);
        } else {
            return false;
        }
        return true;
    }

    std::vector<std::string> controlLines()
    {
        std::vector<std::string> lines;
#ifndef _WIN32
        static std::string buffer;
        static bool open = true;
        while (open) {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(0, &ready);
            timeval none {};
            if (select(1, &ready, nullptr, nullptr, &none) <= 0) {
                break;
            }
            char chunk[512];
            ssize_t count = read(0, chunk, sizeof(chunk));
            if (count <= 0) {
                open = false;
                break;
            }
            buffer.append(chunk, static_cast<size_t>(count));
        }
        size_t end = 0;
        while ((end = buffer.find('\n')) != std::string::npos) {
            lines.push_back(buffer.substr(0, end));
            buffer.erase(0, end + 1);
        }
#endif
        return lines;
    }

    class Game {
    public:
        Game(SDL_Window* target, const Options& settings)
            : window(target)
            , options(settings)
        {
        }

        static bool isWindowed(ut::DisplayMode mode)
        {
            return mode == ut::DisplayMode::Windowed;
        }

        std::pair<int, int> desktopSize() const
        {
            SDL_DisplayMode desktop {};
            if (SDL_GetDesktopDisplayMode(std::max(SDL_GetWindowDisplayIndex(window), 0), &desktop) != 0) {
                return { options.width, options.height };
            }
            return { desktop.w, desktop.h };
        }

        void saveVideoMode(ut::DisplayMode mode, int width, int height)
        {
            ut::Ini& ini = ut::config::system();
            std::string section = ut::videoSection();
            if (mode == ut::DisplayMode::Windowed) {
                ini.set(section, "WindowedViewportX", std::to_string(width));
                ini.set(section, "WindowedViewportY", std::to_string(height));
            } else if (mode == ut::DisplayMode::Fullscreen) {
                ini.set(section, "FullscreenViewportX", std::to_string(width));
                ini.set(section, "FullscreenViewportY", std::to_string(height));
            }
            ini.set(section, "DisplayMode", std::to_string(static_cast<int>(mode)));
            ini.set(section, "StartupFullscreen", isWindowed(mode) ? "False" : "True");
            ini.save();
        }

        std::pair<int, int> savedSize(ut::DisplayMode mode) const
        {
            if (mode == ut::DisplayMode::Borderless) {
                return desktopSize();
            }
            if (mode == ut::DisplayMode::Fullscreen) {
                auto [desktopWidth, desktopHeight] = desktopSize();
                return { ut::videoSetting("FullscreenViewportX", desktopWidth), ut::videoSetting("FullscreenViewportY", desktopHeight) };
            }
            return { ut::videoSetting("WindowedViewportX", options.width), ut::videoSetting("WindowedViewportY", options.height) };
        }

        void applyVideoMode(ut::DisplayMode mode, int width, int height)
        {
            displayMode = mode;
            if (!isWindowed(mode)) {
                lastFullscreenMode = mode;
            }
            if (mode == ut::DisplayMode::Borderless) {
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                return;
            }
            if (mode == ut::DisplayMode::Fullscreen) {
                SDL_DisplayMode wanted {};
                wanted.w = width;
                wanted.h = height;
                SDL_DisplayMode closest {};
                int display = std::max(SDL_GetWindowDisplayIndex(window), 0);
                if (SDL_GetClosestDisplayMode(display, &wanted, &closest)) {
                    SDL_SetWindowDisplayMode(window, &closest);
                }
                if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != SDL_WINDOW_FULLSCREEN) {
                    SDL_SetWindowFullscreen(window, 0);
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                }
                return;
            }
            SDL_SetWindowFullscreen(window, 0);
            SDL_SetWindowBordered(window, SDL_TRUE);
            SDL_SetWindowSize(window, width, height);
            SDL_Rect bounds {};
            if (SDL_GetDisplayUsableBounds(std::max(SDL_GetWindowDisplayIndex(window), 0), &bounds) == 0) {
                SDL_SetWindowPosition(window, bounds.x + std::max((bounds.w - width) / 2, 0), bounds.y + std::max((bounds.h - height) / 2, 0));
            }
        }

        void switchDisplayMode(ut::DisplayMode mode)
        {
            auto [width, height] = savedSize(mode);
            applyVideoMode(mode, width, height);
            saveVideoMode(mode, width, height);
        }

        std::string availableResolutions()
        {
            std::pair<int, int> current;
            SDL_GetWindowSize(window, &current.first, &current.second);
            if (displayMode == ut::DisplayMode::Borderless) {
                return std::to_string(current.first) + "x" + std::to_string(current.second);
            }
            int display = std::max(SDL_GetWindowDisplayIndex(window), 0);
            std::vector<std::pair<int, int>> sizes;
            for (int i = 0; i < SDL_GetNumDisplayModes(display); i++) {
                SDL_DisplayMode mode {};
                if (SDL_GetDisplayMode(display, i, &mode) == 0 && mode.w >= 640 && mode.h >= 480) {
                    sizes.emplace_back(mode.w, mode.h);
                }
            }
            if (displayMode == ut::DisplayMode::Windowed) {
                // Offer common window sizes as well, as long as they fit on the desktop.
                static const std::pair<int, int> common[] = {
                    { 640, 480 },
                    { 800, 600 },
                    { 1024, 768 },
                    { 1152, 864 },
                    { 1280, 720 },
                    { 1280, 800 },
                    { 1280, 960 },
                    { 1280, 1024 },
                    { 1366, 768 },
                    { 1440, 900 },
                    { 1600, 900 },
                    { 1600, 1200 },
                    { 1680, 1050 },
                    { 1920, 1080 },
                    { 1920, 1200 },
                    { 2560, 1080 },
                    { 2560, 1440 },
                    { 2560, 1600 },
                    { 3440, 1440 },
                    { 3840, 2160 },
                };
                SDL_Rect usable {};
                auto [desktopWidth, desktopHeight] = desktopSize();
                if (SDL_GetDisplayUsableBounds(display, &usable) != 0) {
                    usable = { 0, 0, desktopWidth, desktopHeight };
                }
                sizes.erase(std::remove_if(sizes.begin(), sizes.end(), [&](const std::pair<int, int>& size) {
                    return size.first > desktopWidth || size.second > desktopHeight;
                }),
                    sizes.end());
                for (const auto& size : common) {
                    if (size.first <= usable.w && size.second <= usable.h) {
                        sizes.push_back(size);
                    }
                }
            }
            sizes.push_back(current);
            std::sort(sizes.begin(), sizes.end());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            std::string text;
            for (const auto& [width, height] : sizes) {
                if (!text.empty()) {
                    text += " ";
                }
                text += std::to_string(width) + "x" + std::to_string(height);
            }
            return text;
        }

        float displayFov(float fov, int width, int height)
        {
            int index = std::clamp(ut::videoSetting("FieldOfViewIndex", 3), 0, 12);
            float wanted = 60.0f + 5.0f * static_cast<float>(index);
            float adjusted = fov + (wanted - 75.0f) * std::clamp((fov - 25.0f) / 50.0f, 0.0f, 1.0f);
            float aspect = static_cast<float>(width) / static_cast<float>(height) * 0.75f;
            return std::atan(std::tan(adjusted * ut::Pi / 360.0f) * aspect) * 360.0f / ut::Pi;
        }

        void applyRenderOptions()
        {
            ut::RenderOptions render;
            render.anisotropy = static_cast<float>(1 << std::clamp(ut::videoSetting("Anisotropy", 0), 0, 4));
            render.trilinear = ut::videoSetting("UseTrilinear", 0) != 0;
            int multisample = std::clamp(ut::videoSetting("Multisampling", 0), 0, 3);
            render.multisample = multisample > 0 ? 1 << multisample : 0;
            render.fxaa = ut::videoSetting("FXAA", 0) != 0;
            render.smoothLightmaps = ut::videoSetting("SmoothLightmaps", 0) != 0;
            render.highDetailTextures = ut::videoSetting("UseS3TC", 0) != 0;
            const float detailDistances[] = { 380.0f, 1024.0f, 2048.0f };
            render.detailDistance = detailDistances[std::clamp(ut::videoSetting("DetailDistance", 0), 0, 2)];
            render.textureRate = ut::videoSetting("LimitTextureAnimation", 1) != 0 ? 60.0f : 0.0f;
            render.ambientOcclusion = static_cast<ut::OcclusionQuality>(std::clamp(ut::videoSetting("AmbientOcclusion", 0), 0, 3));
            render.softParticles = ut::videoSetting("SoftParticles", 0) != 0;
            renderer->setOptions(render);
            ut::truetype::setEnabled(ut::videoSetting("TrueTypeFonts", 0) != 0);
            if (browser || options.frames >= 0) {
                return;
            }
            int rawMouse = ut::videoSetting("RawMouseInput", 0) != 0 ? 1 : 0;
            if (rawMouse != appliedRawMouse) {
                appliedRawMouse = rawMouse;
                SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, rawMouse ? "0" : "1");
                SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, rawMouse ? "0" : "1");
                if (SDL_GetRelativeMouseMode()) {
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                }
            }
            int vsync = std::clamp(ut::videoSetting("VSync", 1), 0, 2);
            if (vsync != appliedVSync) {
                appliedVSync = vsync;
                if (vsync != 2 || SDL_GL_SetSwapInterval(-1) != 0) {
                    SDL_GL_SetSwapInterval(std::min(vsync, 1));
                }
            }
        }

        void limitFrameRate()
        {
            const int limits[] = { 0, 30, 60, 75, 120, 144, 165, 240 };
            int limit = limits[std::clamp(ut::videoSetting("FrameRateLimit", 0), 0, 7)];
            if (browser || limit == 0 || options.frames >= 0) {
                return;
            }
            uint64_t frequency = SDL_GetPerformanceFrequency();
            uint64_t deadline = frameDeadline + frequency / static_cast<uint64_t>(limit);
            uint64_t now = SDL_GetPerformanceCounter();
            if (now >= deadline) {
                frameDeadline = now;
                return;
            }
            while (now < deadline) {
                uint64_t remaining = (deadline - now) * 1000 / frequency;
                if (remaining > 1) {
                    SDL_Delay(static_cast<uint32_t>(remaining - 1));
                }
                now = SDL_GetPerformanceCounter();
            }
            frameDeadline = deadline;
        }

        bool start()
        {
            ut::engine::initialize(ut::config::findGameRoot(OPENUT_GAME_DATA).string());
            ut::audio::initialize();
            ut::audio::setLightRandom(ut::lightRandomBase);
            renderer = std::make_unique<ut::SceneRenderer>();
            ut::console::openMapHook = [this](const std::string& url, int32_t travelType) {
                pendingMap = url;
                pendingTravelType = travelType;
            };
            ut::console::quitHook = [this]() {
                running = false;
            };
            ut::console::currentResHook = [this]() {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                return std::to_string(width) + "x" + std::to_string(height);
            };
            ut::console::resolutionsHook = [this]() {
                return availableResolutions();
            };
            ut::console::setResHook = [this](int width, int height) {
                if (displayMode == ut::DisplayMode::Borderless) {
                    return;
                }
                applyVideoMode(displayMode, width, height);
                int actualWidth = width;
                int actualHeight = height;
                SDL_GetWindowSize(window, &actualWidth, &actualHeight);
                saveVideoMode(displayMode, actualWidth, actualHeight);
            };
            ut::setDisplayModeHook = [this](ut::DisplayMode mode) {
                switchDisplayMode(mode);
            };
            ut::console::reconnectHook = [this]() {
                if (!lastUrl.empty()) {
                    pendingMap = lastUrl;
                    pendingTravelType = ut::TRAVEL_Absolute;
                }
            };
            ut::console::toggleFullscreenHook = [this]() {
                switchDisplayMode(isWindowed(displayMode) ? lastFullscreenMode : ut::DisplayMode::Windowed);
            };
            ut::console::endFullscreenHook = [this]() {
                if (!isWindowed(displayMode)) {
                    switchDisplayMode(ut::DisplayMode::Windowed);
                }
            };
            ut::console::screenshotHook = [this]() {
                std::filesystem::path directory = ut::config::system().path.parent_path() / "Screenshots";
                std::filesystem::create_directories(directory);
                int index = 0;
                std::filesystem::path file;
                do {
                    char name[32];
                    std::snprintf(name, sizeof(name), "Shot%04d.bmp", index++);
                    file = directory / name;
                } while (std::filesystem::exists(file));
                pendingShot = file.string();
            };
            ut::console::applyAudioSettings();
            if (options.frames < 0 && !options.sizeGiven) {
                ut::DisplayMode mode = ut::displayModeSetting();
                auto [width, height] = savedSize(mode);
                applyVideoMode(mode, width, height);
            }
            ut::CanvasHooks& hooks = ut::canvasHooks();
            hooks.target = &renderer->canvas();
            hooks.drawActor = [this](ut::UObject* actor, bool) {
                renderer->drawActorOverlay(actor, viewLocation, viewRotation, viewFov, true);
            };
            hooks.drawClippedActor = [this](ut::UObject* actor, float x, float y, float width, float height) {
                ut::UObject* player = world ? world->player() : nullptr;
                float fov = player ? ut::pawn::FovAngle(player) : 90.0f;
                float scale = renderer->canvas().width() / ut::canvasHooks().target->width();
                renderer->drawClippedActor(actor, fov, static_cast<int>(std::lround(x * scale)), static_cast<int>(std::lround(y * scale)), static_cast<int>(std::lround(width * scale)), static_cast<int>(std::lround(height * scale)));
            };
            hooks.clipboardCopy = [](const std::string& text) {
                SDL_SetClipboardText(text.c_str());
            };
            hooks.clipboardPaste = []() {
                char* text = SDL_GetClipboardText();
                std::string result = text ? text : "";
                SDL_free(text);
                return result;
            };
            std::string map = options.map;
            std::filesystem::path mapFile = map;
            if (mapFile.has_parent_path() && std::filesystem::is_regular_file(mapFile)) {
                map = mapFile.stem().string();
                ut::objects::loadPackageFile(ut::Name(map), mapFile, false);
            }
            if (map.empty()) {
                auto local = ut::config::system().get("URL", "LocalMap");
                map = local ? *local : "CityIntro.unr";
            }
            return openMap(map);
        }

        bool project(const ut::Vector& location, float& pixelX, float& pixelY)
        {
            ut::Coords axes = ut::rotationAxes(viewRotation);
            ut::Vector delta = location - viewLocation;
            float forward = delta.dot(axes.xAxis);
            if (forward <= 1.0f) {
                return false;
            }
            float right = delta.dot(axes.yAxis);
            float up = delta.dot(axes.zAxis);
            float halfWidth = static_cast<float>(frameWidth) * 0.5f;
            float halfHeight = static_cast<float>(frameHeight) * 0.5f;
            float scale = halfWidth / std::tan(viewFov * ut::Pi / 360.0f);
            pixelX = halfWidth + right / forward * scale;
            pixelY = halfHeight - up / forward * scale;
            return true;
        }

        void closeNetwork()
        {
            driver.reset();
            transport.reset();
        }

        bool connect(const std::string& url)
        {
            closeNetwork();
            size_t mark = url.find_first_of("?/");
            transport = connectTo(url.substr(0, mark));
            if (!transport) {
                std::fprintf(stderr, "cannot connect to %s\n", url.c_str());
                return false;
            }
            driver = std::make_unique<ut::net::Driver>(*transport, false);
            joinOptions = joinOptionsFor(url);
            serverUrl = url;
            lastUrl = url;
            return true;
        }

        void pollNetwork(float delta)
        {
            if (!driver) {
                return;
            }
            driver->tick(delta);
            if (driver->closed) {
                std::string reason = driver->failure;
                std::fprintf(stderr, "disconnected: %s\n", reason.c_str());
                bool inGame = world && world->netMode == ut::NM_Client;
                closeNetwork();
                if (inGame || !world) {
                    openMap("Entry");
                }
                ut::UObject* player = world ? world->player() : nullptr;
                if (player) {
                    ut::vm::Call(player, ut::Name("ClearProgressMessages")).run();
                    ut::vm::Call(player, ut::Name("SetProgressTime")).f(6.0f).run();
                    ut::vm::Call(player, ut::Name("SetProgressMessage")).s(reason).i(0).run();
                }
                ut::UObject* console = scriptConsole();
                if (console) {
                    ut::vm::Call(console, ut::Name("ConnectFailure")).s(reason).s(serverUrl).run();
                    ut::vm::Call(console, ut::Name("LaunchUWindow")).run();
                }
                return;
            }
            if (!driver->welcomeUrl.empty()) {
                std::string url = driver->welcomeUrl;
                driver->welcomeUrl.clear();
                openMap(url, false, ut::NM_Client);
            }
        }

        bool openMap(const std::string& requested, bool carryItems = false, uint8_t netMode = ut::NM_Standalone)
        {
            std::string url = ut::stripProtocol(requested);
            if (netMode == ut::NM_Standalone && isServerAddress(url)) {
                return connect(url);
            }
            if (netMode == ut::NM_Standalone && hasOption(url, "listen")) {
                netMode = ut::NM_ListenServer;
            }
            if (netMode == ut::NM_Standalone || (netMode == ut::NM_ListenServer && !(driver && driver->server))) {
                closeNetwork();
            }
            if (netMode == ut::NM_ListenServer && !driver) {
                transport = listenOn(listenPort(options));
                if (transport) {
                    driver = std::make_unique<ut::net::Driver>(*transport, true);
                } else {
                    std::fprintf(stderr, "cannot listen on port %d\n", listenPort(options));
                    netMode = ut::NM_Standalone;
                }
            }
            std::vector<ut::TravelActor> travel;
            std::vector<std::pair<std::string, std::string>> travelInfo;
            if (carryItems && world) {
                travel = world->collectTravel();
                ut::UObject* player = world->player();
                if (player) {
                    ut::UObject* replication = ut::pawn::PlayerReplicationInfo(player);
                    std::string key = replication ? ut::playerreplicationinfo::PlayerName(replication) : std::string("Player");
                    travelInfo.emplace_back(key, world->travelString(player));
                }
            }
            std::string target = url;
            if (world && !target.empty() && target[0] == '?') {
                target = world->url.substr(0, world->url.find('?')) + target;
            }
            if (hasOption(target, "entry")) {
                target = "Entry";
            }
            ut::UObject* viewport = world ? world->viewport : nullptr;
            ut::UObject* levelConsole = viewport ? ut::player::Console(viewport) : nullptr;
            if (levelConsole) {
                ut::vm::Call(levelConsole, ut::Name("NotifyLevelChange")).run();
            }
            if (viewport) {
                ut::player::Actor(viewport) = nullptr;
            }
            renderer->flushLevel();
            std::vector<std::pair<std::string, std::string>> previousTravelInfo;
            if (world && world->level) {
                previousTravelInfo = world->level->travelInfo;
            }
            ut::Name previousPackage;
            if (world && world->level) {
                previousPackage = world->level->outermost()->name;
                ut::objects::unloadPackage(previousPackage);
            }
            if (driver) {
                driver->setWorld(nullptr);
            }
            world.reset();
            canvasObject = nullptr;
            ut::objects::collectPackage(previousPackage);
            world = std::make_unique<ut::World>();
            world->viewport = viewport;
            world->netMode = netMode;
            world->netPort = netMode == ut::NM_ListenServer ? listenPort(options) : 0;
            if (driver) {
                driver->setWorld(world.get());
            }
            world->incomingTravel = std::move(travel);
            bool restart = target.find("?restart") != std::string::npos || target.find("?RESTART") != std::string::npos;
            if (restart) {
                travelInfo = previousTravelInfo;
            }
            world->arrivalTravelInfo = std::move(travelInfo);
            world->replaceTravelInfo = true;
            world->inputHook = [](ut::UObject* player, float delta) {
                ut::console::readInput(player, delta);
            };
            if (!world->loadMap(target)) {
                return false;
            }
            ut::console::resetInput(world->player());
            if (netMode == ut::NM_Client) {
                driver->join(joinOptions);
            } else if (!isLocalMap(target)) {
                lastUrl = target;
            }
            return true;
        }

        ut::UObject* canvas(int width, int height)
        {
            if (!canvasObject) {
                canvasObject = ut::objects::newObject(ut::engineClasses().canvas, ut::objects::packageObject(ut::Name("Transient")), ut::Name("Canvas0"), 0);
                ut::canvas::Viewport(canvasObject) = world->viewport;
            }
            ut::vm::event(canvasObject, ut::Name("Reset"));
            ut::canvas::SizeX(canvasObject) = width;
            ut::canvas::SizeY(canvasObject) = height;
            ut::canvas::ClipX(canvasObject) = static_cast<float>(width);
            ut::canvas::ClipY(canvasObject) = static_cast<float>(height);
            return canvasObject;
        }

        ut::UObject* scriptConsole()
        {
            if (!world || !world->viewport || !world->player()) {
                return nullptr;
            }
            return ut::player::Console(world->viewport);
        }

        bool consoleKeyEvent(uint8_t key, uint8_t action, float delta)
        {
            ut::UObject* console = scriptConsole();
            if (!console) {
                return false;
            }
            ut::vm::Call call(console, ut::Name("KeyEvent"));
            if (!call.valid()) {
                return false;
            }
            call.byte(key).byte(action).f(delta).run();
            return call.returnValue() && *reinterpret_cast<uint32_t*>(call.returnValue());
        }

        void charEvent(char c)
        {
            ut::UObject* console = scriptConsole();
            if (console) {
                ut::vm::Call call(console, ut::Name("KeyType"));
                if (call.valid()) {
                    call.byte(static_cast<uint8_t>(c)).run();
                }
            }
        }

        void handleKey(uint8_t key, bool pressed)
        {
            if (key == ut::IK_None) {
                return;
            }
            uint8_t action = pressed ? ut::IST_Press : ut::IST_Release;
            if (consoleKeyEvent(key, action, 0)) {
                return;
            }
            ut::console::key(key, action, 0);
        }

        void handleAxis(uint8_t key, float delta)
        {
            if (consoleKeyEvent(key, ut::IST_Axis, delta)) {
                return;
            }
            ut::console::key(key, ut::IST_Axis, delta);
        }

        void processEvents()
        {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (!event.key.repeat) {
                        handleKey(translateKey(event.key.keysym.sym), true);
                    } else {
                        consoleKeyEvent(translateKey(event.key.keysym.sym), ut::IST_Hold, 0);
                    }
                    break;
                case SDL_KEYUP:
                    handleKey(translateKey(event.key.keysym.sym), false);
                    break;
                case SDL_TEXTINPUT:
                    for (const char* c = event.text.text; *c; c++) {
                        charEvent(*c);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    handleAxis(ut::IK_MouseX, static_cast<float>(event.motion.xrel));
                    handleAxis(ut::IK_MouseY, static_cast<float>(-event.motion.yrel));
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    handleKey(translateButton(event.button.button), event.type == SDL_MOUSEBUTTONDOWN);
                    break;
                case SDL_MOUSEWHEEL: {
                    uint8_t key = event.wheel.y > 0 ? ut::IK_MouseWheelUp : ut::IK_MouseWheelDown;
                    if (event.wheel.y != 0) {
                        handleAxis(ut::IK_MouseW, static_cast<float>(event.wheel.y * 120));
                        handleKey(key, true);
                        handleKey(key, false);
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        void handleTravel()
        {
            bool hosting = world && world->netMode == ut::NM_ListenServer;
            if (!pendingMap.empty()) {
                std::string url = isServerAddress(pendingMap) ? pendingMap : ut::travelUrl(world ? world->url : std::string(), pendingMap, pendingTravelType);
                bool keepHosting = hosting && pendingTravelType == ut::TRAVEL_Relative;
                pendingMap.clear();
                openMap(url, false, keepHosting ? ut::NM_ListenServer : ut::NM_Standalone);
                return;
            }
            if (world && world->travelPending) {
                world->travelPending = false;
                if (world->netMode == ut::NM_Client) {
                    return;
                }
                std::string url = ut::travelUrl(world->url, world->travelUrl, world->travelType);
                openMap(url, world->travelItems, hosting ? ut::NM_ListenServer : ut::NM_Standalone);
            }
        }

        void render(int width, int height)
        {
            frameWidth = width;
            frameHeight = height;
            if (world) {
                renderer->setTime(world->timeSeconds);
            }
            renderer->setViewportTime(viewportTime);
            renderer->beginFrame(width, height);
            viewLocation = {};
            viewRotation = {};
            viewFov = 90;
            if (world && world->calcView(viewLocation, viewRotation, viewFov)) {
                viewFov = displayFov(viewFov, width, height);
                ut::UObject* player = world->player();
                ut::UObject* viewActor = nullptr;
                if (player && !ut::pawn::bBehindView(player)) {
                    viewActor = ut::playerpawn::ViewTarget(player) ? ut::playerpawn::ViewTarget(player) : player;
                }
                ut::UObject* console = scriptConsole();
                if (!console || !ut::console::bNoDrawWorld(console)) {
                    renderer->drawScene(world->level, world->actors, viewLocation, viewRotation, viewFov, viewActor);
                }
                ut::UObject* canvasActor = canvas(width, height);
                if (player && !player->destroyed) {
                    ut::vm::Call(player, ut::Name("PreRender")).obj(canvasActor).run();
                    if (viewActor) {
                        ut::vm::Call(viewActor, ut::Name("RenderOverlays")).obj(canvasActor).run();
                    }
                    ut::vm::Call(player, ut::Name("PostRenderFlash")).obj(canvasActor).run();
                    ut::vm::Call(player, ut::Name("PostRender")).obj(canvasActor).run();
                    if (console) {
                        float scale = static_cast<float>(ut::uiScale(height));
                        ut::ScaledCanvas scaled(renderer->canvas(), scale);
                        ut::CanvasHooks& hooks = ut::canvasHooks();
                        hooks.target = &scaled;
                        ut::canvas::ClipX(canvasActor) = static_cast<float>(width) / scale;
                        ut::canvas::ClipY(canvasActor) = static_cast<float>(height) / scale;
                        ut::console::FrameX(console) = static_cast<float>(width) / scale;
                        ut::console::FrameY(console) = static_cast<float>(height) / scale;
                        ut::vm::Call(console, ut::Name("PostRender")).obj(canvasActor).run();
                        hooks.target = &renderer->canvas();
                    }
                }
            }
            renderer->endFrame();
        }

        void scriptedAction(const std::string& action)
        {
            size_t space = action.find(' ');
            std::string verb = action.substr(0, space);
            std::string rest = space == std::string::npos ? std::string() : action.substr(space + 1);
            if (verb == "press" || verb == "release") {
                handleKey(ut::console::keyFromName(rest), verb == "press");
            } else if (verb == "type") {
                for (char c : rest) {
                    charEvent(c);
                }
            } else if (verb == "mouse") {
                float dx = 0;
                float dy = 0;
                std::sscanf(rest.c_str(), "%f %f", &dx, &dy);
                handleAxis(ut::IK_MouseX, dx);
                handleAxis(ut::IK_MouseY, -dy);
            } else if (verb == "exec") {
                std::string output = ut::console::execute(world ? world->player() : nullptr, rest);
                if (!output.empty()) {
                    std::printf("%s\n", output.c_str());
                }
            } else if (verb == "shot") {
                pendingShot = rest;
            } else if (verb == "state") {
                ut::UObject* player = world ? world->player() : nullptr;
                if (player) {
                    ut::Vector location = ut::actor::Location(player);
                    std::printf("player at %.1f %.1f %.1f yaw %d state %s health %d\n", static_cast<double>(location.x), static_cast<double>(location.y), static_cast<double>(location.z), ut::actor::Rotation(player).yaw & 65535, ut::vm::stateName(player).c_str(), ut::pawn::Health(player));
                }
            } else if (world) {
                worldAction(*world, verb, rest);
            }
            std::fflush(stdout);
        }

        void begin()
        {
            for (const std::string& command : options.commands) {
                ut::console::execute(world ? world->player() : nullptr, command);
            }
            last = SDL_GetPerformanceCounter();
        }

        int run()
        {
            begin();
            while (running) {
                step();
            }
            return 0;
        }

        void step()
        {
            processEvents();
            uint64_t now = SDL_GetPerformanceCounter();
            float delta = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
            last = now;
            if (options.frames >= 0) {
                delta = 1.0f / 30.0f;
            }
            viewportTime += static_cast<double>(delta);
            applyRenderOptions();
            if (world) {
                world->tick(delta);
            }
            pollNetwork(delta);
            ut::UObject* console = scriptConsole();
            if (console) {
                ut::vm::Call(console, ut::Name("Tick")).f(delta).run();
                int height = 0;
                SDL_GL_GetDrawableSize(window, nullptr, &height);
                ut::console::ConsoleLines(console) = static_cast<int32_t>(std::min(ut::console::ConsolePos(console) * static_cast<float>(height), static_cast<float>(height)));
            }
            handleTravel();
            int width = 0;
            int height = 0;
            SDL_GL_GetDrawableSize(window, &width, &height);
            render(width, height);
            renderer->applyBrightness(static_cast<float>(std::atof(ut::config::system().get(ut::videoSection(), "Brightness").value_or("0.5").c_str())));
            ut::audio::update(viewLocation, viewRotation, delta);
            if (!pendingShot.empty()) {
                std::vector<uint8_t> pixels;
                int shotWidth = 0;
                int shotHeight = 0;
                renderer->readPixels(pixels, shotWidth, shotHeight);
                saveBmp(pendingShot, pixels, shotWidth, shotHeight);
                pendingShot.clear();
            }
            frame++;
            for (const auto& [actionFrame, action] : options.actions) {
                if (actionFrame == frame) {
                    scriptedAction(action);
                }
            }
            if (options.control) {
                for (const std::string& line : controlLines()) {
                    scriptedAction(line);
                    std::printf("done\n");
                    std::fflush(stdout);
                }
            }
            if (options.frames >= 0 && frame >= options.frames) {
                if (!options.screenshot.empty()) {
                    std::vector<uint8_t> pixels;
                    int shotWidth = 0;
                    int shotHeight = 0;
                    renderer->readPixels(pixels, shotWidth, shotHeight);
                    saveBmp(options.screenshot, pixels, shotWidth, shotHeight);
                }
                running = false;
            }
            SDL_GL_SwapWindow(window);
            limitFrameRate();
            if (options.fps > 0) {
                SDL_Delay(static_cast<uint32_t>(1000 / options.fps));
            }
        }

        bool isRunning() const
        {
            return running;
        }

    private:
        SDL_Window* window;
        Options options;
        std::unique_ptr<ut::SceneRenderer> renderer;
        std::unique_ptr<ut::World> world;
        std::unique_ptr<ut::net::Transport> transport;
        std::unique_ptr<ut::net::Driver> driver;
        std::string joinOptions;
        std::string serverUrl;
        std::string lastUrl;
        ut::UObject* canvasObject = nullptr;
        std::string pendingMap;
        int32_t pendingTravelType = ut::TRAVEL_Partial;
        std::string pendingShot;
        ut::Vector viewLocation;
        ut::Rotator viewRotation;
        float viewFov = 90;
        int frameWidth = 1;
        double viewportTime = 0;
        int frameHeight = 1;
        bool running = true;
        uint64_t last = 0;
        int frame = 0;
        int appliedVSync = -1;
        int appliedRawMouse = -1;
        ut::DisplayMode displayMode = ut::DisplayMode::Windowed;
        ut::DisplayMode lastFullscreenMode = ut::DisplayMode::Borderless;
        uint64_t frameDeadline = 0;
    };
}

namespace {
    constexpr float MaxFrameSeconds = 0.25f;
    constexpr int MaxSteps = 4;

    int runHeadless(const Options& options)
    {
        SDL_Init(SDL_INIT_TIMER);
        ut::engine::initialize(ut::config::findGameRoot(OPENUT_GAME_DATA).string());
        bool server = options.dedicated;
        std::unique_ptr<ut::net::Transport> transport = server ? listenOn(listenPort(options)) : connectTo(options.map.substr(0, options.map.find_first_of("?/")));
        if (!transport) {
            std::fprintf(stderr, server ? "cannot listen on port %d\n" : "cannot connect\n", listenPort(options));
            return 1;
        }
        ut::net::Driver driver(*transport, server);
        std::unique_ptr<ut::World> world;
        std::string pendingMap;
        if (server) {
            pendingMap = options.map.empty() ? std::string("DM-Deck16][?game=Botpack.DeathMatchPlus") : options.map;
        }
        int32_t pendingTravelType = ut::TRAVEL_Absolute;
        bool running = true;
        ut::console::openMapHook = [&](const std::string& url, int32_t travelType) {
            if (server) {
                pendingMap = url;
                pendingTravelType = travelType;
            }
        };
        ut::console::quitHook = [&]() {
            running = false;
        };
        uint64_t last = SDL_GetPerformanceCounter();
        float step = 1.0f / driver.tickRate;
        float accumulated = 0;
        while (running) {
            if (world && world->travelPending) {
                world->travelPending = false;
                if (server) {
                    pendingMap = world->travelUrl;
                    pendingTravelType = world->travelType;
                }
            }
            if (!server && !driver.welcomeUrl.empty()) {
                pendingMap = driver.welcomeUrl;
                pendingTravelType = ut::TRAVEL_Absolute;
                driver.welcomeUrl.clear();
            }
            if (!pendingMap.empty()) {
                std::string url = ut::travelUrl(world ? world->url : std::string(), pendingMap, pendingTravelType);
                pendingMap.clear();
                driver.setWorld(nullptr);
                ut::Name previousPackage;
                if (world && world->level) {
                    previousPackage = world->level->outermost()->name;
                    ut::objects::unloadPackage(previousPackage);
                }
                world.reset();
                ut::objects::collectPackage(previousPackage);
                world = std::make_unique<ut::World>();
                world->netMode = server ? ut::NM_DedicatedServer : ut::NM_Client;
                world->netPort = server ? listenPort(options) : 0;
                world->inputHook = [](ut::UObject* player, float delta) {
                    ut::console::readInput(player, delta);
                };
                driver.setWorld(world.get());
                if (!world->loadMap(url)) {
                    std::fprintf(stderr, "cannot load %s\n", url.c_str());
                    return 1;
                }
                if (server) {
                    std::printf("server running %s on port %d\n", world->url.c_str(), listenPort(options));
                    std::fflush(stdout);
                } else {
                    driver.join(joinOptionsFor(options.map));
                }
                for (const std::string& command : options.commands) {
                    ut::console::execute(nullptr, command);
                }
            }
            uint64_t now = SDL_GetPerformanceCounter();
            float delta = std::min(static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency()), MaxFrameSeconds);
            last = now;
            if (server) {
                accumulated += delta;
                int steps = 0;
                while (accumulated >= step && steps < MaxSteps) {
                    if (world) {
                        world->tick(step);
                    }
                    driver.tick(step);
                    accumulated -= step;
                    steps++;
                }
                accumulated = std::min(accumulated, step);
            } else {
                if (world) {
                    world->tick(delta);
                }
                driver.tick(delta);
            }
            if (driver.closed) {
                std::printf("disconnected: %s\n", driver.failure.c_str());
                return 1;
            }
            if (options.control && world) {
                for (const std::string& line : controlLines()) {
                    size_t space = line.find(' ');
                    std::string verb = line.substr(0, space);
                    std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
                    if (verb == "exec") {
                        ut::console::execute(world->player(), rest);
                    } else if (verb == "press" || verb == "release") {
                        ut::console::key(ut::console::keyFromName(rest), verb == "press" ? ut::IST_Press : ut::IST_Release, 0);
                    } else {
                        worldAction(*world, verb, rest);
                    }
                    std::printf("done\n");
                    std::fflush(stdout);
                }
            }
            if (server) {
                SDL_Delay(static_cast<uint32_t>(std::max((step - accumulated) * 1000.0f, 1.0f)));
            } else {
                SDL_Delay(20);
            }
        }
        driver.setWorld(nullptr);
        return 0;
    }

    int startupFailure(const std::string& message, SDL_Window* window)
    {
        std::fprintf(stderr, "%s\n", message.c_str());
#ifdef __EMSCRIPTEN__
        (void)window;
        showPageError(message.c_str());
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unreal Tournament", message.c_str(), window);
#endif
        return 1;
    }
}

int main(int argc, char** argv)
{
    Options options = parseOptions(argc, argv);
    if (options.dedicated || options.headless) {
        return runHeadless(options);
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        return startupFailure(std::string("Could not initialize the video system: ") + SDL_GetError(), nullptr);
    }
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Unreal Tournament", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, options.width, options.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        return startupFailure(std::string("Could not create the game window: ") + SDL_GetError(), nullptr);
    }
    ut::icon::applyToWindow(window, ut::config::findGameRoot(OPENUT_GAME_DATA) / "Help" / "Unreal.ico");
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        return startupFailure(std::string("OpenGL 3.3 is required and could not be started: ") + SDL_GetError(), window);
    }
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
        return startupFailure("OpenGL 3.3 is required and could not be loaded.", window);
    }
    SDL_GL_SetSwapInterval(options.frames >= 0 ? 0 : 1);
    SDL_StartTextInput();
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetRelativeMouseMode(SDL_TRUE);
    ut::vm::setCriticalHandler([window](const std::string& message) {
        std::fprintf(stderr, "Critical Error: %s\n", message.c_str());
        std::fflush(stderr);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", message.c_str(), window);
        std::_Exit(1);
    });
#ifdef __EMSCRIPTEN__
    try {
        Game* game = new Game(window, options);
        if (!game->start()) {
            return startupFailure("The starting map could not be loaded.", window);
        }
        game->begin();
        emscripten_set_click_callback("#fullscreen", nullptr, true, [](int, const EmscriptenMouseEvent*, void*) -> EM_BOOL {
            ut::console::toggleFullscreenHook();
            return true;
        });
        emscripten_set_main_loop_arg(
            [](void* argument) {
                Game* running = static_cast<Game*>(argument);
                try {
                    running->step();
                } catch (const std::exception& error) {
                    startupFailure(error.what(), nullptr);
                    emscripten_cancel_main_loop();
                    return;
                }
                if (!running->isRunning()) {
                    emscripten_cancel_main_loop();
                }
            },
            game, 0, true);
    } catch (const std::exception& error) {
        return startupFailure(error.what(), window);
    }
    return 0;
#else
    int result = 0;
    try {
        Game game(window, options);
        if (!game.start()) {
            result = startupFailure("The starting map could not be loaded.", window);
        } else {
            result = game.run();
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unreal Tournament", error.what(), window);
        result = 1;
    }
    ut::audio::shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
#endif
}
