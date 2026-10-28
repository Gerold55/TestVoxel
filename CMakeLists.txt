cmake_minimum_required(VERSION 3.15)
project(VoxelEngine C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Find vcpkg packages
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(unofficial-lua CONFIG REQUIRED)

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
    unofficial::lua::lua
    opengl32
)