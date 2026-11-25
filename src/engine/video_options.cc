#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/video_options.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ut {
    std::string videoSection()
    {
        return config::system().get("Engine.Engine", "ViewportManager").value_or("WinDrv.WindowsClient");
    }

    int32_t videoSetting(const char* key, int32_t fallback)
    {
        std::optional<std::string> value = config::system().get(videoSection(), key);
        if (!value) {
            return fallback;
        }
        if (iequals(*value, "True")) {
            return 1;
        }
        if (iequals(*value, "False")) {
            return 0;
        }
        errno = 0;
        char* end = nullptr;
        long result = std::strtol(value->c_str(), &end, 10);
        if (end == value->c_str() || errno == ERANGE || result < INT32_MIN || result > INT32_MAX) {
            return fallback;
        }
        return static_cast<int32_t>(result);
    }

    int32_t uiScale(int32_t screenHeight)
    {
        int32_t setting = videoSetting("UIScale", 0);
        return std::clamp(setting == 0 ? screenHeight / 540 : setting, 1, 4);
    }

    std::function<void(DisplayMode)> setDisplayModeHook;

    DisplayMode displayModeSetting()
    {
        return static_cast<DisplayMode>(std::clamp(videoSetting("DisplayMode", 0), 0, 2));
    }

    namespace {
        constexpr uint8_t DE_Change = 1;

        struct ComboOption {
            const char* caption;
            const char* help;
            const char* key;
            int32_t firstValue;
            std::vector<const char*> items;
        };

        struct CheckOption {
            const char* caption;
            const char* help;
            const char* key;
        };

        const std::vector<ComboOption>& comboOptions()
        {
            static const std::vector<ComboOption> options = {
                { "Display Mode", "Windowed runs in a window, Fullscreen switches the monitor to the chosen resolution, Borderless covers the desktop at its own resolution.", "DisplayMode", 0, { "Windowed", "Fullscreen", "Borderless" } },
                { "Anti-Aliasing", "Smooth jagged edges with multisampling. Higher settings cost more performance.", "Multisampling", 0, { "Off", "2x", "4x", "8x" } },
                { "Anisotropic Filtering", "Keep textures sharp when seen at an angle.", "Anisotropy", 0, { "Off", "2x", "4x", "8x", "16x" } },
                { "UI Scale", "Draw the menus and console larger, for high resolution screens. Auto picks the size from the screen height.", "UIScale", 0, { "Auto", "Off", "2x", "3x", "4x" } },
            };
            return options;
        }

        const std::vector<CheckOption>& checkOptions()
        {
            static const std::vector<CheckOption> options = {
                { "Raw Mouse Input", "Read the mouse without the system pointer acceleration. Wayland always does.", "RawMouseInput" },
                { "High Detail Textures", "Use the high resolution S3TC textures when they are installed.", "UseS3TC" },
                { "Soft Particles", "Fade smoke and explosions where they touch walls and floors instead of cutting them off.", "SoftParticles" },
                { "High Resolution Fonts", "Draw menu and HUD text with smooth Liberation Sans letters when the text is large on screen, such as with UI Scale at 2x or more.", "TrueTypeFonts" },
            };
            return options;
        }

        bool isComboOption(const std::string& text)
        {
            return std::any_of(comboOptions().begin(), comboOptions().end(), [&](const ComboOption& option) {
                return text == option.caption;
            });
        }

        bool isCheckOption(const std::string& text)
        {
            return std::any_of(checkOptions().begin(), checkOptions().end(), [&](const CheckOption& option) {
                return text == option.caption;
            });
        }

        void saveVideoSetting(const char* key, const std::string& value)
        {
            config::system().set(videoSection(), key, value);
            config::system().save();
        }

        UObject*& objectProperty(UObject* object, const char* name)
        {
            return *reinterpret_cast<UObject**>(object->propertyAddress(Name(name)));
        }

        float& floatProperty(UObject* object, const char* name)
        {
            return *reinterpret_cast<float*>(object->propertyAddress(Name(name)));
        }

        uint32_t& boolProperty(UObject* object, const char* name)
        {
            return *reinterpret_cast<uint32_t*>(object->propertyAddress(Name(name)));
        }

        const std::string& controlText(UObject* control)
        {
            return *reinterpret_cast<std::string*>(control->propertyAddress(Name("Text")));
        }

        UFunction* scriptFunction(const char* functionName)
        {
            return objects::findClass("UMenu.UMenuVideoClientWindow")->findFunction(Name(functionName));
        }

        UObject* createControl(UObject* window, const char* className, float x, float y, float width)
        {
            vm::Call call(window, Name("CreateControl"));
            call.obj(objects::findClass(className)).f(x).f(y).f(width).f(1);
            call.run();
            return *reinterpret_cast<UObject**>(call.returnValue());
        }

        void describe(UObject* control, const char* caption, const char* help)
        {
            vm::Call(control, Name("SetText")).s(caption).run();
            vm::Call(control, Name("SetHelpText")).s(help).run();
            vm::Call(control, Name("SetFont")).i(0).run();
        }

        void alignWith(UObject* control, UObject* stock)
        {
            vm::Call(control, Name("SetSize")).f(floatProperty(stock, "WinWidth")).f(1).run();
            floatProperty(control, "WinLeft") = floatProperty(stock, "WinLeft");
        }

        void videoCreated(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            vm::Call(self, scriptFunction("Created")).runScript();
            float& offset = floatProperty(self, "ControlOffset");
            UObject* stockCombo = objectProperty(self, "ScaleCombo");
            UObject* stockCheck = objectProperty(self, "DynamicLightsCheck");
            for (const ComboOption& option : comboOptions()) {
                UObject* combo = createControl(self, "UWindow.UWindowComboControl", floatProperty(stockCombo, "WinLeft"), offset, floatProperty(stockCombo, "WinWidth"));
                describe(combo, option.caption, option.help);
                vm::Call(combo, Name("SetEditable")).b(false).run();
                for (const char* item : option.items) {
                    vm::Call(combo, Name("AddItem")).s(item).run();
                }
                int32_t index = std::clamp(videoSetting(option.key, option.firstValue) - option.firstValue, 0, static_cast<int32_t>(option.items.size()) - 1);
                vm::Call(combo, Name("SetSelectedIndex")).i(index).run();
                floatProperty(combo, "EditBoxWidth") = 100;
                offset += 25;
            }
            for (const CheckOption& option : checkOptions()) {
                UObject* check = createControl(self, "UWindow.UWindowCheckbox", floatProperty(stockCheck, "WinLeft"), offset, floatProperty(stockCheck, "WinWidth"));
                describe(check, option.caption, option.help);
                boolProperty(check, "bChecked") = videoSetting(option.key, 0) != 0 ? 1 : 0;
                *check->propertyAddress(Name("Align")) = 0;
                offset += 25;
            }
        }

        void videoBeforePaint(UObject* self, Frame& stack, void*)
        {
            UObject* canvas = vm::get<UObject*>(stack);
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            vm::finish(stack);
            vm::Call(self, scriptFunction("BeforePaint")).obj(canvas).f(x).f(y).runScript();
            UClass* comboClass = objects::findClass("UWindow.UWindowComboControl");
            UClass* checkClass = objects::findClass("UWindow.UWindowCheckbox");
            UObject* stockCombo = objectProperty(self, "ScaleCombo");
            UObject* stockCheck = objectProperty(self, "DynamicLightsCheck");
            for (UObject* child = objectProperty(self, "FirstChildWindow"); child; child = objectProperty(child, "NextSiblingWindow")) {
                if (child->cls->isChildOf(comboClass) && isComboOption(controlText(child))) {
                    alignWith(child, stockCombo);
                    floatProperty(child, "EditBoxWidth") = 100;
                } else if (child->cls->isChildOf(checkClass) && isCheckOption(controlText(child))) {
                    alignWith(child, stockCheck);
                }
            }
        }

        void videoNotify(UObject* self, Frame& stack, void*)
        {
            UObject* control = vm::get<UObject*>(stack);
            uint8_t event = vm::get<uint8_t>(stack);
            vm::finish(stack);
            vm::Call(self, scriptFunction("Notify")).obj(control).byte(event).runScript();
            if (!control || event != DE_Change) {
                return;
            }
            const std::string& text = controlText(control);
            for (const ComboOption& option : comboOptions()) {
                if (text == option.caption) {
                    vm::Call index(control, Name("GetSelectedIndex"));
                    index.run();
                    int32_t value = *reinterpret_cast<int32_t*>(index.returnValue()) + option.firstValue;
                    if (std::string_view(option.key) == "DisplayMode") {
                        DisplayMode mode = static_cast<DisplayMode>(std::clamp(value, 0, 2));
                        if (mode != displayModeSetting() && setDisplayModeHook) {
                            setDisplayModeHook(mode);
                            vm::Call(self, Name("LoadAvailableSettings")).run();
                        }
                    } else {
                        saveVideoSetting(option.key, std::to_string(value));
                    }
                }
            }
            for (const CheckOption& option : checkOptions()) {
                if (text == option.caption) {
                    saveVideoSetting(option.key, boolProperty(control, "bChecked") ? "True" : "False");
                }
            }
        }
    }

    void registerVideoOptionsHooks()
    {
        vm::registerOverride("UMenuVideoClientWindow", "Created", videoCreated);
        vm::registerOverride("UMenuVideoClientWindow", "BeforePaint", videoBeforePaint);
        vm::registerOverride("UMenuVideoClientWindow", "Notify", videoNotify);
    }
}
