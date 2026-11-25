#include "core/config.h"
#include "core/objects.h"
#include "core/vm.h"
#include "engine/world.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ut {
    namespace {
        std::unordered_map<UObject*, std::unique_ptr<std::ofstream>>& openLogs()
        {
            static std::unordered_map<UObject*, std::unique_ptr<std::ofstream>> logs;
            return logs;
        }

        std::string& stringProperty(UObject* self, const char* name)
        {
            return *reinterpret_cast<std::string*>(self->propertyAddress(Name(name)));
        }

        std::filesystem::path logPath(const std::string& file)
        {
            return std::filesystem::weakly_canonical(config::userDirectory() / "System" / file);
        }

        void nativeOpenLog(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            std::filesystem::path path = logPath(stringProperty(self, "StatLogFile"));
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            openLogs()[self] = std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::trunc);
        }

        void nativeCloseLog(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            auto found = openLogs().find(self);
            if (found == openLogs().end()) {
                return;
            }
            openLogs().erase(found);
            std::error_code error;
            std::filesystem::rename(logPath(stringProperty(self, "StatLogFile")), logPath(stringProperty(self, "StatLogFinal")), error);
        }

        void nativeFileLog(UObject* self, Frame& stack, void*)
        {
            std::string text = vm::get<std::string>(stack);
            vm::finish(stack);
            auto found = openLogs().find(self);
            if (found != openLogs().end()) {
                *found->second << text << "\r\n";
            }
        }

        void nativeFileFlush(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            auto found = openLogs().find(self);
            if (found != openLogs().end()) {
                found->second->flush();
            }
        }

        void nativeGetChecksum(UObject*, Frame& stack, void*)
        {
            std::string* checksum = vm::getRef<std::string>(stack);
            vm::finish(stack);
            checksum->clear();
        }

        void nativeGetPlayerChecksum(UObject*, Frame& stack, void*)
        {
            vm::get<UObject*>(stack);
            std::string* checksum = vm::getRef<std::string>(stack);
            vm::finish(stack);
            checksum->clear();
        }

        void nativeIgnoreObject(UObject*, Frame& stack, void*)
        {
            vm::get<UObject*>(stack);
            vm::finish(stack);
        }

        void nativeIgnoreString(UObject*, Frame& stack, void*)
        {
            vm::get<std::string>(stack);
            vm::finish(stack);
        }

        void nativeLogMutator(UObject* self, Frame& stack, void*)
        {
            UObject* mutator = vm::get<UObject*>(stack);
            vm::finish(stack);
            if (mutator && mutator->cls) {
                vm::Call(self, Name("LogGameSpecial")).s("GameMutator").s(mutator->cls->fullName()).run();
            }
        }

        void nativeNothing(UObject*, Frame& stack, void*)
        {
            vm::finish(stack);
        }

        void nativeGetGMTRef(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            std::time_t now = std::time(nullptr);
            std::tm local = *std::localtime(&now);
            std::tm utc = *std::gmtime(&now);
            local.tm_isdst = 0;
            utc.tm_isdst = 0;
            long offset = static_cast<long>(std::difftime(std::mktime(&local), std::mktime(&utc)));
            vm::ret<std::string>(result, std::to_string(offset / 3600));
        }

        void nativeGetMapFileName(UObject*, Frame& stack, void* result)
        {
            vm::finish(stack);
            std::string map;
            if (World::current) {
                map = World::current->url.substr(0, World::current->url.find('?'));
                if (std::filesystem::path(map).extension().empty()) {
                    map += ".unr";
                }
            }
            vm::ret<std::string>(result, map);
        }
    }

    void registerStatLogNatives()
    {
        objects::onCollect([](const std::unordered_set<UObject*>& doomed) {
            std::erase_if(openLogs(), [&](const auto& entry) {
                return doomed.count(entry.first) != 0;
            });
        });
        vm::registerNative("StatLogFile", "OpenLog", nativeOpenLog);
        vm::registerNative("StatLogFile", "CloseLog", nativeCloseLog);
        vm::registerNative("StatLogFile", "Watermark", nativeFileLog);
        vm::registerNative("StatLogFile", "GetChecksum", nativeGetChecksum);
        vm::registerNative("StatLogFile", "FileFlush", nativeFileFlush);
        vm::registerNative("StatLogFile", "FileLog", nativeFileLog);
        vm::registerNative("StatLog", "ExecuteLocalLogBatcher", nativeNothing);
        vm::registerNative("StatLog", "ExecuteSilentLogBatcher", nativeNothing);
        vm::registerNative("StatLog", "BatchLocal", nativeNothing);
        vm::registerNative("StatLog", "ExecuteWorldLogBatcher", nativeNothing);
        vm::registerNative("StatLog", "BrowseRelativeLocalURL", nativeIgnoreString);
        vm::registerNative("StatLog", "InitialCheck", nativeIgnoreObject);
        vm::registerNative("StatLog", "LogMutator", nativeLogMutator);
        vm::registerNative("StatLog", "GetPlayerChecksum", nativeGetPlayerChecksum);
        vm::registerNative("StatLog", "GetGMTRef", nativeGetGMTRef);
        vm::registerNative("StatLog", "GetMapFileName", nativeGetMapFileName);
    }
}
