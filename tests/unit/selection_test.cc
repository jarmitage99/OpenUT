#include "core/objects.h"
#include "engine/actor.h"
#include "engine/actor_selection.h"
#include "engine/csg.h"
#include "engine/editor_history.h"
#include "engine/props_engine.h"
#include "engine/resources.h"
#include "engine/t3d.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <unordered_map>

#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
    ut::UObject* spawn(const char* className, ut::ULevel* level, const ut::Vector& location)
    {
        ut::UObject* actor = ut::objects::newObject(ut::objects::findClass(className), level->outermost(), ut::Name(), ut::RF_Transactional);
        actor->loaded = true;
        ut::actor::Location(actor) = location;
        level->actors.push_back(actor);
        return actor;
    }

    ut::ULevel* emptyLevel(const char* packageName)
    {
        ut::engine::initialize(OPENUT_GAME_DATA);
        ut::UObject* package = ut::objects::packageObject(ut::Name(packageName));
        ut::ULevel* level = ut::cast<ut::ULevel>(ut::objects::newObject(ut::csg::nativeClass("Level"), package, ut::Name("MyLevel"), 0));
        level->loaded = true;
        level->model = ut::cast<ut::UModel>(ut::objects::newObject(ut::csg::nativeClass("Model"), package, ut::Name(), 0));
        level->model->loaded = true;
        return level;
    }

    ut::UObject* brushActor(ut::ULevel* level, const ut::Vector& location)
    {
        ut::UObject* brush = spawn("Engine.Brush", level, location);
        ut::UModel* model = ut::cast<ut::UModel>(ut::objects::newObject(ut::csg::nativeClass("Model"), level->outermost(), ut::Name(), 0));
        model->loaded = true;
        model->polys = ut::cast<ut::UPolys>(ut::objects::newObject(ut::csg::nativeClass("Polys"), level->outermost(), ut::Name(), 0));
        model->polys->loaded = true;
        ut::csg::buildCube(*model->polys, 128.0f, 64.0f, 32.0f, nullptr);
        ut::csg::updateBounds(*model);
        ut::actor::Brush(brush) = model;
        ut::brush::CsgOper(brush) = ut::csg::CsgSubtract;
        return brush;
    }

    size_t countOf(const std::string& text, const std::string& needle)
    {
        size_t count = 0;
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
            count++;
        }
        return count;
    }
}

TEST_CASE("actors export as UnrealEd T3D text with changed properties and brush polygons", "[data]")
{
    ut::ULevel* level = emptyLevel("T3DExportTest");
    ut::UObject* light = spawn("Engine.Light", level, { 16.0f, -32.0f, 48.0f });
    ut::actor::LightBrightness(light) = 200;
    ut::actor::Tag(light) = ut::Name("Lamp");
    ut::UObject* brush = brushActor(level, { 0.0f, 0.0f, 0.0f });
    std::string text = ut::t3d::exportActors({ light, brush });
    CHECK(text.rfind("Begin Map\r\nBegin Actor Class=Light Name=" + light->name.str() + "\r\n", 0) == 0);
    CHECK(text.find("    LightBrightness=200\r\n") != std::string::npos);
    CHECK(text.find("    Tag=Lamp\r\n") != std::string::npos);
    CHECK(text.find("    Location=(X=16.000000,Y=-32.000000,Z=48.000000)\r\n") != std::string::npos);
    CHECK(text.find("LightRadius=") == std::string::npos);
    CHECK(text.find("Begin Actor Class=Brush Name=" + brush->name.str() + "\r\n") != std::string::npos);
    CHECK(text.find("    CsgOper=CSG_Subtract\r\n") != std::string::npos);
    CHECK(text.find("    Begin Brush Name=" + ut::actor::Brush(brush)->name.str() + "\r\n       Begin PolyList\r\n") != std::string::npos);
    CHECK(countOf(text, "Begin Polygon") == 6);
    CHECK(countOf(text, "End Actor\r\n") == 2);
    CHECK(text.size() > 10);
    CHECK(text.substr(text.size() - 9) == "End Map\r\n");
}

TEST_CASE("pasting T3D text creates new actors with the copied properties and brush", "[data]")
{
    ut::ULevel* level = emptyLevel("T3DPasteTest");
    ut::UObject* info = spawn("Engine.LevelInfo", level, {});
    ut::UObject* light = spawn("Engine.Light", level, { 16.0f, -32.0f, 48.0f });
    ut::actor::LightBrightness(light) = 200;
    ut::actor::Tag(light) = ut::Name("Lamp");
    ut::UObject* brush = brushActor(level, { 64.0f, 0.0f, 0.0f });
    ut::UModel* source = ut::cast<ut::UModel>(ut::actor::Brush(brush));
    std::string text = ut::t3d::exportActors({ info, light, brush });
    std::vector<ut::UObject*> pasted = ut::t3d::importActors(level, text, false);
    REQUIRE(pasted.size() == 2);
    CHECK(level->actors.size() == 5);
    ut::UObject* lightCopy = pasted[0];
    ut::UObject* brushCopy = pasted[1];
    CHECK(lightCopy != light);
    CHECK(lightCopy->name != light->name);
    CHECK(lightCopy->cls == light->cls);
    CHECK(ut::actor::LightBrightness(lightCopy) == 200);
    CHECK(ut::actor::Tag(lightCopy) == ut::Name("Lamp"));
    CHECK(ut::actor::Location(lightCopy).x == 16.0f);
    CHECK(ut::actor::Location(lightCopy).y == -32.0f);
    CHECK(ut::actor::Location(lightCopy).z == 48.0f);
    CHECK(ut::actor::Level(lightCopy) == info);
    CHECK(ut::brush::CsgOper(brushCopy) == ut::csg::CsgSubtract);
    ut::UModel* model = ut::cast<ut::UModel>(ut::actor::Brush(brushCopy));
    REQUIRE(model != nullptr);
    CHECK(model != source);
    REQUIRE(model->polys != nullptr);
    REQUIRE(model->polys->polys.size() == 6);
    for (size_t i = 0; i < 6; i++) {
        const ut::Poly& a = source->polys->polys[i];
        const ut::Poly& b = model->polys->polys[i];
        REQUIRE(a.vertices.size() == b.vertices.size());
        for (size_t v = 0; v < a.vertices.size(); v++) {
            CHECK(a.vertices[v].x == b.vertices[v].x);
            CHECK(a.vertices[v].y == b.vertices[v].y);
            CHECK(a.vertices[v].z == b.vertices[v].z);
        }
    }
    CHECK(model->bounds.max.x == 64.0f);
}

TEST_CASE("pasted actor references point at pasted copies or at the existing actors", "[data]")
{
    ut::ULevel* level = emptyLevel("T3DReferenceTest");
    spawn("Engine.LevelInfo", level, {});
    ut::UObject* owner = spawn("Engine.Light", level, { 0.0f, 0.0f, 0.0f });
    ut::UObject* owned = spawn("Engine.Light", level, { 32.0f, 0.0f, 0.0f });
    ut::actor::Owner(owned) = owner;
    std::vector<ut::UObject*> both = ut::t3d::importActors(level, ut::t3d::exportActors({ owner, owned }), false);
    REQUIRE(both.size() == 2);
    CHECK(ut::actor::Owner(both[1]) == both[0]);
    std::vector<ut::UObject*> single = ut::t3d::importActors(level, ut::t3d::exportActors({ owned }), false);
    REQUIRE(single.size() == 1);
    CHECK(ut::actor::Owner(single[0]) == owner);
}

TEST_CASE("selection helpers pick actors by class, box, inversion and hidden state", "[data]")
{
    ut::ULevel* level = emptyLevel("SelectionHelpersTest");
    ut::UObject* builder = brushActor(level, { 0.0f, 0.0f, 0.0f });
    ut::UObject* a = spawn("Engine.Light", level, { 0.0f, 0.0f, 0.0f });
    ut::UObject* b = spawn("Engine.Light", level, { 100.0f, 50.0f, 500.0f });
    ut::UObject* c = spawn("Engine.PlayerStart", level, { 300.0f, 0.0f, 0.0f });
    ut::UObject* d = spawn("Engine.Light", level, { -200.0f, 0.0f, 0.0f });
    ut::actor::bHiddenEd(d) = 1;
    std::vector<ut::UObject*> actors = { builder, a, b, c, d };
    CHECK(ut::actorselect::ofClass(actors, a->cls) == std::vector<ut::UObject*> { a, b });
    CHECK(ut::actorselect::visible(actors) == std::vector<ut::UObject*> { builder, a, b, c });
    CHECK(ut::actorselect::inBox(actors, 0, 1, 150.0f, 60.0f, 50.0f, -10.0f) == std::vector<ut::UObject*> { b });
    CHECK(ut::actorselect::inBox(actors, 0, 2, -300.0f, -10.0f, 400.0f, 10.0f) == std::vector<ut::UObject*> { builder, a, c });
    CHECK(ut::actorselect::inverted(actors, { a }, builder) == std::vector<ut::UObject*> { b, c });
    CHECK(ut::actorselect::toUnhide(actors) == std::vector<ut::UObject*> { d });
    CHECK(ut::actorselect::commonClass({ a, b }) == a->cls);
    CHECK(ut::actorselect::commonClass({ a, c })->name == ut::Name("Actor"));
    CHECK(ut::actorselect::commonClass({ c, b, builder })->name == ut::Name("Actor"));
}

TEST_CASE("editing a property on several actors shows the common value and undoes in one step", "[data]")
{
    ut::ULevel* level = emptyLevel("MultiEditTest");
    ut::UObject* a = spawn("Engine.Light", level, {});
    ut::UObject* b = spawn("Engine.Light", level, {});
    ut::actor::LightBrightness(a) = 10;
    ut::actor::LightBrightness(b) = 20;
    ut::UProperty* property = a->cls->findProperty(ut::Name("LightBrightness"));
    REQUIRE(property != nullptr);
    std::string text = "unset";
    CHECK_FALSE(ut::actorselect::commonText({ a, b }, property, 0, text));
    CHECK(text.empty());
    ut::EditorHistory history;
    history.begin(level, { a, b }, "Set LightBrightness");
    CHECK(ut::actorselect::setText({ a, b }, property, 0, "77"));
    history.commit();
    CHECK(ut::actor::LightBrightness(a) == 77);
    CHECK(ut::actor::LightBrightness(b) == 77);
    CHECK(ut::actorselect::commonText({ a, b }, property, 0, text));
    CHECK(text == "77");
    REQUIRE(history.undo());
    CHECK(ut::actor::LightBrightness(a) == 10);
    CHECK(ut::actor::LightBrightness(b) == 20);
    CHECK(history.undoCount() == 0);
}

TEST_CASE("z clipping keeps the part of a segment inside the slab")
{
    ut::Vector a { 0.0f, 0.0f, -100.0f };
    ut::Vector b { 100.0f, 0.0f, 100.0f };
    REQUIRE(ut::actorselect::clipZ(a, b, 0.0f, 50.0f));
    CHECK(a.x == 50.0f);
    CHECK(a.z == 0.0f);
    CHECK(b.x == 75.0f);
    CHECK(b.z == 50.0f);
    ut::Vector c { 0.0f, 0.0f, 60.0f };
    ut::Vector d { 10.0f, 0.0f, 90.0f };
    CHECK_FALSE(ut::actorselect::clipZ(c, d, 0.0f, 50.0f));
    ut::Vector e { 0.0f, 5.0f, 10.0f };
    ut::Vector f { 0.0f, 9.0f, 10.0f };
    REQUIRE(ut::actorselect::clipZ(e, f, 0.0f, 50.0f));
    CHECK(e.y == 5.0f);
    CHECK(f.y == 9.0f);
}

TEST_CASE("gizmo rotation axes convert back to the actor rotator")
{
    const ut::Rotator rotations[] = { { 0, 0, 0 }, { 4096, 8192, 0 }, { -3000, 30000, 2000 }, { 0, -16384, -8000 }, { 12000, 100, 30000 } };
    for (const ut::Rotator& rotation : rotations) {
        ut::Rotator back = ut::actorselect::rotatorFromAxes(ut::rotationAxes(rotation));
        CHECK(std::abs(back.pitch - rotation.pitch) <= 1);
        CHECK(std::abs(back.yaw - rotation.yaw) <= 1);
        CHECK(std::abs(back.roll - rotation.roll) <= 1);
    }
    CHECK(ut::actorselect::rotationDelta({ 0, 65000, 0 }, { 0, 100, 0 }) == ut::Rotator { 0, 636, 0 });
    CHECK(ut::actorselect::rotationDelta({ 0, 0, 0 }, { 0, 40000, 0 }) == ut::Rotator { 0, -25536, 0 });
    CHECK(ut::actorselect::rotationDelta({ 100, 0, -50 }, { 300, 0, 50 }) == ut::Rotator { 200, 0, 100 });
}

TEST_CASE("a whole level exports to T3D and imports into a new level with the same actors", "[data]")
{
    ut::engine::initialize(OPENUT_GAME_DATA);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    ut::objects::setEditorMode(true);
    ut::Linker* linker = ut::objects::loadPackage(ut::Name("DM-Deck16]["));
    REQUIRE(linker != nullptr);
    ut::ULevel* source = ut::cast<ut::ULevel>(linker->object(linker->findExport(ut::Name("MyLevel"), ut::Name("Level"), "*")));
    ut::objects::ensureLoaded(source);
    ut::objects::flushLoads();
    std::vector<ut::UObject*> originals;
    for (ut::UObject* actor : source->actors) {
        if (actor) {
            ut::objects::ensureLoaded(actor);
            originals.push_back(actor);
        }
    }
    ut::objects::flushLoads();
    std::string text = ut::t3d::exportActors(originals);
    CHECK(text.find("\n    Name=") == std::string::npos);
    ut::ULevel* target = emptyLevel("T3DLevelImport");
    std::vector<ut::UObject*> imported = ut::t3d::importActors(target, text, true);
    REQUIRE(imported.size() == originals.size());
    REQUIRE(target->actors.size() == originals.size());
    std::unordered_map<ut::UObject*, ut::UObject*> copies;
    for (size_t i = 0; i < originals.size(); i++) {
        copies[originals[i]] = target->actors[i];
    }
    CHECK(target->actors[0]->cls->name == ut::Name("LevelInfo"));
    CHECK(target->actors[1]->cls->name == ut::Name("Brush"));
    size_t brushes = 0;
    size_t mismatches = 0;
    std::string report;
    for (size_t i = 0; i < originals.size(); i++) {
        ut::UObject* original = originals[i];
        ut::UObject* copy = target->actors[i];
        REQUIRE(copy->cls == original->cls);
        for (ut::UProperty* property : original->cls->fieldOrder()) {
            if (property->name == ut::Name("Name") || property->name == ut::Name("Level") || property->name == ut::Name("XLevel") || property->name == ut::Name("Region") || property->name == ut::Name("Brush")) {
                continue;
            }
            if ((property->propertyFlags & (ut::CPF_Transient | ut::CPF_Native)) && property->category.none()) {
                continue;
            }
            if (property->type == ut::PropType::Array || property->type == ut::PropType::Map || property->type == ut::PropType::FixedArray || property->type == ut::PropType::Pointer) {
                continue;
            }
            for (int32_t index = 0; index < property->arrayDim; index++) {
                size_t at = property->offset + static_cast<size_t>(index) * property->elementSize();
                if (property->type == ut::PropType::Object) {
                    ut::UObject* before = *reinterpret_cast<ut::UObject**>(original->data + at);
                    ut::UObject* after = *reinterpret_cast<ut::UObject**>(copy->data + at);
                    auto mapped = copies.find(before);
                    ut::UObject* expected = mapped != copies.end() ? mapped->second : before;
                    if (expected != after) {
                        mismatches++;
                        report += original->name.str() + "." + property->name.str() + "\n";
                    }
                    continue;
                }
                if (property->toText(original->data + at) != property->toText(copy->data + at)) {
                    mismatches++;
                    report += original->name.str() + "." + property->name.str() + " " + property->toText(original->data + at) + " vs " + property->toText(copy->data + at) + "\n";
                }
            }
        }
        ut::UModel* originalBrush = ut::cast<ut::UModel>(ut::actor::Brush(original));
        if (originalBrush && originalBrush->polys) {
            ut::UModel* copyBrush = ut::cast<ut::UModel>(ut::actor::Brush(copy));
            REQUIRE(copyBrush != nullptr);
            REQUIRE(copyBrush->polys != nullptr);
            CHECK(copyBrush->polys->polys.size() == originalBrush->polys->polys.size());
            brushes++;
        }
    }
    INFO(report.substr(0, 4000));
    CHECK(mismatches == 0);
    CHECK(brushes > 10);
    ut::objects::unloadPackage(ut::Name("DM-Deck16]["));
    ut::objects::setEditorMode(false);
}
