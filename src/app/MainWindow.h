#pragma once

#include "model/ProjectModel.h"
#include "model/ProjectSnapshot.h"
#include "io/ProjectInterchange.h"
#include "plugins/PluginScanner.h"

#include <QImage>
#include <QMainWindow>
#include <QLabel>
#include <QIcon>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

class QEvent;
class QToolButton;
class QAction;
class QUndoStack;
class QShortcut;
class QProgressBar;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

namespace openvegas {

class AudioEngine;
class TimelineView;
class TrimmerWindow;
class MixingConsoleWindow;
class VideoEventFxDialogExact;
class AudioEventFxDialog;
class ExplorerPane;
class VideoFxPane;
class MediaGeneratorPane;
class TransitionsPane;
class ProjectNotesPane;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void onNewProject();
    void onOpenProject();
    void onImportMedia();
    void onImportMediaFromProject();
    void onImportPremiere();
    void onImportFinalCutXml();
    void onImportFcpxml();
    void onImportEdl();
    void onImportBroadcastWave();
    void onImportClosedCaptions();
    void onExportProjectArchive();
    void onExportPremiere();
    void onExportFinalCutXml();
    void onExportFcpxml();
    void onExportEdl();
    void onWelcome();
    void onProjectProperties();
    void onRenderAs();
    void onBounceAudioMixdown();
    void onExtractAudioFromCd();
    void onPreferences();
    void onCustomizeKeyboard();
    void onPluginChooser();
    /** Open Video Event FX dialog for a video/still event (FX button / menu). */
    void onVideoEventFx(int eventId);
    void onAudioEventFx(int eventId);
    void onTrackFx(int trackIndex);
    /** Add Color Grading to a video track FX chain and open Video Track FX. */
    void onColorGrading(int trackIndex);
    void onTrackMotion(int trackIndex);
    void onSelectAll();
    void onEditCut();
    void onEditCopy();
    void onEditPaste();
    void onEditDelete();
    void onEditSplit();
    void onEditTrimStart();
    void onEditTrimEnd();
    void onGoToStart();
    void onGoToEnd();
    void onAbout();
    void onLoadDemoTimeline();
    void onMixingConsole();
    void openProjectPath(const QString &path);
    void applyProjectToUi();
    void rememberRecentFile(const QString &path);
    void refreshTimeline();
    /** Repaint ruler labels based on current ruler format selection. */
    void refreshTimecodeLabels();
    void openEventProperties(int eventId);
    void openTrimmer(int eventId);
    void beginTrackRename(int trackIndex);

    ProjectModel &projectModel() { return m_project; }
    const ProjectModel &projectModel() const { return m_project; }
    TimelineView *timelineView() const { return m_timeline; }
    /** Video/OFX + audio plug-in scanner (Vegas OFX roots from Preferences). */
    PluginScanner *pluginScanner() { return &m_pluginScanner; }
    /** Set loop-playback flag and sync toolbar / preview buttons. */
    void setLoopPlaybackEnabled(bool on);

    QUndoStack *undoStack() const { return m_undoStack; }
    QAction *undoAction() const { return m_undoAction; }
    QAction *redoAction() const { return m_redoAction; }

    /** Capture document state before a mutating edit (nested calls are ignored). */
    void beginDocumentEdit();
    /** Push undo command if document changed since beginDocumentEdit(). */
    void commitDocumentEdit(const QString &text);
    void discardDocumentEdit();
    /** begin → mutate → commit in one call. */
    void runDocumentEdit(const QString &text, const std::function<void()> &mutate);
    void clearUndoHistory();
    /** Refresh timeline / mixer / preview after undo or redo. */
    void onDocumentRestored();

    bool overlayGrid() const { return m_overlayGrid; }
    bool overlaySafeAreas() const { return m_overlaySafeAreas; }
    void setOverlayGrid(bool on);
    void setOverlaySafeAreas(bool on);
    /** Preview quality chip: level (Draft/Preview/…) + resolution (Auto/Full/Half/Quarter). */
    void setPreviewQuality(const QString &level, const QString &resolution);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void setupTimeline();
    void setupMediaBin();
    void setupExplorer();
    void setupVideoFx();
    void setupMediaGenerator();
    void setupTransitions();
    void setupProjectNotes();
    void setupToolbar();
    void setupMediaToolbar();
    void setupPreviewChrome();
    void setupTimelineTools();
    void wireTransportButtons();
    void applyKeyboardMap();
    void invokeKeyboardCommand(const QString &commandId);
    void setupMasterBus();
    void cycleDownmixOutput();
    void setDimOutput(bool on);
    void syncDownmixUi();
    void setupStatusBar();
    void restoreUiSettings();
    void saveUiSettings();
    void refreshStatusBar();
    void ensureAudioFxDialog();
    void addToolbarSep(QLayout *layout);
    void setTrackHeaderWidth(int width);
    void refreshMediaEmptyState();
    void importMediaFiles();
    void applyInterchangeImport(const InterchangeResult &result, const QString &statusLabel,
                                bool addEventsToTimeline);
    void populateSampleProjectMedia();
    void addMediaCard(const QString &name, const QString &kind, const QString &meta = QString(),
                      const QString &path = QString(), double lengthSec = 0.0);
    void updateMediaMeta();
    /** Vegas-style missing-media prompts (search / specify / ignore). */
    void resolveMissingMedia();
    void refreshMediaPoolUi();
    QString guessMediaKind(const QString &pathOrName) const;
    QString defaultMetaForKind(const QString &kind) const;
    QIcon mediaThumbIcon(const QString &kind, int variant = 0, const QString &path = QString()) const;
    void placeMediaOnTimeline(const QStringList &paths);
    void openMediaInTrimmer(const QString &path, const QString &nameHint = QString());
    void showProjectMediaContextMenu(const QPoint &pos);
    void showEventContextMenu(int eventId, const QPoint &globalPos);
    void showTimelineEmptyContextMenu(const QPoint &globalPos);
    void showTrackHeaderContextMenu(int trackIndex, const QPoint &globalPos);
    void showTrackEmptyContextMenu(int trackIndex, const QPoint &globalPos);
    void showRulerContextMenu(const QPoint &globalPos);
    void showMarkerLaneContextMenu(const QPoint &globalPos);
    void showMarkerContextMenu(int markerId, const QPoint &globalPos);
    void showPreviewContextMenu(const QPoint &globalPos);
    void showTimeDisplayContextMenu(const QPoint &globalPos);
    void updateTimecodeLabels(double sec);
    void updatePreviewDisplayMeta(double sec);
    void refreshPreviewProjectMeta();
    QString formatPreviewFps() const;
    int previewResolutionDivisor() const;
    void refreshPreviewFrame(double sec);
    /** Show an already-composed render frame in Video Preview (letterboxed). */
    void showRenderPreviewFrame(const QImage &frame, double sec);
    void syncTransportUi(bool playing);
    void syncPreviewOverlays();
    void updateOverlaysButton();

    Ui::MainWindow *ui = nullptr;
    ProjectModel m_project;
    PluginScanner m_pluginScanner;
    QUndoStack *m_undoStack = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    ProjectSnapshot m_editBefore;
    bool m_editCaptureOpen = false;
    TimelineView *m_timeline = nullptr;
    std::unique_ptr<AudioEngine> m_audioEngine;
    bool m_syncingPlayheadFromEngine = false;
    TrimmerWindow *m_trimmer = nullptr;
    MixingConsoleWindow *m_mixingConsole = nullptr;
    VideoEventFxDialogExact *m_videoEventFx = nullptr;
    AudioEventFxDialog *m_audioEventFx = nullptr;
    QString m_audioFxCommitLabel;
    ExplorerPane *m_explorer = nullptr;
    VideoFxPane *m_videoFx = nullptr;
    MediaGeneratorPane *m_mediaGen = nullptr;
    TransitionsPane *m_transitions = nullptr;
    ProjectNotesPane *m_notes = nullptr;
    QWidget *m_rateCol = nullptr;
    QWidget *m_previewOverlay = nullptr;
    QToolButton *m_overlaysBtn = nullptr;
    QAction *m_overlayGridAct = nullptr;
    QAction *m_overlaySafeAct = nullptr;
    bool m_overlayGrid = false;
    bool m_overlaySafeAreas = false;
    /** 1 = Auto/Full, 2 = Half, 4 = Quarter (Vegas Preview quality resolution). */
    int m_previewResDivisor = 1;
    QToolButton *m_previewQualityBtn = nullptr;
    QLabel *m_mainTimecode = nullptr;
    QLabel *m_tlTimecode = nullptr;
    QLabel *m_previewLeftMeta = nullptr;
    QLabel *m_previewRightMeta = nullptr;
    QLabel *m_statusProject = nullptr;
    QLabel *m_statusAudio = nullptr;
    QLabel *m_statusRecord = nullptr;

    /** Master Bus / Mixing Console: Downmix Output mode (Vegas cycles these). */
    enum class DownmixOutputMode { Surround, Stereo, Mono };
    DownmixOutputMode m_downmixMode = DownmixOutputMode::Stereo;
    bool m_dimOutput = false;
    QToolButton *m_masterDownmixBtn = nullptr;
    QToolButton *m_masterDimBtn = nullptr;
    QProgressBar *m_masterMeterL = nullptr;
    QProgressBar *m_masterMeterR = nullptr;
    QLabel *m_masterPeakL = nullptr;
    QLabel *m_masterPeakR = nullptr;

    QToolButton *m_previewLoopBtn = nullptr;
    QToolButton *m_previewPlayBtn = nullptr;
    QToolButton *m_previewPauseBtn = nullptr;
    QToolButton *m_previewStopBtn = nullptr;
    QToolButton *m_tlLoopBtn = nullptr;
    QToolButton *m_tlPlayBtn = nullptr;
    QToolButton *m_tlPauseBtn = nullptr;
    QToolButton *m_tlStopBtn = nullptr;
    QToolButton *m_tlPlayFromStartBtn = nullptr;
    QToolButton *m_tlGoStartBtn = nullptr;
    QToolButton *m_tlGoEndBtn = nullptr;
    QToolButton *m_tlPrevFrameBtn = nullptr;
    QToolButton *m_tlNextFrameBtn = nullptr;

    QImage m_lastPreviewFrame;
    qint64 m_lastAvSyncFrame = -1;
    QVector<QShortcut *> m_keyboardShortcuts;
};

} // namespace openvegas
