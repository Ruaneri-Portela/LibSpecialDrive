cmake_minimum_required(VERSION 3.10.0)
project(LibSpecialDrive VERSION 0.1.0 LANGUAGES C)

file(GLOB LibSpecialDrive_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(SpecialDrive SHARED ${LibSpecialDrive_SRC})
include_directories(SpecialDrive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# IOKit Apple 
if(APPLE)
    find_library(IOKIT_LIBRARY IOKit REQUIRED)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)

    target_link_libraries(SpecialDrive
        ${IOKIT_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
    )
endif()

# Configuração do compilador
if(MSVC)
    message(STATUS "Compilador MSVC detectado")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(SpecialDrive PRIVATE /Zi /Od /Wall /wd4201 /wd4255 /wd4310)
        target_link_options(SpecialDrive PRIVATE /DEBUG)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(SpecialDrive PRIVATE /O2 /GL /GS /W3 /MT)
        target_link_options(SpecialDrive PRIVATE /LTCG)
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Compilador GCC/Clang detectado")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(SpecialDrive PRIVATE -g -O0 -Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith -Wcast-qual -Wconversion)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(SpecialDrive PRIVATE -O2 -march=native -flto -fstack-protector-strong -D_FORTIFY_SOURCE=2)
        target_link_options(SpecialDrive PRIVATE -Wl,-z,relro,-z,now)
    endif()
endif()

# Executavel de teste

add_executable(SpecialDriveMain LibSpecialDriveEntryPoint.c)
target_link_libraries(SpecialDriveMain LibSpecialDrive)
target_include_directories(SpecialDriveMain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)