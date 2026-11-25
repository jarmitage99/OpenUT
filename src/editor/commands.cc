#include "editor/editor.h"

namespace ut::editor {
    bool Editor::command(const std::string& line)
    {
        std::istringstream input(line);
        std::string verb;
        input >> verb;
        std::string rest;
        std::getline(input, rest);
        size_t first = rest.find_first_not_of(' ');
        rest = first == std::string::npos ? std::string() : rest.substr(first);
        if (verb == "shapeeditor" || verb == "shape" || verb == "floorlofter" || verb == "floor" || verb == "heightmap" || verb == "heightmapdialog" || verb == "addspecialdialog" || verb == "addzone") {
            return builderCommand(verb, rest);
        }
        if (verb == "mapcheck" || verb == "result" || verb == "find" || verb == "findnext" || verb == "levelprops" || verb == "setinfo" || verb == "use" || verb == "log") {
            return toolsCommand(verb, rest);
        }
        if (verb == "open") {
            return openMap(rest);
        }
        if (verb == "viewmode" || verb == "viewkind" || verb == "maximize" || verb == "showpaths" || verb == "showicons") {
            return viewCommand(verb, rest);
        }
        if (verb == "selectall" || verb == "selectnone" || verb == "selectinvert" || verb == "selectclass" || verb == "selectadd" || verb == "selecttoggle" || verb == "boxselect" || verb == "copy" || verb == "cut" || verb == "paste" || verb == "addat" || verb == "exportt3d" || verb == "importt3d" || verb == "hide" || verb == "unhide" || verb == "zclip" || verb == "gizmo") {
            return selectionCommand(verb, rest);
        }
        if (verb == "editmode" || verb == "extrude") {
            return componentCommand(verb, rest);
        }
        if (verb == "openfile") {
            return openMapFile(rest);
        }
        if (verb == "loadclass" || verb == "setdefault" || verb == "classmenu" || verb == "classdefaults" || verb == "exportclass" || verb == "deleteclass") {
            return classCommand(verb, rest);
        }
        if (verb == "editscript") {
            ut::UClass* cls = ut::objects::findClass(rest);
            if (!cls || !cls->scriptText) {
                status = "no script for " + rest;
                return false;
            }
            ut::objects::ensureLoaded(cls->scriptText);
            ut::objects::flushLoads();
            openDocument(cls, 0);
            return true;
        }
        if (verb == "compilescripts") {
            return compileScripts(rest == "all");
        }
        if (verb == "newclass") {
            std::istringstream fields(rest);
            std::string packageName;
            std::string className;
            std::string parentPath;
            fields >> packageName >> className >> parentPath;
            ut::UClass* cls = scripts.newClass(packageName, className, ut::objects::findClass(parentPath));
            showScripts = true;
            status = scripts.messages().empty() ? std::string() : scripts.messages().back().text;
            if (!cls) {
                return false;
            }
            refreshClasses();
            placeClass = cls;
            openDocument(cls, 0);
            return true;
        }
        if (verb == "exportscripts") {
            scripts.exportScripts(rest != "all");
            showScripts = true;
            status = scripts.messages().back().text;
            return true;
        }
        if (verb == "savepackage") {
            bool done = scripts.savePackage(rest);
            showScripts = true;
            status = scripts.messages().back().text;
            return done;
        }
        if (verb == "builder") {
            select(builderBrush());
            return selected != nullptr;
        }
        if (verb == "new") {
            return newMap(rest.empty() ? std::string("Untitled") : rest);
        }
        if (verb == "save") {
            std::filesystem::path file = currentFile.empty() ? userMapsDirectory() / (currentMap + ".unr") : currentFile;
            return saveTo(file);
        }
        if (verb == "saveas") {
            std::filesystem::path file = rest.find('/') != std::string::npos ? std::filesystem::path(rest) : userMapsDirectory() / (rest + ".unr");
            if (saveTo(file)) {
                currentMap = file.stem().string();
                return true;
            }
            return false;
        }
        if (verb == "play") {
            if (!command("save")) {
                return false;
            }
            launchGame(currentFile);
            return true;
        }
        if (verb == "undo" || verb == "redo") {
            bool done = verb == "undo" ? history.undo() : history.redo();
            geometryChanged();
            componentSelection.clear();
            status = done ? verb + " done" : "nothing to " + verb;
            return done;
        }
        if (verb == "select") {
            for (ut::UObject* actor : actors) {
                if (lower(actor->name.str()) == lower(rest)) {
                    select(actor);
                    focusSelected();
                    return true;
                }
            }
            status = "no actor " + rest;
            return false;
        }
        if (verb == "move" || verb == "moveto" || verb == "rotate") {
            if (!selected) {
                return false;
            }
            std::istringstream values(rest);
            float a = 0;
            float b = 0;
            float c = 0;
            values >> a >> b >> c;
            std::vector<ut::UObject*> chosen = selectionList();
            history.begin(level, chosen, (verb == "rotate" ? "Rotate " : "Move ") + selected->name.str());
            if (verb == "move") {
                translateSelection({ a, b, c });
            } else if (verb == "moveto") {
                translateSelection(ut::Vector { a, b, c } - ut::actor::Location(selected));
            } else {
                rotateSelection({ static_cast<int32_t>(a), static_cast<int32_t>(b), static_cast<int32_t>(c) });
            }
            for (ut::UObject* actor : chosen) {
                noteMoved(actor);
            }
            history.commit();
            return true;
        }
        if (verb == "set") {
            if (!selected) {
                return false;
            }
            std::istringstream values(rest);
            std::string propertyName;
            values >> propertyName;
            std::string value;
            std::getline(values, value);
            size_t start = value.find_first_not_of(' ');
            value = start == std::string::npos ? std::string() : value.substr(start);
            ut::UProperty* property = selected->cls->findProperty(ut::Name(propertyName));
            return property && setProperty(selected, property, 0, value);
        }
        if (verb == "add") {
            ut::UClass* cls = resolveClass(rest);
            if (!cls || (cls->classFlags & ut::CLASS_Abstract) || !actorClass || !cls->isChildOf(actorClass)) {
                status = "cannot add " + rest;
                return false;
            }
            history.begin(level, {}, "Add " + cls->name.str());
            ut::Vector location = cameraLocation + ut::rotationVector(cameraRotation) * 256.0f;
            ut::UObject* actor = spawnActor(cls, location, { 0, cameraRotation.yaw, 0 }, nullptr);
            history.commit();
            refreshActors();
            select(actor);
            status = "added " + actor->name.str();
            return true;
        }
        if (verb == "duplicate") {
            if (!selected || selected == levelInfo()) {
                return false;
            }
            history.begin(level, {}, "Duplicate " + selected->name.str());
            std::vector<ut::UObject*> copies;
            ut::UObject* primary = nullptr;
            for (ut::UObject* actor : selectionList()) {
                if (actor == levelInfo()) {
                    continue;
                }
                ut::UObject* copy = spawnActor(actor->cls, ut::actor::Location(actor) + ut::Vector { 32, 32, 0 }, ut::actor::Rotation(actor), actor);
                copies.push_back(copy);
                if (actor == selected) {
                    primary = copy;
                }
            }
            history.commit();
            refreshActors();
            select(primary);
            selectedActors.insert(copies.begin(), copies.end());
            status = copies.size() == 1 ? "duplicated as " + primary->name.str() : "duplicated " + std::to_string(copies.size()) + " actors";
            return true;
        }
        if (verb == "delete") {
            std::vector<ut::UObject*> victims = editableSelection();
            if (victims.empty()) {
                status = "cannot delete this actor";
                return false;
            }
            std::string label = victims.size() == 1 ? victims.front()->name.str() : std::to_string(victims.size()) + " actors";
            history.begin(level, victims, "Delete " + label);
            for (ut::UObject* victim : victims) {
                level->actors.erase(std::remove(level->actors.begin(), level->actors.end(), victim), level->actors.end());
                victim->destroyed = true;
            }
            history.commit();
            refreshActors();
            status = "deleted " + label;
            return true;
        }
        if (verb == "cube" || verb == "cylinder" || verb == "sheet") {
            ut::UObject* builder = builderBrush();
            ut::UModel* model = builder ? ut::cast<ut::UModel>(ut::actor::Brush(builder)) : nullptr;
            if (!model) {
                status = "no builder brush";
                return false;
            }
            ut::objects::ensureLoaded(model);
            ut::objects::flushLoads();
            if (!model->polys) {
                ut::UPolys* polys = ut::cast<ut::UPolys>(ut::objects::newObject(ut::csg::nativeClass("Polys"), level->outermost(), ut::Name(), EditorActorFlags));
                polys->loaded = true;
                model->polys = polys;
            }
            ut::objects::ensureLoaded(model->polys);
            ut::objects::flushLoads();
            std::istringstream values(rest);
            float a = 256;
            float b = 256;
            float c = 256;
            values >> a >> b >> c;
            ut::UObject* texture = currentTexture;
            if (verb == "cube") {
                ut::csg::buildCube(*model->polys, a, b, c, texture);
            } else if (verb == "cylinder") {
                ut::csg::buildCylinder(*model->polys, a, b, static_cast<int>(c), texture);
            } else {
                ut::csg::buildSheet(*model->polys, a, b, texture);
            }
            ut::csg::updateBounds(*model);
            brushCache.erase(builder);
            status = "builder brush is a " + verb;
            return true;
        }
        if (verb == "csgadd" || verb == "csgsubtract") {
            ut::UObject* builder = builderBrush();
            if (!builder) {
                return false;
            }
            uint32_t flags = verb == "csgadd" && !rest.empty() ? static_cast<uint32_t>(std::strtoul(rest.c_str(), nullptr, 0)) : 0;
            ut::bsp::setCurrentTexture(currentTexture);
            history.begin(level, { level->model }, verb == "csgadd" ? "Add brush" : "Subtract brush");
            ut::UObject* brush = ut::csg::addBrush(level, builder, verb == "csgadd" ? ut::csg::CsgAdd : ut::csg::CsgSubtract, flags);
            if (!brush) {
                history.cancel();
                return false;
            }
            history.commit();
            geometryChanged();
            select(brush);
            status = "created " + brush->name.str();
            return true;
        }
        if (verb == "addspecial") {
            auto preset = std::find_if(specialPresets.begin(), specialPresets.end(), [&](const SpecialPreset& item) {
                return lower(item.name) == lower(rest);
            });
            if (preset == specialPresets.end()) {
                status = "no special brush preset " + rest;
                return false;
            }
            return command("csgadd " + std::to_string(preset->flags));
        }
        if (verb == "intersect" || verb == "deintersect") {
            ut::UObject* builder = builderBrush();
            if (!builder) {
                return false;
            }
            history.begin(level, { builder, ut::actor::Brush(builder) }, verb == "intersect" ? "Brush from intersection" : "Brush from deintersection");
            bool done = ut::csg::brushFromIntersection(level, builder, verb == "deintersect");
            if (done) {
                history.commit();
            } else {
                history.cancel();
            }
            brushCache.erase(builder);
            status = done ? verb + " done" : verb + " failed";
            return done;
        }
        if (verb == "addmover") {
            ut::UObject* builder = builderBrush();
            ut::UClass* moverClass = rest.empty() ? ut::objects::findClass("Engine.Mover") : resolveClass(rest);
            if (!builder) {
                return false;
            }
            ut::bsp::setCurrentTexture(currentTexture);
            history.begin(level, {}, "Add mover");
            ut::UObject* mover = ut::csg::addMover(level, builder, moverClass);
            if (!mover) {
                history.cancel();
                status = "cannot add mover";
                return false;
            }
            history.commit();
            refreshActors();
            select(mover);
            status = "created " + mover->name.str();
            return true;
        }
        if (verb == "brushbuild") {
            return runBrushBuilder(rest);
        }
        if (verb == "brushexport") {
            ut::UObject* builder = builderBrush();
            if (!builder || rest.empty()) {
                status = "brushexport needs a file";
                return false;
            }
            std::ofstream out(rest, std::ios::binary);
            out << ut::editorops::exportPolys(*ut::cast<ut::UModel>(ut::actor::Brush(builder))->polys);
            status = out ? "exported brush to " + rest : "cannot write " + rest;
            return static_cast<bool>(out);
        }
        if (verb == "brushimport") {
            ut::UObject* builder = builderBrush();
            std::istringstream arguments(rest);
            std::string file;
            int merge = 0;
            uint32_t flags = 0;
            arguments >> file >> merge >> flags;
            std::ifstream in(file, std::ios::binary);
            if (!builder || !in) {
                status = "cannot read " + file;
                return false;
            }
            std::stringstream text;
            text << in.rdbuf();
            ut::UModel* brush = ut::cast<ut::UModel>(ut::actor::Brush(builder));
            history.begin(level, { builder, brush, brush->polys }, "Brush import");
            brush->linked = 0;
            ut::editorops::importPolys(*brush->polys, text.str());
            for (size_t i = 0; i < brush->polys->polys.size(); i++) {
                brush->polys->polys[i].polyFlags |= flags;
                brush->polys->polys[i].link = static_cast<int32_t>(i);
            }
            if (merge) {
                ut::bsp::mergeCoplanars(*brush, false, true);
                ut::brushbuilder::validateBrush(*brush, true);
            }
            history.commit();
            brushCache.erase(builder);
            status = "imported " + std::to_string(brush->polys->polys.size()) + " brush polygons";
            return true;
        }
        if (verb == "rebuild" || verb == "rebuildgeometry" || verb == "rebuildbsp") {
            ut::csg::RebuildOptions settings = rebuildOptions;
            settings.geometry = verb != "rebuildbsp";
            settings.bsp = verb != "rebuildgeometry" && (verb == "rebuildbsp" || rebuildOptions.bsp);
            ut::bsp::setCurrentTexture(currentTexture);
            restoreRegions(true);
            ut::csg::Stats stats = ut::csg::rebuild(level, settings);
            captureRegions();
            geometryChanged();
            status = "geometry: " + std::to_string(stats.brushes) + " brushes, " + std::to_string(stats.nodes) + " nodes, " + std::to_string(stats.surfs) + " surfaces, " + std::to_string(stats.zones) + " zones";
            std::fprintf(stderr, "%s\n", status.c_str());
            return true;
        }
        if (verb == "light") {
            std::unordered_set<ut::UObject*> chosen;
            if (rest == "selected" && selected) {
                chosen.insert(selected);
            }
            restoreRegions(true);
            ut::lightbuild::Stats stats = ut::lightbuild::rebuild(level, rest == "selected" ? &chosen : nullptr);
            captureRegions();
            renderer->flushLevel();
            status = "lighting: " + std::to_string(stats.lights) + " lights, " + std::to_string(stats.surfaces) + " lightmaps";
            std::fprintf(stderr, "%s\n", status.c_str());
            return true;
        }
        if (verb == "brush" || verb == "mover" || verb == "vertexmode" || verb == "vertexmove" || verb == "clipmode" || verb == "clipmarker" || verb == "clip") {
            return brushEditCommand(verb, rest);
        }
        if (verb == "paths") {
            return pathsCommand(rest);
        }
        if (verb == "polyselect") {
            return polySelect(rest);
        }
        if (verb == "texprops" || verb == "texusers" || verb == "findtexture" || verb == "texgroup") {
            return textureCommand(verb, rest);
        }
        if (verb == "surfpan" || verb == "surfpanabs") {
            std::istringstream values(rest);
            int u = 0;
            int v = 0;
            values >> u >> v;
            return surfaceEdit("Pan surface", [&](ut::UModel& model) {
                ut::editorops::texPan(model, selectedSurfs, u, v, verb == "surfpanabs");
            });
        }
        if (verb == "surfscale") {
            std::istringstream values(rest);
            float uu = 1;
            float uv = 0;
            float vu = 0;
            float vv = 1;
            std::string mode;
            values >> uu >> uv >> vu >> vv >> mode;
            return surfaceEdit("Scale surface", [&](ut::UModel& model) {
                ut::editorops::texScale(model, selectedSurfs, uu, uv, vu, vv, mode != "relative");
            });
        }
        if (verb == "surfrotate") {
            double degrees = std::atof(rest.c_str());
            float c = static_cast<float>(std::cos(degrees * 3.14159265358979 / 180.0));
            float sn = static_cast<float>(std::sin(degrees * 3.14159265358979 / 180.0));
            return surfaceEdit("Rotate surface", [&](ut::UModel& model) {
                ut::editorops::texScale(model, selectedSurfs, c, sn, -sn, c, false);
            });
        }
        if (verb == "surfalign") {
            ut::editorops::TexAlign align = ut::editorops::TexAlign::Default;
            if (rest == "floor") {
                align = ut::editorops::TexAlign::Floor;
            } else if (rest == "wall") {
                align = ut::editorops::TexAlign::WallDir;
            } else if (rest == "wallpan") {
                align = ut::editorops::TexAlign::WallPan;
            } else if (rest == "onetile") {
                align = ut::editorops::TexAlign::OneTile;
            }
            return surfaceEdit("Align surface", [&](ut::UModel& model) {
                ut::editorops::texAlign(model, selectedSurfs, align);
            });
        }
        if (verb == "surfflags") {
            std::istringstream values(rest);
            std::string setText;
            std::string clearText;
            values >> setText >> clearText;
            uint32_t setBits = static_cast<uint32_t>(std::strtoul(setText.c_str(), nullptr, 0));
            uint32_t clearBits = static_cast<uint32_t>(std::strtoul(clearText.c_str(), nullptr, 0));
            return surfaceEdit("Surface flags", [&](ut::UModel& model) {
                ut::editorops::setAndClearFlags(model, selectedSurfs, setBits, clearBits);
            });
        }
        if (verb == "surfdetail") {
            uint32_t setBits = 0;
            if (rest == "low") {
                setBits = ut::bsp::PF_LowShadowDetail;
            } else if (rest == "high") {
                setBits = ut::bsp::PF_HighShadowDetail;
            } else if (rest == "lowest") {
                setBits = ut::bsp::PF_LowShadowDetail | ut::bsp::PF_HighShadowDetail;
            }
            return surfaceEdit("Light map detail", [&](ut::UModel& model) {
                ut::editorops::setAndClearFlags(model, selectedSurfs, setBits, ut::bsp::PF_LowShadowDetail | ut::bsp::PF_HighShadowDetail);
            });
        }
        if (verb == "grid") {
            std::istringstream values(rest);
            float size = 16;
            int enabled = 1;
            values >> size >> enabled;
            ut::brushbuilder::Constraints& constraints = ut::brushbuilder::constraints();
            constraints.gridSize = { size, size, size };
            constraints.gridEnabled = enabled != 0;
            return true;
        }
        if (verb == "rotgrid") {
            std::istringstream values(rest);
            int size = 1024;
            int enabled = 1;
            values >> size >> enabled;
            ut::brushbuilder::Constraints& constraints = ut::brushbuilder::constraints();
            constraints.rotGridSize = { size, size, size };
            constraints.rotGridEnabled = enabled != 0;
            return true;
        }
        if (verb == "bspoptions") {
            std::istringstream values(rest);
            std::string optimization;
            int balance = 15;
            int zones = 1;
            int optGeom = 1;
            values >> optimization >> balance >> zones >> optGeom;
            rebuildOptions.optimization = optimization == "lame" ? ut::bsp::Optimization::Lame : optimization == "good" ? ut::bsp::Optimization::Good
                                                                                                                        : ut::bsp::Optimization::Optimal;
            rebuildOptions.balance = std::clamp(balance, 0, 100);
            rebuildOptions.zones = zones != 0;
            rebuildOptions.optimizeGeometry = optGeom != 0;
            return true;
        }
        if (verb == "surface") {
            std::istringstream values(rest);
            float x = 0;
            float y = 0;
            values >> x >> y;
            int32_t surf = pickSurface(rayDirection(ImVec2(x, y), perspectiveOrigin, perspectiveSize));
            if (surf < 0) {
                return false;
            }
            selectedSurfs.insert(surf);
            return true;
        }
        if (verb == "texture") {
            ut::UObject* texture = ut::objects::loadObject(rest, nullptr);
            ut::objects::flushLoads();
            if (!texture || !texture->isA(ut::Name("Texture"))) {
                status = "no texture " + rest;
                return false;
            }
            currentTexture = texture;
            return true;
        }
        if (verb == "applytexture") {
            if (!rest.empty() && !command("texture " + rest)) {
                return false;
            }
            ut::UModel* model = level->model;
            if (!currentTexture || !model) {
                return false;
            }
            ut::editorops::setTexture(*model, selectedSurfs, currentTexture);
            renderer->flushLevel();
            status = "applied " + currentTexture->name.str() + " to " + std::to_string(selectedSurfs.size()) + " surfaces";
            return true;
        }
        if (verb == "camera") {
            std::istringstream values(rest);
            float x = 0;
            float y = 0;
            float z = 0;
            int pitch = 0;
            int yaw = 0;
            values >> x >> y >> z >> pitch >> yaw;
            cameraLocation = { x, y, z };
            cameraRotation = { pitch, yaw, 0 };
            for (OrthoView& view : orthoViews) {
                view.centerH = component(cameraLocation, view.horizontal);
                view.centerV = component(cameraLocation, view.vertical);
            }
            return true;
        }
        if (verb == "zoom") {
            float zoom = static_cast<float>(std::atof(rest.c_str()));
            for (OrthoView& view : orthoViews) {
                view.zoom = zoom;
            }
            return true;
        }
        if (verb == "tab") {
            pendingTab = rest;
            return true;
        }
        bool contentResult = false;
        if (contentCommand(verb, rest, contentResult)) {
            return contentResult;
        }
        status = "unknown command " + verb;
        return false;
    }

    void Editor::printState()
    {
        std::string selection = selected ? selected->name.str() : std::string("none");
        ut::Vector location = selected ? ut::actor::Location(selected) : ut::Vector {};
        ut::Rotator rotation = selected ? ut::actor::Rotation(selected) : ut::Rotator {};
        std::printf("state selected=%s location=%.1f,%.1f,%.1f rotation=%d,%d,%d actors=%zu undo=%zu redo=%zu surfaces=%zu selection=%zu status=%s\n", selection.c_str(), static_cast<double>(location.x), static_cast<double>(location.y), static_cast<double>(location.z), rotation.pitch, rotation.yaw, rotation.roll, actors.size(), history.undoCount(), history.redoCount(), selectedSurfs.size(), selectionList().size(), status.c_str());
        std::fflush(stdout);
    }

    bool Editor::expect(const std::string& rest)
    {
        std::istringstream input(rest);
        std::string what;
        input >> what;
        std::string detail;
        bool passed = false;
        ut::UObject* target = editBrush();
        std::vector<ut::Vector> vertices;
        if (target && isBrush(target)) {
            for (const ut::Poly& poly : ut::brushedit::worldPolys(target)) {
                vertices.insert(vertices.end(), poly.vertices.begin(), poly.vertices.end());
            }
        }
        if (what == "status") {
            std::string text;
            std::getline(input, text);
            text = text.substr(std::min(text.size(), text.find_first_not_of(' ')));
            passed = status.find(text) != std::string::npos;
            detail = "status is '" + status + "'";
        } else if (what == "vertex" || what == "novertex") {
            ut::Vector point;
            input >> point.x >> point.y >> point.z;
            bool found = std::any_of(vertices.begin(), vertices.end(), [&](const ut::Vector& vertex) {
                return (vertex - point).sizeSquared() < 0.25f;
            });
            passed = what == "vertex" ? found : !found;
            detail = std::to_string(vertices.size()) + " brush vertices checked";
        } else if (what == "grid" || what == "offgrid") {
            float size = 16.0f;
            input >> size;
            auto onGrid = [&](float value) {
                return std::fabs(value - std::round(value / size) * size) < 0.01f;
            };
            bool all = !vertices.empty() && std::all_of(vertices.begin(), vertices.end(), [&](const ut::Vector& vertex) {
                return onGrid(vertex.x) && onGrid(vertex.y) && onGrid(vertex.z);
            });
            passed = what == "grid" ? all : !vertices.empty() && !all;
            detail = std::to_string(vertices.size()) + " brush vertices checked";
        } else if (what == "mainscale") {
            float minimum = 1.0f;
            input >> minimum;
            size_t brushes = 0;
            passed = true;
            for (ut::UObject* actor : selectionList()) {
                if (!isBrush(actor)) {
                    continue;
                }
                ut::Vector scale = ut::brushedit::mainScale(actor);
                passed = passed && scale.x > minimum && scale.y > minimum && scale.z > minimum;
                brushes++;
                detail += actor->name.str() + "=" + formatNumber(scale.x) + "," + formatNumber(scale.y) + "," + formatNumber(scale.z) + " ";
            }
            passed = passed && brushes > 0;
        } else {
            detail = "unknown expectation";
        }
        std::printf("expect %s: %s (%s)\n", passed ? "passed" : "FAILED", rest.c_str(), detail.c_str());
        std::fflush(stdout);
        return passed;
    }

    void Editor::scheduleAction(int frame, const std::string& action)
    {
        std::istringstream input(action);
        std::string verb;
        input >> verb;
        std::string viewName;
        auto push = [&](int at, int type, float x, float y, int button, bool down, ImGuiKey key) {
            InputEvent event;
            event.frame = at;
            event.type = type;
            event.x = x;
            event.y = y;
            event.button = button;
            event.down = down;
            event.key = key;
            event.view = viewName;
            inputEvents.push_back(event);
        };
        if (verb == "clickview" || verb == "dragview") {
            input >> viewName;
            verb = verb == "clickview" ? "click" : "drag";
        }
        if (verb == "click" || verb == "rclick") {
            float x = 0;
            float y = 0;
            input >> x >> y;
            int button = verb == "click" ? 0 : 1;
            push(frame, 0, x, y, 0, false, ImGuiKey_None);
            push(frame + 1, 1, x, y, button, true, ImGuiKey_None);
            push(frame + 2, 1, x, y, button, false, ImGuiKey_None);
            return;
        }
        if (verb == "drag" || verb == "rdrag") {
            float x1 = 0;
            float y1 = 0;
            float x2 = 0;
            float y2 = 0;
            input >> x1 >> y1 >> x2 >> y2;
            int button = verb == "drag" ? 0 : 1;
            push(frame, 0, x1, y1, 0, false, ImGuiKey_None);
            push(frame + 1, 1, x1, y1, button, true, ImGuiKey_None);
            for (int step = 1; step <= 8; step++) {
                float t = static_cast<float>(step) / 8.0f;
                push(frame + 1 + step, 0, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, 0, false, ImGuiKey_None);
            }
            push(frame + 10, 1, x2, y2, button, false, ImGuiKey_None);
            return;
        }
        if (verb == "wheel") {
            float x = 0;
            float y = 0;
            float delta = 0;
            input >> x >> y >> delta;
            push(frame, 0, x, y, 0, false, ImGuiKey_None);
            push(frame + 1, 3, 0, delta, 0, false, ImGuiKey_None);
            return;
        }
        if (verb == "type") {
            std::string rest;
            std::getline(input, rest);
            InputEvent event;
            event.frame = frame;
            event.type = 4;
            event.text = rest.empty() ? rest : rest.substr(1);
            inputEvents.push_back(event);
            return;
        }
        if (verb == "key" || verb == "keydown" || verb == "keyup") {
            std::string name;
            input >> name;
            ImGuiKey key = keyNamed(name);
            if (key == ImGuiKey_None) {
                std::fprintf(stderr, "unknown key %s\n", name.c_str());
                return;
            }
            if (verb != "keyup") {
                push(frame, 2, 0, 0, 0, true, key);
            }
            if (verb != "keydown") {
                push(verb == "key" ? frame + 1 : frame, 2, 0, 0, 0, false, key);
            }
            return;
        }
        scriptActions.emplace_back(frame, action);
    }

    void Editor::applyInput(int frameNumber)
    {
        ImGuiIO& io = ImGui::GetIO();
        for (InputEvent event : inputEvents) {
            if (event.frame != frameNumber) {
                continue;
            }
            if (!event.view.empty()) {
                auto rect = viewRects.find(event.view);
                if (rect == viewRects.end()) {
                    continue;
                }
                event.x = rect->second.first.x + rect->second.second.x * event.x;
                event.y = rect->second.first.y + rect->second.second.y * event.y;
            }
            if (event.type == 0) {
                io.AddMousePosEvent(event.x, event.y);
                scriptedMouse = ImVec2(event.x, event.y);
                hasScriptedMouse = true;
            } else if (event.type == 1) {
                io.AddMousePosEvent(event.x, event.y);
                io.AddMouseButtonEvent(event.button, event.down);
            } else if (event.type == 2) {
                io.AddKeyEvent(event.key, event.down);
                ImGuiKey modifier = modifierFor(event.key);
                if (modifier != ImGuiKey_None) {
                    io.AddKeyEvent(modifier, event.down);
                }
            } else if (event.type == 3) {
                io.AddMouseWheelEvent(0, event.y);
            } else if (event.type == 4) {
                io.AddInputCharactersUTF8(event.text.c_str());
            }
        }
        if (hasScriptedMouse) {
            io.AddMousePosEvent(scriptedMouse.x, scriptedMouse.y);
        }
    }

    void Editor::runScript(int frameNumber, int width, int height)
    {
        for (const auto& [frame, action] : scriptActions) {
            if (frame != frameNumber) {
                continue;
            }
            std::istringstream input(action);
            std::string verb;
            input >> verb;
            std::string rest;
            std::getline(input, rest);
            size_t first = rest.find_first_not_of(' ');
            rest = first == std::string::npos ? std::string() : rest.substr(first);
            if (verb == "exec") {
                command(rest);
            } else if (verb == "shot") {
                screenshot(width, height, rest);
            } else if (verb == "state") {
                printState();
            } else if (verb == "expect") {
                (expect(rest) ? passedExpectations : failedExpectations)++;
            }
        }
    }
}
