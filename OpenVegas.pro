# OpenVegas — qmake project for Qt Creator
# Open this file in Qt Creator (Qt 6.8+ Widgets kit).
# Alternative build: CMakeLists.txt (keep SOURCES/HEADERS in sync).

QT       += core gui widgets svg
CONFIG   += c++17 warn_on
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = OpenVegas
VERSION  = 0.1.0

# Headers live under src/; .ui under ui/
INCLUDEPATH += $$PWD/src \
               $$PWD/thirdparty \
               $$PWD/thirdparty/openfx/include \
               $$PWD/thirdparty/vestige
UI_DIR       = $$OUT_PWD/ui_headers
MOC_DIR      = $$OUT_PWD/moc
OBJECTS_DIR  = $$OUT_PWD/obj
RCC_DIR      = $$OUT_PWD/rcc

DEFINES += OPENVGAS_AUDIO=1

# Tell uic where to find .ui files referenced by #include "ui_*.h"
FORMS += \
    ui/MainWindow.ui \
    ui/WelcomeDialog.ui \
    ui/ProjectPropertiesDialog.ui \
    ui/RenderAsDialog.ui \
    ui/PreferencesDialog.ui \
    ui/EventPropertiesDialog.ui \
    ui/TrimmerWindow.ui \
    ui/PluginChooserDialog.ui

HEADERS += \
    src/app/MainWindow.h \
    src/ui/Theme.h \
    src/ui/MenuBuilder.h \
    src/ui/IconFactory.h \
    src/ui/WelcomeDialog.h \
    src/ui/FirstRunDialog.h \
    src/ui/CaptureTrayIcon.h \
    src/ui/CaptureWindow.h \
    src/ui/ProjectPropertiesDialog.h \
    src/ui/MatchMediaVideoSettingsDialog.h \
    src/ui/MissingFileDialog.h \
    src/ui/SearchMissingFilesDialog.h \
    src/ui/FindMissingFileDialog.h \
    src/ui/RenderAsDialog.h \
    src/ui/PreferencesDialog.h \
    src/ui/EventPropertiesDialog.h \
    src/ui/TrimmerWindow.h \
    src/ui/PluginChooserDialog.h \
    src/ui/AudioEventFxDialog.h \
    src/ui/ColorGradingEditor.h \
    src/ui/CollapsibleSection.h \
    src/ui/ColorPickerWidget.h \
    src/ui/TitlesTextEditorDialog.h \
    src/ui/ContextMenuBuilder.h \
    src/ui/FadeCurvePopup.h \
    src/ui/ExplorerPane.h \
    src/ui/VideoFxPane.h \
    src/ui/MediaGeneratorPane.h \
    src/ui/TransitionsPane.h \
    src/ui/ProjectNotesPane.h \
    src/ui/MixingConsoleWindow.h \
    src/ui/ExtractAudioFromCdDialog.h \
    src/ui/RenderingProgressDialog.h \
    src/ui/RateSlider.h \
    src/ui/VideoEventFxDialog.h \
    src/ui/VideoEventFxDialogExact.h \
    src/ui/VideoTrackFxDialog.h \
    src/ui/KeyframeLaneWidgets.h \
    src/ui/TrackMotionDialog.h \
    src/ui/ConfirmWarningDialog.h \
    src/ui/MediaBinListWidget.h \
    src/ui/MediaThumbHoverScrub.h \
    src/ui/KeyboardMap.h \
    src/ui/CustomizeKeyboardDialog.h \
    src/timeline/TimelineView.h \
    src/timeline/TimelineScrollHost.h \
    src/model/ProjectModel.h \
    src/model/TrackColors.h \
    src/model/ProjectSnapshot.h \
    src/model/SnapshotCommand.h \
    src/io/VegReader.h \
    src/io/ProjectInterchange.h \
    src/io/ProjectFile.h \
    src/capture/CaptureSource.h \
    src/capture/CapturePlan.h \
    src/capture/CaptureSources.h \
    src/capture/CaptureRecorder.h \
    src/io/SamplePaths.h \
    src/io/MediaMime.h \
    src/io/MediaThumbCache.h \
    src/io/MediaWaveformCache.h \
    src/io/MediaFilmstripCache.h \
    src/io/FfmpegLocator.h \
    src/io/MediaProbe.h \
    src/io/CdAudioReader.h \
    src/io/RenderTemplateCatalog.h \
    src/io/FFmpegEncoder.h \
    src/io/FFmpegStreamDecoder.h \
    src/media/MediaEngine.h \
    src/plugins/PluginScanner.h \
    src/plugins/PluginDiscovery.h \
    src/plugins/OfxHost.h \
    src/plugins/OfxTransitionSource.h \
    src/plugins/OfxTrace.h \
    src/plugins/OfxPluginPaths.h \
    src/plugins/OfxVegasExtensions.h \
    src/plugins/AudioPluginTypes.h \
    src/plugins/BuiltinAudioCatalog.h \
    src/plugins/VegasSharedAudioCatalog.h \
    src/plugins/VegasVideoPluginCatalog.h \
    src/plugins/AudioPluginScanner.h \
    src/plugins/AudioPluginRegistry.h \
    src/plugins/AudioPluginHost.h \
    src/audio/FadeCurves.h \
    src/audio/AudioUtil.h \
    src/audio/AudioDecodeCache.h \
    src/audio/BuiltinDsp.h \
    src/audio/AudioGraph.h \
    src/audio/AudioEngine.h \
    src/audio/CompositePluginHost.h \
    src/audio/Vst3Host.h \
    src/video/VideoFrameCache.h \
    src/video/VideoKeyframeEval.h \
    src/video/PanCropApply.h \
    src/video/TrackMotionApply.h \
    src/video/ColorCorrectorApply.h \
    src/video/TitlesTextApply.h \
    src/video/MediaGeneratorApply.h \
    src/video/TransitionApply.h \
    src/video/TransitionPresetData.h \
    src/video/TransitionPluginHook.h \
    src/video/VideoCompositor.h

SOURCES += \
    src/app/main.cpp \
    src/app/MainWindow.cpp \
    src/ui/Theme.cpp \
    src/ui/MenuBuilder.cpp \
    src/ui/IconFactory.cpp \
    src/ui/WelcomeDialog.cpp \
    src/ui/FirstRunDialog.cpp \
    src/ui/CaptureTrayIcon.cpp \
    src/ui/CaptureWindow.cpp \
    src/ui/ProjectPropertiesDialog.cpp \
    src/ui/MatchMediaVideoSettingsDialog.cpp \
    src/ui/MissingFileDialog.cpp \
    src/ui/SearchMissingFilesDialog.cpp \
    src/ui/FindMissingFileDialog.cpp \
    src/ui/RenderAsDialog.cpp \
    src/ui/PreferencesDialog.cpp \
    src/ui/EventPropertiesDialog.cpp \
    src/ui/TrimmerWindow.cpp \
    src/ui/PluginChooserDialog.cpp \
    src/ui/AudioEventFxDialog.cpp \
    src/ui/ColorGradingEditor.cpp \
    src/ui/CollapsibleSection.cpp \
    src/ui/ColorPickerWidget.cpp \
    src/ui/TitlesTextEditorDialog.cpp \
    src/ui/ContextMenuBuilder.cpp \
    src/ui/FadeCurvePopup.cpp \
    src/ui/ExplorerPane.cpp \
    src/ui/VideoFxPane.cpp \
    src/ui/MediaGeneratorPane.cpp \
    src/ui/TransitionsPane.cpp \
    src/ui/ProjectNotesPane.cpp \
    src/ui/MixingConsoleWindow.cpp \
    src/ui/ExtractAudioFromCdDialog.cpp \
    src/ui/RenderingProgressDialog.cpp \
    src/ui/RateSlider.cpp \
    src/ui/VideoEventFxDialog.cpp \
    src/ui/VideoEventFxDialogExact.cpp \
    src/ui/VideoTrackFxDialog.cpp \
    src/ui/TrackMotionDialog.cpp \
    src/ui/ConfirmWarningDialog.cpp \
    src/ui/MediaBinListWidget.cpp \
    src/ui/MediaThumbHoverScrub.cpp \
    src/ui/KeyboardMap.cpp \
    src/ui/CustomizeKeyboardDialog.cpp \
    src/timeline/TimelineView.cpp \
    src/timeline/TimelineScrollHost.cpp \
    src/model/ProjectModel.cpp \
    src/model/ProjectSnapshot.cpp \
    src/model/SnapshotCommand.cpp \
    src/io/VegReader.cpp \
    src/io/ProjectInterchange.cpp \
    src/io/ProjectFile.cpp \
    src/capture/CapturePlan.cpp \
    src/capture/CaptureSources.cpp \
    src/capture/CaptureRecorder.cpp \
    src/io/SamplePaths.cpp \
    src/io/MediaMime.cpp \
    src/io/MediaThumbCache.cpp \
    src/io/MediaWaveformCache.cpp \
    src/io/MediaFilmstripCache.cpp \
    src/io/FfmpegLocator.cpp \
    src/io/MediaProbe.cpp \
    src/io/CdAudioReader.cpp \
    src/io/RenderTemplateCatalog.cpp \
    src/io/FFmpegEncoder.cpp \
    src/io/FFmpegStreamDecoder.cpp \
    src/media/MediaEngine.cpp \
    src/plugins/PluginScanner.cpp \
    src/plugins/PluginDiscovery.cpp \
    src/plugins/OfxHost.cpp \
    src/plugins/OfxTransitionSource.cpp \
    src/plugins/OfxTrace.cpp \
    src/plugins/OfxPluginPaths.cpp \
    src/plugins/BuiltinAudioCatalog.cpp \
    src/plugins/VegasSharedAudioCatalog.cpp \
    src/plugins/VegasVideoPluginCatalog.cpp \
    src/plugins/AudioPluginScanner.cpp \
    src/plugins/AudioPluginRegistry.cpp \
    src/plugins/AudioPluginHost.cpp \
    src/audio/FadeCurves.cpp \
    src/audio/AudioDecodeCache.cpp \
    src/audio/BuiltinDsp.cpp \
    src/audio/AudioGraph.cpp \
    src/audio/AudioEngine.cpp \
    src/audio/CompositePluginHost.cpp \
    src/audio/Vst3Host.cpp \
    src/audio/miniaudio_impl.cpp \
    src/video/VideoFrameCache.cpp \
    src/video/VideoKeyframeEval.cpp \
    src/video/PanCropApply.cpp \
    src/video/TrackMotionApply.cpp \
    src/video/ColorCorrectorApply.cpp \
    src/video/TitlesTextApply.cpp \
    src/video/MediaGeneratorApply.cpp \
    src/video/TransitionApply.cpp \
    src/video/TransitionPresetData.cpp \
    src/video/TransitionPluginHook.cpp \
    src/video/VideoCompositor.cpp

RESOURCES += \
    resources/resources.qrc

win32:RC_ICONS = resources/icons/logo.ico
win32:LIBS += -lole32 -lshell32 -luuid -luser32 -lgdi32 -lwinmm -ldwmapi

# Warning levels. For MSVC do not append /W4 on top of Qt mkspec /W3 (D9025).
win32-msvc {
    QMAKE_CFLAGS_WARN_ON = -W4
    QMAKE_CXXFLAGS_WARN_ON = -W4
    QMAKE_CXXFLAGS += /permissive-
} else {
    QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
}

# Optional Vegas staging (gitignored). Prefer CMake -DOPENVGAS_COPY_VEGAS_RUNTIME=ON.
# In Creator: Projects → Build → Additional qmake arguments: CONFIG+=copy_vegas_runtime
copy_vegas_runtime {
    VEGAS_SRC = $$PWD/SAMPLES/VEGAS-PRO-22-PROGRAM-FILES
    VEGAS_DST = $$OUT_PWD/vegas-runtime
    !exists($$VEGAS_SRC) {
        warning("CONFIG+=copy_vegas_runtime set but SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not found")
    } else {
        message("Build will stage OFX/Icons into $$VEGAS_DST (do not commit)")
        QMAKE_POST_LINK += $$quote($$QMAKE_MKDIR $$shell_quote($$VEGAS_DST)) $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += $$quote($$QMAKE_COPY_DIR $$shell_quote($$VEGAS_SRC/Icons) $$shell_quote($$VEGAS_DST/Icons))
    }
}

# Desktop deployment hints
win32 {
    CONFIG += windows
}

unix:!macx {
    target.path = /usr/local/bin
    INSTALLS += target
}
