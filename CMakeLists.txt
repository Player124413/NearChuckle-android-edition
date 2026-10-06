cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
cmake_policy(VERSION 3.12)

project(farcry LANGUAGES CXX)

if (ANDROID)
    set(ANDROID_ALLOW_UNDEFINED_SYMBOLS ON CACHE BOOL "" FORCE)
    string(REPLACE "-Wl,--no-undefined" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REPLACE "-Wl,--fatal-warnings" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REPLACE "-Wl,--no-undefined" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-Wl,--fatal-warnings" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    if (CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(ARCH_TYPE "arm64")
        add_definitions(-DLINUX -DLINUX64 -D__linux__ -D__ANDROID__ -D_CPU_ARM64 -D_ARM64_)
    elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        set(ARCH_TYPE "arm")
        add_definitions(-DLINUX -DLINUX32 -D__linux__ -D__ANDROID__ -D_CPU_ARM -D_ARM_)
    elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
        set(ARCH_TYPE "x64")
        add_definitions(-DLINUX -DLINUX64 -D__linux__ -D__ANDROID__ -D_AMD64_)
    else()
        set(ARCH_TYPE "x86")
        add_definitions(-DLINUX -DLINUX32 -D__linux__ -D__ANDROID__)
    endif()
    if (DEFINED CMAKE_PREFIX_PATH)
        include_directories(${CMAKE_PREFIX_PATH}/include)
        link_directories(${CMAKE_PREFIX_PATH}/lib)
    endif()
    add_compile_options(
        -w
        -fPIC
        -Wno-error=pointer-to-int-cast
        -Wno-pointer-to-int-cast
        -Wno-int-to-pointer-cast
        -Wno-error=address-of-temporary
        -Wno-address-of-temporary
    )
    add_link_options(
        -Wl,--unresolved-symbols=ignore-all
        -Wl,--no-fatal-warnings
    )
    link_libraries(SDL3 log dl m)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_TYPE "x64")
else (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ARCH_TYPE "x86")
endif()
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("ARCH_TYPE=${ARCH_TYPE}")

if (NOT ANDROID)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/${ARCH_TYPE}-${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/${ARCH_TYPE}-${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG})
endif()

if (MSVC)
    if (${ARCH_TYPE} STREQUAL "x64")
        add_definitions(-DWIN64 -D_AMD64_)
    endif()
    # Always generate PDBs
    add_compile_options(/Zi)
    add_link_options(/DEBUG)

    add_compile_options(/W3) # Warning level
elseif (NOT ANDROID)
    add_definitions(-DLINUX -fpermissive)
    if (${ARCH_TYPE} STREQUAL "x64")
        add_definitions(-DLINUX64 -D_AMD64_)
    endif()

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -D_DEBUG -g")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -D_DEBUG -g")
endif()

include_directories(${CMAKE_SOURCE_DIR}/Externals/SDL/include)
include_directories(${CMAKE_SOURCE_DIR}/Externals/openal/include)

add_subdirectory(SourceCode)
