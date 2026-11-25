#include "core/objects.h"
#include "core/vm.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <string>

namespace {
    void initEngine()
    {
        static bool initialized = false;
        if (!initialized) {
            ut::engine::initialize(OPENUT_GAME_DATA);
            initialized = true;
        }
    }

    ut::UObject* coreSelf()
    {
        return ut::objects::objectClass()->defaultObject;
    }

    ut::UFunction* coreFunction(const char* name)
    {
        ut::UFunction* function = ut::objects::objectClass()->findFunction(ut::Name(name));
        REQUIRE(function != nullptr);
        return function;
    }

    template <typename T>
    T result(ut::vm::Call& call)
    {
        return *reinterpret_cast<T*>(call.returnValue());
    }

    ut::UClass* engineClass(const char* path)
    {
        ut::UClass* cls = ut::objects::findClass(path);
        ut::objects::flushLoads();
        REQUIRE(cls != nullptr);
        return cls;
    }

    template <typename T>
    T& property(ut::UObject* object, const char* name)
    {
        uint8_t* address = object->propertyAddress(ut::Name(name));
        REQUIRE(address != nullptr);
        return *reinterpret_cast<T*>(address);
    }
}

TEST_CASE("integer operator natives", "[data]")
{
    initEngine();
    {
        ut::vm::Call call(coreSelf(), coreFunction("Add_IntInt"));
        call.i(2).i(3).run();
        CHECK(result<int32_t>(call) == 5);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Subtract_IntInt"));
        call.i(2).i(7).run();
        CHECK(result<int32_t>(call) == -5);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Divide_IntInt"));
        call.i(7).i(2).run();
        CHECK(result<int32_t>(call) == 3);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("GreaterGreaterGreater_IntInt"));
        call.i(-8).i(1).run();
        CHECK(result<int32_t>(call) == 2147483644);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("GreaterGreater_IntInt"));
        call.i(-8).i(1).run();
        CHECK(result<int32_t>(call) == -4);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Clamp"));
        call.i(15).i(0).i(10).run();
        CHECK(result<int32_t>(call) == 10);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("AddEqual_IntInt"));
        call.i(5).i(3).run();
        CHECK(result<int32_t>(call) == 8);
        CHECK(*reinterpret_cast<int32_t*>(call.parameter(0)) == 8);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("AddAdd_Int"));
        call.i(5).run();
        CHECK(result<int32_t>(call) == 5);
        CHECK(*reinterpret_cast<int32_t*>(call.parameter(0)) == 6);
    }
}

TEST_CASE("float and boolean operator natives", "[data]")
{
    initEngine();
    {
        ut::vm::Call call(coreSelf(), coreFunction("Percent_FloatFloat"));
        call.f(7.5f).f(2.0f).run();
        CHECK(result<float>(call) == 1.5f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Lerp"));
        call.f(0.25f).f(10.0f).f(20.0f).run();
        CHECK(result<float>(call) == 12.5f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Smerp"));
        call.f(0.5f).f(0.0f).f(10.0f).run();
        CHECK(result<float>(call) == 5.0f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("AndAnd_BoolBool"));
        call.b(true).b(false).run();
        CHECK(result<uint32_t>(call) == 0);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("OrOr_BoolBool"));
        call.b(false).b(true).run();
        CHECK(result<uint32_t>(call) == 1);
    }
}

TEST_CASE("string natives", "[data]")
{
    initEngine();
    {
        ut::vm::Call call(coreSelf(), coreFunction("Concat_StrStr"));
        call.s("ab").s("cd").run();
        CHECK(result<std::string>(call) == "abcd");
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("At_StrStr"));
        call.s("ab").s("cd").run();
        CHECK(result<std::string>(call) == "ab cd");
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Mid"));
        call.s("abcdef").i(2).i(3).run();
        CHECK(result<std::string>(call) == "cde");
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Left"));
        call.s("abcdef").i(2).run();
        CHECK(result<std::string>(call) == "ab");
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Right"));
        call.s("abcdef").i(2).run();
        CHECK(result<std::string>(call) == "ef");
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("InStr"));
        call.s("abcdef").s("cd").run();
        CHECK(result<int32_t>(call) == 2);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("InStr"));
        call.s("abcdef").s("zz").run();
        CHECK(result<int32_t>(call) == -1);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Caps"));
        call.s("aBc").run();
        CHECK(result<std::string>(call) == "ABC");
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Chr"));
        call.i(65).run();
        CHECK(result<std::string>(call) == "A");
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Asc"));
        call.s("A").run();
        CHECK(result<int32_t>(call) == 65);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("ComplementEqual_StrStr"));
        call.s("ABC").s("abc").run();
        CHECK(result<uint32_t>(call) == 1);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("EqualEqual_StrStr"));
        call.s("ABC").s("abc").run();
        CHECK(result<uint32_t>(call) == 0);
    }
}

TEST_CASE("vector and rotator natives", "[data]")
{
    initEngine();
    {
        ut::vm::Call call(coreSelf(), coreFunction("Dot_VectorVector"));
        call.v({ 1, 2, 3 }).v({ 4, 5, 6 }).run();
        CHECK(result<float>(call) == 32.0f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Cross_VectorVector"));
        call.v({ 1, 0, 0 }).v({ 0, 1, 0 }).run();
        ut::Vector value = result<ut::Vector>(call);
        CHECK(value.x == 0.0f);
        CHECK(value.y == 0.0f);
        CHECK(value.z == 1.0f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("VSize"));
        call.v({ 3, 4, 0 }).run();
        CHECK(result<float>(call) == 5.0f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("MirrorVectorByNormal"));
        call.v({ 1, -1, 0 }).v({ 0, 1, 0 }).run();
        ut::Vector value = result<ut::Vector>(call);
        CHECK(value.x == 1.0f);
        CHECK(value.y == 1.0f);
        CHECK(value.z == 0.0f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("GreaterGreater_VectorRotator"));
        call.v({ 1, 0, 0 }).r({ 0, 16384, 0 }).run();
        ut::Vector value = result<ut::Vector>(call);
        CHECK(std::fabs(value.x) < 1e-4f);
        CHECK(std::fabs(value.y - 1.0f) < 1e-4f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("LessLess_VectorRotator"));
        call.v({ 0, 1, 0 }).r({ 0, 16384, 0 }).run();
        ut::Vector value = result<ut::Vector>(call);
        CHECK(std::fabs(value.x - 1.0f) < 1e-4f);
        CHECK(std::fabs(value.y) < 1e-4f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("GetAxes"));
        call.r({ 0, 16384, 0 }).run();
        ut::Vector x = *reinterpret_cast<ut::Vector*>(call.parameter(1));
        ut::Vector y = *reinterpret_cast<ut::Vector*>(call.parameter(2));
        ut::Vector z = *reinterpret_cast<ut::Vector*>(call.parameter(3));
        CHECK(std::fabs(x.y - 1.0f) < 1e-4f);
        CHECK(std::fabs(y.x + 1.0f) < 1e-4f);
        CHECK(std::fabs(z.z - 1.0f) < 1e-4f);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Normalize"));
        call.r({ 0, 40000, -40000 }).run();
        ut::Rotator value = result<ut::Rotator>(call);
        CHECK(value.pitch == 0);
        CHECK(value.yaw == -25536);
        CHECK(value.roll == 25536);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("Multiply_RotatorFloat"));
        call.r({ 100, 200, 300 }).f(0.5f).run();
        ut::Rotator value = result<ut::Rotator>(call);
        CHECK(value.pitch == 50);
        CHECK(value.yaw == 100);
        CHECK(value.roll == 150);
    }
}

TEST_CASE("object natives on class objects", "[data]")
{
    initEngine();
    ut::UClass* actor = engineClass("Engine.Actor");
    ut::UClass* pawn = engineClass("Engine.Pawn");
    {
        ut::vm::Call call(coreSelf(), coreFunction("ClassIsChildOf"));
        call.obj(pawn).obj(actor).run();
        CHECK(result<uint32_t>(call) == 1);
    }
    {
        ut::vm::Call call(coreSelf(), coreFunction("ClassIsChildOf"));
        call.obj(actor).obj(pawn).run();
        CHECK(result<uint32_t>(call) == 0);
    }
    {
        ut::vm::Call call(pawn->defaultObject, coreFunction("IsA"));
        call.n(ut::Name("Actor")).run();
        CHECK(result<uint32_t>(call) == 1);
    }
    {
        ut::vm::Call call(pawn->defaultObject, coreFunction("IsA"));
        call.n(ut::Name("Texture")).run();
        CHECK(result<uint32_t>(call) == 0);
    }
}

TEST_CASE("Inventory ArmorPriority short circuits name comparisons", "[data]")
{
    initEngine();
    ut::UClass* inventory = engineClass("Engine.Inventory");
    ut::UObject* item = ut::objects::newObject(inventory, nullptr, ut::Name(), 0);
    property<int32_t>(item, "AbsorptionPriority") = 7;
    property<ut::Name>(item, "ProtectionType2") = ut::Name("Shot");
    ut::UFunction* function = inventory->findFunction(ut::Name("ArmorPriority"));
    {
        ut::vm::Call call(item, function);
        call.n(ut::Name("Drowned")).run();
        CHECK(result<int32_t>(call) == 0);
    }
    {
        ut::vm::Call call(item, function);
        call.n(ut::Name("Shot")).run();
        CHECK(result<int32_t>(call) == 1000000);
    }
    {
        ut::vm::Call call(item, function);
        call.n(ut::Name("None")).run();
        CHECK(result<int32_t>(call) == 7);
    }
    {
        ut::vm::Call call(item, function);
        call.n(ut::Name("Burned")).run();
        CHECK(result<int32_t>(call) == 7);
    }
}

TEST_CASE("TestInfo self tests pass in the interpreter", "[data]")
{
    initEngine();
    ut::UClass* testInfo = engineClass("Engine.TestInfo");
    ut::UObject* object = ut::objects::newObject(testInfo, nullptr, ut::Name(), 0);
    REQUIRE(property<int32_t>(object, "xnum") == 777);

    std::vector<std::string> criticals;
    ut::vm::CriticalHandler previous = ut::vm::setCriticalHandler([&criticals](const std::string& message) {
        criticals.push_back(message);
    });
    uint32_t before = ut::vm::assertionFailures();
    {
        ut::vm::Call call(object, testInfo->findFunction(ut::Name("BeginPlay")));
        call.run();
    }
    {
        ut::vm::Call call(object, testInfo->findFunction(ut::Name("PostBeginPlay")));
        call.run();
    }
    {
        ut::vm::Call call(object, testInfo->findFunction(ut::Name("Tick")));
        call.f(0.1f).run();
    }
    CHECK(ut::vm::assertionFailures() == before);
    CHECK(property<int32_t>(object, "xnum") == 999);
    CHECK(property<uint32_t>(object, "bBool1") == 1);
    ut::Vector v1 = property<ut::Vector>(testInfo->defaultObject, "v1");
    CHECK(v1.x == 1.0f);
    CHECK(v1.y == 2.0f);
    CHECK(v1.z == 3.0f);

    {
        ut::vm::Call call(object, testInfo->findFunction(ut::Name("TestStatic")));
        call.i(5).run();
    }
    CHECK(ut::vm::assertionFailures() == before + 2);
    ut::vm::setCriticalHandler(previous);
    REQUIRE(criticals.size() == 2);
    CHECK(criticals[0].find("Assertion failed, line ") != std::string::npos);
    CHECK(criticals[0].rfind("TestInfo ", 0) == 0);
}

TEST_CASE("GotoState and IsInState follow state inheritance", "[data]")
{
    initEngine();
    ut::UClass* testInfo = engineClass("Engine.TestInfo");
    ut::UObject* object = ut::objects::newObject(testInfo, nullptr, ut::Name(), 0);
    {
        ut::vm::Call call(object, coreFunction("GotoState"));
        call.n(ut::Name("EEDDAA")).run();
    }
    CHECK(ut::vm::stateName(object) == ut::Name("EEDDAA"));
    {
        ut::vm::Call call(object, coreFunction("IsInState"));
        call.n(ut::Name("AA")).run();
        CHECK(result<uint32_t>(call) == 1);
    }
    {
        ut::vm::Call call(object, coreFunction("IsInState"));
        call.n(ut::Name("BB")).run();
        CHECK(result<uint32_t>(call) == 0);
    }
    {
        ut::vm::Call call(object, coreFunction("GetStateName"));
        call.run();
        CHECK(result<ut::Name>(call) == ut::Name("EEDDAA"));
    }
}

TEST_CASE("GetPropertyText reads string property values", "[data]")
{
    initEngine();
    ut::UClass* testInfo = engineClass("Engine.TestInfo");
    ut::UObject* object = ut::objects::newObject(testInfo, nullptr, ut::Name(), 0);
    {
        ut::vm::Call call(object, coreFunction("GetPropertyText"));
        call.s("sxx").run();
        CHECK(result<std::string>(call) == "Tim");
    }
}

TEST_CASE("GameInfo option parsing script runs in the interpreter", "[data]")
{
    initEngine();
    ut::UClass* gameInfo = engineClass("Engine.GameInfo");
    ut::UObject* self = gameInfo->defaultObject;
    {
        ut::vm::Call call(self, gameInfo->findFunction(ut::Name("ParseOption")));
        REQUIRE(call.valid());
        call.s("?Name=Player?Class=Botpack.TMale2").s("class").run();
        CHECK(result<std::string>(call) == "Botpack.TMale2");
    }
    {
        ut::vm::Call call(self, gameInfo->findFunction(ut::Name("HasOption")));
        call.s("?Name=Player?Class=X").s("name").run();
        CHECK(result<uint32_t>(call) == 1);
    }
    {
        ut::vm::Call call(self, gameInfo->findFunction(ut::Name("HasOption")));
        call.s("?a=b").s("zz").run();
        CHECK(result<uint32_t>(call) == 0);
    }
    {
        ut::vm::Call call(self, gameInfo->findFunction(ut::Name("GetIntOption")));
        call.s("?Difficulty=3").s("Difficulty").i(1).run();
        CHECK(result<int32_t>(call) == 3);
    }
    {
        ut::vm::Call call(self, gameInfo->findFunction(ut::Name("GetIntOption")));
        call.s("").s("Difficulty").i(1).run();
        CHECK(result<int32_t>(call) == 1);
    }
    {
        ut::vm::Call call(self, gameInfo->findFunction(ut::Name("GrabOption")));
        call.s("?A=1?B=2").s("").run();
        CHECK(result<uint32_t>(call) == 1);
        CHECK(*reinterpret_cast<std::string*>(call.parameter(0)) == "?B=2");
        CHECK(*reinterpret_cast<std::string*>(call.parameter(1)) == "A=1");
    }
}

namespace {
    void inheritedProbe(ut::UObject*, ut::Frame&, void*)
    {
    }

    void exactProbe(ut::UObject*, ut::Frame&, void*)
    {
    }
}

TEST_CASE("an inherited override reaches every subclass, and an exact override on a class wins only there", "[data]")
{
    initEngine();
    ut::vm::registerInheritedOverride("ScoreBoard", "InheritedOverrideProbe", inheritedProbe);
    ut::vm::registerOverride("TeamScoreBoard", "InheritedOverrideProbe", exactProbe);
    ut::Name probe("InheritedOverrideProbe");
    CHECK(ut::vm::findOverride(engineClass("Engine.ScoreBoard"), probe) == inheritedProbe);
    CHECK(ut::vm::findOverride(engineClass("Botpack.TournamentScoreBoard"), probe) == inheritedProbe);
    CHECK(ut::vm::findOverride(engineClass("Botpack.TeamScoreBoard"), probe) == exactProbe);
    CHECK(ut::vm::findOverride(engineClass("Botpack.AssaultScoreboard"), probe) == inheritedProbe);
    CHECK(ut::vm::findOverride(engineClass("Engine.Info"), probe) == nullptr);
    CHECK(ut::vm::findOverride(engineClass("Botpack.TournamentScoreBoard"), ut::Name("InheritedOverrideOther")) == nullptr);
}
