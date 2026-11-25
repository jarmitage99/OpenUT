#include "editor/editor.h"

namespace ut::editor {
    ut::UObject* Editor::editBrush()
    {
        if (selected && isBrush(selected)) {
            return selected;
        }
        return builderBrush();
    }

    void Editor::noteMoved(ut::UObject* actor)
    {
        if (actor && actor->isA(ut::Name("Mover"))) {
            ut::brushedit::moverMoved(actor);
        }
    }

    bool Editor::brushTransform(const std::string& label, const std::function<void(ut::UObject*)>& edit)
    {
        ut::UObject* target = editBrush();
        if (!target) {
            status = "no brush to edit";
            return false;
        }
        history.begin(level, { target }, label);
        edit(target);
        history.commit();
        brushCache.erase(target);
        status = label;
        return true;
    }

    bool Editor::brushPolysEdit(const std::string& label, const std::function<bool(ut::UObject*, ut::UModel&)>& edit)
    {
        ut::UObject* target = editBrush();
        ut::UModel* model = target ? ut::cast<ut::UModel>(ut::actor::Brush(target)) : nullptr;
        if (!model) {
            status = "no brush to edit";
            return false;
        }
        ut::objects::ensureLoaded(model);
        ut::objects::flushLoads();
        if (!model->polys) {
            status = "brush has no polygons";
            return false;
        }
        ut::objects::ensureLoaded(model->polys);
        ut::objects::flushLoads();
        history.begin(level, { target, model, model->polys }, label);
        bool done = edit(target, *model);
        if (done) {
            history.commit();
        } else {
            history.cancel();
        }
        brushCache.erase(target);
        status = done ? label : label + " failed";
        return done;
    }

    bool Editor::clipCommand(const std::string& rest)
    {
        if (rest == "clear") {
            clipMarkers.clear();
            return true;
        }
        if (rest == "flip") {
            if (clipMarkers.size() < 2) {
                status = "flip needs two clip markers";
                return false;
            }
            std::swap(clipMarkers[0], clipMarkers[1]);
            return true;
        }
        ut::UObject* target = editBrush();
        ut::Vector base;
        ut::Vector normal;
        if (!target || !ut::brushedit::clipPlane(target, clipMarkers, clipViewAxis, base, normal)) {
            status = "clipping needs two clip markers in an orthographic view or three anywhere";
            return false;
        }
        if (rest != "split") {
            return brushPolysEdit("Brush clip", [&](ut::UObject*, ut::UModel& model) {
                return ut::brushedit::clipBrush(model, base, normal);
            });
        }
        if (target == builderBrush()) {
            status = "the builder brush cannot be split";
            return false;
        }
        ut::UModel* model = ut::cast<ut::UModel>(ut::actor::Brush(target));
        std::vector<ut::Poly> original = model->polys->polys;
        ut::UObject* copy = nullptr;
        bool done = brushPolysEdit("Brush split", [&](ut::UObject* actor, ut::UModel& kept) {
            if (!ut::brushedit::clipBrush(kept, base, normal)) {
                return false;
            }
            copy = spawnActor(actor->cls, ut::actor::Location(actor), ut::actor::Rotation(actor), actor);
            ut::UModel& other = *ut::cast<ut::UModel>(ut::actor::Brush(copy));
            other.polys->polys = original;
            if (!ut::brushedit::clipBrush(other, base, normal * -1.0f)) {
                level->actors.pop_back();
                copy->destroyed = true;
                copy = nullptr;
                return true;
            }
            level->actors.pop_back();
            auto position = std::find(level->actors.begin(), level->actors.end(), actor);
            level->actors.insert(position + 1, copy);
            return true;
        });
        if (copy) {
            refreshActors();
            status = "split into " + target->name.str() + " and " + copy->name.str();
        }
        return done;
    }

    bool Editor::brushEditCommand(const std::string& verb, const std::string& rest)
    {
        std::istringstream input(rest);
        if (verb == "mover") {
            std::string what;
            int key = 0;
            input >> what >> key;
            if (!selected || !selected->isA(ut::Name("Mover")) || what != "key") {
                status = "select a mover to choose its key";
                return false;
            }
            history.begin(level, { selected }, "Set mover keyframe");
            ut::brushedit::showKey(selected, key);
            history.commit();
            brushCache.erase(selected);
            status = "mover key " + std::to_string(ut::mover::KeyNum(selected));
            return true;
        }
        if (verb == "vertexmode") {
            bool on = rest.empty() ? editMode != 1 : rest != "0";
            return command(on ? "editmode vertex" : "editmode object");
        }
        if (verb == "clipmode") {
            clipMode = rest.empty() ? !clipMode : rest != "0";
            if (clipMode) {
                editMode = 0;
                componentSelection.clear();
            }
            return true;
        }
        if (verb == "clipmarker") {
            ut::Vector marker;
            input >> marker.x >> marker.y >> marker.z >> clipViewAxis;
            if (clipMarkers.size() >= 3) {
                clipMarkers.clear();
            }
            clipMarkers.push_back(marker);
            return true;
        }
        if (verb == "clip") {
            return clipCommand(rest);
        }
        if (verb == "vertexmove") {
            ut::Vector from;
            ut::Vector offset;
            input >> from.x >> from.y >> from.z >> offset.x >> offset.y >> offset.z;
            return brushPolysEdit("Vertex edit", [&](ut::UObject* actor, ut::UModel& model) {
                ut::Vector local = ut::brushedit::toLocal(actor, from);
                ut::Vector moved = ut::brushedit::toLocal(actor, from + offset) - local;
                if (ut::brushedit::moveVertices(model, { local }, moved) == 0) {
                    return false;
                }
                ut::brushedit::validate(model);
                return true;
            });
        }
        std::string what;
        input >> what;
        if (what == "scale") {
            ut::Vector scale { 1, 1, 1 };
            input >> scale.x >> scale.y >> scale.z;
            return brushTransform("Brush scale", [&](ut::UObject* actor) {
                ut::brushedit::setMainScale(actor, scale);
            });
        }
        if (what == "mirror") {
            std::string axis;
            input >> axis;
            int index = 2;
            if (axis == "x") {
                index = 0;
            } else if (axis == "y") {
                index = 1;
            }
            return brushTransform("Brush mirror " + axis, [&](ut::UObject* actor) {
                ut::brushedit::mirror(actor, index);
            });
        }
        if (what == "reset") {
            std::string part;
            input >> part;
            return brushTransform("Brush reset " + part, [&](ut::UObject* actor) {
                if (part == "rotation") {
                    ut::brushedit::resetRotation(actor);
                } else if (part == "scale") {
                    ut::brushedit::resetScale(actor);
                } else if (part == "pivot") {
                    ut::brushedit::resetPivot(actor);
                } else {
                    ut::brushedit::resetAll(actor);
                }
            });
        }
        if (what == "pivot") {
            ut::Vector point;
            std::string mode;
            input >> point.x >> point.y >> point.z >> mode;
            if (mode == "snapped") {
                point = snapped(point);
            }
            return brushTransform("Brush pivot", [&](ut::UObject* actor) {
                ut::brushedit::setPivot(actor, point);
            });
        }
        if (what == "applytransform") {
            return brushPolysEdit("Transform permanently", [&](ut::UObject* actor, ut::UModel&) {
                ut::brushedit::applyTransform(actor);
                return true;
            });
        }
        if (what == "snap") {
            ut::Vector grid = ut::brushbuilder::constraints().gridSize;
            return brushPolysEdit("Snap brush vertices", [&](ut::UObject* actor, ut::UModel&) {
                ut::brushedit::snapVertices(actor, grid);
                return true;
            });
        }
        status = "unknown brush command " + what;
        return false;
    }

    void Editor::brushMenuItems()
    {
        ut::UObject* target = editBrush();
        bool any = target != nullptr;
        ImGui::Separator();
        if (ImGui::MenuItem(withIcon(icons::Scale, "Scale...").c_str(), nullptr, false, any)) {
            ut::Vector scale = ut::brushedit::mainScale(target);
            brushScaleInput[0] = scale.x;
            brushScaleInput[1] = scale.y;
            brushScaleInput[2] = scale.z;
            showBrushScale = true;
        }
        if (ImGui::BeginMenu(withIcon(icons::Mirror, "Mirror").c_str(), any)) {
            if (ImGui::MenuItem("X Axis")) {
                command("brush mirror x");
            }
            if (ImGui::MenuItem("Y Axis")) {
                command("brush mirror y");
            }
            if (ImGui::MenuItem("Z Axis")) {
                command("brush mirror z");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Reset", any)) {
            if (ImGui::MenuItem("Rotation")) {
                command("brush reset rotation");
            }
            if (ImGui::MenuItem("Scale")) {
                command("brush reset scale");
            }
            if (ImGui::MenuItem("Pivot")) {
                command("brush reset pivot");
            }
            if (ImGui::MenuItem("All")) {
                command("brush reset all");
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Transform Permanently", nullptr, false, any)) {
            command("brush applytransform");
        }
        if (ImGui::MenuItem("Snap Vertices to Grid", nullptr, false, any)) {
            command("brush snap");
        }
        ImGui::Separator();
        editModeMenuItems();
        if (ImGui::BeginMenu(withIcon(icons::Clip, "Clip").c_str())) {
            if (ImGui::MenuItem("Clip Tool", nullptr, clipMode)) {
                command(clipMode ? "clipmode 0" : "clipmode 1");
            }
            bool ready = clipMarkers.size() >= 2;
            if (ImGui::MenuItem("Clip Brush", nullptr, false, ready)) {
                command("clip");
            }
            if (ImGui::MenuItem("Split Brush", nullptr, false, ready && target != builderBrush())) {
                command("clip split");
            }
            if (ImGui::MenuItem("Flip Clip Side", nullptr, false, ready)) {
                command("clip flip");
            }
            if (ImGui::MenuItem("Delete Clip Markers", nullptr, false, !clipMarkers.empty())) {
                command("clip clear");
            }
            ImGui::EndMenu();
        }
    }

    void Editor::moverMenu()
    {
        bool mover = selected && selected->isA(ut::Name("Mover"));
        if (!ImGui::BeginMenu(withIcon(icons::Mover, "Mover").c_str(), mover)) {
            return;
        }
        int current = ut::mover::KeyNum(selected);
        for (int key = 0; key < 8; key++) {
            std::string label = key == 0 ? std::string("Key 0 (Base)") : "Key " + std::to_string(key);
            if (ImGui::MenuItem(label.c_str(), nullptr, key == current)) {
                command("mover key " + std::to_string(key));
            }
        }
        ImGui::EndMenu();
    }

    void Editor::brushScaleWindow()
    {
        if (!showBrushScale) {
            return;
        }
        if (!ImGui::Begin("Brush Scale", &showBrushScale, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }
        ImGui::InputFloat("X", &brushScaleInput[0], 0.25f, 1.0f, "%.3f");
        ImGui::InputFloat("Y", &brushScaleInput[1], 0.25f, 1.0f, "%.3f");
        ImGui::InputFloat("Z", &brushScaleInput[2], 0.25f, 1.0f, "%.3f");
        if (ImGui::Button("Apply")) {
            char text[128];
            std::snprintf(text, sizeof(text), "brush scale %g %g %g", static_cast<double>(brushScaleInput[0]), static_cast<double>(brushScaleInput[1]), static_cast<double>(brushScaleInput[2]));
            command(text);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            command("brush reset scale");
            brushScaleInput[0] = 1.0f;
            brushScaleInput[1] = 1.0f;
            brushScaleInput[2] = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            showBrushScale = false;
        }
        ImGui::End();
    }

    ut::Vector Editor::orthoToWorld(const OrthoView& view, const ImVec2& point, const ImVec2& origin, const ImVec2& size, const ut::Vector& depth)
    {
        ut::Vector result = depth;
        componentRef(result, view.horizontal) = view.centerH + (point.x - origin.x - size.x * 0.5f) / view.zoom;
        componentRef(result, view.vertical) = view.centerV + (point.y - origin.y - size.y * 0.5f) / (view.zoom * view.verticalSign);
        return result;
    }

    bool Editor::brushEditOrtho(const OrthoView& view, const ImVec2& origin, const ImVec2& size, bool hovered)
    {
        ImDrawList* list = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        int depthAxis = 3 - view.horizontal - view.vertical;
        ut::UObject* target = editBrush();
        ut::UModel* model = target ? ut::cast<ut::UModel>(ut::actor::Brush(target)) : nullptr;
        if (model && !model->polys) {
            model = nullptr;
        }
        ut::Vector depth = target ? ut::actor::Location(target) : ut::Vector {};
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 16.0f) {
            orthoClickWorld = orthoToWorld(view, io.MousePos, origin, size, depth);
            ImGui::OpenPopup("brushEditContext");
        }
        if (ImGui::BeginPopup("brushEditContext")) {
            selectionContextItems();
            ImGui::Separator();
            char point[96];
            std::snprintf(point, sizeof(point), "%g %g %g", static_cast<double>(orthoClickWorld.x), static_cast<double>(orthoClickWorld.y), static_cast<double>(orthoClickWorld.z));
            if (ImGui::MenuItem("Pivot Here", nullptr, false, target != nullptr)) {
                command(std::string("brush pivot ") + point);
            }
            if (ImGui::MenuItem("Pivot Snapped", nullptr, false, target != nullptr)) {
                command(std::string("brush pivot ") + point + " snapped");
            }
            if (ImGui::MenuItem("Reset Pivot", nullptr, false, target != nullptr)) {
                command("brush reset pivot");
            }
            if (ImGui::MenuItem("Add Clip Marker Here")) {
                ut::Vector marker = snapped(orthoClickWorld);
                componentRef(marker, depthAxis) = component(orthoClickWorld, depthAxis);
                char text[128];
                std::snprintf(text, sizeof(text), "clipmarker %g %g %g %d", static_cast<double>(marker.x), static_cast<double>(marker.y), static_cast<double>(marker.z), depthAxis);
                command(text);
            }
            ImGui::EndPopup();
        }
        auto project = [&](const ut::Vector& world, ImVec2& out) {
            out = orthoToScreen(view, world, origin, size);
            return true;
        };
        if (editMode != 0 && model) {
            std::vector<ut::Vector> hoverWorld;
            if (hovered && !vertexDragging) {
                std::vector<std::vector<ut::Vector>> candidates = handlesAt(project, io.MousePos, false);
                if (!candidates.empty()) {
                    for (const ut::Vector& vertex : candidates.front()) {
                        hoverWorld.push_back(ut::brushedit::toWorld(target, vertex));
                    }
                }
            }
            drawComponents(list, project, hoverWorld, false);
        }
        drawDimensions(list, view, origin, size);
        for (size_t i = 0; i < clipMarkers.size(); i++) {
            ImVec2 p = orthoToScreen(view, clipMarkers[i], origin, size);
            list->AddCircleFilled(p, 4.0f, IM_COL32(80, 255, 80, 255));
            list->AddText(ImVec2(p.x + 6, p.y - 14), IM_COL32(80, 255, 80, 255), std::to_string(i + 1).c_str());
        }
        if (clipMarkers.size() >= 2) {
            ImVec2 a = orthoToScreen(view, clipMarkers[0], origin, size);
            ImVec2 b = orthoToScreen(view, clipMarkers[1], origin, size);
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.5f) {
                float reach = (size.x + size.y) / length;
                list->AddLine(ImVec2(a.x - dx * reach, a.y - dy * reach), ImVec2(b.x + dx * reach, b.y + dy * reach), IM_COL32(80, 255, 80, 160));
            }
        }
        bool consumed = false;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (clipMode) {
                ut::Vector marker = snapped(orthoToWorld(view, io.MousePos, origin, size, depth));
                componentRef(marker, depthAxis) = component(depth, depthAxis);
                char text[128];
                std::snprintf(text, sizeof(text), "clipmarker %g %g %g %d", static_cast<double>(marker.x), static_cast<double>(marker.y), static_cast<double>(marker.z), depthAxis);
                command(text);
                consumed = true;
            } else if (editMode != 0 && model && !history.recording()) {
                handleCycle = handlesAt(project, io.MousePos, false);
                if (!handleCycle.empty()) {
                    auto chosenBefore = std::find_if(handleCycle.begin(), handleCycle.end(), [&](const std::vector<ut::Vector>& candidate) {
                        return componentSelection.size() == 1 && sameElement(candidate, componentSelection[0]);
                    });
                    cycleOnRelease = !io.KeyCtrl && handleCycle.size() > 1 && chosenBefore != handleCycle.end();
                    std::vector<ut::Vector> picked = chosenBefore != handleCycle.end() ? *chosenBefore : handleCycle.front();
                    chooseComponent(picked, io.KeyCtrl);
                    history.begin(level, { target, model, model->polys }, "Vertex edit");
                    vertexDragPolys = model->polys->polys;
                    vertexDragging = true;
                    vertexDragView = &view;
                    vertexDragStart = ut::brushedit::toWorld(target, picked[0]);
                    vertexDragRaw = vertexDragStart;
                    consumed = true;
                } else if (!io.KeyCtrl) {
                    componentSelection.clear();
                }
            }
        }
        if (vertexDragging && vertexDragView == &view && model) {
            componentRef(vertexDragRaw, view.horizontal) += io.MouseDelta.x / view.zoom;
            componentRef(vertexDragRaw, view.vertical) += io.MouseDelta.y / view.zoom * view.verticalSign;
            ut::Vector moved = snapped(vertexDragRaw);
            componentRef(moved, depthAxis) = component(vertexDragStart, depthAxis);
            ut::Vector offset = ut::brushedit::toLocal(target, moved) - ut::brushedit::toLocal(target, vertexDragStart);
            std::vector<ut::Vector> chosen = componentVertices();
            model->polys->polys = vertexDragPolys;
            ut::brushedit::moveVertices(*model, chosen, offset);
            brushCache.erase(target);
            ut::Vector delta = moved - vertexDragStart;
            if (offset.sizeSquared() > 0.0001f) {
                drawReadout(withIcon(icons::Move, (formatNumber(delta.x) + ", " + formatNumber(delta.y) + ", " + formatNumber(delta.z) + "   " + icons::Ruler + " " + formatNumber(delta.size())).c_str()));
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                vertexDragging = false;
                if (offset.sizeSquared() > 0.0001f) {
                    ut::brushedit::validate(*model);
                    for (std::vector<ut::Vector>& element : componentSelection) {
                        for (ut::Vector& vertex : element) {
                            vertex += offset;
                        }
                    }
                    history.commit();
                    status = "moved " + std::to_string(chosen.size()) + " brush vertices";
                } else {
                    ut::csg::updateBounds(*model);
                    history.cancel();
                    if (cycleOnRelease) {
                        auto current = std::find(handleCycle.begin(), handleCycle.end(), componentSelection.front());
                        size_t next = current == handleCycle.end() ? 0 : static_cast<size_t>(current - handleCycle.begin() + 1) % handleCycle.size();
                        componentSelection = { handleCycle[next] };
                        status = "cycled to " + std::to_string(next + 1) + " of " + std::to_string(handleCycle.size()) + " overlapping";
                    }
                }
                cycleOnRelease = false;
            }
        }
        return consumed;
    }
}
