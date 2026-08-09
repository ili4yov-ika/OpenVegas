cmake_minimum_required(VERSION 3.16)
project(OpenVegas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Look for .ui next to sources and in ui/
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Svg)

option(OPENVGAS_AUDIO "Build audio engine (miniaudio + decode cache)" ON)
option(OPENVGAS_BUILD_TESTS "Build Catch2 unit tests" ON)
option(OPENVGAS_COPY_VEGAS_RUNTIME
    "Copy selected Vegas Pro 22 trees into build/vegas-runtime (gitignored)"
    OFF)
set(OPENVGAS_VST3_SDK_PATH "" CACHE PATH "Path to Steinberg VST3 SDK (optional)")
# Auto-detect vendored shallow clone when CACHE is empty.
if(NOT OPENVGAS_VST3_SDK_PATH AND EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h")
    set(OPENVGAS_VST3_SDK_PATH "${CMAKE_SOURCE_DIR}/thirdparty/vst3sdk" CACHE PATH
        "Path to Steinberg VST3 SDK (optional)" FORCE)
endif()

    set(OPENVGAS_SOURCES
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/ui/Theme.cpp
    src/ui/MenuBuilder.cpp
    src/ui/IconFactory.cpp
    src/ui/WelcomeDialog.cpp
    src/ui/ProjectPropertiesDialog.cpp
    src/ui/MatchMediaVideoSettingsDialog.cpp
    src/ui/MissingFileDialog.cpp
    src/ui/SearchMissingFilesDialog.cpp
    src/ui/FindMissingFileDialog.cpp
    src/ui/RenderAsDialog.cpp
    src/ui/PreferencesDialog.cpp
    src/ui/EventPropertiesDialog.cpp
    src/ui/TrimmerWindow.cpp
    src/ui/PluginChooserDialog.cpp
    src/ui/AudioEventFxDialog.cpp
    src/ui/ColorGradingEditor.cpp
    src/ui/CollapsibleSection.cpp
    src/ui/ColorPickerWidget.cpp
    src/ui/TitlesTextEditorDialog.cpp
    src/ui/ContextMenuBuilder.cpp
    src/ui/FadeCurvePopup.cpp
    src/ui/ExplorerPane.cpp
    src/ui/VideoFxPane.cpp
    src/ui/MediaGeneratorPane.cpp
    src/ui/TransitionsPane.cpp
    src/ui/ProjectNotesPane.cpp
    src/ui/MixingConsoleWindow.cpp
    src/ui/ExtractAudioFromCdDialog.cpp
    src/ui/RenderingProgressDialog.cpp
    src/ui/RateSlider.cpp
    src/ui/VideoEventFxDialog.cpp
    src/ui/VideoEventFxDialogExact.cpp
    src/ui/VideoTrackFxDialog.cpp
    src/ui/TrackMotionDialog.cpp
    src/ui/ConfirmWarningDialog.cpp
    src/ui/MediaBinListWidget.cpp
    src/ui/MediaThumbHoverScrub.cpp
    src/ui/KeyboardMap.cpp
    src/ui/CustomizeKeyboardDialog.cpp
    src/timeline/TimelineView.cpp
    src/timeline/TimelineScrollHost.cpp
    src/model/ProjectModel.cpp
    src/model/ProjectSnapshot.cpp
    src/model/SnapshotCommand.cpp
    src/io/VegReader.cpp
    src/io/ProjectInterchange.cpp
    src/io/SamplePaths.cpp
    src/io/MediaMime.cpp
    src/io/MediaThumbCache.cpp
    src/io/MediaWaveformCache.cpp
    src/io/MediaFilmstripCache.cpp
    src/io/MediaProbe.cpp
    src/io/CdAudioReader.cpp
    src/io/RenderTemplateCatalog.cpp
    src/io/FFmpegEncoder.cpp
    src/io/FFmpegStreamDecoder.cpp
    src/media/MediaEngine.cpp
    src/plugins/PluginScanner.cpp
    src/plugins/OfxHost.cpp
    src/plugins/BuiltinAudioCatalog.cpp
    src/plugins/VegasSharedAudioCatalog.cpp
    src/plugins/VegasVideoPluginCatalog.cpp
    src/plugins/AudioPluginScanner.cpp
    src/plugins/AudioPluginRegistry.cpp
    src/plugins/AudioPluginHost.cpp
    src/audio/FadeCurves.cpp
    src/audio/AudioDecodeCache.cpp
    src/audio/BuiltinDsp.cpp
    src/audio/AudioGraph.cpp
    src/audio/AudioEngine.cpp
    src/audio/CompositePluginHost.cpp
    src/audio/Vst3Host.cpp
    src/audio/miniaudio_impl.cpp
    src/video/VideoFrameCache.cpp
    src/video/VideoKeyframeEval.cpp
    src/video/PanCropApply.cpp
    src/video/TrackMotionApply.cpp
    src/video/ColorCorrectorApply.cpp
    src/video/TitlesTextApply.cpp
    src/video/MediaGeneratorApply.cpp
    src/video/VideoCompositor.cpp
    resources/resources.qrc
)

set(OPENVGAS_HEADERS
    src/app/MainWindow.h
    src/ui/Theme.h
    src/ui/MenuBuilder.h
    src/ui/IconFactory.h
    src/ui/WelcomeDialog.h
    src/ui/ProjectPropertiesDialog.h
    src/ui/MatchMediaVideoSettingsDialog.h
    src/ui/MissingFileDialog.h
    src/ui/SearchMissingFilesDialog.h
    src/ui/FindMissingFileDialog.h
    src/ui/RenderAsDialog.h
    src/ui/PreferencesDialog.h
    src/ui/EventPropertiesDialog.h
    src/ui/TrimmerWindow.h
    src/ui/PluginChooserDialog.h
    src/ui/AudioEventFxDialog.h
    src/ui/ColorGradingEditor.h
    src/ui/CollapsibleSection.h
    src/ui/ColorPickerWidget.h
    src/ui/TitlesTextEditorDialog.h
    src/ui/ContextMenuBuilder.h
    src/ui/FadeCurvePopup.h
    src/ui/ExplorerPane.h
    src/ui/VideoFxPane.h
    src/ui/MediaGeneratorPane.h
    src/ui/TransitionsPane.h
    src/ui/ProjectNotesPane.h
    src/ui/MixingConsoleWindow.h
    src/ui/ExtractAudioFromCdDialog.h
    src/ui/RenderingProgressDialog.h
    src/ui/RateSlider.h
    src/ui/VideoEventFxDialog.h
    src/ui/VideoEventFxDialogExact.h
    src/ui/VideoTrackFxDialog.h
    src/ui/KeyframeLaneWidgets.h
    src/ui/TrackMotionDialog.h
    src/ui/ConfirmWarningDialog.h
    src/ui/MediaBinListWidget.h
    src/ui/MediaThumbHoverScrub.h
    src/ui/KeyboardMap.h
    src/ui/CustomizeKeyboardDialog.h
    src/timeline/TimelineView.h
    src/timeline/TimelineScrollHost.h
    src/model/ProjectModel.h
    src/model/TrackColors.h
    src/model/ProjectSnapshot.h
    src/model/SnapshotCommand.h
    src/io/VegReader.h
    src/io/ProjectInterchange.h
    src/io/SamplePaths.h
    src/io/MediaMime.h
    src/io/MediaThumbCache.h
    src/io/MediaWaveformCache.h
    src/io/MediaFilmstripCache.h
    src/io/MediaProbe.h
    src/io/CdAudioReader.h
    src/io/RenderTemplateCatalog.h
    src/io/FFmpegEncoder.h
    src/io/FFmpegStreamDecoder.h
    src/media/MediaEngine.h
    src/plugins/PluginScanner.h
    src/plugins/OfxHost.h
    src/plugins/AudioPluginTypes.h
    src/plugins/BuiltinAudioCatalog.h
    src/plugins/VegasSharedAudioCatalog.h
    src/plugins/VegasVideoPluginCatalog.h
    src/plugins/AudioPluginScanner.h
    src/plugins/AudioPluginRegistry.h
    src/plugins/AudioPluginHost.h
    src/audio/FadeCurves.h
    src/audio/AudioUtil.h
    src/audio/AudioDecodeCache.h
    src/audio/BuiltinDsp.h
    src/audio/AudioGraph.h
    src/audio/AudioEngine.h
    src/audio/CompositePluginHost.h
    src/audio/Vst3Host.h
    src/video/VideoFrameCache.h
    src/video/VideoKeyframeEval.h
    src/video/PanCropApply.h
    src/video/TrackMotionApply.h
    src/video/ColorCorrectorApply.h
    src/video/TitlesTextApply.h
    src/video/MediaGeneratorApply.h
    src/video/VideoCompositor.h
)

set(OPENVGAS_UI
    ui/MainWindow.ui
    ui/WelcomeDialog.ui
    ui/ProjectPropertiesDialog.ui
    ui/RenderAsDialog.ui
    ui/PreferencesDialog.ui
    ui/EventPropertiesDialog.ui
    ui/TrimmerWindow.ui
    ui/PluginChooserDialog.ui
)

add_executable(OpenVegas
    ${OPENVGAS_SOURCES}
    ${OPENVGAS_HEADERS}
    ${OPENVGAS_UI}
)

target_include_directories(OpenVegas PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/thirdparty
    ${CMAKE_SOURCE_DIR}/thirdparty/openfx/include
    ${CMAKE_SOURCE_DIR}/thirdparty/vestige
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(OpenVegas PRIVATE Qt6::Widgets Qt6::Svg)

if(OPENVGAS_AUDIO)
    target_compile_definitions(OpenVegas PRIVATE OPENVGAS_AUDIO=1)
endif()
set(OPENVGAS_VST3_SDK_SOURCES "")
if(OPENVGAS_VST3_SDK_PATH AND EXISTS "${OPENVGAS_VST3_SDK_PATH}/pluginterfaces/vst/ivstaudioprocessor.h")
    set(OPENVGAS_HAS_VST3_SDK_INTERNAL 1)
    target_compile_definitions(OpenVegas PRIVATE OPENVGAS_HAS_VST3_SDK=1)
    target_include_directories(OpenVegas PRIVATE
        "${OPENVGAS_VST3_SDK_PATH}"
        "${OPENVGAS_VST3_SDK_PATH}/pluginterfaces"
    )
    # Minimal IID symbols for lean host (headers-only process path; no full hosting stack).
    list(APPEND OPENVGAS_VST3_SDK_SOURCES
        "${OPENVGAS_VST3_SDK_PATH}/pluginterfaces/base/coreiids.cpp"
        "${OPENVGAS_VST3_SDK_PATH}/pluginterfaces/base/funknown.cpp"
        "${OPENVGAS_VST3_SDK_PATH}/public.sdk/source/common/commoniids.cpp"
        "${OPENVGAS_VST3_SDK_PATH}/public.sdk/source/vst/vstinitiids.cpp"
    )
    target_sources(OpenVegas PRIVATE ${OPENVGAS_VST3_SDK_SOURCES})
    message(STATUS "VST3 SDK: ${OPENVGAS_VST3_SDK_PATH}")
else()
    set(OPENVGAS_HAS_VST3_SDK_INTERNAL 0)
    message(STATUS "VST3 SDK: not set — Vst3Host stub (pass-through). Set OPENVGAS_VST3_SDK_PATH for real host.")
endif()

# Optional linked FFmpeg (continuous decode prefers libav; CLI raw pipe is always fallback)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libswscale libswresample libavutil)
    if(FFMPEG_FOUND)
        target_compile_definitions(OpenVegas PRIVATE OPENVGAS_FFMPEG=1)
        target_include_directories(OpenVegas PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(OpenVegas PRIVATE ${FFMPEG_LIBRARIES})
        message(STATUS "FFmpeg libs found — OPENVGAS_FFMPEG enabled (linked decode)")
    else()
        message(STATUS "FFmpeg libs: not found — continuous decode via CLI rawvideo pipe")
    endif()
else()
    message(STATUS "FFmpeg libs: pkg-config unavailable — continuous decode via CLI rawvideo pipe")
endif()

if(WIN32)
    target_link_libraries(OpenVegas PRIVATE ole32 shell32 uuid user32 gdi32 winmm)
    # Native EXE icon (Explorer / taskbar). Path relative to this .rc file.
    target_sources(OpenVegas PRIVATE resources/openvegas.rc)
endif()

if(MSVC)
    # Replace default /W3 rather than appending /W4 (avoids D9025).
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    target_compile_options(OpenVegas PRIVATE /W4 /permissive-)
else()
    target_compile_options(OpenVegas PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optional local staging of Vegas trees (never into source tree / git)
if(OPENVGAS_COPY_VEGAS_RUNTIME)
    set(_vegas_src "${CMAKE_SOURCE_DIR}/SAMPLES/VEGAS-PRO-22-PROGRAM-FILES")
    set(_vegas_dst "${CMAKE_BINARY_DIR}/vegas-runtime")
    if(EXISTS "${_vegas_src}")
        add_custom_command(TARGET OpenVegas POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_vegas_dst}"
            COMMAND ${CMAKE_COMMAND} -E echo
                "Staging Vegas runtime subset into ${_vegas_dst} (gitignored)"
            COMMENT "Copy Vegas runtime subset to build/vegas-runtime"
        )
        foreach(_subdir IN ITEMS "OFX Video Plug-Ins" "Icons")
            if(EXISTS "${_vegas_src}/${_subdir}")
                add_custom_command(TARGET OpenVegas POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${_vegas_src}/${_subdir}"
                        "${_vegas_dst}/${_subdir}"
                )
            endif()
        endforeach()
    else()
        message(WARNING "OPENVGAS_COPY_VEGAS_RUNTIME=ON but ${_vegas_src} not found")
    endif()
endif()

install(TARGETS OpenVegas RUNTIME DESTINATION bin)

# Desktop entry + icon (Linux / packaging)
if(UNIX AND NOT APPLE)
    install(FILES tools/openvegas.desktop
        DESTINATION share/applications)
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/logo.svg")
        install(FILES resources/icons/logo.svg
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME openvegas.svg)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/logo.png")
        install(FILES resources/icons/logo.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME openvegas.png)
    endif()
endif()

# CPack (NSIS / DEB / RPM / DragNDrop) — см. tools/README.md
set(CPACK_PACKAGE_NAME "OpenVegas")
set(CPACK_PACKAGE_VENDOR "OpenVegas contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Open-source nonlinear video editor (VEGAS Pro–like)")
set(CPACK_PACKAGE_VERSION_MAJOR 0)
set(CPACK_PACKAGE_VERSION_MINOR 1)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpenVegas")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "OpenVegas")
    set(CPACK_NSIS_PACKAGE_NAME "OpenVegas")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OpenVegas contributors <maintainer@example.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6 (>= 6.8.0), libqt6svg6 (>= 6.8.0)")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0+")
endif()
include(CPack)

# --- Unit tests (Catch2) ---
if(OPENVGAS_BUILD_TESTS)
    include(CTest)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(openvegas_audio_tests
        tests/test_fade_curves.cpp
        tests/test_audio_util.cpp
        tests/test_automation_lane.cpp
        tests/test_builtin_dsp.cpp
        tests/test_vegas_shared_catalog.cpp
        src/audio/FadeCurves.cpp
        src/audio/BuiltinDsp.cpp
        src/plugins/VegasSharedAudioCatalog.cpp
        src/plugins/OfxHost.cpp
        src/plugins/PluginScanner.cpp
        src/plugins/VegasVideoPluginCatalog.cpp
    )
    target_include_directories(openvegas_audio_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/thirdparty
        ${CMAKE_SOURCE_DIR}/thirdparty/openfx/include
    )
    target_link_libraries(openvegas_audio_tests PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        Qt6::Gui
    )
    if(MSVC)
        target_compile_options(openvegas_audio_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME openvegas_audio_tests COMMAND openvegas_audio_tests)

    add_executable(openvegas_video_tests
        tests/test_video_preview.cpp
        tests/test_video_fx_chain.cpp
        tests/test_vegas_video_catalog.cpp
        tests/test_titles_text.cpp
        tests/stub_media_path_for_event.cpp
        src/audio/FadeCurves.cpp
        src/io/SamplePaths.cpp
        src/plugins/OfxHost.cpp
        src/plugins/PluginScanner.cpp
        src/plugins/VegasVideoPluginCatalog.cpp
        src/video/VideoKeyframeEval.cpp
        src/video/PanCropApply.cpp
        src/video/TrackMotionApply.cpp
        src/video/ColorCorrectorApply.cpp
        src/video/TitlesTextApply.cpp
        src/video/MediaGeneratorApply.cpp
        src/video/VideoCompositor.cpp
        src/video/VideoFrameCache.cpp
        src/io/FFmpegStreamDecoder.cpp
        src/io/MediaFilmstripCache.cpp
        src/io/MediaThumbCache.cpp
    )
    target_include_directories(openvegas_video_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/thirdparty
        ${CMAKE_SOURCE_DIR}/thirdparty/openfx/include
    )
    target_link_libraries(openvegas_video_tests PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    # Forward slashes so the string is a valid C literal on Windows too.
    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/tests/fixtures/video" _ov_fixtures)
    target_compile_definitions(openvegas_video_tests PRIVATE
        OPENVGAS_TEST_FIXTURES_DIR="${_ov_fixtures}"
    )
    if(MSVC)
        target_compile_options(openvegas_video_tests PRIVATE /W4 /permissive-)
    endif()
    if(WIN32)
        target_link_libraries(openvegas_video_tests PRIVATE ole32 shell32 uuid user32 gdi32)
    endif()
    add_test(NAME openvegas_video_tests COMMAND openvegas_video_tests)

    add_executable(openvegas_media_tests
        tests/test_ffmpeg_encoder.cpp
        tests/test_ffmpeg_stream_decoder.cpp
        tests/test_plugin_scanner_paths.cpp
        tests/test_ofx_host.cpp
        tests/test_vst3_host.cpp
        tests/test_project_interchange.cpp
        tests/test_plugin_state.cpp
        tests/test_vegas_video_catalog.cpp
        tests/test_media_generator.cpp
        src/io/FFmpegEncoder.cpp
        src/io/FFmpegStreamDecoder.cpp
        src/io/MediaFilmstripCache.cpp
        src/io/MediaThumbCache.cpp
        src/io/ProjectInterchange.cpp
        src/io/SamplePaths.cpp
        src/io/VegReader.cpp
        src/io/MediaProbe.cpp
        src/io/MediaWaveformCache.cpp
        src/model/ProjectModel.cpp
        src/plugins/BuiltinAudioCatalog.cpp
        src/plugins/VegasSharedAudioCatalog.cpp
        src/plugins/VegasVideoPluginCatalog.cpp
        src/plugins/PluginScanner.cpp
        src/plugins/OfxHost.cpp
        src/plugins/AudioPluginScanner.cpp
        src/audio/Vst3Host.cpp
        src/audio/BuiltinDsp.cpp
        src/audio/FadeCurves.cpp
        src/video/VideoKeyframeEval.cpp
        src/video/TitlesTextApply.cpp
        src/video/MediaGeneratorApply.cpp
        src/io/RenderTemplateCatalog.cpp
    )
    target_include_directories(openvegas_media_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/thirdparty
        ${CMAKE_SOURCE_DIR}/thirdparty/openfx/include
        ${CMAKE_SOURCE_DIR}/thirdparty/vestige
    )
    target_link_libraries(openvegas_media_tests PRIVATE
        Catch2::Catch2WithMain
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    if(OPENVGAS_HAS_VST3_SDK_INTERNAL)
        target_compile_definitions(openvegas_media_tests PRIVATE OPENVGAS_HAS_VST3_SDK=1)
        target_include_directories(openvegas_media_tests PRIVATE
            "${OPENVGAS_VST3_SDK_PATH}"
            "${OPENVGAS_VST3_SDK_PATH}/pluginterfaces"
        )
        target_sources(openvegas_media_tests PRIVATE ${OPENVGAS_VST3_SDK_SOURCES})
    endif()
    if(MSVC)
        target_compile_options(openvegas_media_tests PRIVATE /W4 /permissive-)
    endif()
    if(WIN32)
        target_link_libraries(openvegas_media_tests PRIVATE ole32 shell32 uuid user32 gdi32)
    endif()
    add_test(NAME openvegas_media_tests COMMAND openvegas_media_tests)

    # Minimal OFX Gain filter fixture for host process tests.
    add_library(ov_gain_ofx MODULE tests/fixtures/plugins/ov_gain_ofx.cpp)
    set_target_properties(ov_gain_ofx PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(ov_gain_ofx PRIVATE
        ${CMAKE_SOURCE_DIR}/thirdparty/openfx/include
    )
    set(_ov_ofx_fixture_dir "${CMAKE_BINARY_DIR}/tests/fixtures/plugins")
    set_target_properties(ov_gain_ofx PROPERTIES
        PREFIX ""
        OUTPUT_NAME "Gain"
        SUFFIX ".ofx"
        RUNTIME_OUTPUT_DIRECTORY "${_ov_ofx_fixture_dir}"
        LIBRARY_OUTPUT_DIRECTORY "${_ov_ofx_fixture_dir}"
        ARCHIVE_OUTPUT_DIRECTORY "${_ov_ofx_fixture_dir}"
    )
    foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
        set_target_properties(ov_gain_ofx PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${_ov_ofx_fixture_dir}"
            LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${_ov_ofx_fixture_dir}"
            ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${_ov_ofx_fixture_dir}"
        )
    endforeach()
    file(TO_CMAKE_PATH "${_ov_ofx_fixture_dir}" _ov_ofx_fixture_dir_cmake)
    target_compile_definitions(openvegas_media_tests PRIVATE
        OPENVGAS_OFX_FIXTURE_DIR="${_ov_ofx_fixture_dir_cmake}"
    )
    add_dependencies(openvegas_media_tests ov_gain_ofx)
endif()
