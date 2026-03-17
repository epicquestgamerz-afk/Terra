cmake_minimum_required(VERSION 3.20)

# ============================================================
# Project
# ============================================================
project(
    TerraEngine
    VERSION 1.0.0
    DESCRIPTION "Terra AI Engine"
    LANGUAGES CXX
)

# ============================================================
# C++ Standard
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Default Build Type
# ============================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================
# Dependencies
# ============================================================
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Optional graphics
find_package(OpenGL)

# ============================================================
# nlohmann/json
# ============================================================
include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(json)

# ============================================================
# Sources
# ============================================================
set(TERRA_SOURCES

    AIConnector.cpp
    AgentBase.cpp
    AgentBus.cpp
    TransformerBase.cpp
    TerraCore.cpp
    TerraEngine.cpp

    agents/AgentWorld.cpp
    agents/AgentWeather.cpp
    agents/AgentNPC.cpp
    agents/AgentBoss.cpp
    agents/AgentEnemy.cpp
    agents/AgentAnimal.cpp
    agents/AgentWater.cpp
    agents/AgentDay.cpp
    agents/AgentPlayerCare.cpp
)

# ============================================================
# Executable
# ============================================================
add_executable(TerraEngine)

target_sources(TerraEngine PRIVATE ${TERRA_SOURCES})

# ============================================================
# Include directories
# ============================================================
target_include_directories(TerraEngine PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}

)

# ============================================================
# Link libraries
# ============================================================
target_link_libraries(TerraEngine PRIVATE

    CURL::libcurl
    Threads::Threads
    nlohmann_json::nlohmann_json

)

# Optional OpenGL
if(OpenGL_FOUND)
    target_link_libraries(TerraEngine PRIVATE OpenGL::GL)
endif()

# ============================================================
# Compiler Warnings
# ============================================================
if(MSVC)

    target_compile_options(TerraEngine PRIVATE
        /W4
    )

else()

    target_compile_options(TerraEngine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

endif()

# ============================================================
# Platform specific
# ============================================================
if(WIN32)

    target_link_libraries(TerraEngine PRIVATE ws2_32 winmm)
    target_compile_definitions(TerraEngine PRIVATE TERRA_WINDOWS)

elseif(APPLE)

    target_link_libraries(TerraEngine PRIVATE
        "-framework Cocoa"
        "-framework Metal"
    )

    target_compile_definitions(TerraEngine PRIVATE TERRA_MACOS)

elseif(ANDROID)

    target_link_libraries(TerraEngine PRIVATE android log)
    target_compile_definitions(TerraEngine PRIVATE TERRA_ANDROID)

else()

    target_link_libraries(TerraEngine PRIVATE dl)
    target_compile_definitions(TerraEngine PRIVATE TERRA_LINUX)

endif()

# ============================================================
# Copy .env
# ============================================================
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/.env
    ${CMAKE_BINARY_DIR}/.env
    COPYONLY
)

# ============================================================
# Status messages
# ============================================================
message(STATUS "Terra Engine ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
