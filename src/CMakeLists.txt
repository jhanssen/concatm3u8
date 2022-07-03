cmake_minimum_required(VERSION 3.11)
include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)

set(SOURCES
    main.cpp
    )

add_executable(cm3u8 ${SOURCES})

target_link_libraries(cm3u8 reckoning ZLIB::ZLIB)
