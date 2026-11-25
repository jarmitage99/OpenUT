#include "audio/audio.h"
#include "core/config.h"
#include "core/natives.h"
#include "core/objects.h"
#include "engine/actor.h"
#include "engine/console.h"
#include "engine/ipdrv.h"
#include "engine/mesh.h"
#include "engine/props_engine.h"
#include "engine/props_fire.h"
#include "engine/resources.h"
#include "engine/world.h"

#include <filesystem>

namespace ut::engine {
    void initializeCore(const std::string& gameRoot)
    {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;
        std::filesystem::path root(gameRoot);
        config::initialize(root / "System");
        registerEngineResources();
        registerMeshResources();
        registerObjectNatives();
        registerEngineNatives();
        registerIpDrvNatives();
        objects::initialize(root);
    }

    void initialize(const std::string& gameRoot)
    {
        static bool initialized = false;
        if (initialized) {
            return;
        }
        initialized = true;
        initializeCore(gameRoot);
        const char* packages[] = { "Engine", "Fire" };
        for (const char* package : packages) {
            objects::loadPackage(Name(package));
        }
        bindProps_engine();
        bindProps_fire();
        objects::flushLoads();
        bindEngineClasses();
        console::loadBindings();
    }
}
