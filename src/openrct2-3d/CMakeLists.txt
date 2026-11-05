# Filament owns the 3D device; bx remains only as the existing camera math utility.
include(cmake/Filament.cmake)
add_library(openrct2-view3d-math STATIC third_party/bx/src/amalgamated.cpp)
target_include_directories(openrct2-view3d-math SYSTEM PUBLIC third_party/bx/include)
target_compile_definitions(openrct2-view3d-math PUBLIC BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
target_compile_features(openrct2-view3d-math PUBLIC cxx_std_20)
if(MSVC)
    target_include_directories(openrct2-view3d-math PRIVATE third_party/bx/include/compat/msvc)
    target_compile_options(openrct2-view3d-math PRIVATE /w /Zc:__cplusplus /Zc:preprocessor)
    target_compile_definitions(openrct2-view3d-math PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(openrct2-view3d-math PRIVATE -w)
endif()
target_link_libraries(openrct2-view3d-math PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(openrct2-view3d-math PRIVATE psapi)
endif()

add_library(
    openrct2-3d STATIC
    src/ParkView.cpp
    src/GlbMesh.cpp
    src/MeshDraw.cpp
    src/SceneryDraw.cpp
    src/ModelCache.cpp
    src/SceneryChunk.cpp
    src/GpuBudget.cpp
    src/ModelLoadQueue.cpp
    third_party/cgltf/cgltf.cpp
    third_party/stb_image_impl.cpp)
# cgltf and stb_image are third-party. Keep their warnings from failing this build.
if(NOT MSVC)
    set_source_files_properties(third_party/cgltf/cgltf.cpp third_party/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
add_library(OpenRCT2::openrct2-3d ALIAS openrct2-3d)
target_link_libraries(openrct2-3d PUBLIC openrct2-filament libopenrct2)
target_link_libraries(openrct2-3d PRIVATE openrct2-view3d-math)
target_include_directories(
    openrct2-3d
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${ROOT_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bimg/3rdparty/stb")

# Offscreen preview of a GLB and the crossed-sprite fallback. Not installed.
add_executable(
    openrct2-3d-preview preview/ModelPreview.cpp src/GlbMesh.cpp src/MeshDraw.cpp src/GpuBudget.cpp third_party/cgltf/cgltf.cpp
                        third_party/stb_image_impl.cpp)
target_include_directories(
    openrct2-3d-preview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bimg/3rdparty/stb")
target_link_libraries(openrct2-3d-preview PRIVATE openrct2-filament openrct2-view3d-math)
target_compile_features(openrct2-3d-preview PRIVATE cxx_std_20)
target_compile_features(openrct2-3d PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(openrct2-3d PRIVATE /Zc:__cplusplus /Zc:preprocessor)
    target_compile_options(openrct2-3d-preview PRIVATE /Zc:__cplusplus /Zc:preprocessor)
endif()

if(NOT DISABLE_GUI)
    target_sources(openrct2-3d PRIVATE src/FilamentView.cpp)
endif()
if(TARGET PkgConfig::SDL2)
    target_link_libraries(openrct2-3d PRIVATE PkgConfig::SDL2)
elseif(TARGET SDL2::SDL2-static)
    target_link_libraries(openrct2-3d PRIVATE SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
    target_link_libraries(openrct2-3d PRIVATE SDL2::SDL2)
elseif(SDL2_LDFLAGS AND NOT SDL2_LDFLAGS MATCHES "-NOTFOUND")
    target_include_directories(openrct2-3d PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(openrct2-3d PRIVATE ${SDL2_LDFLAGS})
endif()

# The executable already exists: openrct2-ui is included before this directory.
# Android and Emscripten do not reach this, and compile the UI stub instead.
if(TARGET openrct2)
    # openrct2-ui already linked this target with the plain signature.
    target_link_libraries(openrct2 openrct2-3d)
    target_compile_definitions(openrct2 PRIVATE OPENRCT2_HAVE_VIEW3D=1)
endif()
