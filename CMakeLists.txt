cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
cmake_policy(VERSION 3.12)

project(farcry LANGUAGES CXX)

if (ANDROID)
    # Android loads the engine modules dynamically; do not reject their late-bound symbols.
    set(ANDROID_ALLOW_UNDEFINED_SYMBOLS ON CACHE BOOL "" FORCE)
    string(REPLACE "-Wl,--no-undefined" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REPLACE "-Wl,--fatal-warnings" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REPLACE "-Wl,--no-undefined" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-Wl,--fatal-warnings" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    add_compile_options(-w -fPIC -Wno-error=pointer-to-int-cast -Wno-pointer-to-int-cast
        -Wno-int-to-pointer-cast -Wno-error=address-of-temporary -Wno-address-of-temporary)
    add_link_options(-Wl,--unresolved-symbols=ignore-all -Wl,--no-fatal-warnings)
    link_libraries(SDL3 log dl m)
endif()

if (ANDROID)
    if (CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
        set(ARCH_TYPE "arm64")
        add_definitions(-DLINUX -DLINUX64 -D__linux__ -D__ANDROID__ -D_CPU_ARM64 -D_ARM64_)
    elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
        set(ARCH_TYPE "arm")
        add_definitions(-DLINUX -DLINUX64 -D__linux__ -D__ANDROID__ -D_CPU_ARM -D_ARM_)
    elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
        set(ARCH_TYPE "x64")
        add_definitions(-DLINUX -DLINUX64 -D__linux__ -D__ANDROID__ -D_AMD64_)
    else()
        set(ARCH_TYPE "x86")
        add_definitions(-DLINUX -DLINUX32 -D__linux__ -D__ANDROID__)
    endif()
elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_TYPE "x64")
else()
    set(ARCH_TYPE "x86")
endif()
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("ARCH_TYPE=${ARCH_TYPE}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin/${ARCH_TYPE}-${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin/${ARCH_TYPE}-${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG})

if (MSVC)
    if (${ARCH_TYPE} STREQUAL "x64")
        add_definitions(-DWIN64 -D_AMD64_)
    endif()
    # Always generate PDBs
    add_compile_options(/Zi)
    add_link_options(/DEBUG)

    add_compile_options(/W3) # Warning level
else()
    add_definitions(-DLINUX -fpermissive)
    if (${ARCH_TYPE} STREQUAL "x64")
        add_definitions(-DLINUX64 -D_AMD64_)
    endif()
    if (APPLE)
        add_definitions(-DDARWIN_NO_CARBON)
        # Modules are dlopen()ed by hard-coded lib*.so names.
        set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
        set(CMAKE_SHARED_MODULE_SUFFIX ".so")
        execute_process(COMMAND brew --prefix OUTPUT_VARIABLE BREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
        include_directories(SYSTEM ${BREW_PREFIX}/include ${BREW_PREFIX}/opt/openal-soft/include)
        link_directories(${BREW_PREFIX}/lib ${BREW_PREFIX}/opt/openal-soft/lib)
        set(DISABLE_CG ON CACHE BOOL "" FORCE)
        # Modules resolve each other's symbols at dlopen time, as they do on Linux.
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-undefined,dynamic_lookup")
        # Apple clang 21 fuses a virtual ILog::Log call with a sibling virtual call into one variadic call, whose argument then goes on the stack instead of x2.
        add_compile_options(-mllvm -simplifycfg-sink-common=false)
    endif()

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # The tree takes the address of temporaries passed straight to callees; GCC allows it under -fpermissive.
        add_compile_options(-Wno-address-of-temporary)
    endif()
    # The tree assumes x86's signed char (e.g. the save game's (char)-1 table terminator); arm defaults to unsigned.
    add_compile_options(-fsigned-char)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -D_DEBUG -g")
endif()

add_subdirectory(SourceCode)
