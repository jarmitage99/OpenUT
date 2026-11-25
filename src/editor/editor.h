#pragma once

#include "audio/audio.h"
#include "core/config.h"
#include "core/objects.h"
#include "editor/icons.h"
#include "editor/log_capture.h"
#include "editor/script_workspace.h"
#include "engine/actor.h"
#include "engine/actor_search.h"
#include "engine/brush_builder.h"
#include "engine/brush_edit.h"
#include "engine/bsp.h"
#include "engine/csg.h"
#include "engine/editor_history.h"
#include "engine/editor_ops.h"
#include "engine/editor_views.h"
#include "engine/extra_builders.h"
#include "engine/floor_lofter.h"
#include "engine/shape_editor.h"
#include "engine/special_brush.h"
#include "engine/fractal_paint.h"
#include "engine/lightmap_builder.h"
#include "engine/map_check.h"
#include "engine/mesh.h"
#include "engine/map_writer.h"
#include "engine/path_builder.h"
#include "engine/property_edit.h"
#include "engine/props_engine.h"
#include "engine/surface_select.h"
#include "engine/resources.h"
#include "engine/world.h"
#include "render/scene_renderer.h"
#include "render/texture_cache.h"

#include <SDL.h>
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ut::editor {
    constexpr uint32_t EditorActorFlags = ut::RF_Transactional | 0x00070000;

    struct Options {
        std::string map = "DM-Deck16][";
        std::string blankMap;
        std::string screenshot;
        std::string select;
        std::vector<std::pair<int, std::string>> actions;
        int frames = -1;
        int width = 1600;
        int height = 900;
    };

    inline void saveBmp(const std::string& path, std::vector<uint8_t>& rgba, int width, int height)
    {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(rgba.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ABGR8888);
        SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }

    inline std::string lower(const std::string& text)
    {
        std::string result = text;
        for (char& c : result) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return result;
    }

    inline std::filesystem::path userMapsDirectory()
    {
        return ut::config::userDirectory() / "Maps";
    }

    inline ImGuiKey keyNamed(const std::string& name)
    {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
            if (lower(ImGui::GetKeyName(static_cast<ImGuiKey>(key))) == lower(name)) {
                return static_cast<ImGuiKey>(key);
            }
        }
        return ImGuiKey_None;
    }

    inline ImGuiKey modifierFor(ImGuiKey key)
    {
        if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift) {
            return ImGuiMod_Shift;
        }
        if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl) {
            return ImGuiMod_Ctrl;
        }
        if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt) {
            return ImGuiMod_Alt;
        }
        return ImGuiKey_None;
    }

    inline std::string withIcon(const char* icon, const char* text)
    {
        return std::string(icon) + "  " + text;
    }

    inline std::string formatNumber(float value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", static_cast<double>(std::round(value * 10.0f) / 10.0f));
        return text;
    }

    inline bool sameElement(const std::vector<ut::Vector>& a, const std::vector<ut::Vector>& b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        return std::all_of(a.begin(), a.end(), [&](const ut::Vector& vertex) {
            return std::any_of(b.begin(), b.end(), [&](const ut::Vector& other) {
                return (other - vertex).sizeSquared() < 0.01f;
            });
        });
    }

    struct Edge {
        ut::Vector a;
        ut::Vector b;
    };

    struct ComponentCache {
        ut::UObject* brush = nullptr;
        int mode = -1;
        uint64_t signature = 0;
        std::vector<std::vector<ut::Vector>> elements;
        std::vector<ut::Vector> centers;
        std::vector<ut::Poly> polys;
    };

    struct OrthoView {
        const char* title;
        int horizontal;
        int vertical;
        float verticalSign;
        float centerH = 0;
        float centerV = 0;
        float zoom = 0.08f;
    };

    struct ViewPane {
        ut::editorviews::PaneKind kind = ut::editorviews::PaneKind::Top;
        ut::RenderMode mode = ut::RenderMode::Textured;
        bool shared = true;
        OrthoView ortho {};
        ut::Vector location;
        ut::Rotator rotation;
        uint32_t framebuffer = 0;
        uint32_t colorTexture = 0;
        uint32_t depthBuffer = 0;
        int fbWidth = 0;
        int fbHeight = 0;
    };

    using SpecialPreset = ut::specialbrush::Preset;

    struct InputEvent {
        int frame = 0;
        int type = 0;
        float x = 0;
        float y = 0;
        int button = 0;
        bool down = false;
        ImGuiKey key = ImGuiKey_None;
        std::string view;
        std::string text;
    };

    struct ScriptDocument {
        ut::UClass* cls = nullptr;
        std::string text;
        std::string compiled;
        std::vector<size_t> lineStarts;
        std::vector<uint8_t> commentState;
        float width = 0;
        int jumpLine = 0;
        bool scrollPending = false;
        int cursor = 0;
        int lastCursor = -1;
    };

    inline float component(const ut::Vector& v, int axis)
    {
        if (axis == 0) {
            return v.x;
        }
        if (axis == 1) {
            return v.y;
        }
        return v.z;
    }

    inline float& componentRef(ut::Vector& v, int axis)
    {
        if (axis == 0) {
            return v.x;
        }
        if (axis == 1) {
            return v.y;
        }
        return v.z;
    }

    class Editor {
    public:
        Editor(SDL_Window* target, const Options& settings, const std::filesystem::path& gameExecutable);
        ~Editor();

        Editor(const Editor&) = delete;
        Editor& operator=(const Editor&) = delete;

        void start();
        bool openMapFile(const std::filesystem::path& file);
        bool openMap(const std::string& mapName);
        bool useLevel(ut::ULevel* newLevel, const std::string& mapName);
        void refreshActors();
        void placeCamera();
        void focusSelected();
        void select(ut::UObject* actor);
        void buildEdges();
        void buildClassTree();
        std::vector<Edge> brushEdges(ut::UObject* actor);
        const std::vector<Edge>& cachedBrushEdges(ut::UObject* actor);
        ImU32 brushColor(ut::UObject* actor);
        ut::UObject* builderBrush();
        bool isBrush(ut::UObject* actor);
        void ensureFramebuffer(int width, int height);
        void releaseFramebuffer();
        void renderPerspective(int width, int height);
        bool projectCamera(const ut::Vector& point, ut::Vector& out);
        ImVec2 cameraToScreen(const ut::Vector& camera, const ImVec2& origin, const ImVec2& size);
        void drawPerspectiveLine(ImDrawList* list, const ut::Vector& a, const ut::Vector& b, const ImVec2& origin, const ImVec2& size, ImU32 color);
        void drawCylinder(ImDrawList* list, ut::UObject* actor, const ImVec2& origin, const ImVec2& size);
        ut::Vector rayDirection(const ImVec2& mouse, const ImVec2& origin, const ImVec2& size);
        ut::UObject* pickPerspective(const ImVec2& mouse, const ImVec2& origin, const ImVec2& size);
        int32_t pickSurface(const ut::Vector& direction);
        void drawSelectedSurfaces(ImDrawList* list, const ImVec2& origin, const ImVec2& size);
        void perspectiveWindow(const ImVec2& position, const ImVec2& size);
        ImVec2 orthoToScreen(const OrthoView& view, const ut::Vector& point, const ImVec2& origin, const ImVec2& size);
        void orthoWindow(OrthoView& view, const ImVec2& position, const ImVec2& size);
        void finishDrag();
        void moveSelectionWithKeys();
        void actorBrowser();
        void classNode(ut::UClass* cls);
        void classBrowser();
        void newClassPopup();
        void openDocument(ut::UClass* cls, int line);
        void refreshClasses();
        bool compileScripts(bool all);
        void scriptView(ScriptDocument& document, float height);
        void scriptWindow();
        void loadTexturePackage(const std::string& packageName);
        void textureBrowser();
        bool editableType(const ut::UProperty* property);
        void transformPanel();
        void trackEdit(const std::string& label);
        void propertiesPanel();
        bool setProperty(ut::UObject* actor, ut::UProperty* property, int32_t index, const std::string& value);
        void propertyRow(ut::UProperty* property, int32_t index);
        void valueEditor(ut::UObject* object, ut::UProperty* property, uint8_t* memory, const void* key, bool tracked, const std::function<bool(const std::string&)>& apply);
        void objectPropertyRow(ut::UObject* object, ut::UProperty* property, int32_t index, bool tracked);
        void builderPanel();
        std::vector<ut::UClass*> moverClasses();
        void surfacePanel();
        void rebuildWindow();
        void gridWindow();
        ut::Vector snapped(const ut::Vector& location);
        void menuBar();
        void frame(int width, int height);
        bool newMap(const std::string& name);
        ut::UObject* levelInfo();
        ut::UClass* resolveClass(const std::string& name);
        ut::UObject* spawnActor(ut::UClass* cls, const ut::Vector& location, const ut::Rotator& rotation, ut::UObject* templateActor);
        void restoreRegions(bool restore);
        bool saveTo(const std::filesystem::path& file);
        void launchGame(const std::filesystem::path& file);
        void captureRegions();
        void geometryChanged();
        bool surfaceEdit(const std::string& label, const std::function<void(ut::UModel&)>& edit);
        void surfaceSelectMenu();
        void surfacePopup();
        bool polySelect(const std::string& line);
        bool textureCommand(const std::string& verb, const std::string& rest);
        void texturePropertiesWindow();
        std::set<int32_t> surfMemory;
        ut::UObject* propertiesTexture = nullptr;
        bool showTextureProperties = false;
        std::vector<std::string> textureGroupNames;
        int textureGroupChoice = -1;
        bool scrollToTexture = false;
        bool runBrushBuilder(const std::string& line);
        bool pathsCommand(const std::string& line);
        bool command(const std::string& line);
        void printState();
        bool expect(const std::string& rest);
        void scheduleAction(int frame, const std::string& action);
        void applyInput(int frameNumber);
        void runScript(int frameNumber, int width, int height);
        void run();
        void screenshot(int width, int height, const std::string& path);

        SDL_Window* window;
        Options options;
        std::filesystem::path gamePath;
        std::unique_ptr<ut::SceneRenderer> renderer;
        ut::ULevel* level = nullptr;
        std::vector<ut::UObject*> actors;
        std::vector<Edge> edges;
        std::unordered_map<ut::UObject*, std::vector<Edge>> brushCache;
        std::unordered_map<ut::UObject*, std::array<uint8_t, sizeof(ut::PointRegion)>> savedRegions;
        std::vector<std::string> mapNames;
        std::vector<std::string> texturePackages;
        std::string texturePackage;
        std::vector<ut::UObject*> textureList;
        ut::UObject* currentTexture = nullptr;
        std::string currentMap;
        std::filesystem::path currentFile;
        std::string pendingCommand;
        std::string pendingTab;
        std::string status;
        std::map<ut::UClass*, std::vector<ut::UClass*>> classChildren;
        ut::UClass* actorClass = nullptr;
        ut::UClass* placeClass = nullptr;
        ut::UObject* selected = nullptr;
        std::set<int32_t> selectedSurfs;
        bool surfaceMode = false;
        bool selectionChanged = false;
        bool dragging = false;
        ut::Vector dragStart;
        ut::Vector dragRaw;
        const void* editingKey = nullptr;
        ut::UObject* editingObject = nullptr;
        bool focusEdit = false;
        std::array<char, 512> editBuffer {};
        std::array<char, 128> filter {};
        std::array<char, 128> saveName {};
        std::array<char, 256> brushFile {};
        bool brushMerge = true;
        bool editableOnly = false;
        bool showActorsTab = false;
        bool pendingActorsTab = false;
        ut::Vector cameraLocation;
        ut::Rotator cameraRotation;
        float fov = 90.0f;
        float moveSpeed = 800.0f;
        std::array<OrthoView, 3> orthoViews {};
        ImVec2 perspectiveOrigin;
        ImVec2 perspectiveSize;
        std::map<std::string, std::pair<ImVec2, ImVec2>> viewRects;
        uint32_t framebuffer = 0;
        uint32_t colorTexture = 0;
        uint32_t depthBuffer = 0;
        int fbWidth = 0;
        int fbHeight = 0;
        bool running = true;
        int failedExpectations = 0;
        int passedExpectations = 0;
        ut::EditorHistory history;
        ut::csg::RebuildOptions rebuildOptions;
        size_t builderChoice = 0;
        std::vector<ut::brushbuilder::ExtraBuilder> extraBuilderList;
        size_t specialChoice = 0;
        size_t moverChoice = 0;
        int panStep[2] = { 16, 0 };
        float scaleValues[2] = { 1.0f, 1.0f };
        bool showRebuild = false;
        bool showGrid = false;
        bool lightSelectedOnly = false;
        int thumbnailSize = 64;
        ut::TextureCache thumbnails;
        int pathOptimization = 1;
        std::vector<SpecialPreset> specialPresets = ut::specialbrush::presets();
        ut::editor::ScriptWorkspace scripts;
        std::vector<ScriptDocument> documents;
        int activeDocument = -1;
        int selectDocument = -1;
        bool showScripts = false;
        bool showAllClasses = false;
        std::array<char, 64> newPackage {};
        std::array<char, 64> newClassName {};
        std::vector<InputEvent> inputEvents;
        std::vector<std::pair<int, std::string>> scriptActions;
        ImVec2 scriptedMouse;
        bool hasScriptedMouse = false;

        void brushMenuItems();
        void moverMenu();
        void brushScaleWindow();
        ut::UObject* editBrush();
        bool brushEditCommand(const std::string& verb, const std::string& rest);
        bool brushTransform(const std::string& label, const std::function<void(ut::UObject*)>& edit);
        bool brushPolysEdit(const std::string& label, const std::function<bool(ut::UObject*, ut::UModel&)>& edit);
        bool clipCommand(const std::string& rest);
        bool brushEditOrtho(const OrthoView& view, const ImVec2& origin, const ImVec2& size, bool hovered);
        ut::Vector orthoToWorld(const OrthoView& view, const ImVec2& point, const ImVec2& origin, const ImVec2& size, const ut::Vector& depth);
        void noteMoved(ut::UObject* actor);
        bool clipMode = false;
        bool showBrushScale = false;
        float brushScaleInput[3] = { 1.0f, 1.0f, 1.0f };
        std::vector<ut::Poly> vertexDragPolys;
        bool vertexDragging = false;
        const OrthoView* vertexDragView = nullptr;
        ut::Vector vertexDragStart;
        ut::Vector vertexDragRaw;
        std::vector<ut::Vector> clipMarkers;
        int clipViewAxis = 2;
        ut::Vector orthoClickWorld;

        std::vector<std::vector<ut::Vector>> brushComponents(const ut::UModel& model);
        const ComponentCache& componentCache();
        std::vector<ut::Vector> componentVertices();
        void chooseComponent(const std::vector<ut::Vector>& element, bool toggle);
        std::vector<std::vector<ut::Vector>> handlesAt(const std::function<bool(const ut::Vector&, ImVec2&)>& project, const ImVec2& mouse, bool occlude);
        bool handleOccluded(const std::vector<ut::Poly>& polys, const ut::Vector& world);
        std::vector<ut::Vector> cycleChoice(const std::vector<std::vector<ut::Vector>>& candidates);
        void drawComponents(ImDrawList* list, const std::function<bool(const ut::Vector&, ImVec2&)>& project, const std::vector<ut::Vector>& hoveredWorld, bool occlude);
        std::vector<ut::Vector> hoveredComponent(const ImVec2& mouse);
        bool perspectivePoint(const ut::Vector& world, ImVec2& out);
        void drawReadout(const std::string& text);
        void drawDimensions(ImDrawList* list, const OrthoView& view, const ImVec2& origin, const ImVec2& size);
        bool pickPerspectiveComponent(const ImVec2& mouse, const ImVec2& origin, const ImVec2& size, bool toggle);
        void gizmoMatrices(const ImVec2& size, float* view, float* projection);
        bool componentGizmo(const ImVec2& origin, const ImVec2& size);
        bool editToolbar(const ImVec2& origin);
        void editModeKeys();
        void editModeMenuItems();
        bool componentCommand(const std::string& verb, const std::string& rest);
        int editMode = 0;
        std::vector<std::vector<ut::Vector>> componentSelection;
        bool componentDragging = false;
        std::array<float, 16> componentMatrix {};
        ut::Vector componentPivot;
        ut::Vector componentNormal;
        std::vector<std::vector<ut::Vector>> handleCycle;
        bool cycleOnRelease = false;
        bool popupWasOpen = false;
        ComponentCache cachedComponents;

        void valueRows(ut::UObject* object, ut::UProperty* property, uint8_t* memory, const std::string& label, bool tracked, const std::function<bool(const std::string&)>& apply);
        bool memberEdit(ut::UObject* object, const std::string& label, bool tracked, const std::function<bool()>& edit);
        void objectPropertyTable(ut::UObject* object);
        void runMapCheck();
        void resultsWindow();
        void logWindow();
        void findWindow();
        bool findNext();
        void levelPropertiesWindow();
        void toolsWindows();
        bool toolsCommand(const std::string& verb, const std::string& rest);
        LogCapture log;
        std::vector<ut::mapcheck::Issue> checkResults;
        bool showResults = false;
        bool showLog = false;
        bool showFind = false;
        bool showLevelProperties = false;
        bool logScrollToEnd = true;
        std::array<char, 256> logCommand {};
        ut::actorsearch::Query findQuery;
        std::array<std::array<char, 64>, 4> findFields {};

        void viewLayout(const ImVec2& position, const ImVec2& size);
        void paneMenu(size_t index);
        void setPaneKind(size_t index, ut::editorviews::PaneKind kind);
        void swapPaneCamera(ViewPane& pane);
        void syncPanes();
        void releasePanes();
        bool viewCommand(const std::string& verb, const std::string& rest);
        void drawOrthoPaths(ImDrawList* list, const OrthoView& view, const ImVec2& origin, const ImVec2& size);
        void drawPerspectivePaths(ImDrawList* list, const ImVec2& origin, const ImVec2& size);
        bool drawActorIcon(ImDrawList* list, ut::UObject* actor, const ImVec2& point, float zoom);
        std::array<ViewPane, 4> panes;
        int maximizedPane = -1;
        bool showPaths = false;
        bool showActorIcons = true;
        ut::RenderMode perspectiveMode = ut::RenderMode::Textured;
        std::string paneTitle;
        std::string paneKey;
        size_t currentPane = 0;

        bool isSelected(ut::UObject* actor);
        std::vector<ut::UObject*> selectionList();
        std::vector<ut::UObject*> editableSelection();
        void setSelection(const std::vector<ut::UObject*>& chosen);
        void toggleSelect(ut::UObject* actor);
        void clickSelect(ut::UObject* actor, bool toggle);
        bool selectionCommand(const std::string& verb, const std::string& rest);
        void translateSelection(const ut::Vector& delta);
        void rotateSelection(const ut::Rotator& delta);
        bool actorHidden(ut::UObject* actor, const OrthoView& view);
        bool zClipEdge(const OrthoView& view, Edge& edge);
        void drawSelectionPerspective(ImDrawList* list, const ImVec2& origin, const ImVec2& size);
        bool perspectiveGizmo(const ImVec2& origin, const ImVec2& size);
        void perspectiveContext(const ImVec2& mouse, const ImVec2& origin, const ImVec2& size);
        void orthoPress(const OrthoView& view, ut::UObject* best);
        void orthoBox(const OrthoView& view, const ImVec2& origin, const ImVec2& size);
        void orthoRightClick(const OrthoView& view, const ImVec2& origin, const ImVec2& size, bool hovered);
        void selectionContextItems();
        void addHereItems();
        void selectionPopup();
        void multiPropertiesPanel();
        void selectionEditItems();
        void actorMenu();
        void selectionViewItems();
        void t3dFileItems();
        std::unordered_set<ut::UObject*> selectedActors;
        ut::UObject* contextActor = nullptr;
        ut::Vector contextPoint;
        bool boxSelecting = false;
        const OrthoView* boxView = nullptr;
        ImVec2 boxStart;
        bool zClip = false;
        float zClipRange[2] = { -256.0f, 256.0f };
        int gizmoMode = 1;
        bool gizmoActive = false;
        ut::Vector gizmoStart;
        ut::Rotator gizmoStartRotation;
        ut::Vector gizmoStartScale;
        std::array<char, 256> t3dFile {};

        std::vector<ut::UObject*> contentObjects(const std::string& packageName, const std::vector<std::string>& classNames);
        void showTexturePackage(const std::string& packageName);
        void showSoundPackage(const std::string& packageName);
        void showMusicPackage(const std::string& packageName);
        void showMeshPackage(const std::string& packageName);
        void viewMesh(ut::UObject* object);
        bool ensureAudio();
        void contentPackagesMenu();
        void openContentDialog(const char* name);
        void textureContentTools();
        void soundBrowser();
        void musicBrowser();
        void meshBrowser();
        void drawMesh(ImDrawList* list, const ImVec2& origin, const ImVec2& size);
        void meshViewerWindow();
        void fractalPaintPanel();
        bool paintFractal(int32_t x, int32_t y, bool left, bool right);
        bool runContentImport(const std::string& rest, const std::string& execCommand, std::string& packageName);
        bool contentCommand(const std::string& verb, const std::string& rest, bool& result);
        std::vector<std::string> contentModified;
        std::string contentDialog;
        std::vector<std::string> soundPackages;
        std::string soundPackage;
        std::vector<ut::UObject*> soundList;
        ut::UObject* currentSound = nullptr;
        int32_t soundVoice = 0;
        std::vector<std::string> musicPackages;
        std::string musicPackage;
        std::vector<ut::UObject*> musicList;
        ut::UObject* currentMusic = nullptr;
        std::vector<std::string> meshPackages;
        std::string meshPackage;
        std::vector<ut::UObject*> meshList;
        ut::UMesh* currentMesh = nullptr;
        std::vector<ut::UObject*> meshSkins;
        ut::Name meshSequence;
        float meshFrame = 0;
        float meshYaw = 0;
        float meshPitch = 0;
        float meshZoom = 1;
        bool meshPlaying = false;
        bool meshWire = false;
        bool showMeshViewer = false;
        bool audioReady = false;
        std::vector<ut::UClass*> textureClasses;
        int newTextureClass = 0;
        int newUSize = 8;
        int newVSize = 8;
        std::array<char, 512> importFile {};
        std::array<char, 512> importData {};
        std::array<char, 64> importPackage {};
        std::array<char, 64> importGroup {};
        std::array<char, 64> importName {};
        std::array<char, 128> importDetail {};
        std::array<float, 3> importScale { 1.0f, 1.0f, 1.0f };
        bool importMips = true;
        bool importMasked = false;
        bool importLod = true;
        ut::fractalpaint::PaintState paintState;

        bool applyBuilder(const std::string& label, const std::function<bool(ut::UObject*, std::string*)>& build);
        void builderMenuItems();
        void builderToolsMenuItems();
        void builderWindows();
        bool builderCommand(const std::string& verb, const std::string& rest);
        bool floorCommand(std::istringstream& input);
        bool shapeCommand(std::istringstream& input);
        void shapeEditorWindow();
        void shapeCanvas();
        void splitSelectedSides();
        void deleteSelectedVertices();
        void floorLofterWindow();
        void heightMapWindow();
        void addSpecialWindow();
        bool showShapeEditor = false;
        bool showFloorLofter = false;
        bool showHeightMap = false;
        bool showAddSpecial = false;
        ut::shapeeditor::Document shapeDocument = ut::shapeeditor::newDocument();
        std::vector<std::pair<size_t, size_t>> shapeSelection;
        std::vector<ut::shapeeditor::Point> shapeDragOrigins;
        ut::shapeeditor::Point shapeDragStart;
        ut::shapeeditor::Point shapeDragPivot;
        ut::shapeeditor::Point shapeContextPoint;
        bool shapeOriginSelected = false;
        bool shapeDragging = false;
        int shapeGrid = 16;
        float shapeZoom = 1.0f;
        ImVec2 shapePan;
        std::array<char, 256> shapeFile {};
        int shapeDepth = 256;
        int shapePointDepth = 256;
        int shapeBevelDepth = 128;
        int shapeBevelCap = 32;
        int shapeRevolveTotal = 8;
        int shapeRevolveSides = 4;
        ut::brushbuilder::FloorGrid floorGrid;
        int floorNewSize[3] = { 4, 4, 64 };
        int floorRow = 0;
        int floorDragVertex = -1;
        std::array<char, 256> floorImage {};
        std::array<char, 256> heightMapFile {};
        double heightMapCell = 64;
        double heightMapScale = 2;
        double heightMapBase = 64;
        ut::specialbrush::Options specialOptions;

        void classActions();
        void classContextMenu(ut::UClass* cls);
        void classDefaultsWindow();
        bool classCommand(const std::string& verb, const std::string& rest);
        void loadDocumentText(ut::UClass* cls, int line, const std::string& script);
        ut::UClass* defaultsClass = nullptr;
        ut::UClass* classMenuTarget = nullptr;
        ImGuiID classMenuId = 0;
        bool classMenuRequest = false;
        bool showClassDefaults = false;
        bool openNewClass = false;
        bool openLoadClass = false;
        std::array<char, 512> loadClassFile {};
    };
}
