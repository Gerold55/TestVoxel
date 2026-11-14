cmake_minimum_required(VERSION 3.15)
project(VoxelEngine C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Find vcpkg packages
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# vcpkg ships Lua as unofficial-lua, other platforms use CMake's FindLua
find_package(unofficial-lua CONFIG QUIET)
if (TARGET unofficial::lua::lua)
    set(VOXEL_LUA_TARGET unofficial::lua::lua)
else()
    find_package(Lua 5.4 REQUIRED)
    add_library(voxel_lua IMPORTED INTERFACE)
    target_include_directories(voxel_lua INTERFACE ${LUA_INCLUDE_DIR})
    target_link_libraries(voxel_lua INTERFACE ${LUA_LIBRARIES})
    set(VOXEL_LUA_TARGET voxel_lua)
endif()

if (NOT unofficial-lua_FOUND AND NOT Lua_FOUND)
    message(FATAL_ERROR "Could not find unofficial-lua (VCPKG) or Lua library")
endif()

# ENet (multiplayer networking) - downloaded and built automatically the first time
# CMake configures. ENet's own CMakeLists is old, so tell newer CMake versions to accept it.
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(enet
    URL https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(enet)

add_library(stb IMPORTED INTERFACE)
add_library(unofficial::stb ALIAS stb)

target_include_directories(stb INTERFACE "${CMAKE_CURRENT_LIST_DIR}/libs")

add_executable(VoxelEngine
    main.cpp
    Player.cpp
    World.cpp
    ChunkMesh.cpp
    Block.cpp
    Hotbar.cpp
    PauseMenu.cpp
    FontRenderer.cpp
    ParticleSystem.cpp
    Lighting.cpp
    Lighting.h
    Skybox.cpp
    Skybox.h
    CaveGenerator.cpp
    CaveGenerator.h
    Model.cpp
    Model.h
    ChatSystem.cpp
    ChatSystem.h
    MainMenu.cpp
    MainMenu.h
    Clouds.cpp
    Clouds.h
    Fog.h
    Fog.cpp
    Inventory.cpp
    Inventory.h
    WorldRenderer.cpp
    WorldRenderer.h
    ChunkData.cpp
    ChunkData.h
    Net.cpp
    Net.h
    PlayerAvatars.cpp
    PlayerAvatars.h
    deps/glad.c
)

target_include_directories(VoxelEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps
    ${enet_SOURCE_DIR}/include
)

target_link_libraries(VoxelEngine PRIVATE
    glfw
    glm::glm
    sol2::sol2
    nlohmann_json::nlohmann_json
    OpenGL::GL
    ${CMAKE_DL_LIBS}
    unofficial::stb
    ${VOXEL_LUA_TARGET}
    enet
)

# ENet needs the Windows socket and timer libraries
if (WIN32)
    target_link_libraries(VoxelEngine PRIVATE ws2_32 winmm)
endif()

if (TARGET unofficial::lua::lua)
    target_link_libraries(VoxelEngine PRIVATE unofficial::lua::lua)
else()
    target_link_libraries(VoxelEngine PRIVATE ${LUA_LIBRARY})
endif()