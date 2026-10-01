cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
cmake_policy(VERSION 3.12)

project(farcry LANGUAGES CXX)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_TYPE "x64")
elseif (ANDROID)
    # 32-bit arm: none of the x86 assembly or SSE switches apply.
    set(ARCH_TYPE "arm")
else ()
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
else()
    add_definitions(-DLINUX -fpermissive)
    # 32-bit arm takes the 64-bit configuration too: it selects the portable code, the 32-bit side is Windows-era x86.
    if (${ARCH_TYPE} STREQUAL "x64" OR ${ARCH_TYPE} STREQUAL "arm")
        add_definitions(-DLINUX64 -D_AMD64_)
    endif()
    if (${ARCH_TYPE} STREQUAL "arm")
        # The data loaders read packed file structs in place; keep the compiler from merging those reads into ldrd/ldm.
        add_compile_options(-mllvm -arm-assume-misaligned-load-store)
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
