project(FarCry)

set(SRC_FILES
    "FarCry.rc"
    "Main.cpp"
)

if (ANDROID)
# The app loads the launcher as libfarcry.so and calls FarCry_AndroidMain() from
# PortableInit(); the OpenTouch glue (JNI bridge, touch overlay) is linked in here.
set(SRC_FILES
    "Main.cpp"
    "mobile/game_interface.cpp"
    "mobile/touch_interface_farcry.cpp"
    "${CLIBS_DIR}/sigma/android_jni.cpp"
    "${CLIBS_DIR}/touch_interface_base.cpp"
    "${CLIBS_DIR}/LogWritter.c"
    "${CLIBS_DIR}/fts.c"
    "${CLIBS_DIR}/rand.c"
)
add_library(farcry SHARED ${SRC_FILES})
set_target_properties(farcry PROPERTIES PREFIX "lib" OUTPUT_NAME "farcry")
# ENGINE_NAME names the touch-control layout XML files.
target_compile_definitions(farcry PRIVATE ENGINE_NAME="farcry" USE_SDL3)
# mobile/ must precede CLIBS_DIR so the shared touch_interface_base.cpp and JNI
# glue resolve *this* engine's touch_interface.h, not another engine's.
target_include_directories(farcry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mobile
    ${farcry_SOURCE_DIR}/SourceCode/CryCommon
    ${SDL3_INCLUDE_DIR}
    ${CLIBS_DIR}
    ${TOUCH_CONTROLS_DIR}
)
target_link_libraries(farcry SDL3 touchcontrols saffal log android dl)
# Every engine module is dlopen()ed by name at runtime, so build them all with the launcher.
# CryGame and CrySystem link farcry, so they are not listed here.
add_dependencies(farcry Cry3DEngine CryAISystem CryAnimation CryEntitySystem
    CryFont CryInput CryMovie CryNetwork CryPhysics CryScriptSystem CrySoundSystem XRenderGLES)
else()

add_executable(${PROJECT_NAME} WIN32 ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${farcry_SOURCE_DIR}/SourceCode/CryCommon
    ${farcry_SOURCE_DIR}/Externals/SDL/include
)

target_link_libraries(${PROJECT_NAME} SDL3 dl)
endif()
