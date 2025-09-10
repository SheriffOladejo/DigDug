cmake_minimum_required(VERSION 3.10)
project(DigDugClone)

set(CMAKE_CXX_STANDARD 17)

# Find raylib
find_package(raylib REQUIRED)

# Add raylib-cpp include folder
include_directories(${CMAKE_SOURCE_DIR}/raylib-cpp/include)

# Add source files
add_executable(DigDugClone src/main.cpp)

# Link raylib
target_link_libraries(DigDugClone raylib)

# macOS needs these extra frameworks for raylib
if(APPLE)
    target_link_libraries(DigDugClone "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
