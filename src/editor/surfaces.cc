#include "editor/editor.h"

namespace ut::editor {
    void Editor::surfacePanel()
    {
        ut::UModel* model = level ? level->model : nullptr;
        if (!model || selectedSurfs.empty()) {
            ImGui::TextUnformatted("No surfaces selected (shift+click in the 3D view)");
            if (model && ImGui::Button("Select all surfaces")) {
                command("polyselect all");
            }
            return;
        }
        const ut::BspSurf& surf = model->surfs[static_cast<size_t>(*selectedSurfs.begin())];
        ImGui::Text("%zu surfaces, texture %s", selectedSurfs.size(), surf.texture ? surf.texture->name.str().c_str() : "None");
        if (surf.texture && ImGui::Button("Texture properties")) {
            command("texprops " + surf.texture->pathName());
        }
        ImGui::SameLine();
        if (surf.texture && ImGui::Button("Find in browser")) {
            command("findtexture");
        }
        if (ImGui::CollapsingHeader("Select surfaces")) {
            surfaceSelectMenu();
        }
        ImGui::Text("Pan U %d V %d", surf.panU, surf.panV);
        if (ImGui::Button("Apply current texture") && currentTexture) {
            command("applytexture");
        }
        ImGui::InputInt2("Pan", panStep);
        if (ImGui::Button("Pan")) {
            command("surfpan " + std::to_string(panStep[0]) + " " + std::to_string(panStep[1]));
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset pan")) {
            command("surfpanabs 0 0");
        }
        ImGui::InputFloat2("Scale U V", scaleValues);
        if (ImGui::Button("Scale")) {
            command("surfscale " + std::to_string(1.0f / scaleValues[0]) + " 0 0 " + std::to_string(1.0f / scaleValues[1]));
        }
        ImGui::SameLine();
        if (ImGui::Button("Rotate 90")) {
            command("surfrotate 90");
        }
        ImGui::SameLine();
        if (ImGui::Button("Rotate 45")) {
            command("surfrotate 45");
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip U")) {
            command("surfscale -1 0 0 1 relative");
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip V")) {
            command("surfscale 1 0 0 -1 relative");
        }
        const char* aligns[] = { "default", "floor", "wall", "wallpan", "onetile" };
        for (const char* align : aligns) {
            if (ImGui::Button((std::string("Align ") + align).c_str())) {
                command(std::string("surfalign ") + align);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        const char* details[] = { "high", "normal", "low", "lowest" };
        for (const char* detail : details) {
            if (ImGui::Button((std::string("Light map ") + detail).c_str())) {
                command(std::string("surfdetail ") + detail);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        static const std::pair<uint32_t, const char*> flags[] = {
            { 0x00000001, "Invisible" },
            { 0x00000002, "Masked" },
            { 0x00000004, "Translucent" },
            { 0x00000008, "Not solid" },
            { 0x00000010, "Environment" },
            { 0x00000020, "Semisolid" },
            { 0x00000040, "Modulated" },
            { 0x00000080, "Fake backdrop" },
            { 0x00000100, "Two sided" },
            { 0x00000200, "U pan" },
            { 0x00000400, "V pan" },
            { 0x00000800, "No smooth" },
            { 0x00001000, "Big wavy" },
            { 0x00002000, "Small wavy" },
            { 0x00004000, "Flat" },
            { 0x00010000, "No merge" },
            { 0x00020000, "Cloud wavy" },
            { 0x00040000, "Dirty shadows" },
            { 0x00080000, "Bright corners" },
            { 0x00100000, "Special lit" },
            { 0x00200000, "Gouraud" },
            { 0x00400000, "Unlit" },
            { 0x04000000, "Zone portal" },
            { 0x08000000, "Mirrored" },
        };
        for (const auto& [bit, label] : flags) {
            bool value = (surf.polyFlags & bit) != 0;
            if (ImGui::Checkbox(label, &value)) {
                char text[64];
                std::snprintf(text, sizeof(text), "surfflags 0x%x 0x%x", value ? bit : 0u, value ? 0u : bit);
                command(text);
            }
        }
    }

    bool Editor::surfaceEdit(const std::string& label, const std::function<void(ut::UModel&)>& edit)
    {
        ut::UModel* model = level ? level->model : nullptr;
        if (!model || selectedSurfs.empty()) {
            status = "no surfaces selected";
            return false;
        }
        std::vector<ut::UObject*> touched { model };
        for (int32_t index : selectedSurfs) {
            ut::UObject* owner = model->surfs[static_cast<size_t>(index)].actor;
            ut::UModel* brush = owner && owner->data ? ut::cast<ut::UModel>(ut::actor::Brush(owner)) : nullptr;
            if (brush && brush->polys) {
                touched.push_back(brush->polys);
            }
        }
        history.begin(level, touched, label);
        edit(*model);
        history.commit();
        renderer->flushLevel();
        status = label + " on " + std::to_string(selectedSurfs.size()) + " surfaces";
        return true;
    }

    void Editor::surfaceSelectMenu()
    {
        static const std::pair<const char*, const char*> entries[] = {
            { "Matching Groups", "matching groups" },
            { "Matching Items", "matching items" },
            { "Matching Brush", "matching brush" },
            { "Matching Texture", "matching texture" },
            { nullptr, nullptr },
            { "All Adjacents", "adjacent all" },
            { "Adjacent Coplanars", "adjacent coplanars" },
            { "Adjacent Walls", "adjacent walls" },
            { "Adjacent Floors/Ceils", "adjacent floors" },
            { "Adjacent Slants", "adjacent slants" },
            { nullptr, nullptr },
            { "Reverse", "reverse" },
            { nullptr, nullptr },
            { "Memorize Set", "memory set" },
            { "Recall Memory", "memory recall" },
            { "Or with Memory", "memory or" },
            { "And with Memory", "memory and" },
            { "Xor with Memory", "memory xor" },
            { nullptr, nullptr },
            { "Select All Surfaces", "all" },
            { "Select None", "none" },
        };
        for (const auto& [label, verb] : entries) {
            if (!label) {
                ImGui::Separator();
                continue;
            }
            if (ImGui::MenuItem(label)) {
                command(std::string("polyselect ") + verb);
            }
        }
    }

    void Editor::surfacePopup()
    {
        if (!ImGui::BeginPopup("SurfacePopup")) {
            return;
        }
        ut::UModel* model = level ? level->model : nullptr;
        ut::UObject* texture = model && !selectedSurfs.empty() ? model->surfs[static_cast<size_t>(*selectedSurfs.begin())].texture : nullptr;
        ImGui::TextDisabled("%zu surfaces", selectedSurfs.size());
        if (ImGui::MenuItem("Surface Properties")) {
            pendingTab = "surface";
        }
        if (ImGui::MenuItem("Texture Properties", nullptr, false, texture != nullptr)) {
            command("texprops " + texture->pathName());
        }
        if (ImGui::MenuItem("Find Texture in Browser", nullptr, false, texture != nullptr)) {
            command("findtexture");
        }
        if (ImGui::MenuItem("Apply Current Texture", nullptr, false, currentTexture != nullptr)) {
            command("applytexture");
        }
        if (ImGui::BeginMenu("Select Surfaces")) {
            surfaceSelectMenu();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        addHereItems();
        ImGui::EndPopup();
    }

    bool Editor::polySelect(const std::string& line)
    {
        ut::UModel* model = level ? level->model : nullptr;
        if (!model) {
            return false;
        }
        std::istringstream words(lower(line));
        std::string verb;
        std::string kind;
        words >> verb >> kind;
        if (verb == "none") {
            selectedSurfs.clear();
        } else if (verb == "all") {
            ut::editorops::selectAllSurfs(*model, selectedSurfs);
        } else if (verb == "reverse") {
            ut::editorops::selectReverse(*model, selectedSurfs);
        } else if (verb == "matching" && kind == "groups") {
            ut::editorops::selectMatchingGroups(*model, selectedSurfs);
        } else if (verb == "matching" && kind == "items") {
            ut::editorops::selectMatchingItems(*model, selectedSurfs);
        } else if (verb == "matching" && kind == "brush") {
            ut::editorops::selectMatchingBrush(*model, selectedSurfs);
        } else if (verb == "matching" && kind == "texture") {
            ut::editorops::selectMatchingTexture(*model, selectedSurfs);
        } else if (verb == "adjacent" && kind == "all") {
            ut::editorops::selectAdjacent(*model, selectedSurfs, ut::editorops::Adjacent::All);
        } else if (verb == "adjacent" && kind == "coplanars") {
            ut::editorops::selectAdjacent(*model, selectedSurfs, ut::editorops::Adjacent::Coplanars);
        } else if (verb == "adjacent" && kind == "walls") {
            ut::editorops::selectAdjacent(*model, selectedSurfs, ut::editorops::Adjacent::Walls);
        } else if (verb == "adjacent" && (kind == "floors" || kind == "ceilings")) {
            ut::editorops::selectAdjacent(*model, selectedSurfs, ut::editorops::Adjacent::Floors);
        } else if (verb == "adjacent" && kind == "slants") {
            ut::editorops::selectAdjacent(*model, selectedSurfs, ut::editorops::Adjacent::Slants);
        } else if (verb == "memory" && kind == "set") {
            surfMemory = selectedSurfs;
        } else if (verb == "memory" && kind == "recall") {
            selectedSurfs = surfMemory;
        } else if (verb == "memory" && kind == "or") {
            ut::editorops::combineWithMemory(selectedSurfs, surfMemory, ut::editorops::MemoryOp::Or);
        } else if (verb == "memory" && kind == "and") {
            ut::editorops::combineWithMemory(selectedSurfs, surfMemory, ut::editorops::MemoryOp::And);
        } else if (verb == "memory" && kind == "xor") {
            ut::editorops::combineWithMemory(selectedSurfs, surfMemory, ut::editorops::MemoryOp::Xor);
        } else {
            status = "unknown surface selection " + line;
            return false;
        }
        status = "selected " + std::to_string(selectedSurfs.size()) + " surfaces";
        return true;
    }

    void Editor::texturePropertiesWindow()
    {
        if (!showTextureProperties || !propertiesTexture) {
            return;
        }
        ImGui::SetNextWindowPos(ImVec2(360, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Texture Properties", &showTextureProperties)) {
            ImGui::End();
            return;
        }
        ut::UTexture* texture = ut::cast<ut::UTexture>(propertiesTexture);
        std::string group = ut::editorops::textureGroup(propertiesTexture);
        ImGui::Text("Name: %s", propertiesTexture->pathName().c_str());
        ImGui::Text("Group: %s", group.empty() ? "(none)" : group.c_str());
        if (texture && texture->data) {
            ut::UPalette* palette = texture->palette();
            ImGui::Text("Size: %u x %u, %zu mips", texture->width(), texture->height(), texture->mips.size());
            ImGui::Text("Format: %s", ut::editorops::textureFormatName(texture->format()).c_str());
            ImGui::Text("Palette: %s (%zu colors)", palette ? palette->name.str().c_str() : "None", palette ? palette->colors.size() : size_t { 0 });
            ut::UObject* detail = ut::texture::DetailTexture(texture);
            ut::UObject* macro = ut::texture::MacroTexture(texture);
            ImGui::Text("Detail: %s", detail ? detail->pathName().c_str() : "None");
            ImGui::Text("Macro: %s", macro ? macro->pathName().c_str() : "None");
            ImGui::Text("Scale: %.3f", static_cast<double>(ut::texture::DrawScale(texture)));
            static const std::pair<size_t ut::texture::Offsets::*, const char*> flags[] = {
                { &ut::texture::Offsets::bInvisible, "Invisible" },
                { &ut::texture::Offsets::bMasked, "Masked" },
                { &ut::texture::Offsets::bTransparent, "Transparent" },
                { &ut::texture::Offsets::bNotSolid, "NotSolid" },
                { &ut::texture::Offsets::bEnvironment, "Environment" },
                { &ut::texture::Offsets::bSemisolid, "Semisolid" },
                { &ut::texture::Offsets::bModulate, "Modulate" },
                { &ut::texture::Offsets::bFakeBackdrop, "FakeBackdrop" },
                { &ut::texture::Offsets::bTwoSided, "TwoSided" },
                { &ut::texture::Offsets::bAutoUPan, "AutoUPan" },
                { &ut::texture::Offsets::bAutoVPan, "AutoVPan" },
                { &ut::texture::Offsets::bNoSmooth, "NoSmooth" },
                { &ut::texture::Offsets::bBigWavy, "BigWavy" },
                { &ut::texture::Offsets::bSmallWavy, "SmallWavy" },
                { &ut::texture::Offsets::bWaterWavy, "WaterWavy" },
                { &ut::texture::Offsets::bCloudWavy, "CloudWavy" },
                { &ut::texture::Offsets::bUnlit, "Unlit" },
                { &ut::texture::Offsets::bMirrored, "Mirrored" },
                { &ut::texture::Offsets::bRealtime, "Realtime" },
                { &ut::texture::Offsets::bParametric, "Parametric" },
            };
            std::string flagText;
            for (const auto& [offset, label] : flags) {
                if (*reinterpret_cast<uint32_t*>(texture->data + ut::offsets_texture.*offset)) {
                    flagText += flagText.empty() ? label : std::string(" ") + label;
                }
            }
            ImGui::TextWrapped("Flags: %s", flagText.empty() ? "None" : flagText.c_str());
            const ut::GLTextureInfo* info = thumbnails.get(texture);
            if (info) {
                float scale = std::min(1.0f, 256.0f / static_cast<float>(std::max(info->width, info->height)));
                ImGui::Image(static_cast<ImTextureID>(info->id), ImVec2(static_cast<float>(info->width) * scale, static_cast<float>(info->height) * scale));
            }
        }
        if (ImGui::Button("Use as current texture")) {
            currentTexture = propertiesTexture;
            ut::bsp::setCurrentTexture(propertiesTexture);
        }
        ImGui::SameLine();
        if (ImGui::Button("Select surfaces using it")) {
            currentTexture = propertiesTexture;
            command("texusers");
        }
        if (propertiesTexture->data && propertiesTexture->cls) {
            std::vector<uint8_t> before(propertiesTexture->data, propertiesTexture->data + propertiesTexture->cls->size);
            std::map<std::string, std::vector<ut::UProperty*>> categories;
            for (ut::UProperty* property : propertiesTexture->cls->properties) {
                if (property->propertyFlags & ut::CPF_Edit) {
                    categories[property->category.none() ? std::string("None") : property->category.str()].push_back(property);
                }
            }
            ImGui::BeginChild("textureProperties");
            for (auto& category : categories) {
                if (!ImGui::CollapsingHeader(category.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    continue;
                }
                if (!ImGui::BeginTable(category.first.c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
                    continue;
                }
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                for (ut::UProperty* property : category.second) {
                    for (int32_t index = 0; index < property->arrayDim; index++) {
                        objectPropertyRow(propertiesTexture, property, index, false);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
            if (std::memcmp(before.data(), propertiesTexture->data, before.size()) != 0) {
                renderer->flushLevel();
                thumbnails.clear();
                status = "edited " + propertiesTexture->name.str();
            }
        }
        ImGui::End();
    }

    bool Editor::textureCommand(const std::string& verb, const std::string& rest)
    {
        ut::UModel* model = level ? level->model : nullptr;
        if (verb == "texprops") {
            if (!rest.empty() && !command("texture " + rest)) {
                return false;
            }
            if (!currentTexture) {
                return false;
            }
            propertiesTexture = currentTexture;
            showTextureProperties = true;
            return true;
        }
        if (verb == "texusers") {
            if (!model || !currentTexture) {
                return false;
            }
            selectedSurfs = ut::editorops::surfsUsingTexture(*model, currentTexture);
            status = "selected " + std::to_string(selectedSurfs.size()) + " surfaces using " + currentTexture->name.str();
            return true;
        }
        if (verb == "findtexture") {
            if (!model || selectedSurfs.empty()) {
                return false;
            }
            ut::UObject* texture = model->surfs[static_cast<size_t>(*selectedSurfs.begin())].texture;
            if (!texture) {
                status = "surface has no texture";
                return false;
            }
            ut::UObject* package = texture;
            while (package->outer) {
                package = package->outer;
            }
            if (package->name.str() != texturePackage) {
                loadTexturePackage(package->name.str());
            }
            command("texgroup " + ut::editorops::textureGroup(texture));
            currentTexture = texture;
            ut::bsp::setCurrentTexture(texture);
            scrollToTexture = true;
            pendingTab = "textures";
            status = "found " + texture->pathName();
            return true;
        }
        if (verb == "texgroup") {
            auto found = std::find(textureGroupNames.begin(), textureGroupNames.end(), rest == "(none)" ? std::string() : rest);
            textureGroupChoice = found == textureGroupNames.end() || rest == "(all)" ? -1 : static_cast<int>(found - textureGroupNames.begin());
            return true;
        }
        return false;
    }
}
