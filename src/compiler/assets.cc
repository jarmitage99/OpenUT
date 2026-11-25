#include "compiler/assets.h"

#include "core/objects.h"

#include <algorithm>

namespace ut::compiler {
    const std::vector<AssetHandler>& assetHandlers()
    {
        static const std::vector<AssetHandler> handlers = { textureAssets(), audioAssets(), meshAssets() };
        return handlers;
    }

    bool importAsset(ExecContext& context, const std::string& command)
    {
        for (const AssetHandler& handler : assetHandlers()) {
            if (handler.import && handler.import(context, command)) {
                return true;
            }
        }
        return false;
    }

    bool exportAssetSource(ExecContext& context, const std::string& command)
    {
        for (const AssetHandler& handler : assetHandlers()) {
            if (handler.exportSource && handler.exportSource(context, command)) {
                return true;
            }
        }
        return false;
    }

    bool exportAssetObject(UObject* object, const std::string& extension, const std::filesystem::path& file)
    {
        for (const AssetHandler& handler : assetHandlers()) {
            if (handler.exportObject && handler.exportObject(object, extension, file)) {
                return true;
            }
        }
        return false;
    }

    bool serializeAsset(PackageWriter& package, UObject* object, Writer& out)
    {
        for (const AssetHandler& handler : assetHandlers()) {
            if (handler.serialize && handler.serialize(package, object, out)) {
                return true;
            }
        }
        return false;
    }

    void finishAssets(ExecContext& context)
    {
        for (const AssetHandler& handler : assetHandlers()) {
            if (handler.finish) {
                handler.finish(context);
            }
        }
    }

    bool packageExports(UObject* package, const std::string& name)
    {
        Linker* linker = package ? objects::findLinker(package->name) : nullptr;
        if (!linker) {
            return true;
        }
        const Package& file = linker->file();
        for (size_t i = 0; i < file.exports().size(); i++) {
            int32_t ref = static_cast<int32_t>(i + 1);
            if (iequals(file.objectName(ref), name) && file.classOf(ref) != "Class") {
                return true;
            }
        }
        return false;
    }

    namespace {
        bool findExisting(const std::filesystem::path& current, const std::vector<std::filesystem::path>& parts, size_t index, std::filesystem::path& found)
        {
            if (index == parts.size()) {
                found = current;
                return std::filesystem::exists(current);
            }
            if (!std::filesystem::is_directory(current)) {
                return false;
            }
            if (findExisting(current / parts[index], parts, index + 1, found)) {
                return true;
            }
            for (const auto& item : std::filesystem::directory_iterator(current)) {
                std::string name = item.path().filename().string();
                if (name != parts[index].string() && iequals(name, parts[index].string()) && findExisting(item.path(), parts, index + 1, found)) {
                    return true;
                }
            }
            return false;
        }
    }

    std::filesystem::path resolveFile(const std::filesystem::path& directory, std::string text)
    {
        std::replace(text.begin(), text.end(), '\\', '/');
        std::filesystem::path relative(text);
        std::vector<std::filesystem::path> parts(relative.begin(), relative.end());
        std::filesystem::path found;
        if (findExisting(directory, parts, 0, found)) {
            return found;
        }
        std::filesystem::path current = directory;
        for (const std::filesystem::path& part : parts) {
            std::filesystem::path exact = current / part;
            if (std::filesystem::exists(exact) || !std::filesystem::is_directory(current)) {
                current = exact;
                continue;
            }
            std::filesystem::path match = exact;
            for (const auto& item : std::filesystem::directory_iterator(current)) {
                if (iequals(item.path().filename().string(), part.string())) {
                    match = item.path();
                    break;
                }
            }
            current = match;
        }
        return current;
    }

    std::filesystem::path objLoadFile(const std::filesystem::path& directory, const std::string& file)
    {
        std::filesystem::path local = resolveFile(directory, file);
        if (std::filesystem::exists(local)) {
            return local;
        }
        return resolveFile(objects::gameRoot() / "System", file);
    }

    UObject* objLoadGroup(const std::string& target, bool create)
    {
        size_t dot = target.find('.');
        if (dot == std::string::npos) {
            return nullptr;
        }
        UClass* packageClass = objects::intrinsicClass(Name("Package"));
        UObject* outer = objects::packageObject(Name(target.substr(0, dot)));
        std::string rest = target.substr(dot + 1);
        while (outer && !rest.empty()) {
            size_t next = rest.find('.');
            Name part(rest.substr(0, next));
            UObject* child = nullptr;
            for (UObject* object : objects::all()) {
                if (object && !object->destroyed && object->outer == outer && object->name == part && object->cls == packageClass) {
                    child = object;
                    break;
                }
            }
            if (!child && create) {
                child = objects::newObject(packageClass, outer, part, RF_Public);
            }
            outer = child;
            rest = next == std::string::npos ? std::string() : rest.substr(next + 1);
        }
        return outer;
    }
}
