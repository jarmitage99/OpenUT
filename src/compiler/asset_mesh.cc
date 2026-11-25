#include "compiler/assets.h"

#include "compiler/asset_mesh_lod.h"
#include "compiler/exec_parse.h"
#include "core/objects.h"
#include "engine/mesh.h"
#include "engine/package.h"
#include "engine/reader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace ut::compiler {
    namespace {
        constexpr uint32_t PolyInvisible = 0x00000001;
        constexpr uint32_t PolyMasked = 0x00000002;
        constexpr uint32_t PolyTranslucent = 0x00000004;
        constexpr uint32_t PolyEnvironment = 0x00000010;
        constexpr uint32_t PolyModulated = 0x00000040;
        constexpr uint32_t PolyTwoSided = 0x00000100;
        constexpr uint32_t PolyNoSmooth = 0x00000800;
        constexpr uint32_t PolyFlat = 0x00004000;
        constexpr uint32_t PolyUnlit = 0x00400000;
        constexpr uint32_t ObjectFlags = RF_Public | RF_Standalone;
        constexpr size_t DataHeaderSize = 48;
        constexpr size_t VertexSize = 4;
        constexpr size_t WideVertexSize = 8;
        constexpr int32_t NumAngles = 16384;
        constexpr double Pi = 3.1415926535897932;

        struct JsTri {
            uint16_t vertex[3] = {};
            uint8_t type = 0;
            uint8_t color = 0;
            uint8_t uv[3][2] = {};
            uint8_t textureNum = 0;
            uint8_t flags = 0;
        };

        struct Axes {
            Vector x { 1.0f, 0.0f, 0.0f };
            Vector y { 0.0f, 1.0f, 0.0f };
            Vector z { 0.0f, 0.0f, 1.0f };
        };

        struct ImportSettings {
            std::string meshName;
            std::string anivFile;
            std::string dataFile;
            bool unmirror = false;
            bool zeroTex = false;
            int32_t unmirrorTex = -1;
            bool levelOfDetail = true;
            LodSettings lod;
        };

        std::vector<uint8_t> readFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        uint16_t readU16(const std::vector<uint8_t>& data, size_t at)
        {
            return static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
        }

        void putU16(std::vector<uint8_t>& data, uint16_t value)
        {
            data.push_back(static_cast<uint8_t>(value & 0xff));
            data.push_back(static_cast<uint8_t>(value >> 8));
        }

        bool shippedTextureLod(const Package& file, int32_t ref, std::vector<float>& lod)
        {
            Reader reader(file.data(), static_cast<size_t>(file.exportAt(ref).offset));
            if (file.nameAt(reader.index()) != "None") {
                return false;
            }
            auto skipLazy = [&reader]() {
                reader.seek(reader.u32());
            };
            reader.skip(41);
            skipLazy();
            skipLazy();
            int32_t sequences = reader.index();
            for (int32_t i = 0; i < sequences; i++) {
                reader.index();
                reader.index();
                reader.skip(8);
                int32_t notifys = reader.index();
                for (int32_t n = 0; n < notifys; n++) {
                    reader.skip(4);
                    reader.index();
                }
                reader.skip(4);
            }
            skipLazy();
            reader.skip(41);
            skipLazy();
            int32_t textures = reader.index();
            for (int32_t i = 0; i < textures; i++) {
                reader.index();
            }
            reader.skip(static_cast<size_t>(reader.index()) * 25);
            reader.skip(static_cast<size_t>(reader.index()) * 16);
            reader.skip(60);
            lod.resize(static_cast<size_t>(reader.index()));
            for (float& value : lod) {
                value = reader.f32();
            }
            return true;
        }

        bool legacyMeshTool(UObject* package)
        {
            static std::map<std::string, bool> known;
            std::string key = package->name.str();
            std::transform(key.begin(), key.end(), key.begin(), [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });
            auto found = known.find(key);
            if (found != known.end()) {
                return found->second;
            }
            int computed = 0;
            int plain = 0;
            std::filesystem::path shipped = resolveFile(objects::gameRoot() / "System", package->name.str() + ".u");
            if (std::filesystem::exists(shipped)) {
                Package file(shipped);
                for (size_t i = 0; file.version() > 61 && i < file.exports().size(); i++) {
                    int32_t ref = static_cast<int32_t>(i + 1);
                    std::string className = file.classOf(ref);
                    std::vector<float> lod;
                    if ((className != "LodMesh" && className != "Mesh") || file.exportAt(ref).size <= 0 || !shippedTextureLod(file, ref, lod) || lod.empty()) {
                        continue;
                    }
                    bool fractional = std::any_of(lod.begin(), lod.end(), [](float value) {
                        return value <= 0.0f || value != std::floor(value);
                    });
                    computed += fractional ? 1 : 0;
                    plain += fractional ? 0 : 1;
                }
            }
            known[key] = computed > plain;
            return known[key];
        }

        uint32_t packVert(int32_t x, int32_t y, int32_t z)
        {
            return (static_cast<uint32_t>(x) & 0x7ff) | ((static_cast<uint32_t>(y) & 0x7ff) << 11) | ((static_cast<uint32_t>(z) & 0x3ff) << 22);
        }

        bool writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& data)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            return static_cast<bool>(out);
        }

        bool getVector(std::string_view stream, Vector& value)
        {
            int count = 0;
            count += parseFloat(stream, "X=", value.x) ? 1 : 0;
            count += parseFloat(stream, "Y=", value.y) ? 1 : 0;
            count += parseFloat(stream, "Z=", value.z) ? 1 : 0;
            if (count == 0) {
                value.x = static_cast<float>(std::atof(std::string(stream).c_str()));
                size_t comma = stream.find(',');
                if (comma == std::string_view::npos) {
                    return false;
                }
                stream.remove_prefix(comma + 1);
                value.y = static_cast<float>(std::atof(std::string(stream).c_str()));
                comma = stream.find(',');
                if (comma == std::string_view::npos) {
                    return false;
                }
                stream.remove_prefix(comma + 1);
                value.z = static_cast<float>(std::atof(std::string(stream).c_str()));
                count = 3;
            }
            return count == 3;
        }

        int32_t appRound(double value)
        {
            return static_cast<int32_t>(std::nearbyint(value));
        }

        void getRotator(std::string_view stream, Rotator& rotation, int32_t scale)
        {
            float temp = 0.0f;
            int count = 0;
            if (parseFloat(stream, "PITCH=", temp)) {
                rotation.pitch = appRound(static_cast<double>(temp) * scale);
                count++;
            }
            if (parseFloat(stream, "YAW=", temp)) {
                rotation.yaw = appRound(static_cast<double>(temp) * scale);
                count++;
            }
            if (parseFloat(stream, "ROLL=", temp)) {
                rotation.roll = appRound(static_cast<double>(temp) * scale);
                count++;
            }
            if (count != 0) {
                return;
            }
            rotation.pitch = appRound(std::atof(std::string(stream).c_str()) * scale);
            size_t comma = stream.find(',');
            if (comma == std::string_view::npos) {
                return;
            }
            stream.remove_prefix(comma + 1);
            rotation.yaw = appRound(std::atof(std::string(stream).c_str()) * scale);
            comma = stream.find(',');
            if (comma == std::string_view::npos) {
                return;
            }
            stream.remove_prefix(comma + 1);
            rotation.roll = appRound(std::atof(std::string(stream).c_str()) * scale);
        }

        UClass* meshClass(bool levelOfDetail)
        {
            return objects::intrinsicClass(Name(levelOfDetail ? "LodMesh" : "Mesh"));
        }

        UMesh* findMesh(UObject*, std::string_view stream, std::string_view key)
        {
            std::string name;
            if (!parseString(stream, key, name)) {
                return nullptr;
            }
            objects::createExportsNamed(Name(name));
            UMesh* found = nullptr;
            auto rank = [](const UObject* object) {
                return std::make_tuple(object->linker ? object->linker->loadOrder : UINT64_MAX, object->linker ? object->exportRef : 0, object->handle);
            };
            for (UObject* object : objects::all()) {
                UMesh* mesh = object && !object->destroyed ? cast<UMesh>(object) : nullptr;
                if (mesh && iequals(mesh->name.str(), name) && (!found || rank(mesh) < rank(found))) {
                    found = mesh;
                }
            }
            if (found) {
                objects::ensureLoaded(found);
            }
            return found;
        }

        UObject* findTexture(std::string_view stream)
        {
            std::string name;
            if (!parseString(stream, "TEXTURE=", name)) {
                return nullptr;
            }
            UClass* textureClass = objects::findClass("Engine.Texture");
            if (name.find('.') != std::string::npos) {
                return objects::findObject(name, textureClass);
            }
            for (UObject* object : objects::all()) {
                if (object && !object->destroyed && object->cls && iequals(object->name.str(), name) && object->isA(textureClass)) {
                    return object;
                }
            }
            return nullptr;
        }

        uint32_t polyFlagsFromType(uint8_t type)
        {
            uint32_t flags = 0;
            switch (type & 15) {
            case 1:
                flags = PolyTwoSided;
                break;
            case 4:
                flags = PolyTwoSided | PolyModulated;
                break;
            case 2:
                flags = PolyTwoSided | PolyTranslucent;
                break;
            case 3:
                flags = PolyTwoSided | PolyMasked;
                break;
            case 8:
                flags = PolyTwoSided | PolyInvisible;
                break;
            default:
                break;
            }
            if (type & 0x10) {
                flags |= PolyUnlit;
            }
            if (type & 0x20) {
                flags |= PolyFlat;
            }
            if (type & 0x40) {
                flags |= PolyEnvironment;
            }
            if (type & 0x80) {
                flags |= PolyNoSmooth;
            }
            return flags;
        }

        bool typeFromPolyFlags(uint32_t flags, uint8_t& type)
        {
            uint32_t base = flags & (PolyTwoSided | PolyModulated | PolyTranslucent | PolyMasked | PolyInvisible);
            uint32_t extra = flags & ~base;
            if (base == 0) {
                type = 0;
            } else if (base == PolyTwoSided) {
                type = 1;
            } else if (base == (PolyTwoSided | PolyModulated)) {
                type = 4;
            } else if (base == (PolyTwoSided | PolyTranslucent)) {
                type = 2;
            } else if (base == (PolyTwoSided | PolyMasked)) {
                type = 3;
            } else if (base == (PolyTwoSided | PolyInvisible)) {
                type = 8;
            } else {
                return false;
            }
            if (extra & PolyUnlit) {
                type |= 0x10;
            }
            if (extra & PolyFlat) {
                type |= 0x20;
            }
            if (extra & PolyEnvironment) {
                type |= 0x40;
            }
            if (extra & PolyNoSmooth) {
                type |= 0x80;
            }
            return (extra & ~(PolyUnlit | PolyFlat | PolyEnvironment | PolyNoSmooth)) == 0;
        }

        int compareTris(const MeshTri& a, const MeshTri& b)
        {
            if ((a.polyFlags & PolyTranslucent) > (b.polyFlags & PolyTranslucent)) {
                return 1;
            }
            if ((a.polyFlags & PolyTranslucent) < (b.polyFlags & PolyTranslucent)) {
                return -1;
            }
            if (a.textureIndex > b.textureIndex) {
                return 1;
            }
            if (a.textureIndex < b.textureIndex) {
                return -1;
            }
            if (a.polyFlags > b.polyFlags) {
                return 1;
            }
            if (a.polyFlags < b.polyFlags) {
                return -1;
            }
            return 0;
        }

        Vector vertexVector(const MeshVert& vert)
        {
            return { static_cast<float>(vert.x), static_cast<float>(vert.y), static_cast<float>(vert.z) };
        }

        MeshVert meshVert(const Vector& vector)
        {
            MeshVert vert;
            vert.x = static_cast<int16_t>(vector.x);
            vert.y = static_cast<int16_t>(vector.y);
            vert.z = static_cast<int16_t>(vector.z);
            return vert;
        }

        void addPoint(BoundingBox& box, const Vector& point)
        {
            if (!box.valid) {
                box.min = point;
                box.max = point;
                box.valid = true;
                return;
            }
            box.min.x = std::min(box.min.x, point.x);
            box.min.y = std::min(box.min.y, point.y);
            box.min.z = std::min(box.min.z, point.z);
            box.max.x = std::max(box.max.x, point.x);
            box.max.y = std::max(box.max.y, point.y);
            box.max.z = std::max(box.max.z, point.z);
        }

        Plane boundingSphere(const std::vector<Vector>& points, bool legacy)
        {
            Plane sphere;
            if (points.empty()) {
                return sphere;
            }
            BoundingBox box;
            for (const Vector& point : points) {
                addPoint(box, point);
            }
            float sumX = box.min.x + box.max.x;
            float sumY = box.min.y + box.max.y;
            float sumZ = box.min.z + box.max.z;
            sphere.x = sumX * 0.5f;
            sphere.y = sumY * 0.5f;
            sphere.z = sumZ * 0.5f;
            float radius = 0.0f;
            for (const Vector& point : points) {
                double dx = static_cast<double>(sphere.x) - static_cast<double>(point.x);
                double dy = static_cast<double>(sphere.y) - static_cast<double>(point.y);
                double dz = static_cast<double>(sphere.z) - static_cast<double>(point.z);
                double distance = dy * dy + dx * dx;
                distance = distance + dz * dz;
                if (distance > static_cast<double>(radius)) {
                    radius = static_cast<float>(distance);
                }
            }
            sphere.w = static_cast<float>(std::sqrt(static_cast<double>(radius)) * (legacy ? 1.001 : static_cast<double>(1.001f)));
            return sphere;
        }

        void buildBounds(UMesh* mesh, bool legacy)
        {
            std::vector<Vector> all;
            mesh->frameBounds.assign(static_cast<size_t>(mesh->animFrames), BoundingBox());
            mesh->frameSpheres.assign(static_cast<size_t>(mesh->animFrames), Plane());
            for (size_t frame = 0; frame < static_cast<size_t>(mesh->animFrames); frame++) {
                std::vector<Vector> one;
                for (size_t vertex = 0; vertex < static_cast<size_t>(mesh->frameVerts); vertex++) {
                    Vector point = mesh->verts[frame * static_cast<size_t>(mesh->frameVerts) + vertex];
                    one.push_back(point);
                    all.push_back(point);
                }
                for (const Vector& point : one) {
                    addPoint(mesh->frameBounds[frame], point);
                }
                mesh->frameSpheres[frame] = boundingSphere(one, legacy);
            }
            mesh->meshBounds = BoundingBox();
            for (const Vector& point : all) {
                addPoint(mesh->meshBounds, point);
            }
            mesh->meshSphere = boundingSphere(all, legacy);
            mesh->bounds = mesh->meshBounds;
            mesh->sphere = mesh->meshSphere;
        }

        float trig(int32_t index)
        {
            return static_cast<float>(std::sin(static_cast<double>(static_cast<float>(index) * 2.0f) * Pi / NumAngles));
        }

        float sinTab(int32_t angle)
        {
            return trig((angle >> 2) & (NumAngles - 1));
        }

        float cosTab(int32_t angle)
        {
            return trig(((angle + 16384) >> 2) & (NumAngles - 1));
        }

        float dotAxis(const Vector& vector, const Vector& axis)
        {
            double sum = static_cast<double>(vector.x) * static_cast<double>(axis.x) + static_cast<double>(vector.y) * static_cast<double>(axis.y);
            sum = sum + static_cast<double>(vector.z) * static_cast<double>(axis.z);
            return static_cast<float>(sum);
        }

        Vector transformVector(const Vector& vector, const Axes& coords)
        {
            return { dotAxis(vector, coords.x), dotAxis(vector, coords.y), dotAxis(vector, coords.z) };
        }

        void transformAxes(Axes& coords, const Axes& by)
        {
            coords.x = transformVector(coords.x, by);
            coords.y = transformVector(coords.y, by);
            coords.z = transformVector(coords.z, by);
        }

        Axes meshCoords(const UMesh* mesh)
        {
            Axes coords;
            const Rotator& rotation = mesh->rotOrigin;
            Axes yaw;
            yaw.x = { cosTab(rotation.yaw), sinTab(rotation.yaw), 0.0f };
            yaw.y = { -sinTab(rotation.yaw), cosTab(rotation.yaw), 0.0f };
            yaw.z = { 0.0f, 0.0f, 1.0f };
            transformAxes(coords, yaw);
            Axes pitch;
            pitch.x = { cosTab(rotation.pitch), 0.0f, sinTab(rotation.pitch) };
            pitch.y = { 0.0f, 1.0f, 0.0f };
            pitch.z = { -sinTab(rotation.pitch), 0.0f, cosTab(rotation.pitch) };
            transformAxes(coords, pitch);
            Axes roll;
            roll.x = { 1.0f, 0.0f, 0.0f };
            roll.y = { 0.0f, cosTab(rotation.roll), -sinTab(rotation.roll) };
            roll.z = { 0.0f, sinTab(rotation.roll), cosTab(rotation.roll) };
            transformAxes(coords, roll);
            transformAxes(coords, Axes());
            for (Vector* axis : { &coords.x, &coords.y, &coords.z }) {
                axis->x *= mesh->scale.x;
                axis->y *= mesh->scale.y;
                axis->z *= mesh->scale.z;
            }
            return coords;
        }

        void computeTextureLod(UMesh* mesh)
        {
            Axes coords = meshCoords(mesh);
            std::vector<float> rms(mesh->textureLod.size(), 0.0f);
            std::vector<float> count(mesh->textureLod.size(), 0.0f);
            for (size_t frame = 0; frame < static_cast<size_t>(mesh->animFrames); frame++) {
                size_t frameStart = frame * static_cast<size_t>(mesh->frameVerts);
                for (const MeshTri& tri : mesh->tris) {
                    for (size_t j = 0, k = 2; j < 3; k = j++) {
                        const Vector& a = mesh->verts[frameStart + tri.vertex[k]];
                        const Vector& b = mesh->verts[frameStart + tri.vertex[j]];
                        Vector delta = transformVector({ b.x - a.x, b.y - a.y, b.z - a.z }, coords);
                        double z = static_cast<double>(delta.z);
                        double y = static_cast<double>(delta.y);
                        double x = static_cast<double>(delta.x);
                        double size = z * z + (y * y + x * x);
                        float space = static_cast<float>(std::sqrt(size));
                        int32_t du = tri.uv[j][0] - tri.uv[k][0];
                        int32_t dv = tri.uv[j][1] - tri.uv[k][1];
                        float texels = static_cast<float>(std::sqrt(static_cast<double>(dv * dv + du * du)));
                        size_t index = static_cast<size_t>(tri.textureIndex);
                        rms[index] = static_cast<float>(static_cast<double>(space) / (static_cast<double>(texels) + 1.0) + static_cast<double>(rms[index]));
                        count[index] = static_cast<float>(static_cast<double>(count[index]) + 1.0);
                    }
                }
            }
            for (size_t i = 0; i < mesh->textureLod.size(); i++) {
                mesh->textureLod[i] = static_cast<float>(static_cast<double>(rms[i]) / (static_cast<double>(count[i]) + 0.01));
            }
        }

        void setScale(UMesh* mesh, const Vector& scale)
        {
            mesh->scale = scale;
            if (ULodMesh* lod = cast<ULodMesh>(mesh)) {
                float largest = std::max(std::fabs(scale.x), std::max(std::fabs(scale.y), std::fabs(scale.z)));
                lod->meshScaleMax = static_cast<float>(static_cast<double>(1.0f / 128.0f) * static_cast<double>(lod->meshSphere.w) * static_cast<double>(largest));
            }
        }

        bool parseImportSettings(std::string_view text, ImportSettings& settings)
        {
            if (!parseString(text, "MESH=", settings.meshName) || !parseString(text, "ANIVFILE=", settings.anivFile) || !parseString(text, "DATAFILE=", settings.dataFile)) {
                return false;
            }
            int32_t reorder = 0;
            parseInt(text, "REORDER=", reorder);
            parseBool(text, "UNMIRROR=", settings.unmirror);
            parseBool(text, "ZEROTEX=", settings.zeroTex);
            parseBool(text, "MLOD=", settings.levelOfDetail);
            parseInt(text, "LODSTYLE=", settings.lod.style);
            parseInt(text, "LODFRAME=", settings.lod.sampleFrame);
            parseBool(text, "LODNOTEX=", settings.lod.noUVData);
            if (!parseInt(text, "UNMIRRORTEX=", settings.unmirrorTex)) {
                settings.unmirrorTex = -1;
            }
            return true;
        }

        void importMesh(ExecContext& context, const ImportSettings& settings)
        {
            std::vector<uint8_t> aniv = readFile(resolveFile(context.directory, settings.anivFile));
            std::vector<uint8_t> data = readFile(resolveFile(context.directory, settings.dataFile));
            if (aniv.size() < 4 || data.size() < DataHeaderSize) {
                context.log("Warning: MESH IMPORT could not read " + settings.anivFile + " / " + settings.dataFile);
                return;
            }
            size_t numPolys = readU16(data, 0);
            size_t numVertices = readU16(data, 2);
            size_t numFrames = readU16(aniv, 0);
            size_t frameSize = readU16(aniv, 2);
            UClass* cls = meshClass(settings.levelOfDetail);
            for (UObject* object : objects::all()) {
                if (object && !object->destroyed && object->outer == context.package && object->cls == cls && object->name == Name(settings.meshName)) {
                    object->destroyed = true;
                }
            }
            UMesh* mesh = cast<UMesh>(objects::newObject(cls, context.package, Name(settings.meshName), ObjectFlags));
            mesh->scale = { 1.0f, 1.0f, 1.0f };
            mesh->frameVerts = static_cast<int32_t>(numVertices);
            mesh->animFrames = static_cast<int32_t>(numFrames);
            mesh->andFlags = settings.levelOfDetail ? ~0u : 0u;
            mesh->orFlags = 0;
            std::vector<MeshTri> tris(numPolys);
            int32_t maxTextureIndex = 0;
            for (size_t i = 0; i < numPolys; i++) {
                size_t at = DataHeaderSize + i * 16;
                if (at + 16 > data.size()) {
                    context.log("Warning: MESH IMPORT truncated " + settings.dataFile);
                    break;
                }
                JsTri tri;
                for (size_t k = 0; k < 3; k++) {
                    tri.vertex[k] = readU16(data, at + k * 2);
                    tri.uv[k][0] = data[at + 8 + k * 2];
                    tri.uv[k][1] = data[at + 9 + k * 2];
                }
                tri.type = data[at + 6];
                tri.color = data[at + 7];
                tri.textureNum = data[at + 14];
                tri.flags = data[at + 15];
                if (settings.unmirror) {
                    std::swap(tri.vertex[1], tri.vertex[2]);
                    std::swap(tri.uv[1][0], tri.uv[2][0]);
                    std::swap(tri.uv[1][1], tri.uv[2][1]);
                    if (tri.textureNum == settings.unmirrorTex) {
                        for (auto& uv : tri.uv) {
                            uv[0] = static_cast<uint8_t>(255 - uv[0]);
                        }
                    }
                }
                if (settings.zeroTex) {
                    tri.textureNum = 0;
                }
                MeshTri& out = tris[i];
                for (size_t k = 0; k < 3; k++) {
                    out.vertex[k] = tri.vertex[k];
                    out.uv[k][0] = tri.uv[k][0];
                    out.uv[k][1] = tri.uv[k][1];
                }
                out.textureIndex = tri.textureNum;
                maxTextureIndex = std::max<int32_t>(maxTextureIndex, tri.textureNum);
                while (tri.textureNum >= mesh->textures.size()) {
                    mesh->textures.push_back(nullptr);
                }
                out.polyFlags = polyFlagsFromType(tri.type);
            }
            msvcQsort(tris.data(), tris.size(), compareTris);
            mesh->textureLod.assign(static_cast<size_t>(maxTextureIndex) + 1, legacyMeshTool(context.package) ? 0.0f : 1.0f);
            std::vector<MeshVert> verts(numVertices * numFrames);
            bool wide = numVertices > 0 && frameSize >= numVertices * WideVertexSize;
            size_t vertexSize = wide ? WideVertexSize : VertexSize;
            mesh->wideVerts = wide;
            for (size_t frame = 0; frame < numFrames; frame++) {
                size_t start = 4 + frame * frameSize;
                for (size_t v = 0; v < numVertices; v++) {
                    size_t at = start + v * vertexSize;
                    if (at + vertexSize > aniv.size()) {
                        break;
                    }
                    MeshVert& vert = verts[frame * numVertices + v];
                    if (wide) {
                        vert.x = static_cast<int16_t>(readU16(aniv, at));
                        vert.y = static_cast<int16_t>(readU16(aniv, at + 2));
                        vert.z = static_cast<int16_t>(readU16(aniv, at + 4));
                        if (settings.unmirror) {
                            vert.x = static_cast<int16_t>(-vert.x);
                        }
                        continue;
                    }
                    uint32_t value = static_cast<uint32_t>(readU16(aniv, at)) | (static_cast<uint32_t>(readU16(aniv, at + 2)) << 16);
                    vert.x = static_cast<int16_t>(static_cast<int32_t>(value << 21) >> 21);
                    vert.y = static_cast<int16_t>(static_cast<int32_t>(value << 10) >> 21);
                    vert.z = static_cast<int16_t>(static_cast<int32_t>(value) >> 22);
                    if (settings.unmirror) {
                        vert.x = static_cast<int16_t>(static_cast<int32_t>(packVert(-vert.x, 0, 0) << 21) >> 21);
                    }
                }
            }
            mesh->verts.resize(verts.size());
            for (size_t i = 0; i < verts.size(); i++) {
                mesh->verts[i] = vertexVector(verts[i]);
            }
            if (!settings.levelOfDetail) {
                mesh->connects.assign(numVertices, MeshConnect());
                for (size_t i = 0; i < numVertices; i++) {
                    mesh->connects[i].triangleListOffset = static_cast<int32_t>(mesh->vertLinks.size());
                    for (size_t j = 0; j < tris.size(); j++) {
                        for (uint16_t vertex : tris[j].vertex) {
                            if (vertex == i) {
                                mesh->vertLinks.push_back(static_cast<int32_t>(j));
                                mesh->connects[i].numVertTriangles++;
                            }
                        }
                    }
                }
            }
            mesh->tris = tris;
            buildBounds(mesh, legacyMeshTool(context.package));
            ULodMesh* lod = cast<ULodMesh>(mesh);
            if (!lod) {
                return;
            }
            LodProcessed processed = meshLodProcess(tris, verts, mesh->frameVerts, mesh->animFrames, settings.lod, nullptr);
            lod->tris = processed.tris;
            lod->verts.resize(processed.verts.size());
            for (size_t i = 0; i < processed.verts.size(); i++) {
                lod->verts[i] = vertexVector(processed.verts[i]);
            }
            lod->frameVerts = processed.frameVerts;
            lod->collapsePointThus = processed.collapsePointThus;
            lod->faceLevel = processed.faceLevel;
            lod->faces = processed.faces;
            lod->collapseWedgeThus = processed.collapseWedgeThus;
            lod->wedges = processed.wedges;
            lod->materials = processed.materials;
            lod->specialFaces = processed.specialFaces;
            lod->modelVerts = static_cast<uint32_t>(processed.modelVerts);
            lod->specialVerts = static_cast<uint32_t>(processed.specialVerts);
            lod->oldFrameVerts = legacyMeshTool(context.package) ? static_cast<uint32_t>(processed.frameVerts) : 0;
            lod->lodMinVerts = 10;
            lod->lodStrength = 1.0f;
            lod->lodMorph = 0.3f;
            lod->lodZDisplace = 0.0f;
            lod->lodHysteresis = 0.0f;
        }

        void meshSequence(UMesh* mesh, std::string_view text)
        {
            AnimSequence sequence;
            sequence.rate = 30.0f;
            std::string name;
            std::string group;
            if (!parseString(text, "SEQ=", name) || !parseInt(text, "STARTFRAME=", sequence.startFrame) || !parseInt(text, "NUMFRAMES=", sequence.numFrames)) {
                return;
            }
            sequence.name = Name(name);
            parseFloat(text, "RATE=", sequence.rate);
            if (parseString(text, "GROUP=", group)) {
                sequence.group = Name(group);
            }
            for (AnimSequence& existing : mesh->sequences) {
                if (existing.name == sequence.name) {
                    existing = sequence;
                    return;
                }
            }
            mesh->sequences.push_back(sequence);
        }

        bool meshNotify(UMesh* mesh, std::string_view text)
        {
            std::string sequenceName;
            std::string function;
            AnimNotify notify;
            if (!parseString(text, "SEQ=", sequenceName) || !parseFloat(text, "TIME=", notify.time) || !parseString(text, "FUNCTION=", function)) {
                return false;
            }
            notify.function = Name(function);
            for (AnimSequence& sequence : mesh->sequences) {
                if (sequence.name == Name(sequenceName)) {
                    sequence.notifys.push_back(notify);
                    return true;
                }
            }
            return false;
        }

        void meshLodParams(ULodMesh* mesh, std::string_view text, const LogSink& log)
        {
            int32_t minVerts = static_cast<int32_t>(mesh->lodMinVerts);
            parseInt(text, "MINVERTS=", minVerts);
            parseFloat(text, "STRENGTH=", mesh->lodStrength);
            parseFloat(text, "MORPH=", mesh->lodMorph);
            parseFloat(text, "HYSTERESIS=", mesh->lodHysteresis);
            parseFloat(text, "ZDISP=", mesh->lodZDisplace);
            if (mesh->lodMorph < 0.0f || mesh->lodMorph > 1.0f) {
                mesh->lodMorph = 0.0f;
                log("Warning: Bad LOD MORPH supplied.");
            }
            if (minVerts < 0 || minVerts > mesh->frameVerts) {
                minVerts = std::min(10, mesh->frameVerts);
                log("Warning: Bad LOD MINVERTS supplied.");
            }
            mesh->lodMinVerts = static_cast<uint32_t>(minVerts);
            if (mesh->lodStrength < 0.00001f) {
                mesh->lodStrength = 0.0f;
            }
        }

        bool meshCommand(ExecContext& context, std::string_view text)
        {
            if (parseCommand(text, "IMPORT")) {
                ImportSettings settings;
                if (parseImportSettings(text, settings)) {
                    importMesh(context, settings);
                } else {
                    context.log("Warning: Bad MESH IMPORT");
                }
                return true;
            }
            if (parseCommand(text, "DROPFRAMES")) {
                UMesh* mesh = findMesh(context.package, text, "MESH=");
                int32_t start = 0;
                int32_t count = 0;
                if (mesh && parseInt(text, "STARTFRAME=", start) && parseInt(text, "NUMFRAMES=", count) && start >= 0 && count >= 0 && start + count <= mesh->animFrames) {
                    auto first = mesh->verts.begin() + static_cast<std::ptrdiff_t>(start) * mesh->frameVerts;
                    mesh->verts.erase(first, first + static_cast<std::ptrdiff_t>(count) * mesh->frameVerts);
                    mesh->animFrames -= count;
                }
                return true;
            }
            if (parseCommand(text, "LODPARAMS")) {
                UMesh* mesh = findMesh(context.package, text, "MESH=");
                if (!mesh) {
                    context.log("Warning: Bad MESH LODPARAMS");
                } else if (ULodMesh* lod = cast<ULodMesh>(mesh)) {
                    meshLodParams(lod, text, context.log);
                } else {
                    context.log("Warning: Need a LOD mesh (MLOD=1) for these LODPARAMS.");
                }
                return true;
            }
            if (parseCommand(text, "ORIGIN")) {
                UMesh* mesh = findMesh(context.package, text, "MESH=");
                if (mesh) {
                    getVector(text, mesh->origin);
                    getRotator(text, mesh->rotOrigin, 256);
                } else {
                    context.log("Warning: Bad MESH ORIGIN");
                }
                return true;
            }
            if (parseCommand(text, "BOUNDINGBOX")) {
                UMesh* mesh = findMesh(context.package, text, "MESH=");
                if (mesh) {
                    parseFloat(text, "XMIN=", mesh->meshBounds.min.x);
                    parseFloat(text, "YMIN=", mesh->meshBounds.min.y);
                    parseFloat(text, "ZMIN=", mesh->meshBounds.min.z);
                    parseFloat(text, "XMAX=", mesh->meshBounds.max.x);
                    parseFloat(text, "YMAX=", mesh->meshBounds.max.y);
                    parseFloat(text, "ZMAX=", mesh->meshBounds.max.z);
                    mesh->bounds = mesh->meshBounds;
                } else {
                    context.log("Warning: Bad MESH Bounding Box");
                }
                return true;
            }
            if (parseCommand(text, "SEQUENCE")) {
                UMesh* mesh = findMesh(context.package, text, "MESH=");
                if (mesh) {
                    meshSequence(mesh, text);
                } else {
                    context.log("Warning: Bad MESH SEQUENCE");
                }
                return true;
            }
            if (parseCommand(text, "NOTIFY")) {
                UMesh* mesh = findMesh(context.package, text, "MESH=");
                if (!mesh || !meshNotify(mesh, text)) {
                    context.log("Warning: Bad MESH NOTIFY");
                }
                return true;
            }
            return false;
        }

        bool meshMapCommand(ExecContext& context, std::string_view text)
        {
            if (parseCommand(text, "NEW")) {
                return true;
            }
            if (parseCommand(text, "SCALE")) {
                UMesh* mesh = findMesh(context.package, text, "MESHMAP=");
                if (!mesh) {
                    context.log("Warning: Missing meshmap");
                    return true;
                }
                Vector scale;
                getVector(text, scale);
                setScale(mesh, scale);
                if (legacyMeshTool(context.package)) {
                    computeTextureLod(mesh);
                }
                return true;
            }
            if (parseCommand(text, "SETTEXTURE")) {
                UMesh* mesh = findMesh(context.package, text, "MESHMAP=");
                UObject* texture = findTexture(text);
                int32_t number = 0;
                if (!mesh || !texture || !parseInt(text, "NUM=", number) || number < 0 || static_cast<size_t>(number) >= mesh->textures.size()) {
                    context.log("Warning: Missing meshmap, texture, or num (" + std::string(text) + ")");
                    return true;
                }
                mesh->textures[static_cast<size_t>(number)] = texture;
                float textureLod = 1.0f;
                parseFloat(text, "TLOD=", textureLod);
                if (static_cast<size_t>(number) < mesh->textureLod.size()) {
                    mesh->textureLod[static_cast<size_t>(number)] *= textureLod;
                }
                return true;
            }
            return false;
        }

        bool importCommand(ExecContext& context, const std::string& command)
        {
            std::string_view text = command;
            if (parseCommand(text, "MESHMAP")) {
                return meshMapCommand(context, text);
            }
            text = command;
            if (parseCommand(text, "MESH")) {
                return meshCommand(context, text);
            }
            return false;
        }

        void writeBox(Writer& out, const BoundingBox& box)
        {
            out.f32(box.min.x);
            out.f32(box.min.y);
            out.f32(box.min.z);
            out.f32(box.max.x);
            out.f32(box.max.y);
            out.f32(box.max.z);
            out.u8(box.valid ? 1 : 0);
        }

        void writeSphere(Writer& out, const Plane& sphere)
        {
            out.f32(sphere.x);
            out.f32(sphere.y);
            out.f32(sphere.z);
            out.f32(sphere.w);
        }

        void writeWords(Writer& out, const std::vector<uint16_t>& values)
        {
            out.index(static_cast<int32_t>(values.size()));
            for (uint16_t value : values) {
                out.i16(static_cast<int16_t>(value));
            }
        }

        void writeFaces(Writer& out, const std::vector<MeshFace>& faces)
        {
            out.index(static_cast<int32_t>(faces.size()));
            for (const MeshFace& face : faces) {
                for (uint16_t wedge : face.wedge) {
                    out.i16(static_cast<int16_t>(wedge));
                }
                out.i16(static_cast<int16_t>(face.material));
            }
        }

        bool serializeMesh(PackageWriter& package, UObject* object, Writer& out)
        {
            UMesh* mesh = cast<UMesh>(object);
            if (!mesh) {
                return false;
            }
            ULodMesh* lod = cast<ULodMesh>(mesh);
            writeBox(out, mesh->bounds);
            writeSphere(out, mesh->sphere);
            size_t skip = package.beginLazyArray(out);
            out.index(static_cast<int32_t>(mesh->verts.size()));
            for (const Vector& vertex : mesh->verts) {
                MeshVert vert = meshVert(vertex);
                if (mesh->wideVerts) {
                    out.i16(vert.x);
                    out.i16(vert.y);
                    out.i16(vert.z);
                    out.i16(0);
                    continue;
                }
                out.u32(packVert(vert.x, vert.y, vert.z));
            }
            package.endLazyArray(out, skip);
            skip = package.beginLazyArray(out);
            if (lod) {
                out.index(0);
            } else {
                out.index(static_cast<int32_t>(mesh->tris.size()));
                for (const MeshTri& tri : mesh->tris) {
                    for (uint16_t vertex : tri.vertex) {
                        out.i16(static_cast<int16_t>(vertex));
                    }
                    for (const auto& uv : tri.uv) {
                        out.u8(uv[0]);
                        out.u8(uv[1]);
                    }
                    out.u32(tri.polyFlags);
                    out.i32(tri.textureIndex);
                }
            }
            package.endLazyArray(out, skip);
            out.index(static_cast<int32_t>(mesh->sequences.size()));
            for (const AnimSequence& sequence : mesh->sequences) {
                package.writeName(out, sequence.name);
                package.writeName(out, sequence.group);
                out.i32(sequence.startFrame);
                out.i32(sequence.numFrames);
                out.index(static_cast<int32_t>(sequence.notifys.size()));
                for (const AnimNotify& notify : sequence.notifys) {
                    out.f32(notify.time);
                    package.writeName(out, notify.function);
                }
                out.f32(sequence.rate);
            }
            skip = package.beginLazyArray(out);
            out.index(static_cast<int32_t>(mesh->connects.size()));
            for (const MeshConnect& connect : mesh->connects) {
                out.i32(connect.numVertTriangles);
                out.i32(connect.triangleListOffset);
            }
            package.endLazyArray(out, skip);
            writeBox(out, mesh->meshBounds);
            writeSphere(out, mesh->meshSphere);
            skip = package.beginLazyArray(out);
            out.index(static_cast<int32_t>(mesh->vertLinks.size()));
            for (int32_t link : mesh->vertLinks) {
                out.i32(link);
            }
            package.endLazyArray(out, skip);
            out.index(static_cast<int32_t>(mesh->textures.size()));
            for (UObject* texture : mesh->textures) {
                package.writeObject(out, texture);
            }
            out.index(static_cast<int32_t>(mesh->frameBounds.size()));
            for (const BoundingBox& box : mesh->frameBounds) {
                writeBox(out, box);
            }
            out.index(static_cast<int32_t>(mesh->frameSpheres.size()));
            for (const Plane& sphere : mesh->frameSpheres) {
                writeSphere(out, sphere);
            }
            out.i32(mesh->frameVerts);
            out.i32(mesh->animFrames);
            out.u32(mesh->andFlags);
            out.u32(mesh->orFlags);
            out.f32(mesh->scale.x);
            out.f32(mesh->scale.y);
            out.f32(mesh->scale.z);
            out.f32(mesh->origin.x);
            out.f32(mesh->origin.y);
            out.f32(mesh->origin.z);
            out.i32(mesh->rotOrigin.pitch);
            out.i32(mesh->rotOrigin.yaw);
            out.i32(mesh->rotOrigin.roll);
            out.i32(mesh->curPoly);
            out.i32(mesh->curVertex);
            out.index(static_cast<int32_t>(mesh->textureLod.size()));
            for (float value : mesh->textureLod) {
                out.f32(value);
            }
            if (!lod) {
                return true;
            }
            writeWords(out, lod->collapsePointThus);
            writeWords(out, lod->faceLevel);
            writeFaces(out, lod->faces);
            writeWords(out, lod->collapseWedgeThus);
            out.index(static_cast<int32_t>(lod->wedges.size()));
            for (const MeshWedge& wedge : lod->wedges) {
                out.i16(static_cast<int16_t>(wedge.vertex));
                out.u8(wedge.u);
                out.u8(wedge.v);
            }
            out.index(static_cast<int32_t>(lod->materials.size()));
            for (const MeshMaterial& material : lod->materials) {
                out.u32(material.polyFlags);
                out.i32(material.textureIndex);
            }
            writeFaces(out, lod->specialFaces);
            out.u32(lod->modelVerts);
            out.u32(lod->specialVerts);
            out.f32(lod->meshScaleMax);
            out.f32(lod->lodHysteresis);
            out.f32(lod->lodStrength);
            out.u32(lod->lodMinVerts);
            out.f32(lod->lodMorph);
            out.f32(lod->lodZDisplace);
            writeWords(out, lod->remapAnimVerts);
            out.u32(lod->oldFrameVerts);
            return true;
        }

        struct SourceMesh {
            ImportSettings settings;
            UObject* package = nullptr;
            std::filesystem::path directory;
            ULodMesh* mesh = nullptr;
            std::vector<MeshTri> tris;
            std::vector<MeshVert> verts;
            std::vector<int32_t> toCanonical;
        };

        std::string sourceKey(const std::filesystem::path& directory, std::string file)
        {
            std::replace(file.begin(), file.end(), '\\', '/');
            std::string key = (directory / file).lexically_normal().string();
            std::transform(key.begin(), key.end(), key.begin(), [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });
            return key;
        }

        std::vector<ImportSettings> packageImports(UObject* package)
        {
            std::vector<ImportSettings> result;
            if (Linker* linker = objects::findLinker(package->name)) {
                for (size_t i = 0; i < linker->file().exports().size(); i++) {
                    int32_t ref = static_cast<int32_t>(i + 1);
                    if (linker->file().classOf(ref) == "Class") {
                        linker->object(ref);
                    }
                }
            }
            for (UObject* object : objects::all()) {
                UClass* cls = object && !object->destroyed ? cast<UClass>(object) : nullptr;
                if (!cls || cls->outermost() != package || !cls->scriptText) {
                    continue;
                }
                objects::ensureLoaded(cls->scriptText);
                std::istringstream lines(cls->scriptText->text);
                std::string line;
                while (std::getline(lines, line)) {
                    std::string_view text = line;
                    ImportSettings settings;
                    if (parseCommand(text, "#exec") && parseCommand(text, "MESH") && parseCommand(text, "IMPORT") && parseImportSettings(text, settings)) {
                        result.push_back(settings);
                    }
                }
            }
            return result;
        }

        std::vector<std::pair<int32_t, int32_t>> droppedFrames(UObject* package, const std::string& meshName)
        {
            std::vector<std::pair<int32_t, int32_t>> result;
            for (UObject* object : objects::all()) {
                UClass* cls = object && !object->destroyed ? cast<UClass>(object) : nullptr;
                if (!cls || cls->outermost() != package || !cls->scriptText) {
                    continue;
                }
                objects::ensureLoaded(cls->scriptText);
                std::istringstream lines(cls->scriptText->text);
                std::string line;
                while (std::getline(lines, line)) {
                    std::string_view text = line;
                    std::string name;
                    int32_t start = 0;
                    int32_t count = 0;
                    if (parseCommand(text, "#exec") && parseCommand(text, "MESH") && parseCommand(text, "DROPFRAMES") && parseString(text, "MESH=", name) && iequals(name, meshName) && parseInt(text, "STARTFRAME=", start) && parseInt(text, "NUMFRAMES=", count)) {
                        result.emplace_back(start, count);
                    }
                }
            }
            return result;
        }

        float sphereRadius(double distance, bool legacy)
        {
            float radius = static_cast<float>(distance);
            return static_cast<float>(std::sqrt(static_cast<double>(radius)) * (legacy ? 1.001 : static_cast<double>(1.001f)));
        }

        bool standInFrame(const BoundingBox& box, const Plane& sphere, const BoundingBox& whole, const Plane& wholeSphere, bool legacy, size_t count, std::vector<MeshVert>& frame)
        {
            float gx = (whole.min.x + whole.max.x) * 0.5f;
            float gy = (whole.min.y + whole.max.y) * 0.5f;
            float gz = (whole.min.z + whole.max.z) * 0.5f;
            auto inside = [&](int32_t x, int32_t y, int32_t z) {
                double dx = static_cast<double>(gx) - x;
                double dy = static_cast<double>(gy) - y;
                double dz = static_cast<double>(gz) - z;
                double result = dy * dy + dx * dx;
                return sphereRadius(result + dz * dz, legacy) <= wholeSphere.w;
            };
            float cx = (box.min.x + box.max.x) * 0.5f;
            float cy = (box.min.y + box.max.y) * 0.5f;
            float cz = (box.min.z + box.max.z) * 0.5f;
            auto distance = [&](int32_t x, int32_t y, int32_t z) {
                double dx = static_cast<double>(cx) - x;
                double dy = static_cast<double>(cy) - y;
                double dz = static_cast<double>(cz) - z;
                double result = dy * dy + dx * dx;
                return result + dz * dz;
            };
            int32_t x0 = static_cast<int32_t>(box.min.x);
            int32_t x1 = static_cast<int32_t>(box.max.x);
            int32_t y0 = static_cast<int32_t>(box.min.y);
            int32_t y1 = static_cast<int32_t>(box.max.y);
            int32_t z0 = static_cast<int32_t>(box.min.z);
            int32_t z1 = static_cast<int32_t>(box.max.z);
            int32_t mx = static_cast<int32_t>(std::lround(cx));
            int32_t my = static_cast<int32_t>(std::lround(cy));
            int32_t mz = static_cast<int32_t>(std::lround(cz));
            std::array<int32_t, 3> low = { x0, y0, z0 };
            std::array<int32_t, 3> high = { x1, y1, z1 };
            std::array<std::array<int32_t, 2>, 3> free = { {
                { mx, std::clamp(static_cast<int32_t>(std::lround(gx)), x0, x1) },
                { my, std::clamp(static_cast<int32_t>(std::lround(gy)), y0, y1) },
                { mz, std::clamp(static_cast<int32_t>(std::lround(gz)), z0, z1) },
            } };
            std::vector<std::array<int32_t, 3>> points;
            double reach = 0;
            for (size_t axis = 0; axis < 3; axis++) {
                for (int32_t edge : { low[axis], high[axis] }) {
                    std::optional<std::array<int32_t, 3>> best;
                    double bestDistance = 0;
                    for (size_t a = 0; a < 2; a++) {
                        for (size_t b = 0; b < 2; b++) {
                            std::array<int32_t, 3> point = { free[0][a], free[1][a], free[2][b] };
                            point[axis] = edge;
                            if (axis == 2) {
                                point[1] = free[1][b];
                            }
                            double away = distance(point[0], point[1], point[2]);
                            if (inside(point[0], point[1], point[2]) && (!best || away < bestDistance)) {
                                best = point;
                                bestDistance = away;
                            }
                        }
                    }
                    if (!best) {
                        return false;
                    }
                    points.push_back(*best);
                    reach = std::max(reach, bestDistance);
                }
            }
            double target = static_cast<double>(sphere.w) / (legacy ? 1.001 : static_cast<double>(1.001f));
            for (int32_t x = x0; x <= x1; x++) {
                for (int32_t y = y0; y <= y1; y++) {
                    double rest = target * target - (x - static_cast<double>(cx)) * (x - static_cast<double>(cx)) - (y - static_cast<double>(cy)) * (y - static_cast<double>(cy));
                    if (rest < 0) {
                        continue;
                    }
                    double offset = std::sqrt(rest);
                    for (double guess : { static_cast<double>(cz) + offset, static_cast<double>(cz) - offset }) {
                        for (int32_t z : { static_cast<int32_t>(std::floor(guess)), static_cast<int32_t>(std::ceil(guess)) }) {
                            if (z < z0 || z > z1) {
                                continue;
                            }
                            double far = distance(x, y, z);
                            if (far < reach || sphereRadius(far, legacy) != sphere.w || !inside(x, y, z)) {
                                continue;
                            }
                            points.push_back({ x, y, z });
                            frame.assign(count, MeshVert { static_cast<int16_t>(mx), static_cast<int16_t>(my), static_cast<int16_t>(mz), 0 });
                            for (size_t i = 0; i < points.size() && i < count; i++) {
                                frame[i] = MeshVert { static_cast<int16_t>(points[i][0]), static_cast<int16_t>(points[i][1]), static_cast<int16_t>(points[i][2]), 0 };
                            }
                            return count >= points.size();
                        }
                    }
                }
            }
            return false;
        }

        bool restoreDroppedFrames(SourceMesh& base, std::vector<SourceMesh>& group, const LogSink& log)
        {
            std::vector<std::pair<int32_t, int32_t>> drops = droppedFrames(base.package, base.settings.meshName);
            if (drops.empty()) {
                return true;
            }
            size_t count = static_cast<size_t>(base.mesh->frameVerts);
            std::vector<int32_t> original(static_cast<size_t>(base.mesh->animFrames));
            for (size_t i = 0; i < original.size(); i++) {
                original[i] = -1 - static_cast<int32_t>(i);
            }
            for (auto drop = drops.rbegin(); drop != drops.rend(); ++drop) {
                original.insert(original.begin() + drop->first, static_cast<size_t>(drop->second), 0);
            }
            bool legacy = legacyMeshTool(base.package);
            for (SourceMesh& source : group) {
                bool mirrored = source.settings.unmirror != base.settings.unmirror;
                std::vector<MeshVert> restored;
                for (size_t frame = 0; frame < original.size(); frame++) {
                    if (original[frame] < 0) {
                        size_t kept = static_cast<size_t>(-1 - original[frame]);
                        restored.insert(restored.end(), source.verts.begin() + static_cast<std::ptrdiff_t>(kept * count), source.verts.begin() + static_cast<std::ptrdiff_t>((kept + 1) * count));
                        continue;
                    }
                    if (frame >= base.mesh->frameBounds.size() || frame >= base.mesh->frameSpheres.size()) {
                        log("Warning: " + base.settings.meshName + " has no bounds for dropped frame " + std::to_string(frame));
                        return false;
                    }
                    std::vector<MeshVert> standIn;
                    if (!standInFrame(base.mesh->frameBounds[frame], base.mesh->frameSpheres[frame], base.mesh->meshBounds, base.mesh->meshSphere, legacy, count, standIn)) {
                        log("Warning: " + base.settings.meshName + " dropped frame " + std::to_string(frame) + " has no matching stand-in");
                        return false;
                    }
                    for (size_t v = 0; v < count; v++) {
                        MeshVert vert = standIn[static_cast<size_t>(source.toCanonical[v])];
                        if (mirrored) {
                            vert.x = static_cast<int16_t>(-vert.x);
                        }
                        restored.push_back(vert);
                    }
                }
                source.verts = restored;
            }
            return true;
        }

        std::vector<MeshTri> lodTris(const ULodMesh* mesh)
        {
            std::vector<MeshTri> tris;
            uint16_t special = static_cast<uint16_t>(mesh->specialVerts);
            for (const MeshFace& face : mesh->faces) {
                MeshTri tri;
                for (size_t k = 0; k < 3; k++) {
                    const MeshWedge& wedge = mesh->wedges[face.wedge[k]];
                    tri.vertex[k] = static_cast<uint16_t>(special + wedge.vertex);
                    tri.uv[k][0] = wedge.u;
                    tri.uv[k][1] = wedge.v;
                }
                tri.polyFlags = mesh->materials[face.material].polyFlags;
                tri.textureIndex = mesh->materials[face.material].textureIndex;
                tris.push_back(tri);
            }
            int32_t specialTexture = mesh->textures.empty() ? 0 : static_cast<int32_t>(mesh->textures.size()) - 1;
            for (const MeshTri& tri : tris) {
                if (tri.textureIndex >= specialTexture) {
                    specialTexture = 0;
                }
            }
            for (size_t k = mesh->specialFaces.size(); k-- > 0;) {
                MeshTri tri;
                for (size_t j = 0; j < 3; j++) {
                    tri.vertex[j] = mesh->specialFaces[k].wedge[j];
                }
                tri.polyFlags = PolyTwoSided | PolyInvisible;
                tri.textureIndex = specialTexture;
                tris.push_back(tri);
            }
            std::stable_sort(tris.begin(), tris.end(), [](const MeshTri& a, const MeshTri& b) {
                return compareTris(a, b) < 0;
            });
            return tris;
        }

        std::vector<MeshVert> meshVerts(const UMesh* mesh)
        {
            std::vector<MeshVert> verts;
            for (const Vector& vertex : mesh->verts) {
                verts.push_back(meshVert(vertex));
            }
            return verts;
        }

        bool faceTextures(const ULodMesh* base, const ULodMesh* other, std::vector<int32_t>& textures)
        {
            if (!base->specialFaces.empty()) {
                return false;
            }
            std::map<std::array<uint8_t, 6>, int32_t> byUv;
            auto key = [](const ULodMesh* mesh, const MeshFace& face) {
                std::array<uint8_t, 6> result {};
                for (size_t k = 0; k < 3; k++) {
                    result[k * 2] = mesh->wedges[face.wedge[k]].u;
                    result[k * 2 + 1] = mesh->wedges[face.wedge[k]].v;
                }
                return result;
            };
            for (const MeshFace& face : other->faces) {
                int32_t texture = other->materials[face.material].textureIndex;
                auto [it, inserted] = byUv.emplace(key(other, face), texture);
                if (!inserted && it->second != texture) {
                    return false;
                }
            }
            textures.clear();
            for (const MeshFace& face : base->faces) {
                auto found = byUv.find(key(base, face));
                if (found == byUv.end()) {
                    return false;
                }
                textures.push_back(found->second);
            }
            return true;
        }

        std::vector<size_t> matchingFaces(const ULodMesh* base, const std::vector<int32_t>& textures)
        {
            std::vector<MeshTri> sorted;
            for (const MeshFace& face : base->faces) {
                MeshTri tri;
                tri.polyFlags = base->materials[face.material].polyFlags;
                tri.textureIndex = base->materials[face.material].textureIndex;
                sorted.push_back(tri);
            }
            std::vector<int32_t> slots(sorted.size());
            for (size_t i = 0; i < slots.size(); i++) {
                slots[i] = static_cast<int32_t>(i);
            }
            msvcQsort(slots.data(), slots.size(), [&](int32_t a, int32_t b) {
                return compareTris(sorted[static_cast<size_t>(a)], sorted[static_cast<size_t>(b)]);
            });
            std::vector<MeshTri> input(sorted.size());
            std::vector<size_t> inputToBase(sorted.size());
            for (size_t i = 0; i < slots.size(); i++) {
                size_t slot = static_cast<size_t>(slots[i]);
                input[slot] = sorted[i];
                input[slot].textureIndex = textures[i];
                inputToBase[slot] = i;
            }
            std::vector<int32_t> order(input.size());
            for (size_t i = 0; i < order.size(); i++) {
                order[i] = static_cast<int32_t>(i);
            }
            msvcQsort(order.data(), order.size(), [&](int32_t a, int32_t b) {
                return compareTris(input[static_cast<size_t>(a)], input[static_cast<size_t>(b)]);
            });
            std::vector<size_t> result(order.size());
            for (size_t i = 0; i < order.size(); i++) {
                result[i] = inputToBase[static_cast<size_t>(order[i])];
            }
            return result;
        }

        bool mapToCanonical(const SourceMesh& base, SourceMesh& other, const std::vector<int32_t>& textures)
        {
            const ULodMesh* a = base.mesh;
            const ULodMesh* b = other.mesh;
            if (a->faces.size() != b->faces.size() || a->specialFaces.size() != b->specialFaces.size() || a->frameVerts != b->frameVerts || a->specialVerts != b->specialVerts) {
                return false;
            }
            std::vector<size_t> faceOf = !textures.empty() && !other.settings.zeroTex ? matchingFaces(a, textures) : std::vector<size_t>();
            bool mirrored = base.settings.unmirror != other.settings.unmirror;
            other.toCanonical.assign(static_cast<size_t>(b->frameVerts), -1);
            for (size_t f = 0; f < a->faces.size(); f++) {
                const MeshFace& baseFace = a->faces[faceOf.empty() ? f : faceOf[f]];
                for (size_t k = 0; k < 3; k++) {
                    size_t corner = mirrored && k > 0 ? 3 - k : k;
                    const MeshWedge& wedge = b->wedges[b->faces[f].wedge[corner]];
                    const MeshWedge& baseWedge = a->wedges[baseFace.wedge[k]];
                    bool sameU = wedge.u == baseWedge.u || (mirrored && wedge.u == 255 - baseWedge.u);
                    if (!sameU || wedge.v != baseWedge.v) {
                        return false;
                    }
                    size_t from = b->specialVerts + wedge.vertex;
                    int32_t to = static_cast<int32_t>(a->specialVerts + baseWedge.vertex);
                    if (other.toCanonical[from] != -1 && other.toCanonical[from] != to) {
                        return false;
                    }
                    other.toCanonical[from] = to;
                }
            }
            for (size_t f = 0; f < a->specialFaces.size(); f++) {
                for (size_t k = 0; k < 3; k++) {
                    size_t corner = mirrored && k > 0 ? 3 - k : k;
                    other.toCanonical[b->specialFaces[f].wedge[corner]] = a->specialFaces[f].wedge[k];
                }
            }
            std::vector<bool> used(static_cast<size_t>(a->frameVerts), false);
            for (int32_t to : other.toCanonical) {
                if (to >= 0) {
                    used[static_cast<size_t>(to)] = true;
                }
            }
            size_t next = 0;
            for (int32_t& to : other.toCanonical) {
                if (to >= 0) {
                    continue;
                }
                while (next < used.size() && used[next]) {
                    next++;
                }
                if (next >= used.size()) {
                    return false;
                }
                to = static_cast<int32_t>(next);
                used[next] = true;
            }
            return true;
        }

        std::vector<int32_t> constrainedOrder(int32_t count, int32_t first, const std::vector<std::pair<int32_t, int32_t>>& precedes)
        {
            std::vector<std::set<int32_t>> after(static_cast<size_t>(count));
            std::vector<int32_t> incoming(static_cast<size_t>(count), 0);
            for (const auto& [a, b] : precedes) {
                if (after[static_cast<size_t>(a)].insert(b).second) {
                    incoming[static_cast<size_t>(b)]++;
                }
            }
            std::set<int32_t> ready;
            for (int32_t v = first; v < count; v++) {
                if (incoming[static_cast<size_t>(v)] == 0) {
                    ready.insert(v);
                }
            }
            std::vector<int32_t> order;
            while (!ready.empty()) {
                int32_t v = *ready.begin();
                ready.erase(ready.begin());
                order.push_back(v);
                for (int32_t w : after[static_cast<size_t>(v)]) {
                    if (--incoming[static_cast<size_t>(w)] == 0) {
                        ready.insert(w);
                    }
                }
            }
            return order;
        }

        std::vector<uint8_t> dataFileBytes(const std::vector<MeshTri>& sorted, const std::vector<int32_t>& textures, const std::vector<int32_t>& newIndex, size_t vertexCount, const ImportSettings& settings, bool& ok)
        {
            std::vector<int32_t> slots(sorted.size());
            for (size_t i = 0; i < slots.size(); i++) {
                slots[i] = static_cast<int32_t>(i);
            }
            msvcQsort(slots.data(), slots.size(), [&](int32_t a, int32_t b) {
                return compareTris(sorted[static_cast<size_t>(a)], sorted[static_cast<size_t>(b)]);
            });
            std::vector<MeshTri> input(sorted.size());
            for (size_t i = 0; i < slots.size(); i++) {
                input[static_cast<size_t>(slots[i])] = sorted[i];
                if (!textures.empty()) {
                    input[static_cast<size_t>(slots[i])].textureIndex = textures[i];
                }
            }
            std::vector<uint8_t> data;
            putU16(data, static_cast<uint16_t>(input.size()));
            putU16(data, static_cast<uint16_t>(vertexCount));
            data.resize(DataHeaderSize, 0);
            for (MeshTri tri : input) {
                if (settings.unmirror) {
                    std::swap(tri.vertex[1], tri.vertex[2]);
                    std::swap(tri.uv[1], tri.uv[2]);
                    if (tri.textureIndex == settings.unmirrorTex) {
                        for (auto& uv : tri.uv) {
                            uv[0] = static_cast<uint8_t>(255 - uv[0]);
                        }
                    }
                }
                for (uint16_t vertex : tri.vertex) {
                    putU16(data, static_cast<uint16_t>(newIndex[vertex]));
                }
                uint8_t type = 0;
                if (!typeFromPolyFlags(tri.polyFlags, type)) {
                    ok = false;
                }
                data.push_back(type);
                data.push_back(0);
                for (const auto& uv : tri.uv) {
                    data.push_back(uv[0]);
                    data.push_back(uv[1]);
                }
                data.push_back(static_cast<uint8_t>(tri.textureIndex));
                data.push_back(0);
            }
            return data;
        }

        std::vector<uint8_t> anivFileBytes(const std::vector<MeshVert>& verts, size_t frameVerts, size_t animFrames, const std::vector<int32_t>& sourceOf, bool unmirror, bool wide)
        {
            std::vector<uint8_t> data;
            putU16(data, static_cast<uint16_t>(animFrames));
            putU16(data, static_cast<uint16_t>(frameVerts * (wide ? WideVertexSize : VertexSize)));
            for (size_t frame = 0; frame < animFrames; frame++) {
                for (size_t v = 0; v < frameVerts; v++) {
                    const MeshVert& vert = verts[frame * frameVerts + static_cast<size_t>(sourceOf[v])];
                    if (wide) {
                        putU16(data, static_cast<uint16_t>(unmirror ? -vert.x : vert.x));
                        putU16(data, static_cast<uint16_t>(vert.y));
                        putU16(data, static_cast<uint16_t>(vert.z));
                        putU16(data, 0);
                        continue;
                    }
                    uint32_t value = packVert(unmirror ? -vert.x : vert.x, vert.y, vert.z);
                    putU16(data, static_cast<uint16_t>(value & 0xffff));
                    putU16(data, static_cast<uint16_t>(value >> 16));
                }
            }
            return data;
        }

        bool exportPlainMesh(ExecContext& context, const ImportSettings& settings, UMesh* mesh)
        {
            std::vector<int32_t> identity(static_cast<size_t>(mesh->frameVerts));
            for (size_t i = 0; i < identity.size(); i++) {
                identity[i] = static_cast<int32_t>(i);
            }
            bool ok = true;
            std::vector<uint8_t> data = dataFileBytes(mesh->tris, {}, identity, identity.size(), settings, ok);
            std::vector<uint8_t> aniv = anivFileBytes(meshVerts(mesh), identity.size(), static_cast<size_t>(mesh->animFrames), identity, settings.unmirror, mesh->wideVerts);
            return ok && writeFile(resolveFile(context.directory, settings.dataFile), data) && writeFile(resolveFile(context.directory, settings.anivFile), aniv);
        }

        ULodMesh* scratchImport(const SourceMesh& source)
        {
            ExecContext check;
            check.package = objects::packageObject(Name("OpenUTExportCheck"));
            check.directory = source.directory;
            check.log = [](const std::string&) {
            };
            importMesh(check, source.settings);
            ULodMesh* imported = nullptr;
            for (UObject* object : objects::all()) {
                ULodMesh* mesh = object && !object->destroyed ? cast<ULodMesh>(object) : nullptr;
                if (mesh && mesh->outer == check.package && iequals(mesh->name.str(), source.settings.meshName)) {
                    imported = mesh;
                }
            }
            if (imported) {
                imported->destroyed = true;
            }
            return imported;
        }

        std::vector<MeshTri> removedTris(const SourceMesh& source, const ULodMesh* imported)
        {
            std::vector<MeshTri> result;
            std::multiset<std::tuple<uint16_t, uint8_t, uint8_t>> produced;
            for (const MeshWedge& wedge : imported->wedges) {
                produced.insert({ wedge.vertex, wedge.u, wedge.v });
            }
            std::vector<std::vector<size_t>> runs;
            for (size_t w = 0; w < source.mesh->wedges.size(); w++) {
                const MeshWedge& wedge = source.mesh->wedges[w];
                auto found = produced.find({ wedge.vertex, wedge.u, wedge.v });
                if (found != produced.end()) {
                    produced.erase(found);
                    continue;
                }
                if (runs.empty() || runs.back().back() + 1 != w) {
                    runs.emplace_back();
                }
                runs.back().push_back(w);
            }
            std::vector<bool> used(source.mesh->materials.size(), false);
            for (const MeshFace& face : source.mesh->faces) {
                used[face.material] = true;
            }
            std::vector<size_t> unused;
            for (size_t m = 0; m < used.size(); m++) {
                if (!used[m]) {
                    unused.push_back(m);
                }
            }
            if (runs.size() != unused.size()) {
                return result;
            }
            uint16_t special = static_cast<uint16_t>(source.mesh->specialVerts);
            for (size_t i = 0; i < runs.size(); i++) {
                const std::vector<size_t>& run = runs[i];
                if (run.size() > 3) {
                    return {};
                }
                MeshTri tri;
                for (size_t k = 0; k < 3; k++) {
                    const MeshWedge& wedge = source.mesh->wedges[run[std::min(k, run.size() - 1)]];
                    tri.vertex[k] = static_cast<uint16_t>(special + wedge.vertex);
                    tri.uv[k][0] = wedge.u;
                    tri.uv[k][1] = wedge.v;
                }
                tri.polyFlags = source.mesh->materials[unused[i]].polyFlags;
                tri.textureIndex = source.mesh->materials[unused[i]].textureIndex;
                result.push_back(tri);
            }
            return result;
        }

        std::optional<std::pair<int32_t, int32_t>> importDivergence(const SourceMesh& source, const ULodMesh* imported)
        {
            std::optional<std::pair<int32_t, int32_t>> result;
            if (!imported) {
                return result;
            }
            size_t frames = static_cast<size_t>(source.mesh->animFrames);
            size_t count = static_cast<size_t>(source.mesh->frameVerts);
            if (imported->frameVerts != source.mesh->frameVerts || imported->verts.size() != source.mesh->verts.size()) {
                return result;
            }
            auto track = [frames](const ULodMesh* mesh, size_t vertex) {
                std::vector<float> values;
                for (size_t frame = 0; frame < frames; frame++) {
                    const Vector& point = mesh->verts[frame * static_cast<size_t>(mesh->frameVerts) + vertex];
                    values.insert(values.end(), { point.x, point.y, point.z });
                }
                return values;
            };
            std::map<std::vector<float>, size_t> expected;
            for (size_t v = 0; v < count; v++) {
                expected.emplace(track(source.mesh, v), v);
            }
            for (size_t p = count; p-- > 0;) {
                std::vector<float> got = track(imported, p);
                if (got == track(source.mesh, p)) {
                    continue;
                }
                auto actual = expected.find(got);
                if (actual == expected.end()) {
                    return result;
                }
                result = std::make_pair(source.toCanonical[p], source.toCanonical[actual->second]);
                return result;
            }
            return result;
        }

        bool exportLodGroup(ExecContext& context, std::vector<SourceMesh>& group)
        {
            std::stable_partition(group.begin(), group.end(), [](const SourceMesh& source) {
                return source.settings.zeroTex;
            });
            SourceMesh& base = group.front();
            std::vector<int32_t> textures;
            for (const SourceMesh& source : group) {
                if (base.settings.zeroTex && !source.settings.zeroTex && textures.empty() && !faceTextures(base.mesh, source.mesh, textures)) {
                    context.log("Warning: meshes sharing " + source.settings.dataFile + " do not share texture coordinates");
                    return false;
                }
            }
            int32_t vertexCount = base.mesh->frameVerts;
            int32_t special = static_cast<int32_t>(base.mesh->specialVerts);
            std::vector<std::pair<int32_t, int32_t>> precedes;
            for (SourceMesh& source : group) {
                if (&source == &base) {
                    source.toCanonical.resize(static_cast<size_t>(vertexCount));
                    for (size_t i = 0; i < source.toCanonical.size(); i++) {
                        source.toCanonical[i] = static_cast<int32_t>(i);
                    }
                } else if (!mapToCanonical(base, source, textures)) {
                    context.log("Warning: meshes sharing " + source.settings.dataFile + " do not share triangles");
                    return false;
                }
                std::vector<int32_t> forced;
                int32_t modelVerts = static_cast<int32_t>(source.mesh->modelVerts);
                int32_t sourceSpecial = static_cast<int32_t>(source.mesh->specialVerts);
                for (int32_t step = 0; step < modelVerts; step++) {
                    forced.push_back(sourceSpecial + modelVerts - 1 - step);
                }
                LodTrace trace;
                trace.forcedOrder = &forced;
                meshLodProcess(lodTris(source.mesh), source.verts, source.mesh->frameVerts, source.mesh->animFrames, source.settings.lod, &trace);
                if (trace.mismatches) {
                    context.log("Warning: " + source.settings.meshName + " LOD collapse order not reproducible (" + std::to_string(trace.mismatches) + " steps)");
                }
                for (const auto& [a, b] : trace.precedes) {
                    precedes.emplace_back(source.toCanonical[static_cast<size_t>(a)], source.toCanonical[static_cast<size_t>(b)]);
                }
            }
            if (!restoreDroppedFrames(base, group, context.log)) {
                return false;
            }
            bool ok = true;
            std::vector<MeshTri> baseTris = lodTris(base.mesh);
            bool augmented = false;
            for (int32_t round = 0; round < 64; round++) {
                std::vector<int32_t> order = constrainedOrder(vertexCount, special, precedes);
                if (static_cast<int32_t>(order.size()) != vertexCount - special) {
                    context.log("Warning: inconsistent LOD collapse order for " + base.settings.dataFile);
                    return false;
                }
                std::vector<int32_t> newIndex(static_cast<size_t>(vertexCount));
                for (int32_t v = 0; v < special; v++) {
                    newIndex[static_cast<size_t>(v)] = v;
                }
                for (size_t i = 0; i < order.size(); i++) {
                    newIndex[static_cast<size_t>(order[i])] = special + static_cast<int32_t>(i);
                }
                std::vector<uint8_t> data = dataFileBytes(baseTris, textures, newIndex, static_cast<size_t>(vertexCount), base.settings, ok);
                ok = ok && writeFile(resolveFile(base.directory, base.settings.dataFile), data);
                for (SourceMesh& source : group) {
                    std::vector<int32_t> sourceOf(static_cast<size_t>(vertexCount));
                    for (size_t from = 0; from < source.toCanonical.size(); from++) {
                        sourceOf[static_cast<size_t>(newIndex[static_cast<size_t>(source.toCanonical[from])])] = static_cast<int32_t>(from);
                    }
                    std::vector<uint8_t> aniv = anivFileBytes(source.verts, static_cast<size_t>(vertexCount), source.verts.size() / static_cast<size_t>(vertexCount), sourceOf, source.settings.unmirror, source.mesh->wideVerts);
                    ok = ok && writeFile(resolveFile(source.directory, source.settings.anivFile), aniv);
                }
                if (!ok) {
                    return ok;
                }
                std::optional<std::pair<int32_t, int32_t>> divergence;
                bool retry = false;
                for (SourceMesh& source : group) {
                    ULodMesh* imported = scratchImport(source);
                    if (!augmented && &source == &base && imported && textures.empty()) {
                        augmented = true;
                        std::vector<MeshTri> removed = removedTris(source, imported);
                        for (const MeshTri& tri : removed) {
                            auto at = std::upper_bound(baseTris.begin(), baseTris.end(), tri, [](const MeshTri& a, const MeshTri& b) {
                                return compareTris(a, b) < 0;
                            });
                            baseTris.insert(at, tri);
                        }
                        if (!removed.empty()) {
                            retry = true;
                            break;
                        }
                    }
                    divergence = importDivergence(source, imported);
                    if (divergence) {
                        break;
                    }
                }
                if (retry) {
                    continue;
                }
                if (!divergence) {
                    return true;
                }
                precedes.push_back(*divergence);
            }
            context.log("Warning: LOD collapse order for " + base.settings.dataFile + " not reproduced after refinement");
            return ok;
        }

        UMesh* packageMesh(UObject* package, const std::string& name)
        {
            for (UObject* object : objects::all()) {
                UMesh* mesh = object && !object->destroyed ? cast<UMesh>(object) : nullptr;
                if (mesh && mesh->outermost() == package && iequals(mesh->name.str(), name)) {
                    return mesh;
                }
            }
            return nullptr;
        }

        bool exportSourceCommand(ExecContext& context, const std::string& command)
        {
            std::string_view text = command;
            if (parseCommand(text, "MESHMAP")) {
                return true;
            }
            text = command;
            if (!parseCommand(text, "MESH")) {
                return false;
            }
            ImportSettings settings;
            if (!parseCommand(text, "IMPORT")) {
                return true;
            }
            if (!parseImportSettings(text, settings)) {
                return false;
            }
            UMesh* mesh = packageMesh(context.package, settings.meshName);
            if (!mesh && !packageExports(context.package, settings.meshName)) {
                context.log("Skipping mesh " + settings.meshName + ": not in the package");
                return true;
            }
            if (!mesh) {
                context.log("Warning: mesh " + settings.meshName + " not found");
                return false;
            }
            if (!cast<ULodMesh>(mesh)) {
                return exportPlainMesh(context, settings, mesh);
            }
            for (const ImportSettings& other : packageImports(context.package)) {
                UMesh* plain = iequals(other.dataFile, settings.dataFile) ? packageMesh(context.package, other.meshName) : nullptr;
                if (plain && !cast<ULodMesh>(plain)) {
                    return exportPlainMesh(context, other, plain);
                }
            }
            std::vector<std::pair<UObject*, std::filesystem::path>> sharing = { { context.package, context.directory } };
            std::string relative = settings.dataFile;
            std::replace(relative.begin(), relative.end(), '\\', '/');
            if (relative.rfind("../", 0) == 0 && relative.find('/', 3) != std::string::npos) {
                std::string other = relative.substr(3, relative.find('/', 3) - 3);
                Linker* linker = objects::findLinker(Name(other));
                if (linker && linker->packageObject != context.package) {
                    sharing.emplace_back(linker->packageObject, context.directory.parent_path() / linker->packageObject->name.str());
                }
            }
            std::string target = sourceKey(context.directory, settings.dataFile);
            std::vector<SourceMesh> group;
            for (const auto& [package, directory] : sharing) {
                for (const ImportSettings& other : packageImports(package)) {
                    if (sourceKey(directory, other.dataFile) != target) {
                        continue;
                    }
                    objects::createExportsNamed(Name(other.meshName));
                    ULodMesh* member = cast<ULodMesh>(packageMesh(package, other.meshName));
                    if (!member) {
                        continue;
                    }
                    objects::ensureLoaded(member);
                    objects::flushLoads();
                    SourceMesh source;
                    source.settings = other;
                    source.package = package;
                    source.directory = directory;
                    source.mesh = member;
                    source.verts = meshVerts(member);
                    if (package == context.package && iequals(other.meshName, settings.meshName)) {
                        group.insert(group.begin(), source);
                    } else {
                        group.push_back(source);
                    }
                }
            }
            if (group.empty()) {
                return false;
            }
            return exportLodGroup(context, group);
        }

        bool exportMeshObject(UObject* object, const std::string& extension, const std::filesystem::path& file)
        {
            UMesh* mesh = cast<UMesh>(object);
            if (!mesh || extension != "3d") {
                return false;
            }
            ExecContext context;
            context.package = mesh->outermost();
            context.directory = file.parent_path();
            context.log = [](const std::string&) {
            };
            std::string stem = file.stem().string();
            std::string command = "MESH IMPORT MESH=" + mesh->name.str() + " ANIVFILE=" + stem + "_a.3d DATAFILE=" + stem + "_d.3d";
            if (!cast<ULodMesh>(mesh)) {
                command += " MLOD=0";
                ImportSettings settings;
                std::string_view text = command;
                parseCommand(text, "MESH");
                parseCommand(text, "IMPORT");
                parseImportSettings(text, settings);
                return exportPlainMesh(context, settings, mesh);
            }
            std::vector<SourceMesh> group(1);
            std::string_view text = command;
            parseCommand(text, "MESH");
            parseCommand(text, "IMPORT");
            parseImportSettings(text, group[0].settings);
            group[0].mesh = cast<ULodMesh>(mesh);
            group[0].package = context.package;
            group[0].directory = context.directory;
            group[0].verts = meshVerts(mesh);
            return exportLodGroup(context, group);
        }
    }

    AssetHandler meshAssets()
    {
        AssetHandler handler;
        handler.import = importCommand;
        handler.exportSource = exportSourceCommand;
        handler.exportObject = exportMeshObject;
        handler.serialize = serializeMesh;
        return handler;
    }
}
