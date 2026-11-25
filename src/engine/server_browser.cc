#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/package.h"

#include <string>
#include <vector>

namespace ut {
    namespace {
        constexpr uint8_t DE_Click = 2;
        constexpr uint8_t DE_EnterPressed = 7;
        constexpr int32_t HistorySize = 10;

        UObject*& objectProperty(UObject* object, const char* name, int32_t index = 0)
        {
            return *reinterpret_cast<UObject**>(object->propertyAddress(Name(name), index));
        }

        std::string& stringProperty(UObject* object, const char* name, int32_t index = 0)
        {
            return *reinterpret_cast<std::string*>(object->propertyAddress(Name(name), index));
        }

        float& floatProperty(UObject* object, const char* name)
        {
            return *reinterpret_cast<float*>(object->propertyAddress(Name(name)));
        }

        UObject* returnedObject(vm::Call& call)
        {
            call.run();
            return *reinterpret_cast<UObject**>(call.returnValue());
        }

        UFunction* scriptFunction(const char* className, const char* functionName)
        {
            UClass* cls = objects::findClass(className);
            return cls ? cls->findFunction(Name(functionName)) : nullptr;
        }

        UObject* createWindow(UObject* parent, UClass* cls, float x, float y, float width, float height)
        {
            vm::Call call(parent, Name("CreateWindow"));
            call.obj(cls).f(x).f(y).f(width).f(height);
            return returnedObject(call);
        }

        UObject* addPage(UObject* pageControl, const std::string& caption, UClass* cls, Name objectName = Name())
        {
            vm::Call call(pageControl, Name("AddPage"));
            call.s(caption).obj(cls).n(objectName);
            return returnedObject(call);
        }

        UObject* createControl(UObject* dialog, const char* className, float x, float y, float width, float height)
        {
            vm::Call call(dialog, Name("CreateControl"));
            call.obj(objects::findClass(className)).f(x).f(y).f(width).f(height);
            return returnedObject(call);
        }

        UObject* directConnectCombo(UObject* page)
        {
            UClass* comboClass = objects::findClass("UWindow.UWindowComboControl");
            for (UObject* child = objectProperty(page, "FirstChildWindow"); child; child = objectProperty(child, "NextSiblingWindow")) {
                if (child->cls->isChildOf(comboClass)) {
                    return child;
                }
            }
            return nullptr;
        }

        void rememberAddress(const std::string& address)
        {
            UClass* openClass = objects::findClass("UBrowser.UBrowserOpenCW");
            UObject* defaults = openClass->defaultObject;
            std::vector<std::string> history { address };
            for (int32_t i = 0; i < HistorySize; i++) {
                const std::string& entry = stringProperty(defaults, "OpenHistory", i);
                if (!entry.empty() && !iequals(entry, address)) {
                    history.push_back(entry);
                }
            }
            for (int32_t i = 0; i < HistorySize; i++) {
                stringProperty(defaults, "OpenHistory", i) = static_cast<size_t>(i) < history.size() ? history[static_cast<size_t>(i)] : std::string();
            }
            config::saveObjectConfig(defaults, openClass);
        }

        void connect(UObject* page, UObject* combo)
        {
            vm::Call value(combo, Name("GetValue"));
            value.run();
            std::string address = *reinterpret_cast<std::string*>(value.returnValue());
            if (address.empty()) {
                return;
            }
            rememberAddress(address);
            vm::Call parent(page, Name("GetParent"));
            parent.obj(objects::findClass("UBrowser.UBrowserMainWindow"));
            UObject* browser = returnedObject(parent);
            vm::Call(browser, Name("OpenURL")).s(address).run();
        }

        void multiplayerMenuCreated(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            vm::Call(self, scriptFunction("UMenu.UMenuMultiplayerMenu", "Created")).runScript();
            vm::Call(objectProperty(self, "Browser"), Name("SetCaption")).s("&Server Browser").run();
            stringProperty(self, "BrowserHelp") = "Find games on your local network, or connect to a server by its address.";
            UObject* patch = objectProperty(self, "Patch");
            vm::Call(objectProperty(patch, "Prev"), Name("Remove")).run();
            for (const char* item : { "LAN", "OpenLocation", "Patch" }) {
                vm::Call(objectProperty(self, item), Name("Remove")).run();
            }
        }

        void utMultiplayerMenuCreated(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            vm::Call(self, scriptFunction("UTMenu.UTMultiplayerMenu", "Created")).runScript();
            UObject* items = objectProperty(self, "Items");
            for (UObject* last = objectProperty(items, "Last"); last && last != items && stringProperty(last, "Caption") == "-"; last = objectProperty(items, "Last")) {
                vm::Call(last, Name("Remove")).run();
            }
        }

        void browserClientCreated(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            vm::Call(self, objects::findClass("UBrowser.UBrowserMainClientWindow")->superClass()->findFunction(Name("Created"))).run();
            UObject* info = createWindow(objectProperty(self, "Root"), objects::findClass("UBrowser.UBrowserInfoWindow"), 10, 40, 310, 170);
            objectProperty(self, "InfoWindow") = info;
            vm::Call(info, Name("HideWindow")).run();

            UObject* pageControl = createWindow(self, objects::findClass("UWindow.UWindowPageControl"), 0, 0, floatProperty(self, "WinWidth"), floatProperty(self, "WinHeight"));
            objectProperty(self, "PageControl") = pageControl;
            vm::Call(pageControl, Name("SetMultiLine")).b(true).run();

            const std::string& lanName = stringProperty(self, "LANTabName");
            UObject* lan = addPage(pageControl, "", objects::findClass(stringProperty(self, "ServerListWindowClass")), Name(lanName));
            UObject* lanList = objectProperty(lan, "Page");
            const std::string& title = stringProperty(lanList, "ServerListTitle");
            vm::Call(lan, Name("SetCaption")).s(title.empty() ? config::localize("ServerListTitles", lanName, "UBrowser") : title).run();
            objectProperty(self, "LANPage") = lan;
            objectProperty(self, "FactoryWindows") = lanList;

            objectProperty(self, "Favorites") = addPage(pageControl, stringProperty(self, "FavoritesName"), objects::findClass(stringProperty(self, "FavoriteServersClass")));

            UObject* page = objectProperty(addPage(pageControl, "Direct Connect", objects::findClass("UWindow.UWindowPageWindow")), "Page");
            float width = floatProperty(page, "WinWidth");
            UObject* combo = createControl(page, "UWindow.UWindowComboControl", 10, 20, width - 100, 1);
            vm::Call(combo, Name("SetText")).s("Address:").run();
            vm::Call(combo, Name("SetHelpText")).s("Enter the address of a server, for example 192.168.1.20 or 192.168.1.20:7777, and press Enter.").run();
            vm::Call(combo, Name("SetFont")).i(0).run();
            vm::Call(combo, Name("SetEditable")).b(true).run();
            floatProperty(combo, "EditBoxWidth") = width - 160;
            UObject* history = objects::findClass("UBrowser.UBrowserOpenCW")->defaultObject;
            for (int32_t i = 0; i < HistorySize; i++) {
                const std::string& entry = stringProperty(history, "OpenHistory", i);
                if (!entry.empty()) {
                    vm::Call(combo, Name("AddItem")).s(entry).run();
                }
            }
            UObject* button = createControl(page, "UWindow.UWindowSmallButton", width - 80, 20, 64, 16);
            vm::Call(button, Name("SetText")).s("Connect").run();
            vm::Call(pageControl, Name("GotoTab")).obj(lan).run();
        }

        void dialogNotify(UObject* self, Frame& stack, void*)
        {
            UObject* control = vm::get<UObject*>(stack);
            uint8_t event = vm::get<uint8_t>(stack);
            vm::finish(stack);
            vm::Call call(self, scriptFunction("UWindow.UWindowDialogClientWindow", "Notify"));
            call.obj(control).byte(event).runScript();
            static const Name pageWindow("UWindowPageWindow");
            if (self->cls->name != pageWindow || !control) {
                return;
            }
            UObject* combo = directConnectCombo(self);
            bool entered = control == combo && event == DE_EnterPressed;
            bool clicked = control->isA(Name("UWindowSmallButton")) && event == DE_Click;
            if (combo && (entered || clicked)) {
                connect(self, combo);
            }
        }
    }

    void registerServerBrowserHooks()
    {
        vm::registerOverride("UMenuMultiplayerMenu", "Created", multiplayerMenuCreated);
        vm::registerOverride("UTMultiplayerMenu", "Created", utMultiplayerMenuCreated);
        vm::registerOverride("UBrowserMainClientWindow", "Created", browserClientCreated);
        vm::registerOverride("UWindowDialogClientWindow", "Notify", dialogNotify);
    }
}
