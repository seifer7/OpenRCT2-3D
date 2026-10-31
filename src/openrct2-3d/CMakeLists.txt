# 3D viewer. bgfx, bx, and bimg are pinned submodules under third_party/.
# Examples and tools stay off so this only builds the renderer library.

set(BX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bx" CACHE PATH "" FORCE)
set(BIMG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bimg" CACHE PATH "" FORCE)
set(BGFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bgfx" CACHE PATH "" FORCE)

set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
# Static archive so the game does not have to find libbgfx.so at runtime.
set(BGFX_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)

# OpenRCT2's STATIC option is a cache variable. bgfx.cmake compares
# BGFX_LIBRARY_TYPE to the unquoted token STATIC, and CMake then reads that
# option instead of the literal. Shadow it for this directory only.
set(STATIC "STATIC")
add_subdirectory(third_party/bgfx.cmake)
unset(STATIC)

# OpenRCT2 compiles the rest of the tree with warnings as errors. bgfx and
# its dependencies do not, so keep those warnings from failing this build.
if(MSVC)
    set(bgfx_warning_flags /WX-)
else()
    set(bgfx_warning_flags -Wno-error)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND bgfx_warning_flags -Wno-missing-prototypes -Wno-missing-declarations)
    endif()
endif()
foreach(target bx bimg bimg_decode bimg_encode bgfx glslang spirv-cross spirv-opt l-smash tint)
    if(TARGET ${target})
        target_compile_options(${target} PRIVATE ${bgfx_warning_flags})
    endif()
endforeach()

# GCC's x86 headers declare _mm_cvtsi128_si64 and _bswap64 only for x86-64.
# etcpak calls both from a template, so the 32-bit MinGW build rejects them.
# Provide the same operations from intrinsics that do exist on i386.
if(MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND TARGET bimg_encode)
    target_compile_options(
        bimg_encode
        PRIVATE
            "-D_mm_cvtsi128_si64(value)=((__INT64_TYPE__)(__UINT32_TYPE__)_mm_cvtsi128_si32(value)|((__INT64_TYPE__)(__UINT32_TYPE__)_mm_cvtsi128_si32(_mm_srli_si128((value),4))<<32))"
            "-D_bswap64(x)=__builtin_bswap64(x)")
endif()

add_library(
    openrct2-3d STATIC
    src/BgfxRenderer.cpp
    src/View3D.cpp
    src/ParkView.cpp
    src/GlbMesh.cpp
    src/MeshDraw.cpp
    src/SceneryDraw.cpp
    third_party/cgltf/cgltf.cpp
    third_party/stb_image_impl.cpp)
# cgltf and stb_image are third-party. Keep their warnings from failing this build.
if(NOT MSVC)
    set_source_files_properties(third_party/cgltf/cgltf.cpp third_party/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
add_library(OpenRCT2::openrct2-3d ALIAS openrct2-3d)

# bx is private to bgfx, and ParkView calls bx math directly. Keep that link
# private so bx's compile flags stay off the game executable. libopenrct2 is
# linked again so the executable resolves ParkView's game symbols after this
# static library.
target_link_libraries(openrct2-3d PUBLIC bgfx libopenrct2)
target_link_libraries(openrct2-3d PRIVATE bx)
target_include_directories(
    openrct2-3d
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${ROOT_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bimg/3rdparty/stb")

# Offscreen preview of a GLB and the crossed-sprite fallback. Not installed.
add_executable(openrct2-3d-preview preview/ModelPreview.cpp src/GlbMesh.cpp src/MeshDraw.cpp third_party/cgltf/cgltf.cpp third_party/stb_image_impl.cpp)
target_include_directories(
    openrct2-3d-preview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf"
                                       "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bimg/3rdparty/stb")
target_link_libraries(openrct2-3d-preview PRIVATE bgfx bx)
target_compile_features(openrct2-3d-preview PRIVATE cxx_std_20)
target_compile_features(openrct2-3d PUBLIC cxx_std_20)

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
