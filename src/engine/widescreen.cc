#include "core/vm.h"
#include "engine/props_engine.h"

#include <algorithm>

namespace ut {
    namespace {
        float designWidth(UObject* canvasObject)
        {
            return std::min(canvas::ClipX(canvasObject), canvas::ClipY(canvasObject) * 4.0f / 3.0f);
        }

        class CenteredCanvas {
        public:
            explicit CenteredCanvas(UObject* canvasObject)
                : target(canvasObject)
            {
                if (!target) {
                    return;
                }
                orgX = canvas::OrgX(target);
                clipX = canvas::ClipX(target);
                float width = designWidth(target);
                canvas::OrgX(target) = orgX + (clipX - width) * 0.5f;
                canvas::ClipX(target) = width;
            }

            ~CenteredCanvas()
            {
                if (!target) {
                    return;
                }
                canvas::OrgX(target) = orgX;
                canvas::ClipX(target) = clipX;
            }

            CenteredCanvas(const CenteredCanvas&) = delete;
            CenteredCanvas& operator=(const CenteredCanvas&) = delete;

        private:
            UObject* target;
            float orgX = 0;
            float clipX = 0;
        };

        struct VirtualScreen {
            UObject* canvas = nullptr;
            float orgX = 0;
        };

        VirtualScreen* activeScreen = nullptr;

        UFunction* hookedFunction(Frame& stack)
        {
            return cast<UFunction>(stack.node);
        }

        void hudSetup(UObject* self, Frame& stack, void*)
        {
            UObject* canvas = vm::get<UObject*>(stack);
            vm::finish(stack);
            vm::Call(self, hookedFunction(stack)).obj(canvas).runScript();
            if (!canvas) {
                return;
            }
            float hudScale = *reinterpret_cast<float*>(self->propertyAddress(Name("HUDScale")));
            *reinterpret_cast<float*>(self->propertyAddress(Name("Scale"))) = hudScale * designWidth(canvas) / 1280.0f;
        }

        void drawCrosshair(UObject* self, Frame& stack, void*)
        {
            UObject* canvas = vm::get<UObject*>(stack);
            int32_t x = vm::get<int32_t>(stack);
            int32_t y = vm::get<int32_t>(stack);
            vm::finish(stack);
            CenteredCanvas centered(canvas);
            vm::Call(self, hookedFunction(stack)).obj(canvas).i(x).i(y).runScript();
        }

        void redeemerPostRender(UObject* self, Frame& stack, void*)
        {
            UObject* canvas = vm::get<UObject*>(stack);
            vm::finish(stack);
            bool guiding = *reinterpret_cast<uint32_t*>(self->propertyAddress(Name("bGuiding"))) != 0;
            CenteredCanvas centered(guiding ? canvas : nullptr);
            vm::Call(self, hookedFunction(stack)).obj(canvas).runScript();
        }

        void hudPostRender(UObject* self, Frame& stack, void*)
        {
            UObject* canvas = vm::get<UObject*>(stack);
            vm::finish(stack);
            UFunction* function = hookedFunction(stack);
            if (!canvas || self->isA(Name("ChallengeHUD"))) {
                vm::Call(self, function).obj(canvas).runScript();
                return;
            }
            CenteredCanvas centered(canvas);
            VirtualScreen screen { canvas, canvas::OrgX(canvas) };
            VirtualScreen* previous = activeScreen;
            activeScreen = &screen;
            vm::Call(self, function).obj(canvas).runScript();
            activeScreen = previous;
        }

        void canvasReset(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            vm::Call(self, hookedFunction(stack)).runScript();
            if (activeScreen && activeScreen->canvas == self) {
                canvas::OrgX(self) = activeScreen->orgX;
            }
        }

        void canvasSetOrigin(UObject* self, Frame& stack, void*)
        {
            float x = vm::get<float>(stack);
            float y = vm::get<float>(stack);
            vm::finish(stack);
            vm::Call(self, hookedFunction(stack)).f(x).f(y).runScript();
            if (activeScreen && activeScreen->canvas == self) {
                canvas::OrgX(self) = activeScreen->orgX + x;
            }
        }

        void centeredCanvasCall(UObject* self, Frame& stack, void*)
        {
            UObject* canvas = vm::get<UObject*>(stack);
            vm::finish(stack);
            CenteredCanvas centered(canvas);
            vm::Call(self, hookedFunction(stack)).obj(canvas).runScript();
        }
    }

    void registerWidescreenHooks()
    {
        vm::registerInheritedOverride("ChallengeHUD", "HUDSetup", hudSetup);
        vm::registerInheritedOverride("ChallengeHUD", "DrawCrossHair", drawCrosshair);
        vm::registerInheritedOverride("HUD", "PostRender", hudPostRender);
        vm::registerInheritedOverride("Canvas", "Reset", canvasReset);
        vm::registerInheritedOverride("Canvas", "SetOrigin", canvasSetOrigin);
        vm::registerInheritedOverride("Pawn", "RenderOverlays", centeredCanvasCall);
        vm::registerInheritedOverride("SniperRifle", "PostRender", centeredCanvasCall);
        vm::registerInheritedOverride("UT_Eightball", "PostRender", centeredCanvasCall);
        vm::registerInheritedOverride("WarheadLauncher", "PostRender", redeemerPostRender);
        vm::registerInheritedOverride("ScoreBoard", "ShowScores", centeredCanvasCall);
    }
}
