#pragma once

#include "compiler/script_compiler.h"
#include "core/object.h"
#include "core/package_writer.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ut::compiler {
    struct ExecContext {
        UObject* package = nullptr;
        std::filesystem::path directory;
        LogSink log;
    };

    struct AssetHandler {
        bool (*import)(ExecContext& context, const std::string& command) = nullptr;
        bool (*exportSource)(ExecContext& context, const std::string& command) = nullptr;
        bool (*exportObject)(UObject* object, const std::string& extension, const std::filesystem::path& file) = nullptr;
        bool (*serialize)(PackageWriter& package, UObject* object, Writer& out) = nullptr;
        void (*finish)(ExecContext& context) = nullptr;
    };

    AssetHandler textureAssets();
    AssetHandler audioAssets();
    AssetHandler meshAssets();

    const std::vector<AssetHandler>& assetHandlers();
    bool importAsset(ExecContext& context, const std::string& command);
    bool exportAssetSource(ExecContext& context, const std::string& command);
    bool exportAssetObject(UObject* object, const std::string& extension, const std::filesystem::path& file);
    bool serializeAsset(PackageWriter& package, UObject* object, Writer& out);
    void finishAssets(ExecContext& context);
    bool packageExports(UObject* package, const std::string& name);
    std::filesystem::path resolveFile(const std::filesystem::path& directory, std::string text);
    std::filesystem::path objLoadFile(const std::filesystem::path& directory, const std::string& file);
    UObject* objLoadGroup(const std::string& target, bool create);
}
