project(FarCry)

set(SRC_FILES
    "FarCry.rc"
    "Main.cpp"
)

add_executable(${PROJECT_NAME} WIN32 ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${farcry_SOURCE_DIR}/SourceCode/CryCommon
    ${farcry_SOURCE_DIR}/Externals/SDL/include
)

target_link_libraries(${PROJECT_NAME} SDL3 dl)
