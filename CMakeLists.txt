cmake_minimum_required(VERSION 3.20)
project(LaserScriptDock VERSION 2.0.0 LANGUAGES CXX)

# ----------------------------------------------------------------------------
# You normally don't run this by hand: double-click build.bat (Windows) or
# run ./build_mac.sh (Mac). They pass AI_SDK_ROOT for you.
# ----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AI_SDK_ROOT "" CACHE PATH "Path to the Adobe Illustrator SDK folder (the one containing 'illustratorapi')")
if(NOT AI_SDK_ROOT OR NOT EXISTS "${AI_SDK_ROOT}/illustratorapi")
    message(FATAL_ERROR
        "Illustrator SDK not found at '${AI_SDK_ROOT}'.\n"
        "Pass -DAI_SDK_ROOT=<folder that contains 'illustratorapi'>.")
endif()

set(DEFAULTS_DIR_NAME "Laser Script Dock defaults")

set(COMMON_SOURCES
    src/LaserScriptDockPlugin.cpp
    src/LaserScriptDockSuites.cpp
    src/DockConfig.cpp
    src/MiniJson.cpp
    "${AI_SDK_ROOT}/samplecode/common/source/Plugin.cpp"
    "${AI_SDK_ROOT}/samplecode/common/source/Suites.cpp"
    "${AI_SDK_ROOT}/samplecode/common/source/AppContext.cpp"
    "${AI_SDK_ROOT}/samplecode/common/source/Main.cpp"
    "${AI_SDK_ROOT}/illustratorapi/illustrator/IAIUnicodeString.cpp"
)

# The 2027 SDK makes the plugin's PiPL (its "ID card") with a Python tool.
set(PIPL_GENERATOR "${AI_SDK_ROOT}/tools/pipl/create_pipl.py")
set(PIPL_OUTPUT    "${CMAKE_CURRENT_BINARY_DIR}/plugin.pipl")
set(PIPL_METADATA  "[{\"name\":\"LaserScriptDock\",\"entry_point\":\"PluginMain\"}]")

if(WIN32)
    # ------------------------------------------------------------------ Windows
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(NOT EXISTS "${PIPL_GENERATOR}")
        message(FATAL_ERROR "Missing Illustrator SDK PiPL generator: ${PIPL_GENERATOR}")
    endif()

    add_custom_command(
        OUTPUT "${PIPL_OUTPUT}"
        COMMAND "${Python3_EXECUTABLE}" "${PIPL_GENERATOR}" -input "${PIPL_METADATA}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS "${PIPL_GENERATOR}"
        COMMENT "Generating Adobe Illustrator plugin.pipl"
        VERBATIM
    )
    add_custom_target(LaserScriptDockPiPL DEPENDS "${PIPL_OUTPUT}")

    add_library(LaserScriptDock MODULE
        ${COMMON_SOURCES}
        src/PanelUIWin.cpp
        src/ScriptExecutorWin.cpp
        resources/Win/LaserScriptDock.rc
    )
    add_dependencies(LaserScriptDock LaserScriptDockPiPL)
    set_source_files_properties(resources/Win/LaserScriptDock.rc PROPERTIES OBJECT_DEPENDS "${PIPL_OUTPUT}")
    target_include_directories(LaserScriptDock PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

    set_target_properties(LaserScriptDock PROPERTIES SUFFIX ".aip" PREFIX "")

    target_compile_definitions(LaserScriptDock PRIVATE
        WIN_ENV _WINDOWS UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)

    # /wd4828: silence hundreds of harmless "invalid character" warnings
    # coming from Adobe's own SDK headers.
    target_compile_options(LaserScriptDock PRIVATE /utf-8 /W4 /wd4828)
    target_link_options(LaserScriptDock PRIVATE "/EXPORT:PluginMain")

    target_link_libraries(LaserScriptDock PRIVATE
        comctl32 ole32 oleaut32 shell32 user32 gdi32 uuid)

    # Put the default scripts next to the plugin
    add_custom_command(TARGET LaserScriptDock POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/defaults"
            "$<TARGET_FILE_DIR:LaserScriptDock>/${DEFAULTS_DIR_NAME}"
    )

elseif(APPLE)
    # ---------------------------------------------------------------------- Mac
    enable_language(OBJCXX)

    add_library(LaserScriptDock MODULE
        ${COMMON_SOURCES}
        src/PanelUIMac.mm
        src/ScriptExecutorMac.mm
    )

    set_target_properties(LaserScriptDock PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "aip"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Mac/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.laserscriptdock.plugin"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    )

    target_compile_definitions(LaserScriptDock PRIVATE MAC_ENV)
    set_source_files_properties(src/PanelUIMac.mm src/ScriptExecutorMac.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    target_link_libraries(LaserScriptDock PRIVATE ${COCOA_FRAMEWORK} ${CARBON_FRAMEWORK})

    set(BUNDLE_RES "$<TARGET_BUNDLE_CONTENT_DIR:LaserScriptDock>/Resources")

    if(EXISTS "${PIPL_GENERATOR}")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_custom_command(TARGET LaserScriptDock POST_BUILD
            COMMAND "${Python3_EXECUTABLE}" "${PIPL_GENERATOR}" -input "${PIPL_METADATA}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_RES}/pipl"
            COMMAND ${CMAKE_COMMAND} -E copy "${PIPL_OUTPUT}" "${BUNDLE_RES}/pipl/plugin.pipl"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Generating Adobe Illustrator plugin.pipl"
            VERBATIM
        )
    else()
        message(WARNING "create_pipl.py not found in the SDK - Illustrator may not recognise the plugin.")
    endif()

    add_custom_command(TARGET LaserScriptDock POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/defaults"
            "${BUNDLE_RES}/${DEFAULTS_DIR_NAME}"
        VERBATIM
    )
endif()

target_include_directories(LaserScriptDock PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${AI_SDK_ROOT}/illustratorapi/illustrator"
    "${AI_SDK_ROOT}/illustratorapi/illustrator/actions"
    "${AI_SDK_ROOT}/illustratorapi/pica_sp"
    "${AI_SDK_ROOT}/illustratorapi/ate"
    "${AI_SDK_ROOT}/samplecode/common/includes"
)
