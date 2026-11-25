set(OPENUT_WARNINGS
  -Wall -Wextra -Wpedantic -Walloca -Wcast-align -Wcast-qual
  -Wconversion -Wdouble-promotion -Werror -Wformat=2
  -Wimplicit-fallthrough -Winit-self -Wnon-virtual-dtor
  -Wold-style-cast -Woverloaded-virtual
  -Wredundant-decls -Wshadow -Wsign-conversion -Wsuggest-override
  -Wswitch-enum -Wundef -Wuninitialized -Wunused
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  list(APPEND OPENUT_WARNINGS -Wno-restrict)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND OPENUT_WARNINGS -Wno-double-promotion -Wno-unneeded-internal-declaration -Wno-unused-but-set-variable)
endif()

# Sanitizers
set(SANITIZER "none" CACHE STRING "Sanitizer to build with: none, asan, tsan")
set_property(CACHE SANITIZER PROPERTY STRINGS none asan tsan)

if(SANITIZER STREQUAL "asan")
  set(SANITIZER_FLAGS -fsanitize=address,undefined,float-cast-overflow -fno-sanitize=vptr)
elseif(SANITIZER STREQUAL "tsan")
  set(SANITIZER_FLAGS -fsanitize=thread,undefined -fno-sanitize=vptr)
elseif(SANITIZER STREQUAL "none")
  set(SANITIZER_FLAGS "")
else()
  message(FATAL_ERROR "SANITIZER must be one of: none, asan, tsan (got '${SANITIZER}')")
endif()

file(GLOB UT_ENGINE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cc
)
add_library(ut-engine STATIC ${UT_ENGINE_SOURCES})
target_include_directories(ut-engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ut-engine PUBLIC miniaudio SDL2::SDL2-static PRIVATE stb_truetype liberation_fonts)
if(WIN32)
  target_link_libraries(ut-engine PUBLIC ws2_32)
endif()

file(GLOB UT_RENDER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/render/*.cc)
add_library(ut-render STATIC ${UT_RENDER_SOURCES})
target_include_directories(ut-render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ut-render PUBLIC ut-engine glad stb_image)

file(GLOB UT_GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game/*.cc)
add_executable(openut ${UT_GAME_SOURCES})
target_link_libraries(openut PRIVATE ut-render SDL2::SDL2-static SDL2::SDL2main)
if(UT_NETWORKING)
  file(GLOB UT_NET_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/net/*.cc)
  add_library(ut-net STATIC ${UT_NET_SOURCES})
  target_include_directories(ut-net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(ut-net PUBLIC ut-engine PRIVATE GameNetworkingSockets::static)
  target_link_libraries(openut PRIVATE ut-net)
  target_compile_definitions(openut PRIVATE UT_NETWORKING)
endif()
if(EMSCRIPTEN)
  set(UT_WEB_EXCLUDE *.dll *.exe IpDrv.* IpServer.* D3DDrv.int Galaxy.int GlideDrv.int MeTaLDrv.int
    OpenGlDrv.int OpenGlDrv.ini SGLDrv.int SoftDrv.int WinDrv.int Window.int Setup.int Startup.int)
  list(TRANSFORM UT_WEB_EXCLUDE PREPEND "SHELL:--exclude-file */")
  target_compile_definitions(openut PRIVATE OPENUT_GAME_DATA="/game")
  set_target_properties(openut PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
  set(UT_WEB_SHELL ${CMAKE_SOURCE_DIR}/cmake/emscripten/shell.html)
  set_property(TARGET openut APPEND PROPERTY LINK_DEPENDS ${UT_WEB_SHELL})
  target_link_options(openut PRIVATE
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=16MB
    -sEXPORTED_RUNTIME_METHODS=FS -lidbfs.js
    -sLZ4=1 --use-preload-cache
    --shell-file ${UT_WEB_SHELL}
    "SHELL:--preload-file ${OPENUT_GAME_DATA}/System@/game/System"
    ${UT_WEB_EXCLUDE}
    "SHELL:--preload-file ${OPENUT_GAME_DATA}/Maps@/game/Maps"
    "SHELL:--preload-file ${OPENUT_GAME_DATA}/Music@/game/Music"
    "SHELL:--preload-file ${OPENUT_GAME_DATA}/Sounds@/game/Sounds"
    "SHELL:--preload-file ${OPENUT_GAME_DATA}/Textures@/game/Textures"
  )
else()
  target_compile_definitions(openut PRIVATE OPENUT_GAME_DATA="${OPENUT_GAME_DATA}")
endif()

set(UT_TARGETS ut-engine ut-render openut)
if(UT_NETWORKING)
  list(APPEND UT_TARGETS ut-net)
endif()

file(GLOB UT_COMPILER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compiler/*.cc)
add_library(ut-compiler STATIC ${UT_COMPILER_SOURCES})
target_include_directories(ut-compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ut-compiler PUBLIC ut-engine)
list(APPEND UT_TARGETS ut-compiler)

if(NOT EMSCRIPTEN)
  file(GLOB UT_UCC_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ucc/*.cc)
  add_executable(ucc ${UT_UCC_SOURCES})
  target_link_libraries(ucc PRIVATE ut-compiler SDL2::SDL2-static)
  target_compile_definitions(ucc PRIVATE OPENUT_GAME_DATA="${OPENUT_GAME_DATA}")
  target_link_options(ucc PRIVATE -static-libstdc++ -static-libgcc)
  list(APPEND UT_TARGETS ucc)
endif()

file(GLOB UT_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cc)
if(UT_EDITOR_SOURCES)
  add_executable(openut-editor ${UT_EDITOR_SOURCES})
  target_link_libraries(openut-editor PRIVATE ut-render ut-compiler imgui imguizmo SDL2::SDL2-static SDL2::SDL2main)
  target_include_directories(openut-editor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/icons)
  target_compile_definitions(openut-editor PRIVATE OPENUT_GAME_DATA="${OPENUT_GAME_DATA}")
  target_link_options(openut-editor PRIVATE -static-libstdc++ -static-libgcc)
  list(APPEND UT_TARGETS openut-editor)
endif()

foreach(tgt ${UT_TARGETS})
  target_compile_options(${tgt} PRIVATE ${OPENUT_WARNINGS})

  if(SANITIZER_FLAGS)
    target_compile_options(${tgt} PUBLIC ${SANITIZER_FLAGS} -fno-omit-frame-pointer -g)
    target_link_options(${tgt} PUBLIC ${SANITIZER_FLAGS})
    target_compile_options(${tgt} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>)
  endif()

  target_compile_options(${tgt} PRIVATE
    $<$<CONFIG:Debug>:-D_GLIBCXX_ASSERTIONS>
  )
endforeach()

if(NOT EMSCRIPTEN)
  target_link_options(openut PRIVATE -static-libstdc++ -static-libgcc)
endif()
