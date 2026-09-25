project(CryGame)

add_definitions(-D_USRDLL -DCRYGAME_EXPORTS)

option(DISABLE_FFMPEG "Disable FFMPEG and use libbinkdec for video support" ON)

if (DISABLE_FFMPEG)
add_definitions(-DCRY_NO_FFMPEG)
endif()

set(SRC_FILES
    "AdvCamSystem.cpp"
    "AimAssist.cpp"
    "AIHandler.cpp"
    "BitStream_Base.cpp"
    "BitStream_Compressed.cpp"
    "BugsFlock.cpp"
    "CryGame.cpp"
    "EntityClassRegistry.cpp"
    "Flock.cpp"
    "Game.cpp"
    "GameActionInput.cpp"
    "GameCallbacks.cpp"
    "GameClientServer.cpp"
    "GameDevMode.cpp"
    "GameEquipPacks.cpp"
    "GameEvents.cpp"
    "GameLoading.cpp"
    "GameMemStats.cpp"
    "GameMisc.cpp"
    "GameMods.cpp"
    "GameMovieUser.cpp"
    "GameRadar.cpp"
    "GameTagPoints.cpp"
    "IngameDialog.cpp"
    "NetEntityInfo.cpp"
    "ScriptObjectAdvCamSystem.cpp"
    "ScriptObjectAI.cpp"
    "ScriptObjectBoids.cpp"
    "ScriptObjectClient.cpp"
    "ScriptObjectGame.cpp"
    "ScriptObjectInput.cpp"
    "ScriptObjectLanguage.cpp"
    "ScriptObjectPlayer.cpp"
    "ScriptObjectRenderer.cpp"
    "ScriptObjectServer.cpp"
    "ScriptObjectServerSlot.cpp"
    "ScriptObjectSpectator.cpp"
    "ScriptObjectStream.cpp"
    "ScriptObjectSynched2DTable.cpp"
    "ScriptObjectUI.cpp"
    "ScriptObjectVehicle.cpp"
    "ScriptObjectWeaponClass.cpp"
    "ScriptTimerMgr.cpp"
    "Spectator.cpp"
    "StdAfx.cpp"
    "StringTableMgr.cpp"
    "Synched2DTable.cpp"
    "SynchedRandomSeed.cpp"
    "TimeDemoRecorder.cpp"
    "UIButton.cpp"
    "UICheckBox.cpp"
    "UIComboBox.cpp"
    "UICVars.cpp"
    "UIDivX_Video.cpp"
    "UIEditBox.cpp"
    "UIHud.cpp"
    "UIListView.cpp"
    "UIScreen.cpp"
    "UIScrollBar.cpp"
    "UIStatic.cpp"
    "UISystem.cpp"
    "UIVideoPanel.cpp"
    "UIWidget.cpp"
    "WeaponClass.cpp"
    "WeaponSystemEx.cpp"
    "XArea.cpp"
    "XClient.cpp"
    "XClientSnapshot.cpp"
    "XDemoMgr.cpp"
    "XEntityProcessingCmd.cpp"
    "XNetwork.cpp"
    "XNetworkStats.cpp"
    "XObjectProxy.cpp"
    "XPlayer.cpp"
    "XPlayerCamera.cpp"
    "XPlayerLight.cpp"
    "XplayerVehicle.cpp"
    "XPuppetProxy.cpp"
    "XServer.cpp"
    "XServerRules.cpp"
    "XServerSlot.cpp"
    "XSnapshot.cpp"
    "XSurfaceMgr.cpp"
    "XSystemBase.cpp"
    "XSystemClient.cpp"
    "XSystemDummy.cpp"
    "XSystemEntities.cpp"
    "XSystemServer.cpp"
    "XVehicle.cpp"
    "XVehicleProxy.cpp"
    "XVehicleProxyHeli.cpp"
    "XVehicleSystem.cpp"
)

if (NOT DISABLE_FFMPEG)
set(SRC_FILES ${SRC_FILES} "UIVideoFFmpeg.cpp")
else()
set(SRC_FILES ${SRC_FILES} 
"UIVideoBinkDec.cpp"
"libbinkdec/src/BinkAudio.cpp"
"libbinkdec/src/BinkAudio.cpp"
"libbinkdec/src/BinkDecoder.cpp"
"libbinkdec/src/BinkVideo.cpp"
"libbinkdec/src/BitReader.cpp"
"libbinkdec/src/FileStream.cpp"
"libbinkdec/src/HuffmanVLC.cpp"
"libbinkdec/src/LogError.cpp"
"libbinkdec/src/Util.cpp"
"libbinkdec/src/avfft.c"
"libbinkdec/src/dct32.c"
"libbinkdec/src/dct.c"
"libbinkdec/src/fft.c"
"libbinkdec/src/mdct.c"
"libbinkdec/src/rdft.c"
)
include_directories(
    ./libbinkdec/include
)
endif()

add_library(${PROJECT_NAME} SHARED ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${farcry_SOURCE_DIR}/SourceCode/CryCommon
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${farcry_SOURCE_DIR}/SourceCode/BinkSDK
)

if (MSVC)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Wsock32.lib
)
else()
if (NOT DISABLE_FFMPEG)
target_link_libraries(${PROJECT_NAME} PRIVATE avutil avformat swscale)
endif()
endif()
