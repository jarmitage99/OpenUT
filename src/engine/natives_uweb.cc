#include "core/objects.h"
#include "core/vm.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ut {
    namespace {
        using Variables = std::vector<std::pair<std::string, std::string>>;

        std::unordered_map<UObject*, Variables>& requestVariables()
        {
            static std::unordered_map<UObject*, Variables> table;
            return table;
        }

        std::unordered_map<UObject*, std::unordered_map<std::string, std::string>>& replacements()
        {
            static std::unordered_map<UObject*, std::unordered_map<std::string, std::string>> table;
            return table;
        }

        std::string lowered(std::string text)
        {
            for (char& c : text) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return text;
        }

        int base64Value(char c)
        {
            if (c >= 'A' && c <= 'Z') {
                return c - 'A';
            }
            if (c >= 'a' && c <= 'z') {
                return c - 'a' + 26;
            }
            if (c >= '0' && c <= '9') {
                return c - '0' + 52;
            }
            if (c == '+') {
                return 62;
            }
            if (c == '/') {
                return 63;
            }
            return -1;
        }

        void nativeDecodeBase64(UObject*, Frame& stack, void* result)
        {
            std::string encoded = vm::get<std::string>(stack);
            vm::finish(stack);
            std::string decoded;
            uint32_t bits = 0;
            int count = 0;
            for (char c : encoded) {
                int value = base64Value(c);
                if (value < 0) {
                    continue;
                }
                bits = (bits << 6) | static_cast<uint32_t>(value);
                count += 6;
                if (count >= 8) {
                    count -= 8;
                    decoded.push_back(static_cast<char>((bits >> count) & 0xFF));
                }
            }
            vm::ret<std::string>(result, decoded);
        }

        void nativeAddVariable(UObject* self, Frame& stack, void*)
        {
            std::string name = vm::get<std::string>(stack);
            std::string value = vm::get<std::string>(stack);
            vm::finish(stack);
            requestVariables()[self].emplace_back(lowered(name), value);
        }

        std::vector<const std::string*> valuesOf(UObject* self, const std::string& name)
        {
            std::vector<const std::string*> values;
            auto found = requestVariables().find(self);
            if (found == requestVariables().end()) {
                return values;
            }
            std::string key = lowered(name);
            for (const auto& [variable, value] : found->second) {
                if (variable == key) {
                    values.push_back(&value);
                }
            }
            return values;
        }

        void nativeGetVariable(UObject* self, Frame& stack, void* result)
        {
            std::string name = vm::get<std::string>(stack);
            std::string fallback = vm::getOpt<std::string>(stack, std::string());
            vm::finish(stack);
            std::vector<const std::string*> values = valuesOf(self, name);
            vm::ret<std::string>(result, values.empty() ? fallback : *values.front());
        }

        void nativeGetVariableCount(UObject* self, Frame& stack, void* result)
        {
            std::string name = vm::get<std::string>(stack);
            vm::finish(stack);
            vm::ret<int32_t>(result, static_cast<int32_t>(valuesOf(self, name).size()));
        }

        void nativeGetVariableNumber(UObject* self, Frame& stack, void* result)
        {
            std::string name = vm::get<std::string>(stack);
            int32_t number = vm::get<int32_t>(stack);
            std::string fallback = vm::getOpt<std::string>(stack, std::string());
            vm::finish(stack);
            std::vector<const std::string*> values = valuesOf(self, name);
            bool inRange = number >= 0 && static_cast<size_t>(number) < values.size();
            vm::ret<std::string>(result, inRange ? *values[static_cast<size_t>(number)] : fallback);
        }

        void nativeSubst(UObject* self, Frame& stack, void*)
        {
            std::string variable = vm::get<std::string>(stack);
            std::string value = vm::get<std::string>(stack);
            bool clear = vm::getBoolOpt(stack, false);
            vm::finish(stack);
            auto& table = replacements()[self];
            if (clear) {
                table.clear();
            }
            table[lowered(variable)] = value;
        }

        void nativeClearSubst(UObject* self, Frame& stack, void*)
        {
            vm::finish(stack);
            replacements().erase(self);
        }

        bool readInclude(UObject* self, const std::string& filename, std::string& contents)
        {
            std::filesystem::path relative = std::filesystem::path(filename).lexically_normal();
            if (relative.empty() || relative.has_root_name() || relative.has_root_directory() || filename.find('\\') != std::string::npos) {
                return false;
            }
            for (const std::filesystem::path& part : relative) {
                if (part == "..") {
                    return false;
                }
            }
            std::string includePath = *reinterpret_cast<std::string*>(self->propertyAddress(Name("IncludePath")));
            std::filesystem::path file = objects::gameRoot() / "System" / includePath / relative;
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error)) {
                return false;
            }
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                return false;
            }
            contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        std::string substitute(UObject* self, const std::string& page)
        {
            const auto empty = std::unordered_map<std::string, std::string>();
            auto found = replacements().find(self);
            const auto& table = found == replacements().end() ? empty : found->second;
            std::string output;
            size_t at = 0;
            while (at < page.size()) {
                size_t open = page.find('%', at);
                if (open == std::string::npos) {
                    output.append(page, at, std::string::npos);
                    break;
                }
                output.append(page, at, open - at);
                size_t close = page.find('%', open + 1);
                if (close == std::string::npos) {
                    output.append(page, open, std::string::npos);
                    break;
                }
                if (close == open + 1) {
                    output.push_back('%');
                } else {
                    auto value = table.find(lowered(page.substr(open + 1, close - open - 1)));
                    if (value != table.end()) {
                        output += value->second;
                    }
                }
                at = close + 1;
            }
            return output;
        }

        void nativeIncludeUHTM(UObject* self, Frame& stack, void*)
        {
            std::string filename = vm::get<std::string>(stack);
            vm::finish(stack);
            std::string page;
            if (!readInclude(self, filename, page)) {
                return;
            }
            vm::Call(self, Name("SendText")).s(substitute(self, page)).b(true).run();
        }

        void nativeIncludeBinaryFile(UObject* self, Frame& stack, void*)
        {
            std::string filename = vm::get<std::string>(stack);
            vm::finish(stack);
            std::string contents;
            if (!readInclude(self, filename, contents)) {
                return;
            }
            for (size_t at = 0; at < contents.size() && !self->destroyed; at += 255) {
                vm::Call call(self, Name("SendBinary"));
                if (!call.valid()) {
                    return;
                }
                int32_t count = static_cast<int32_t>(std::min<size_t>(255, contents.size() - at));
                *reinterpret_cast<int32_t*>(call.parameter(0)) = count;
                std::copy_n(contents.data() + at, count, reinterpret_cast<char*>(call.parameter(1)));
                call.run();
            }
        }
    }

    void registerWebNatives()
    {
        objects::onCollect([](const std::unordered_set<UObject*>& doomed) {
            std::erase_if(requestVariables(), [&](const auto& entry) {
                return doomed.count(entry.first) != 0;
            });
            std::erase_if(replacements(), [&](const auto& entry) {
                return doomed.count(entry.first) != 0;
            });
        });
        vm::registerNative("WebRequest", "DecodeBase64", nativeDecodeBase64);
        vm::registerNative("WebRequest", "AddVariable", nativeAddVariable);
        vm::registerNative("WebRequest", "GetVariable", nativeGetVariable);
        vm::registerNative("WebRequest", "GetVariableCount", nativeGetVariableCount);
        vm::registerNative("WebRequest", "GetVariableNumber", nativeGetVariableNumber);
        vm::registerNative("WebResponse", "Subst", nativeSubst);
        vm::registerNative("WebResponse", "ClearSubst", nativeClearSubst);
        vm::registerNative("WebResponse", "IncludeUHTM", nativeIncludeUHTM);
        vm::registerNative("WebResponse", "IncludeBinaryFile", nativeIncludeBinaryFile);
    }
}
