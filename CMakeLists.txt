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

# --- ROBUST LUA HANDLING (Bypasses vcpkg source download timeouts) ---
find_package(Lua 5.4 QUIET)
if (Lua_FOUND)
    message(STATUS "Using system/standard Lua 5.4 package")
    if (NOT TARGET Lua::Lua)
        add_library(Lua::Lua INTERFACE IMPORTED)
        target_include_directories(Lua::Lua INTERFACE ${LUA_INCLUDE_DIR})
        target_link_libraries(Lua::Lua INTERFACE ${LUA_LIBRARIES})
    endif()
    set(VOXEL_LUA_TARGET Lua::Lua)
else()
    # Fallback to vcpkg unofficial-lua if system Lua isn't present
    find_package(unofficial-lua CONFIG QUIET)
    if (TARGET unofficial::lua::lua)
        set(VOXEL_LUA_TARGET unofficial::lua::lua)
        message(STATUS "Using vcpkg unofficial-lua target")
    else()
        message(FATAL_ERROR "Could not find Lua 5.4 or unofficial-lua. Please install liblua5.4-dev or check vcpkg.")
    endif()
endif()
# -------------------------------------------------------------------

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
    ChatSystem.cpp
    Lighting.cpp
    Lighting.h
    Skybox.cpp
    Skybox.h
    Clouds.cpp
    Clouds.h
    CaveGenerator.cpp
    CaveGenerator.h
    Model.cpp
    Model.h
    MainMenu.cpp
    MainMenu.h
    deps/glad.c
)

target_include_directories(VoxelEngine PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps
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
)

if (TARGET unofficial::lua::lua)
    target_link_libraries(VoxelEngine PRIVATE unofficial::lua::lua)
endif()