cmake_minimum_required(VERSION 3.20)
project(TerraEngine VERSION 1.0.0 LANGUAGES CXX)

# ============================================================
# C++ Standard
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Build Type
# ============================================================
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG   "-g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ============================================================
# Find Required Packages
# ============================================================
find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# ============================================================
# nlohmann/json (header only)
# ============================================================
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/include/nlohmann
)
FetchContent_MakeAvailable(nlohmann_json)

# ============================================================
# Source Files
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
# Header Files
# ============================================================
set(TERRA_HEADERS
    AIConnector.h
    AgentBase.h
    AgentBus.h
    TransformerBase.h
    TerraCore.h
    TerraEngine.h

    agents/AgentWorld.h
    agents/AgentWeather.h
    agents/AgentNPC.h
    agents/AgentBoss.h
    agents/AgentEnemy.h
    agents/AgentAnimal.h
    agents/AgentWater.h
    agents/AgentDay.h
    agents/AgentPlayerCare.h
)

# ============================================================
# Executable
# ============================================================
add_executable(TerraEngine ${TERRA_SOURCES} ${TERRA_HEADERS})

# ============================================================
# Include Directories
# ============================================================
target_include_directories(TerraEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
    ${CURL_INCLUDE_DIRS}
)

# ============================================================
# Link Libraries
# ============================================================
target_link_libraries(TerraEngine PRIVATE
    ${CURL_LIBRARIES}
    ${OPENGL_LIBRARIES}
    Threads::Threads
)

# ============================================================
# Platform Specific
# ============================================================
if(WIN32)
    target_link_libraries(TerraEngine PRIVATE ws2_32 winmm)
    target_compile_definitions(TerraEngine PRIVATE TERRA_WINDOWS)
elseif(APPLE)
    target_link_libraries(TerraEngine PRIVATE "-framework Cocoa" "-framework Metal")
    target_compile_definitions(TerraEngine PRIVATE TERRA_MACOS)
elseif(ANDROID)
    target_link_libraries(TerraEngine PRIVATE android log)
    target_compile_definitions(TerraEngine PRIVATE TERRA_ANDROID)
else()
    target_link_libraries(TerraEngine PRIVATE dl)
    target_compile_definitions(TerraEngine PRIVATE TERRA_LINUX)
endif()

# ============================================================
# Copy .env to build directory
# ============================================================
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/.env
    ${CMAKE_BINARY_DIR}/.env
    COPYONLY
)

message(STATUS "Terra Engine ${PROJECT_VERSION} configured")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
