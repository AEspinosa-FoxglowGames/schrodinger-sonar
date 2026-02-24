cmake_minimum_required(VERSION 3.21)
project(SonarPOC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Sources ----
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(SonarPOC ${SOURCES})

target_include_directories(SonarPOC PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ---- Dependencies via vcpkg ----
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

target_link_libraries(SonarPOC PRIVATE
    glfw
    glad::glad
    glm::glm
)

# ---- Warnings ----
if (MSVC)
  target_compile_options(SonarPOC PRIVATE /W4 /permissive-)
else()
  target_compile_options(SonarPOC PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Copy assets next to the exe ----
add_custom_command(TARGET SonarPOC POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:SonarPOC>/assets"
)
