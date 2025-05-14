cmake_minimum_required(VERSION 3.10.0)
project(LibSpecialDrive VERSION 0.1.0 LANGUAGES C)

file(GLOB LibSpecialDrive_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(LibSpecialDrive SHARED ${LibSpecialDrive_SRC})
include_directories(LibSpecialDrive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(APPLE)
    find_library(IOKIT_LIBRARY IOKit REQUIRED)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)

    target_link_libraries(LibSpecialDrive
        ${IOKIT_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
    )
endif()


add_executable(LibSpecialDriveEntryPoint LibSpecialDriveEntryPoint.c)
target_link_libraries(LibSpecialDriveEntryPoint LibSpecialDrive)
target_include_directories(LibSpecialDriveEntryPoint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)