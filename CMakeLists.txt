cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(qadx)

# Project
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")
set(PROJECT_NAME qadx)
set(WAYLAND_DOCKER_PATH "/wayland-ivi-extension/build/ivi-layermanagement-api/")

find_package(Boost REQUIRED)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "You need to have Boost installed")
else()
    set(Boost_USE_STATIC_LIBS OFF)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endif()

find_package(Wayland REQUIRED)
set(ILM_USABLE false)

if(NOT WAYLAND_FOUND)
    message(FATAL_ERROR "You need to have Wayland installed")
else()
    include_directories(${WAYLAND_INCLUDE_DIRS})
    link_libraries(${WAYLAND_LIBRARIES})

    if (EXISTS ${WAYLAND_DOCKER_PATH})
        message("${WAYLAND_DOCKER_PATH} does exist")
        include_directories(${WAYLAND_DOCKER_PATH})
        add_definitions(-DQADX_USE_ILM)
        set(ILM_USABLE true)
    endif ()
endif ()

# Find libdrm
find_package(Libdrm)
set_package_properties(Libdrm PROPERTIES TYPE REQUIRED PURPOSE "Required for drm output on Wayland.")

if(NOT Libdrm_FOUND)
    message(FATAL_ERROR "You need to have libdrm-dev(el) installed on your machine")
else()
    include_directories(${Libdrm_INCLUDE_DIRS})
    link_libraries(${Libdrm_LIBRARIES})
endif ()

include_directories(${PROJECT_DIR}/ext)
include_directories(${PROJECT_DIR}/ext/spdlog/include)
include_directories(${PROJECT_DIR}/include/)

link_directories(/usr/lib)
link_directories(/usr/local/lib)
link_libraries(pthread png stdc++fs)

option(ENABLE_WEBSOCKET    "Compile the library with Websocket support" off)
option(ENABLE_STATIC_SCREEN_CALL  "Allow calls to screens' statics during test " off)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DEBUG}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DEBUG}")
    set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${OUTPUT_DEBUG}")
else()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_RELEASE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_RELEASE}")
    set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${OUTPUT_RELEASE}")
endif()

# Messages
message("${PROJECT_NAME}: MAIN PROJECT: ${CMAKE_PROJECT_NAME}")
message("${PROJECT_NAME}: CURR PROJECT: ${CMAKE_CURRENT_SOURCE_DIR}")
message("${PROJECT_NAME}: CURR BIN DIR: ${CMAKE_CURRENT_BINARY_DIR}")

############### Files & Targets ############################
# Files of project and target to build                     #
############################################################

# Source Files
set(SRC_FILES
      main.cpp
      src/backends/input/common.cpp
      src/backends/screen/kms.cpp
      src/images/bmp.cpp
      src/images/png.cpp
      src/server.cpp
      src/network_session.cpp
      src/string_utils.cpp
      src/endpoint.cpp
      src/input_device_guess.cpp)

# Header Files
set(HEADER_FILES
      include/image.hpp
      include/backends/input/evdev.hpp
      include/backends/input/common.hpp
      include/backends/input/uinput.hpp
      include/backends/screen/kms.hpp
      include/server.hpp
      include/network_session.hpp
      include/string_utils.hpp
      include/backends/input.hpp
      include/arguments.hpp
      include/enumerations.hpp
      include/backends/screen/base_screen.hpp
      include/endpoint.hpp
      include/backends/input/base_input.hpp
)

if (${ILM_USABLE})
    list(APPEND HEADER_FILES
       include/backends/screen/ilm.hpp)
    list(APPEND SRC_FILES
       src/backends/screen/ilm.cpp)
    link_libraries(wayland-client ilmControl)
endif ()

if (ENABLE_WEBSOCKET)
    message("Websocket enabled")
    add_definitions(-DQADX_USE_WITH_WEBSOCKET)
    list(APPEND HEADER_FILES
      include/websocket_server.hpp
      include/backends/screen/kms_page_flip.hpp
    )
    list(APPEND SRC_FILES
      src/websocket_server.cpp
      src/backends/screen/kms_page_flip.cpp)
endif ()

if (ENABLE_STATIC_SCREEN_CALL)
    add_definitions(-DQADX_ENABLE_STATIC_SCREEN_CALL)
endif ()

source_group("Headers" FILES ${HEADER_FILES})
source_group("Sources" FILES ${SRC_FILES})

# Add executable to build.
add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEADER_FILES})

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -std=c++17 -O3 -Werror")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17 /O2")
endif()

# Preprocessor definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            -D_DEBUG
            -D_CONSOLE
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE  /W3 /MD /Od /Zi /EHsc /std:c++17)
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            -DNDEBUG
            -D_CONSOLE
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE  /W3 /GL /Oi /Gy /Zi /EHsc /std:c++17)
    endif()
endif()
