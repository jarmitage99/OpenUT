#include "core/config.h"
#include "core/objects.h"
#include "core/url.h"
#include "core/vm.h"
#include "engine/world.h"

#include <catch_amalgamated.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
    void setEnvironment(const char* name, const char* value)
    {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        if (*value) {
            setenv(name, value, 1);
        } else {
            unsetenv(name);
        }
#endif
    }

    void initEngine()
    {
        static bool initialized = false;
        if (!initialized) {
            ut::engine::initialize(OPENUT_GAME_DATA);
            initialized = true;
        }
    }

    ut::UClass* testInfoClass()
    {
        ut::UClass* cls = ut::objects::findClass("Engine.TestInfo");
        ut::objects::flushLoads();
        REQUIRE(cls != nullptr);
        return cls;
    }

    ut::UFunction* coreFunction(const char* name)
    {
        ut::UFunction* function = ut::objects::objectClass()->findFunction(ut::Name(name));
        REQUIRE(function != nullptr);
        return function;
    }

    struct Bytes {
        std::vector<uint8_t> data;

        Bytes& op(std::initializer_list<uint8_t> values)
        {
            data.insert(data.end(), values);
            return *this;
        }

        Bytes& u16(uint16_t value)
        {
            data.push_back(static_cast<uint8_t>(value & 0xff));
            data.push_back(static_cast<uint8_t>(value >> 8));
            return *this;
        }

        Bytes& u32(uint32_t value)
        {
            for (int i = 0; i < 4; i++) {
                data.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
            return *this;
        }

        Bytes& f32(float value)
        {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            return u32(bits);
        }

        Bytes& str(const char* text)
        {
            data.insert(data.end(), text, text + std::strlen(text) + 1);
            return *this;
        }

        Bytes& object(const ut::UObject* value)
        {
            return u32(value->handle);
        }

        Bytes& name(const char* text)
        {
            return u32(ut::Name(text).index());
        }
    };

    ut::UFunction* makeFunction(ut::UClass* owner, const char* name, const Bytes& code, uint32_t flags = 0, ut::UField* params = nullptr)
    {
        ut::UFunction* function = new ut::UFunction();
        function->name = ut::Name(name);
        function->outer = owner;
        function->functionFlags = flags;
        function->children = params;
        function->script = code.data;
        ut::objects::registerObject(function);
        function->link();
        return function;
    }

    ut::UProperty* makeParam(const char* name, uint32_t flags)
    {
        ut::UProperty* property = new ut::UProperty(ut::PropType::Int);
        property->name = ut::Name(name);
        property->propertyFlags = flags;
        ut::objects::registerObject(property);
        return property;
    }

    ut::UState* makeState(ut::UClass* owner, const char* name, const Bytes& code, uint16_t labelTable)
    {
        ut::UState* state = new ut::UState();
        state->name = ut::Name(name);
        state->outer = owner;
        state->script = code.data;
        state->labelTableOffset = labelTable;
        state->ignoreMask = ~0ull;
        ut::objects::registerObject(state);
        owner->states[state->name] = state;
        return state;
    }

    int32_t& xnum(ut::UObject* object)
    {
        uint8_t* address = object->propertyAddress(ut::Name("xnum"));
        REQUIRE(address != nullptr);
        return *reinterpret_cast<int32_t*>(address);
    }

    ut::UProperty* xnumProperty(ut::UClass* cls)
    {
        ut::UProperty* property = cls->findProperty(ut::Name("xnum"));
        REQUIRE(property != nullptr);
        return property;
    }

    Bytes incrementXnum(ut::UClass* cls)
    {
        Bytes code;
        code.op({ 0xA3, 0x01 }).object(xnumProperty(cls)).op({ 0x16 });
        return code;
    }

    template <typename T>
    T evaluate(ut::UObject* self, const Bytes& code)
    {
        ut::UStruct node;
        node.script = code.data;
        ut::Frame frame;
        frame.object = self;
        frame.node = &node;
        frame.code = node.script.data();
        T value {};
        frame.step(self, &value);
        return value;
    }
}

TEST_CASE("vector to string conversion prints six decimals like the original", "[data]")
{
    initEngine();
    Bytes code;
    code.op({ 0x58, 0x23 }).f32(1.5f).f32(-2.0f).f32(0.25f);
    CHECK(evaluate<std::string>(ut::objects::objectClass()->defaultObject, code) == "1.500000,-2.000000,0.250000");
}

TEST_CASE("string switch matches case labels case insensitively", "[data]")
{
    initEngine();
    Bytes code;
    code.op({ 0x05, 0x00, 0x1F }).str("abc");
    code.op({ 0x0A }).u16(20).op({ 0x1F }).str("ABC");
    code.op({ 0x0B, 0x0B, 0x0B, 0x0B, 0x0B });
    code.op({ 0x0A }).u16(0xffff);
    ut::UStruct node;
    node.script = code.data;
    ut::Frame frame;
    frame.object = ut::objects::objectClass()->defaultObject;
    frame.node = &node;
    frame.code = node.script.data();
    frame.step(frame.object, nullptr);
    CHECK(frame.code - node.script.data() == 15);
}

TEST_CASE("Mid clamps the start index as unsigned like FString Mid", "[data]")
{
    initEngine();
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Mid"));
        call.s("abcdef").i(-1).i(3).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "");
    }
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Mid"));
        call.s("abcdef").i(4).i(10).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "ef");
    }
}

TEST_CASE("MirrorVectorByNormal normalizes the normal first", "[data]")
{
    initEngine();
    ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("MirrorVectorByNormal"));
    call.v({ 1, -1, 0 }).v({ 0, 2, 0 }).run();
    ut::Vector value = *reinterpret_cast<ut::Vector*>(call.returnValue());
    CHECK(value.x == 1.0f);
    CHECK(value.y == 1.0f);
    CHECK(value.z == 0.0f);
}

TEST_CASE("script calls to a disabled probe function are skipped", "[data]")
{
    initEngine();
    ut::UClass* cls = testInfoClass();
    ut::UObject* object = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    Bytes timerCode = incrementXnum(cls);
    timerCode.op({ 0x04, 0x0B });
    ut::UFunction* timer = makeFunction(cls, "Timer", timerCode);
    Bytes callerCode;
    callerCode.op({ 0x1C }).object(timer).op({ 0x16, 0x04, 0x0B });
    ut::UFunction* caller = makeFunction(cls, "CoreProbeCaller", callerCode);
    xnum(object) = 10;
    ut::vm::disable(object, ut::Name("Timer"));
    {
        ut::vm::Call call(object, caller);
        call.run();
    }
    CHECK(xnum(object) == 10);
    {
        ut::vm::Call call(object, timer);
        call.run();
    }
    CHECK(xnum(object) == 10);
    ut::vm::enable(object, ut::Name("Timer"));
    {
        ut::vm::Call call(object, caller);
        call.run();
    }
    CHECK(xnum(object) == 11);
}

TEST_CASE("singular functions do not reenter on the same object", "[data]")
{
    initEngine();
    ut::UClass* cls = testInfoClass();
    ut::UObject* object = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    ut::UFunction* recurse = makeFunction(cls, "CoreSingular", Bytes(), ut::FUNC_Singular);
    Bytes code = incrementXnum(cls);
    code.op({ 0x1C }).object(recurse).op({ 0x16, 0x04, 0x0B });
    recurse->script = code.data;
    std::vector<std::string> criticals;
    ut::vm::CriticalHandler previous = ut::vm::setCriticalHandler([&criticals](const std::string& message) {
        criticals.push_back(message);
    });
    xnum(object) = 0;
    {
        ut::vm::Call call(object, recurse);
        call.run();
    }
    CHECK(xnum(object) == 1);
    CHECK(criticals.empty());
    {
        ut::vm::Call call(object, recurse);
        call.run();
    }
    CHECK(xnum(object) == 2);
    ut::vm::setCriticalHandler(previous);
}

TEST_CASE("out parameters are copied back in reverse order", "[data]")
{
    initEngine();
    ut::UClass* cls = testInfoClass();
    ut::UObject* object = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    ut::UProperty* a = makeParam("A", ut::CPF_Parm | ut::CPF_OutParm);
    ut::UProperty* b = makeParam("B", ut::CPF_Parm | ut::CPF_OutParm);
    a->next = b;
    Bytes code;
    code.op({ 0x0F, 0x00 }).object(a).op({ 0x26 });
    code.op({ 0x0F, 0x00 }).object(b).op({ 0x2C, 0x02 });
    code.op({ 0x04, 0x0B });
    ut::UFunction* setBoth = makeFunction(cls, "CoreSetBoth", code, 0, a);
    Bytes callerCode;
    callerCode.op({ 0x1C }).object(setBoth).op({ 0x01 }).object(xnumProperty(cls)).op({ 0x01 }).object(xnumProperty(cls)).op({ 0x16, 0x04, 0x0B });
    ut::UFunction* caller = makeFunction(cls, "CoreSetBothCaller", callerCode);
    xnum(object) = 0;
    ut::vm::Call call(object, caller);
    call.run();
    CHECK(xnum(object) == 1);
}

TEST_CASE("GotoState from EndState preempts the outer GotoState", "[data]")
{
    initEngine();
    ut::UClass* cls = testInfoClass();
    ut::UObject* object = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    Bytes begin;
    begin.op({ 0x08 }).name("Begin").u32(0).u32(0).u32(0);
    ut::UState* first = makeState(cls, "CoreFirst", begin, 1);
    makeState(cls, "CoreSecond", begin, 1);
    makeState(cls, "CoreThird", begin, 1);
    Bytes endCode;
    endCode.op({ 0x71, 0x21 }).name("CoreSecond").op({ 0x16, 0x04, 0x0B });
    first->functions[ut::Name("EndState")] = makeFunction(cls, "EndState", endCode);
    std::vector<std::string> criticals;
    ut::vm::CriticalHandler previous = ut::vm::setCriticalHandler([&criticals](const std::string& message) {
        criticals.push_back(message);
    });
    REQUIRE(ut::vm::gotoState(object, ut::Name("CoreFirst")));
    ut::vm::gotoState(object, ut::Name("CoreThird"));
    ut::vm::setCriticalHandler(previous);
    CHECK(criticals.empty());
    CHECK(ut::vm::stateName(object) == ut::Name("CoreSecond"));
}

TEST_CASE("state code stops after four state changes in one update", "[data]")
{
    initEngine();
    ut::UClass* cls = testInfoClass();
    ut::UObject* object = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    Bytes ping;
    ping.op({ 0x71, 0x21 }).name("CorePong").op({ 0x16 }).name("Begin").u32(0).u32(0).u32(0);
    Bytes pong;
    pong.op({ 0x71, 0x21 }).name("CorePing").op({ 0x16 }).name("Begin").u32(0).u32(0).u32(0);
    ut::UState* pingState = makeState(cls, "CorePing", ping, 7);
    ut::UState* pongState = makeState(cls, "CorePong", pong, 7);
    Bytes count = incrementXnum(cls);
    count.op({ 0x04, 0x0B });
    ut::UFunction* beginState = makeFunction(cls, "BeginState", count);
    pingState->functions[ut::Name("BeginState")] = beginState;
    pongState->functions[ut::Name("BeginState")] = beginState;
    REQUIRE(ut::vm::gotoState(object, ut::Name("CorePing")));
    xnum(object) = 0;
    ut::vm::runStateCode(object);
    CHECK(xnum(object) == 5);
    CHECK(ut::vm::stateName(object) == ut::Name("CorePong"));
    ut::vm::runStateCode(object);
    CHECK(xnum(object) == 10);
    CHECK(ut::vm::stateName(object) == ut::Name("CorePing"));
}

TEST_CASE("Normal returns zero below the SafeNormal size squared threshold", "[data]")
{
    initEngine();
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Normal"));
        call.v({ 0.00005f, 0, 0 }).run();
        ut::Vector value = *reinterpret_cast<ut::Vector*>(call.returnValue());
        CHECK(value.x == 0.0f);
        CHECK(value.y == 0.0f);
        CHECK(value.z == 0.0f);
    }
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Normal"));
        call.v({ 0, 0.5f, 0 }).run();
        ut::Vector value = *reinterpret_cast<ut::Vector*>(call.returnValue());
        CHECK(value.x == 0.0f);
        CHECK(value.y == 1.0f);
        CHECK(value.z == 0.0f);
    }
}

TEST_CASE("Chr keeps sixteen bits of the code and Caps only uppercases ASCII", "[data]")
{
    initEngine();
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Chr"));
        call.i(321).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "?");
    }
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Chr"));
        call.i(65536).run();
        CHECK(reinterpret_cast<std::string*>(call.returnValue())->empty());
    }
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Chr"));
        call.i(65601).run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "A");
    }
    {
        ut::vm::Call call(ut::objects::objectClass()->defaultObject, coreFunction("Caps"));
        call.s("a\xe9z").run();
        CHECK(*reinterpret_cast<std::string*>(call.returnValue()) == "A\xe9Z");
    }
}

TEST_CASE("new defaults the outer to self and masks the object flags", "[data]")
{
    initEngine();
    ut::UClass* cls = testInfoClass();
    ut::UObject* self = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    Bytes code;
    code.op({ 0x11, 0x0B, 0x21 }).u32(0).op({ 0x1D }).u32(0xffffffffu).op({ 0x20 }).object(cls);
    ut::UObject* created = evaluate<ut::UObject*>(self, code);
    REQUIRE(created != nullptr);
    CHECK(created->outer == self);
    CHECK(created->flags == 0x704005u);
}

TEST_CASE("a destroyed but not yet cleaned up object still runs calls made through it", "[data]")
{
    initEngine();
    ut::UClass* cls = testInfoClass();
    ut::UObject* caller = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    ut::UObject* target = ut::objects::newObject(cls, nullptr, ut::Name(), 0);
    Bytes incrementCode = incrementXnum(cls);
    incrementCode.op({ 0x04, 0x0B });
    ut::UFunction* increment = makeFunction(cls, "CoreContextIncrement", incrementCode);
    Bytes callerCode;
    callerCode.op({ 0x19, 0x20 }).object(target).u16(6).op({ 0x00, 0x1C }).object(increment).op({ 0x16, 0x04, 0x0B });
    ut::UFunction* through = makeFunction(cls, "CoreContextCaller", callerCode);
    xnum(target) = 0;
    ut::objects::destroyObject(target);
    ut::vm::Call call(caller, through);
    call.run();
    CHECK(xnum(target) == 1);
}

TEST_CASE("a subclass created at runtime inherits the parent's defaults, functions and states", "[data]")
{
    initEngine();
    ut::UClass* parent = testInfoClass();
    ut::UClass* sub = ut::objects::createSubclass(parent, ut::Name("RuntimeTestInfo"));
    CHECK(sub->superClass() == parent);
    CHECK(sub->isChildOf(parent));
    CHECK(sub->findFunction(ut::Name("TestStructBools")) == parent->findFunction(ut::Name("TestStructBools")));
    CHECK(sub->findState(ut::Name("AA")) == parent->findState(ut::Name("AA")));
    ut::UObject* object = ut::objects::newObject(sub, nullptr, ut::Name(), 0);
    CHECK(object->cls == sub);
    CHECK(object->isA(parent));
    CHECK(xnum(object) == 777);
    CHECK(*reinterpret_cast<std::string*>(object->propertyAddress(ut::Name("sxx"))) == "Tim");
}

TEST_CASE("globalconfig properties load from the declaring class section", "[data]")
{
    initEngine();
    ut::UClass* player = ut::objects::findClass("Botpack.TMale2");
    ut::objects::flushLoads();
    REQUIRE(player != nullptr);
    uint8_t* address = player->defaultObject->propertyAddress(ut::Name("bSnapToLevel"));
    REQUIRE(address != nullptr);
    CHECK(*reinterpret_cast<uint32_t*>(address) == 0);
}

TEST_CASE("SaveConfig writes globalconfig to the owner section and reloads class defaults", "[data]")
{
    initEngine();
    ut::UClass* gameInfo = ut::objects::findClass("Engine.GameInfo");
    ut::UClass* deathMatch = ut::objects::findClass("Botpack.DeathMatchPlus");
    ut::objects::flushLoads();
    REQUIRE(gameInfo != nullptr);
    REQUIRE(deathMatch != nullptr);
    ut::UObject* object = ut::objects::newObject(deathMatch, nullptr, ut::Name(), 0);
    int32_t& spectators = *reinterpret_cast<int32_t*>(object->propertyAddress(ut::Name("MaxSpectators")));
    int32_t original = spectators;
    spectators = original + 5;
    {
        ut::vm::Call call(object, coreFunction("SaveConfig"));
        call.run();
    }
    CHECK(ut::config::forName(ut::config::configNameOf(deathMatch))->get("Engine.GameInfo", "MaxSpectators") == std::to_string(original + 5));
    CHECK(*reinterpret_cast<int32_t*>(gameInfo->defaultObject->propertyAddress(ut::Name("MaxSpectators"))) == original + 5);
    CHECK(*reinterpret_cast<int32_t*>(deathMatch->defaultObject->propertyAddress(ut::Name("MaxSpectators"))) == original + 5);
    spectators = original;
    {
        ut::vm::Call call(object, coreFunction("SaveConfig"));
        call.run();
    }
    CHECK(*reinterpret_cast<int32_t*>(deathMatch->defaultObject->propertyAddress(ut::Name("MaxSpectators"))) == original);
}

TEST_CASE("relative and partial travel inherit the current URL options like FURL")
{
    std::string base = "DM-Deck16][?Name=Player?Class=Botpack.TMale2?load?Difficulty=2?push";
    CHECK(ut::travelUrl(base, "DM-Agony?difficulty=3?Quiet", ut::TRAVEL_Relative) == "DM-Agony?Name=Player?Class=Botpack.TMale2?difficulty=3?Quiet");
    CHECK(ut::travelUrl(base, "?Name=Other", ut::TRAVEL_Relative) == "DM-Deck16][?Name=Other?Class=Botpack.TMale2?Difficulty=2");
    CHECK(ut::travelUrl(base, "DM-Morpheus", ut::TRAVEL_Partial) == "DM-Morpheus?Name=Player?Class=Botpack.TMale2?Difficulty=2");
    CHECK(ut::travelUrl(base, "DM-Morpheus?Name=X", ut::TRAVEL_Absolute) == "DM-Morpheus?Name=X");
}

TEST_CASE("the unreal protocol prefix is removed from server and map URLs like FURL")
{
    CHECK(ut::stripProtocol("unreal://192.168.1.20:7777?Name=Alice") == "192.168.1.20:7777?Name=Alice");
    CHECK(ut::stripProtocol("UNREAL://10.0.0.5") == "10.0.0.5");
    CHECK(ut::stripProtocol("DM-Deck16][?game=Botpack.DeathMatchPlus") == "DM-Deck16][?game=Botpack.DeathMatchPlus");
    CHECK(ut::stripProtocol("unreal:/") == "unreal:/");
}

TEST_CASE("findGameRoot looks for game-data next to the AppImage")
{
    std::filesystem::path base = std::filesystem::temp_directory_path() / "openut-appimage-test";
    std::filesystem::create_directories(base / "game-data" / "System");
    std::ofstream marker(base / "game-data" / "System" / "Botpack.u");
    marker.close();
    std::string previous = std::getenv("OPENUT_GAME_DATA") ? std::getenv("OPENUT_GAME_DATA") : "";
    setEnvironment("OPENUT_GAME_DATA", (base / "missing").string().c_str());
    setEnvironment("APPIMAGE", (base / "openut.AppImage").string().c_str());
    std::filesystem::path root = ut::config::findGameRoot("fallback");
    setEnvironment("APPIMAGE", "");
    setEnvironment("OPENUT_GAME_DATA", previous.c_str());
    std::filesystem::remove_all(base);
    CHECK(root == base / "game-data");
}

TEST_CASE("float to int and byte conversions give the x86 result for NaN, infinity and huge values", "[data]")
{
    initEngine();
    ut::UObject* self = ut::objects::objectClass()->defaultObject;
    auto toInt = [&](float value) {
        Bytes code;
        code.op({ 0x44, 0x1E }).f32(value);
        return evaluate<int32_t>(self, code);
    };
    auto toByte = [&](float value) {
        Bytes code;
        code.op({ 0x43, 0x1E }).f32(value);
        return evaluate<uint8_t>(self, code);
    };
    CHECK(toInt(3.7f) == 3);
    CHECK(toInt(-3.7f) == -3);
    CHECK(toInt(2147483520.0f) == 2147483520);
    CHECK(toInt(-2147483648.0f) == -2147483647 - 1);
    CHECK(toInt(2147483648.0f) == -2147483647 - 1);
    CHECK(toInt(1e20f) == -2147483647 - 1);
    CHECK(toInt(-1e20f) == -2147483647 - 1);
    CHECK(toInt(std::numeric_limits<float>::infinity()) == -2147483647 - 1);
    CHECK(toInt(std::numeric_limits<float>::quiet_NaN()) == -2147483647 - 1);
    CHECK(toByte(200.9f) == 200);
    CHECK(toByte(300.0f) == 44);
    CHECK(toByte(std::numeric_limits<float>::quiet_NaN()) == 0);

    ut::vm::Call multiply(self, coreFunction("MultiplyEqual_IntFloat"));
    multiply.i(7).f(std::numeric_limits<float>::quiet_NaN()).run();
    CHECK(*reinterpret_cast<int32_t*>(multiply.parameter(0)) == -2147483647 - 1);
    ut::vm::Call divide(self, coreFunction("DivideEqual_IntFloat"));
    divide.i(7).f(0.5f).run();
    CHECK(*reinterpret_cast<int32_t*>(divide.parameter(0)) == 14);
}

TEST_CASE("saving an ini replaces the file in one step and leaves it untouched when the write fails")
{
    std::filesystem::path directory = std::filesystem::temp_directory_path() / ("openut-ini-" + std::to_string(Catch::getSeed()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    std::filesystem::path file = directory / "Test.ini";
    {
        std::ofstream original(file, std::ios::binary);
        original << "[Old]\r\nKey=1\r\n";
    }
    ut::Ini ini;
    REQUIRE(ini.load(file));
    ini.set("New", "Key", "2");
    REQUIRE(ini.save());
    CHECK_FALSE(std::filesystem::exists(directory / "Test.ini.tmp"));
    ut::Ini reloaded;
    REQUIRE(reloaded.load(file));
    CHECK(reloaded.get("Old", "Key") == "1");
    CHECK(reloaded.get("New", "Key") == "2");

    std::ifstream before(file, std::ios::binary);
    std::string saved((std::istreambuf_iterator<char>(before)), std::istreambuf_iterator<char>());
    before.close();
    std::filesystem::create_directory(directory / "Test.ini.tmp");
    ini.set("New", "Key", "3");
    CHECK_FALSE(ini.save());
    std::ifstream after(file, std::ios::binary);
    std::string kept((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>());
    CHECK(kept == saved);
    after.close();
    std::filesystem::remove_all(directory);
}
