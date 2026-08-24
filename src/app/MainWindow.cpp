#include "app/MainWindow.h"
#include "ui_MainWindow.h"

#include "timeline/TimelineView.h"
#include "timeline/TimelineScrollHost.h"
#include "ui/MenuBuilder.h"
#include "ui/IconFactory.h"
#include "ui/WelcomeDialog.h"
#include "ui/ProjectPropertiesDialog.h"
#include "ui/RenderAsDialog.h"
#include "ui/RenderingProgressDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/EventPropertiesDialog.h"
#include "ui/TrimmerWindow.h"
#include "ui/PluginChooserDialog.h"
#include "ui/AudioEventFxDialog.h"
#include "ui/ContextMenuBuilder.h"
#include "ui/ExplorerPane.h"
#include "ui/VideoFxPane.h"
#include "ui/MediaGeneratorPane.h"
#include "ui/TransitionsPane.h"
#include "ui/ProjectNotesPane.h"
#include "ui/MixingConsoleWindow.h"
#include "ui/ExtractAudioFromCdDialog.h"
#include "ui/RateSlider.h"
#include "ui/VideoEventFxDialogExact.h"
#include "ui/TitlesTextEditorDialog.h"
#include "ui/TransitionPropertiesDialog.h"
#include "ui/VideoTrackFxDialog.h"
#include "ui/AudioEventFxDialog.h"
#include "ui/TrackMotionDialog.h"
#include "ui/CustomizeKeyboardDialog.h"
#include "ui/KeyboardMap.h"
#include "ui/MissingFileDialog.h"
#include "ui/SearchMissingFilesDialog.h"
#include "ui/FindMissingFileDialog.h"
#include "plugins/AudioPluginRegistry.h"
#include "plugins/AudioPluginHost.h"
#include "plugins/BuiltinAudioCatalog.h"
#include "audio/AudioEngine.h"
#include "audio/AudioUtil.h"
#include "audio/CompositePluginHost.h"
#include "video/VideoCompositor.h"
#include "video/VideoFrameCache.h"
#include "video/TitlesTextApply.h"
#include "io/VegReader.h"
#include "io/ProjectFile.h"
#include "io/ProjectInterchange.h"
#include "io/SamplePaths.h"
#include "io/MediaMime.h"
#include "io/MediaProbe.h"
#include "io/MediaThumbCache.h"
#include "io/MediaWaveformCache.h"
#include "media/MediaEngine.h"
#include "ui/MediaBinListWidget.h"
#include "model/SnapshotCommand.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QUndoStack>
#include <QTimer>
#include <cmath>
#include <functional>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QFrame>
#include <QSlider>
#include <QProgressBar>
#include <QButtonGroup>
#include <QListView>
#include <QAbstractItemView>
#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QPixmap>
#include <QLinearGradient>
#include <QKeySequence>
#include <QEvent>
#include <QContextMenuEvent>
#include <QDir>
#include <QSizePolicy>
#include <QSet>
#include <QShortcut>
#include <QStatusBar>
#include <QTabBar>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

void clearLayout(QLayout *layout)
{
    if (!layout) {
        return;
    }
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
}

/** dB scale between stereo VU meters (Vegas-style ticks 3…57). */
class VuScaleWidget : public QWidget
{
public:
    explicit VuScaleWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("vuScale"));
        setFixedWidth(22);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        QFont f = font();
        f.setPixelSize(8);
        f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Arial")});
        p.setFont(f);
        p.setPen(QColor(0x8a, 0x8a, 0x8a));

        static const int kMarks[] = {3,  6,  9,  12, 15, 18, 21, 24, 27, 30,
                                     33, 36, 39, 42, 45, 48, 51, 54, 57};
        constexpr int n = int(sizeof(kMarks) / sizeof(kMarks[0]));
        const int top = 4;
        const int bottom = height() - 4;
        const int span = qMax(1, bottom - top);
        const QFontMetrics fm(f);

        for (int i = 0; i < n; ++i) {
            const double t = double(i) / double(n - 1);
            const int y = top + int(std::lround(t * span));
            const QString num = QString::number(kMarks[i]);
            const int tw = fm.horizontalAdvance(num);
            const int th = fm.ascent();
            const int cx = width() / 2;
            const int tx = cx - tw / 2;
            const int ty = y + th / 2 - 1;
            p.drawText(tx, ty, num);
            const int dashY = y;
            const int gap = tw / 2 + 2;
            p.drawLine(1, dashY, cx - gap, dashY);
            p.drawLine(cx + gap, dashY, width() - 2, dashY);
        }
    }
};

/** Grid / Safe Areas drawn over Video Preview (Vegas Overlays). */
class PreviewOverlayLayer : public QWidget
{
public:
    explicit PreviewOverlayLayer(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("previewOverlayLayer"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setOverlays(bool grid, bool safeAreas)
    {
        if (m_grid == grid && m_safe == safeAreas) {
            return;
        }
        m_grid = grid;
        m_safe = safeAreas;
        setVisible(m_grid || m_safe);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_grid && !m_safe) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(QColor(255, 255, 255, 200));
        pen.setWidth(1);
        pen.setCosmetic(true);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({3.0, 3.0});
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        const QRect r = rect().adjusted(0, 0, -1, -1);
        if (r.width() < 8 || r.height() < 8) {
            return;
        }

        if (m_grid) {
            constexpr int cols = 12;
            constexpr int rows = 8;
            for (int c = 1; c < cols; ++c) {
                const int x = r.left() + (c * r.width()) / cols;
                p.drawLine(x, r.top(), x, r.bottom());
            }
            for (int row = 1; row < rows; ++row) {
                const int y = r.top() + (row * r.height()) / rows;
                p.drawLine(r.left(), y, r.right(), y);
            }
        }

        if (m_safe) {
            auto insetRect = [&](double frac) {
                const int dx = qMax(1, int(std::lround(r.width() * (1.0 - frac) / 2.0)));
                const int dy = qMax(1, int(std::lround(r.height() * (1.0 - frac) / 2.0)));
                return r.adjusted(dx, dy, -dx, -dy);
            };
            // Vegas: Action Safe ~90%, Title Safe ~80%
            p.drawRect(insetRect(0.90));
            p.drawRect(insetRect(0.80));
        }
    }

private:
    bool m_grid = false;
    bool m_safe = false;
};

/**
 * Vegas-style on-canvas move/resize handles for the Titles & Text event currently open
 * in TitlesTextEditorDialog. Geometry is set to exactly the letterboxed content rect
 * (see MainWindow::previewContentRect) so the widget's own local coordinates already
 * equal frame pixel coordinates — no separate offset math needed for hit-testing/paint.
 *
 * Only a uniform scale + move are supported (4 corner handles, no edge/side handles, no
 * rotation) because TitlesTextParams has no separate scaleX/scaleY or angle — matching
 * what's actually adjustable in the property dialog.
 */
class TitlesTextOverlayLayer : public QWidget {
public:
    explicit TitlesTextOverlayLayer(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("titlesTextOverlayLayer"));
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        hide();
    }

    std::function<void()> onEdited;

    void setActiveEvent(ProjectModel *project, int eventId)
    {
        m_project = project;
        m_eventId = eventId;
        m_dragging = false;
        m_hit = Hit::None;
        setVisible(m_project && m_eventId >= 0);
        update();
    }

    void clearActiveEvent()
    {
        m_project = nullptr;
        m_eventId = -1;
        m_dragging = false;
        m_hit = Hit::None;
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const TitlesTextParams params = currentParams();
        const QRectF box = titlesTextBoundingBox(params, size());
        if (box.isEmpty()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(QColor(255, 255, 255, 235));
        pen.setWidth(1);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(box);

        p.setBrush(QColor(255, 255, 255, 235));
        for (const QPointF &h : cornerHandles(box)) {
            p.drawRect(QRectF(h.x() - 4, h.y() - 4, 8, 8));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        TrackEvent *ev = currentEvent();
        if (!ev || ev->fxChain.isEmpty()) {
            return;
        }
        m_pressParams = titlesTextFromSlot(ev->fxChain.first());
        m_pressBox = titlesTextBoundingBox(m_pressParams, size());
        m_hit = hitTest(event->pos(), m_pressBox);
        if (m_hit == Hit::None) {
            return;
        }
        m_dragging = true;
        m_pressPos = event->pos();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging) {
            const QRectF box = titlesTextBoundingBox(currentParams(), size());
            updateHoverCursor(hitTest(event->pos(), box));
            return;
        }
        applyDrag(event->pos());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !m_dragging) {
            return;
        }
        applyDrag(event->pos());
        m_dragging = false;
        m_hit = Hit::None;
        updateHoverCursor(hitTest(event->pos(), titlesTextBoundingBox(currentParams(), size())));
    }

    void leaveEvent(QEvent *) override
    {
        if (!m_dragging) {
            unsetCursor();
        }
    }

private:
    enum class Hit { None, Move, TopLeft, TopRight, BottomLeft, BottomRight };

    TrackEvent *currentEvent() const
    {
        if (!m_project || m_eventId < 0) {
            return nullptr;
        }
        return m_project->findEvent(m_eventId);
    }

    TitlesTextParams currentParams() const
    {
        const TrackEvent *ev = currentEvent();
        if (!ev || ev->fxChain.isEmpty()) {
            return TitlesTextParams();
        }
        return titlesTextFromSlot(ev->fxChain.first());
    }

    static QVector<QPointF> cornerHandles(const QRectF &box)
    {
        return {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()};
    }

    Hit hitTest(const QPoint &pos, const QRectF &box) const
    {
        if (box.isEmpty()) {
            return Hit::None;
        }
        constexpr double kHandleRadius = 7.0;
        auto near = [&](const QPointF &pt) {
            return std::hypot(pt.x() - pos.x(), pt.y() - pos.y()) <= kHandleRadius;
        };
        if (near(box.topLeft())) {
            return Hit::TopLeft;
        }
        if (near(box.topRight())) {
            return Hit::TopRight;
        }
        if (near(box.bottomLeft())) {
            return Hit::BottomLeft;
        }
        if (near(box.bottomRight())) {
            return Hit::BottomRight;
        }
        if (box.contains(pos)) {
            return Hit::Move;
        }
        return Hit::None;
    }

    void updateHoverCursor(Hit hit)
    {
        switch (hit) {
        case Hit::TopLeft:
        case Hit::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Hit::TopRight:
        case Hit::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Hit::Move:
            setCursor(Qt::SizeAllCursor);
            break;
        default:
            unsetCursor();
            break;
        }
    }

    void applyDrag(const QPoint &pos)
    {
        TrackEvent *ev = currentEvent();
        if (!ev || ev->fxChain.isEmpty()) {
            return;
        }
        TitlesTextParams p = m_pressParams;
        if (m_hit == Hit::Move) {
            const double dx = double(pos.x() - m_pressPos.x()) / std::max(1, width());
            const double dy = double(pos.y() - m_pressPos.y()) / std::max(1, height());
            p.locationX = std::clamp(m_pressParams.locationX + dx, -2.0, 3.0);
            p.locationY = std::clamp(m_pressParams.locationY + dy, -2.0, 3.0);
        } else {
            // Uniform scale about the anchor point (locationX/Y): compare the drag
            // point's distance from the anchor to the same corner's original distance.
            const QPointF anchorPt(m_pressParams.locationX * width(),
                                   m_pressParams.locationY * height());
            QPointF corner;
            switch (m_hit) {
            case Hit::TopLeft:
                corner = m_pressBox.topLeft();
                break;
            case Hit::TopRight:
                corner = m_pressBox.topRight();
                break;
            case Hit::BottomLeft:
                corner = m_pressBox.bottomLeft();
                break;
            case Hit::BottomRight:
                corner = m_pressBox.bottomRight();
                break;
            default:
                return;
            }
            const double origDist = std::hypot(corner.x() - anchorPt.x(), corner.y() - anchorPt.y());
            if (origDist < 4.0) {
                return; // anchor sits ~on this corner — no stable ratio to scale by
            }
            const double newDist = std::hypot(pos.x() - anchorPt.x(), pos.y() - anchorPt.y());
            p.scale = std::clamp(m_pressParams.scale * (newDist / origDist), 0.01, 20.0);
        }

        titlesTextSaveToSlot(&ev->fxChain[0], p);
        update();
        if (onEdited) {
            onEdited();
        }
    }

    ProjectModel *m_project = nullptr;
    int m_eventId = -1;
    bool m_dragging = false;
    Hit m_hit = Hit::None;
    QPoint m_pressPos;
    QRectF m_pressBox;
    TitlesTextParams m_pressParams;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_undoStack = new QUndoStack(this);
    m_undoAction = m_undoStack->createUndoAction(this, tr("Undo"));
    m_undoAction->setShortcuts(QKeySequence::Undo);
    m_redoAction = m_undoStack->createRedoAction(this, tr("Redo"));
    m_redoAction->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence::Redo});

    MenuBuilder::build(this, ui->menubar);
    setupMediaBin();
    setupExplorer();
    setupVideoFx();
    setupMediaGenerator();
    setupTransitions();
    setupProjectNotes();
    setupMediaToolbar();
    setupPreviewChrome();
    setupMasterBus();
    setupTimeline();
    setupTimelineTools();

    // AudioEngine must exist before wireTransportButtons (positionChanged clock sync).
    m_audioEngine = std::make_unique<AudioEngine>(this);
    m_audioEngine->setProject(&m_project);
    m_audioEngine->setPluginHost(&CompositePluginHost::instance());
    if (m_timeline) {
        m_timeline->setExternalTransportClock(true);
    }
    m_audioEngine->startDevice();
    m_audioEngine->syncGraphFromProject();
    wireTransportButtons();

    setupToolbar();
    setupStatusBar();

    auto *meterTick = new QTimer(this);
    meterTick->setInterval(50);
    connect(meterTick, &QTimer::timeout, this, [this]() {
        if (!m_audioEngine) {
            return;
        }
        const auto &m = m_audioEngine->graph().masterMeter();
        const bool playing = m_audioEngine->isPlaying();
        const float peakL = playing ? m.peakL.load() : 0.f;
        const float peakR = playing ? m.peakR.load() : 0.f;

        auto toPct = [](float p) {
            if (p <= 1e-6f) {
                return 0;
            }
            const double db = linearToDb(p);
            return int(std::clamp((db + 60.0) / 72.0 * 100.0, 0.0, 100.0));
        };
        auto peakText = [](float p) {
            if (p <= 1e-6f) {
                return QStringLiteral("-Inf");
            }
            return QString::number(linearToDb(p), 'f', 1);
        };
        if (m_masterMeterL) {
            m_masterMeterL->setValue(toPct(peakL));
        }
        if (m_masterMeterR) {
            m_masterMeterR->setValue(toPct(peakR));
        }
        if (m_masterPeakL) {
            m_masterPeakL->setText(peakText(peakL));
        }
        if (m_masterPeakR) {
            m_masterPeakR->setText(peakText(peakR));
        }

        if (m_mixingConsole && m_mixingConsole->isVisible()) {
            m_mixingConsole->setMasterMeter(peakL, peakR);
            const auto tracks = m_project.tracks();
            int audioIdx = 0;
            auto meters = m_audioEngine->graph().trackMeters();
            for (const Track &t : tracks) {
                if (t.kind != TrackKind::Audio) {
                    continue;
                }
                if (audioIdx < meters.size() && meters[audioIdx]) {
                    m_mixingConsole->setTrackMeter(t.id, meters[audioIdx]->peakL.load(),
                                                   meters[audioIdx]->peakR.load());
                }
                ++audioIdx;
            }
        }
    });
    meterTick->start();

    ui->workspaceSplitter->setStretchFactor(0, 55);
    ui->workspaceSplitter->setStretchFactor(1, 45);
    ui->workspaceSplitter->setSizes({450, 350});

    ui->upperSplitter->setStretchFactor(0, 3);
    ui->upperSplitter->setStretchFactor(1, 5);
    ui->upperSplitter->setStretchFactor(2, 0);
    ui->upperSplitter->setSizes({420, 740, 104});

    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    m_pluginScanner.setPreferredPath(settings.value(QStringLiteral("plugins/ofxPath")).toString());
    m_pluginScanner.setVegasProPath(settings.value(QStringLiteral("plugins/vegasProPath")).toString());
    if (m_pluginScanner.vegasProPath().isEmpty()) {
        m_pluginScanner.setVegasProPath(PluginScanner::sampleVegasProPath());
    }

    restoreUiSettings();

    // Persist splitter drags without waiting for app exit
    connect(ui->workspaceSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        s.setValue(QStringLiteral("ui/workspaceSplitter"), ui->workspaceSplitter->saveState());
    });
    connect(ui->upperSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        s.setValue(QStringLiteral("ui/upperSplitter"), ui->upperSplitter->saveState());
    });
    connect(ui->mediaTabs, &QTabWidget::currentChanged, this, [](int index) {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        s.setValue(QStringLiteral("ui/mediaTab"), index);
    });

    connect(&KeyboardMap::instance(), &KeyboardMap::mapChanged, this, &MainWindow::applyKeyboardMap);
    applyKeyboardMap();
}

MainWindow::~MainWindow()
{
    if (m_audioEngine) {
        m_audioEngine->stop();
        m_audioEngine->stopDevice();
    }
    saveUiSettings();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveUiSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::restoreUiSettings()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));

    const QByteArray geo = s.value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
    const QByteArray winState = s.value(QStringLiteral("ui/windowState")).toByteArray();
    if (!winState.isEmpty()) {
        restoreState(winState);
    }

    const QByteArray ws = s.value(QStringLiteral("ui/workspaceSplitter")).toByteArray();
    if (!ws.isEmpty()) {
        ui->workspaceSplitter->restoreState(ws);
    }
    const QByteArray us = s.value(QStringLiteral("ui/upperSplitter")).toByteArray();
    if (!us.isEmpty()) {
        ui->upperSplitter->restoreState(us);
    }

    if (s.contains(QStringLiteral("ui/mediaTab"))) {
        const int tab = s.value(QStringLiteral("ui/mediaTab")).toInt();
        if (tab >= 0 && tab < ui->mediaTabs->count()) {
            ui->mediaTabs->setCurrentIndex(tab);
        }
    }

    if (s.contains(QStringLiteral("timeline/pixelsPerSecond"))) {
        m_project.setPixelsPerSecond(s.value(QStringLiteral("timeline/pixelsPerSecond")).toDouble());
        if (m_timeline) {
            m_timeline->refreshLayout();
            m_timeline->update();
        }
    }

    {
        const int fmt = s.value(QStringLiteral("timeline/rulerTimeFormat"),
                                static_cast<int>(RulerTimeFormat::TimeFrames))
                            .toInt();
        if (fmt >= static_cast<int>(RulerTimeFormat::Samples)
            && fmt <= static_cast<int>(RulerTimeFormat::AudioCDTime)) {
            m_project.setRulerTimeFormat(static_cast<RulerTimeFormat>(fmt));
        }
    }

    m_project.setSnappingEnabled(
        s.value(QStringLiteral("timeline/snappingEnabled"), true).toBool());
    m_project.setSnapToGrid(s.value(QStringLiteral("timeline/snapToGrid"), false).toBool());
    m_project.setSnapToMarkers(s.value(QStringLiteral("timeline/snapToMarkers"), true).toBool());
    m_project.setSnapToAllEvents(
        s.value(QStringLiteral("timeline/snapToAllEvents"), true).toBool());
    m_project.setAutomaticCrossfades(
        s.value(QStringLiteral("timeline/automaticCrossfades"), true).toBool());
    if (m_tlAutoCfBtn) {
        m_tlAutoCfBtn->setChecked(m_project.automaticCrossfades());
    }
    m_project.setQuantizeToFrames(
        s.value(QStringLiteral("timeline/quantizeToFrames"), true).toBool());
    {
        const int gs = s.value(QStringLiteral("timeline/gridSpacing"),
                               static_cast<int>(TimelineGridSpacing::RulerMarks))
                           .toInt();
        if (gs >= static_cast<int>(TimelineGridSpacing::RulerMarks)
            && gs <= static_cast<int>(TimelineGridSpacing::HalfFrames)) {
            m_project.setGridSpacing(static_cast<TimelineGridSpacing>(gs));
        }
    }
}

void MainWindow::saveUiSettings()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/windowState"), saveState());
    s.setValue(QStringLiteral("ui/workspaceSplitter"), ui->workspaceSplitter->saveState());
    s.setValue(QStringLiteral("ui/upperSplitter"), ui->upperSplitter->saveState());
    s.setValue(QStringLiteral("ui/mediaTab"), ui->mediaTabs->currentIndex());
    s.setValue(QStringLiteral("timeline/pixelsPerSecond"), m_project.pixelsPerSecond());
    s.setValue(QStringLiteral("timeline/rulerTimeFormat"),
               static_cast<int>(m_project.rulerTimeFormat()));
    s.setValue(QStringLiteral("timeline/snappingEnabled"), m_project.snappingEnabled());
    s.setValue(QStringLiteral("timeline/snapToGrid"), m_project.snapToGrid());
    s.setValue(QStringLiteral("timeline/snapToMarkers"), m_project.snapToMarkers());
    s.setValue(QStringLiteral("timeline/snapToAllEvents"), m_project.snapToAllEvents());
    s.setValue(QStringLiteral("timeline/automaticCrossfades"), m_project.automaticCrossfades());
    s.setValue(QStringLiteral("timeline/quantizeToFrames"), m_project.quantizeToFrames());
    s.setValue(QStringLiteral("timeline/gridSpacing"), static_cast<int>(m_project.gridSpacing()));
    if (m_timeline) {
        s.setValue(QStringLiteral("timeline/headerWidth"), m_timeline->headerWidth());
    }
    if (m_explorer) {
        m_explorer->saveSettings();
    }
    if (m_videoFx) {
        m_videoFx->saveSettings();
    }
    if (m_mediaGen) {
        m_mediaGen->saveSettings();
    }
    if (m_transitions) {
        m_transitions->saveSettings();
    }
    if (m_notes) {
        m_notes->saveSettings();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->previewViewport) {
        if (event->type() == QEvent::Resize) {
            if (m_previewOverlay) {
                m_previewOverlay->setGeometry(ui->previewViewport->rect());
                m_previewOverlay->raise();
            }
            syncTitlesTextOverlayGeometry();
            updatePreviewDisplayMeta(m_project.playheadSec());
        } else if (event->type() == QEvent::ContextMenu) {
            auto *ce = static_cast<QContextMenuEvent *>(event);
            showPreviewContextMenu(ce->globalPos());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::addToolbarSep(QLayout *layout)
{
    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("toolbarSep"));
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(7);
    sep->setFixedHeight(18);
    layout->addWidget(sep);
}

void MainWindow::setupToolbar()
{
    auto *layout = ui->mainToolBarLayout;
    clearLayout(layout);

    auto add = [&](const QString &title, const QString &svg, auto slot) {
        auto *btn = IconFactory::toolButton(this, title, svg);
        connect(btn, &QToolButton::clicked, this, slot);
        layout->addWidget(btn);
        return btn;
    };

    add(tr("New Project"), IconFactory::svgNew(), &MainWindow::onNewProject);
    add(tr("Open"), IconFactory::svgOpen(), &MainWindow::onOpenProject);
    add(tr("Save"), IconFactory::svgSave(), &MainWindow::onSaveProject);
    add(tr("Render As"), IconFactory::svgRender(), &MainWindow::onRenderAs);
    add(tr("Project Properties"), IconFactory::svgGear(), &MainWindow::onProjectProperties);
    addToolbarSep(layout);
    add(tr("Cut"), IconFactory::svgCut(), &MainWindow::onEditCut);
    add(tr("Copy"), IconFactory::svgCopy(), &MainWindow::onEditCopy);
    add(tr("Paste"), IconFactory::svgPaste(), &MainWindow::onEditPaste);
    addToolbarSep(layout);
    {
        auto *undoBtn = IconFactory::toolButton(this, tr("Undo"), IconFactory::svgUndo());
        undoBtn->setEnabled(false);
        connect(undoBtn, &QToolButton::clicked, m_undoStack, &QUndoStack::undo);
        connect(m_undoStack, &QUndoStack::canUndoChanged, undoBtn, &QWidget::setEnabled);
        layout->addWidget(undoBtn);
        auto *redoBtn = IconFactory::toolButton(this, tr("Redo"), IconFactory::svgRedo());
        redoBtn->setEnabled(false);
        connect(redoBtn, &QToolButton::clicked, m_undoStack, &QUndoStack::redo);
        connect(m_undoStack, &QUndoStack::canRedoChanged, redoBtn, &QWidget::setEnabled);
        layout->addWidget(redoBtn);
    }
    layout->addStretch(1);
}

void MainWindow::setupMediaToolbar()
{
    auto *layout = ui->mediaToolbarLayout;
    clearLayout(layout);

    auto *importBtn = new QToolButton(this);
    importBtn->setObjectName(QStringLiteral("textToolBtn"));
    importBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    importBtn->setIcon(IconFactory::iconFromSvgBody(IconFactory::svgImport(), 14));
    importBtn->setIconSize(QSize(14, 14));
    importBtn->setText(tr("Import Media... ▾"));
    importBtn->setAutoRaise(true);
    importBtn->setToolTip(tr("Import Media"));
    connect(importBtn, &QToolButton::clicked, this, &MainWindow::importMediaFiles);
    layout->addWidget(importBtn);
    addToolbarSep(layout);
    layout->addWidget(IconFactory::toolButton(this, tr("Auto Preview"), IconFactory::svgAutoPreview()));
    layout->addWidget(IconFactory::toolButton(this, tr("Capture Video"), IconFactory::svgCapture()));
    {
        auto *cdBtn = IconFactory::toolButton(this, tr("Extract Audio from CD"), IconFactory::svgCdExtract());
        connect(cdBtn, &QToolButton::clicked, this, &MainWindow::onExtractAudioFromCd);
        layout->addWidget(cdBtn);
    }
    layout->addWidget(IconFactory::toolButton(this, tr("Get Media from the Web"), IconFactory::svgWeb()));
    addToolbarSep(layout);
    layout->addWidget(IconFactory::toolButton(this, tr("Remove Selected Media"), IconFactory::svgRemove()));
    layout->addWidget(IconFactory::toolButton(this, tr("Media Properties"), IconFactory::svgGear()));
    auto *fx = new QToolButton(this);
    fx->setObjectName(QStringLiteral("msBtn"));
    fx->setText(QStringLiteral("fx"));
    fx->setToolTip(tr("Apply Non-Real-Time Event FX"));
    fx->setFixedSize(24, 24);
    fx->setAutoRaise(true);
    layout->addWidget(fx);
    addToolbarSep(layout);
    layout->addWidget(IconFactory::toolButton(this, tr("Start Preview"), IconFactory::svgPlay()));
    layout->addWidget(IconFactory::toolButton(this, tr("Stop Preview"), IconFactory::svgStop()));
    layout->addWidget(IconFactory::toolButton(this, tr("Open in Audio Editor"), IconFactory::svgWaveform()));
    addToolbarSep(layout);
    layout->addWidget(IconFactory::toolButton(this, tr("Views"), IconFactory::svgViews(), true, true));
    layout->addStretch(1);
    layout->addWidget(IconFactory::toolButton(this, tr("Search Media"), IconFactory::svgSearch()));
    layout->addWidget(IconFactory::toolButton(this, tr("Filter Media"), IconFactory::svgFilter()));
}

void MainWindow::setupPreviewChrome()
{
    auto *tb = ui->previewToolbarLayout;
    clearLayout(tb);
    tb->setContentsMargins(4, 2, 4, 2);
    tb->setSpacing(2);

    // Order from Vegas Pro 22 / preview-toolbar in static pages
    auto *btnProps = IconFactory::toolButton(this, tr("Project Properties"), IconFactory::svgGear());
    connect(btnProps, &QToolButton::clicked, this, &MainWindow::onProjectProperties);
    tb->addWidget(btnProps);
    tb->addWidget(IconFactory::toolButton(this, tr("Preview on External Monitor"), IconFactory::svgExternalMonitor()));
    {
        auto *fx = new QToolButton(this);
        fx->setObjectName(QStringLiteral("msBtn"));
        fx->setText(QStringLiteral("fx"));
        fx->setToolTip(tr("Video Output FX"));
        fx->setFixedSize(22, 20);
        fx->setAutoRaise(true);
        tb->addWidget(fx);
    }
    {
        auto *split = IconFactory::toolButton(this, tr("Split Screen View"), IconFactory::svgSplitScreen());
        connect(split, &QToolButton::clicked, this, [split]() {
            ContextMenuBuilder::showSplitScreenMenu(split, split->mapToGlobal(QPoint(0, split->height())));
        });
        tb->addWidget(split);
    }

    m_previewQualityBtn = new QToolButton(this);
    m_previewQualityBtn->setObjectName(QStringLiteral("previewChip"));
    m_previewQualityBtn->setText(tr("Preview (Auto) ▾"));
    m_previewQualityBtn->setToolTip(tr("Preview Quality"));
    m_previewQualityBtn->setAutoRaise(true);
    connect(m_previewQualityBtn, &QToolButton::clicked, this, [this]() {
        ContextMenuBuilder::showQualityMenu(
            this, m_previewQualityBtn,
            m_previewQualityBtn->mapToGlobal(QPoint(0, m_previewQualityBtn->height())));
    });
    tb->addWidget(m_previewQualityBtn);

    auto *zoom = new QToolButton(this);
    zoom->setObjectName(QStringLiteral("previewChip"));
    zoom->setText(tr("100 % ▾"));
    zoom->setToolTip(tr("Zoom"));
    zoom->setAutoRaise(true);
    connect(zoom, &QToolButton::clicked, this, [zoom]() {
        ContextMenuBuilder::showZoomMenu(zoom, zoom->mapToGlobal(QPoint(0, zoom->height())));
    });
    tb->addWidget(zoom);

    auto *overlays = new QToolButton(this);
    overlays->setObjectName(QStringLiteral("iconBtn"));
    overlays->setText(QStringLiteral("# ▾"));
    overlays->setToolTip(
        tr("Overlays: Displays graphical overlays in the Video Preview and Trimmer windows "
           "to help you perform visual alignment and color analysis."));
    overlays->setFixedSize(36, 22);
    overlays->setCheckable(true);
    overlays->setAutoRaise(true);
    overlays->setFocusPolicy(Qt::NoFocus);
    m_overlaysBtn = overlays;

    auto *ovMenu = new QMenu(overlays);
    m_overlayGridAct = ovMenu->addAction(tr("Grid"));
    m_overlayGridAct->setCheckable(true);
    m_overlaySafeAct = ovMenu->addAction(tr("Safe Areas"));
    m_overlaySafeAct->setCheckable(true);
    ovMenu->addSeparator();
    {
        auto *ccGroup = new QActionGroup(ovMenu);
        ccGroup->setExclusive(true);
        auto addCc = [&](const QString &label) {
            auto *a = ovMenu->addAction(label);
            a->setCheckable(true);
            ccGroup->addAction(a);
            return a;
        };
        addCc(tr("Closed Captioning CC1 (Primary)"));
        addCc(tr("Closed Captioning CC2"));
        addCc(tr("Closed Captioning CC3 (Secondary)"));
        addCc(tr("Closed Captioning CC4"));
    }
    ovMenu->addSeparator();
    {
        auto *chGroup = new QActionGroup(ovMenu);
        chGroup->setExclusive(true);
        auto addCh = [&](const QString &label) {
            auto *a = ovMenu->addAction(label);
            a->setCheckable(true);
            chGroup->addAction(a);
            return a;
        };
        addCh(tr("Red"));
        addCh(tr("Green"));
        addCh(tr("Blue"));
        addCh(tr("Red as Grayscale"));
        addCh(tr("Green as Grayscale"));
        addCh(tr("Blue as Grayscale"));
        addCh(tr("Alpha as Grayscale"));
    }
    connect(m_overlayGridAct, &QAction::toggled, this, &MainWindow::setOverlayGrid);
    connect(m_overlaySafeAct, &QAction::toggled, this, &MainWindow::setOverlaySafeAreas);
    overlays->setMenu(ovMenu);
    overlays->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(overlays);

    tb->addWidget(IconFactory::toolButton(this, tr("Copy Snapshot to Clipboard"), IconFactory::svgCopy()));
    tb->addWidget(IconFactory::toolButton(this, tr("Save Snapshot to File"), IconFactory::svgSave()));

    auto *btn360 = new QToolButton(this);
    btn360->setObjectName(QStringLiteral("msBtn"));
    btn360->setText(QStringLiteral("360"));
    btn360->setToolTip(tr("360° Video"));
    btn360->setFixedSize(28, 20);
    btn360->setEnabled(false);
    btn360->setAutoRaise(true);
    tb->addWidget(btn360);

    auto *btnHdr = new QToolButton(this);
    btnHdr->setObjectName(QStringLiteral("msBtn"));
    btnHdr->setText(QStringLiteral("HDR"));
    btnHdr->setToolTip(tr("HDR"));
    btnHdr->setFixedSize(28, 20);
    btnHdr->setEnabled(false);
    btnHdr->setAutoRaise(true);
    tb->addWidget(btnHdr);
    tb->addStretch(1);

    // Pure black viewport fills remaining height; footer stays compact (Vegas layout)
    ui->previewLabel->clear();
    ui->previewLabel->setText(QString());
    ui->previewViewportLayout->setContentsMargins(0, 0, 0, 0);
    ui->previewViewportLayout->setSpacing(0);
    ui->previewViewport->setMinimumSize(160, 120);
    ui->previewViewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->previewFooterHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    ui->previewToolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Rebuild footer: transport (centered) + Project/Preview | Frame/Display
    auto *footerHost = ui->previewFooterHostLayout;
    clearLayout(footerHost);
    footerHost->setContentsMargins(0, 0, 0, 0);
    footerHost->setSpacing(0);

    auto *transportRow = new QWidget(ui->previewFooterHost);
    transportRow->setObjectName(QStringLiteral("previewTransportRow"));
    transportRow->setFixedHeight(26);
    transportRow->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *transport = new QHBoxLayout(transportRow);
    transport->setContentsMargins(8, 2, 8, 0);
    transport->setSpacing(2);
    transport->addStretch(1);
    auto addTransport = [&](const QString &tip, const QString &svg, bool checkable = false, bool checked = false) {
        auto *b = IconFactory::toolButton(transportRow, tip, svg, checkable, checked);
        b->setObjectName(QStringLiteral("previewTransportBtn"));
        transport->addWidget(b);
        return b;
    };
    auto *previewLoop = addTransport(tr("Loop Playback"), IconFactory::svgLoop(), true, true);
    previewLoop->setChecked(m_project.loopPlaybackEnabled());
    connect(previewLoop, &QToolButton::toggled, this, [this](bool on) {
        m_project.setLoopPlaybackEnabled(on);
        if (m_tlLoopBtn && m_tlLoopBtn->isChecked() != on) {
            QSignalBlocker b(m_tlLoopBtn);
            m_tlLoopBtn->setChecked(on);
        }
    });
    auto *previewPlay = addTransport(tr("Play"), IconFactory::svgPlay());
    auto *previewPause = addTransport(tr("Pause"), IconFactory::svgPause());
    auto *previewStop = addTransport(tr("Stop"), IconFactory::svgStop());
    addTransport(tr("More"), IconFactory::svgMore());
    transport->addStretch(1);
    footerHost->addWidget(transportRow);

    // Wired later in setupTimeline once m_timeline exists; stash for connect.
    m_previewLoopBtn = previewLoop;
    m_previewPlayBtn = previewPlay;
    m_previewPauseBtn = previewPause;
    m_previewStopBtn = previewStop;

    auto *infoRow = new QWidget(ui->previewFooterHost);
    infoRow->setObjectName(QStringLiteral("previewInfoRow"));
    infoRow->setMinimumHeight(32);
    infoRow->setMaximumHeight(36);
    infoRow->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *infoLay = new QHBoxLayout(infoRow);
    infoLay->setContentsMargins(8, 0, 8, 2);
    infoLay->setSpacing(12);

    m_previewLeftMeta = new QLabel(infoRow);
    m_previewLeftMeta->setObjectName(QStringLiteral("previewFooterLeft"));
    m_previewLeftMeta->setTextFormat(Qt::RichText);
    m_previewLeftMeta->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_previewRightMeta = new QLabel(infoRow);
    m_previewRightMeta->setObjectName(QStringLiteral("previewFooterRight"));
    m_previewRightMeta->setTextFormat(Qt::RichText);
    m_previewRightMeta->setAlignment(Qt::AlignRight | Qt::AlignTop);

    infoLay->addWidget(m_previewLeftMeta, 1);
    infoLay->addWidget(m_previewRightMeta, 0);
    footerHost->addWidget(infoRow);

    if (auto *old = ui->previewPanel->findChild<QLabel *>(QStringLiteral("previewPanelTab"))) {
        old->deleteLater();
    }
    auto *tab = new QLabel(tr("  Video Preview"), ui->previewPanel);
    tab->setObjectName(QStringLiteral("previewPanelTab"));
    tab->setFixedHeight(22);
    tab->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->previewLayout->addWidget(tab);

    // Stretch: toolbar=0, viewport=1, footer=0, tab=0
    ui->previewLayout->setStretch(0, 0);
    ui->previewLayout->setStretch(1, 1);
    ui->previewLayout->setStretch(2, 0);
    ui->previewLayout->setStretch(3, 0);

    ui->previewViewport->installEventFilter(this);
    {
        auto *layer = new PreviewOverlayLayer(ui->previewViewport);
        m_previewOverlay = layer;
        layer->setGeometry(ui->previewViewport->rect());
        layer->raise();
        layer->hide();
    }
    {
        auto *layer = new TitlesTextOverlayLayer(ui->previewViewport);
        m_titlesTextOverlay = layer;
        layer->onEdited = [this] {
            refreshPreviewFrame(m_project.playheadSec());
            if (m_titlesTextEditor) {
                m_titlesTextEditor->refreshFromEvent();
            }
        };
        layer->raise();
        layer->hide();
    }
    syncTitlesTextOverlayGeometry();
    refreshPreviewProjectMeta();
    updatePreviewDisplayMeta(0.0);
}

void MainWindow::setupMasterBus()
{
    auto *layout = ui->masterLayout;
    clearLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *top = new QHBoxLayout();
    top->setContentsMargins(2, 3, 2, 1);
    top->setSpacing(0);
    auto addTop = [this, top](const QString &tip, const QString &svg, bool checkable = false) {
        QToolButton *b = IconFactory::toolButton(this, tip, svg, checkable);
        b->setFixedSize(22, 20);
        b->setIconSize(QSize(14, 14));
        top->addWidget(b);
        return b;
    };
    {
        QToolButton *props = addTop(tr("Master Bus Properties"), IconFactory::svgGear());
        connect(props, &QToolButton::clicked, this, &MainWindow::onProjectProperties);
    }
    // Vegas order: Downmix Output (cycles icon) → Dim Output → Mixing Console
    m_masterDownmixBtn = addTop(tr("Downmix Output"), IconFactory::svgDownmix(), true);
    connect(m_masterDownmixBtn, &QToolButton::clicked, this, &MainWindow::cycleDownmixOutput);
    m_masterDimBtn = addTop(tr("Dim Output"), IconFactory::svgDimOutput(), true);
    connect(m_masterDimBtn, &QToolButton::toggled, this, &MainWindow::setDimOutput);
    {
        QToolButton *mix = addTop(tr("Open Mixing Console"), IconFactory::svgMixingConsole());
        connect(mix, &QToolButton::clicked, this, &MainWindow::onMixingConsole);
    }
    top->addStretch(1);
    layout->addLayout(top);
    syncDownmixUi();

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(4, 2, 4, 4);
    titleRow->setSpacing(4);
    auto *titleIco = new QLabel(this);
    titleIco->setPixmap(IconFactory::iconFromSvgBody(IconFactory::svgMasterTitle(), 12).pixmap(12, 12));
    titleIco->setFixedSize(12, 12);
    auto *title = new QLabel(tr("Master"), this);
    title->setObjectName(QStringLiteral("masterTitle"));
    titleRow->addWidget(titleIco);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    layout->addLayout(titleRow);

    auto *btns = new QHBoxLayout();
    btns->setContentsMargins(3, 0, 3, 4);
    btns->setSpacing(3);
    auto makeMs = [this](const QString &text, const QString &tip, bool checkable = false) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("msBtn"));
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setFixedSize(20, 18);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    btns->addWidget(makeMs(QStringLiteral("fx"), tr("Track FX")));
    auto *autoWrite = new QToolButton(this);
    autoWrite->setObjectName(QStringLiteral("msBtn"));
    autoWrite->setIcon(IconFactory::iconFromSvgBody(IconFactory::svgAutomation(), 12));
    autoWrite->setIconSize(QSize(12, 12));
    autoWrite->setToolTip(tr("Automation Write"));
    autoWrite->setFixedSize(20, 18);
    autoWrite->setAutoRaise(true);
    autoWrite->setFocusPolicy(Qt::NoFocus);
    btns->addWidget(autoWrite);
    btns->addWidget(makeMs(QStringLiteral("M"), tr("Mute"), true));
    btns->addWidget(makeMs(QStringLiteral("S"), tr("Solo"), true));
    btns->addStretch(1);
    layout->addLayout(btns);

    auto *body = new QHBoxLayout();
    body->setContentsMargins(2, 0, 3, 0);
    body->setSpacing(3);

    auto *faderCol = new QVBoxLayout();
    faderCol->setContentsMargins(0, 0, 0, 0);
    faderCol->setSpacing(2);
    auto *fader = new QSlider(Qt::Vertical, this);
    fader->setObjectName(QStringLiteral("masterFader"));
    fader->setRange(0, 100);
    // 70 is unity on this scale, the same mapping the mixing console uses. The slider used
    // to open at 32, which reads as roughly −33 dB — it looked wrong even before it was
    // connected to anything.
    fader->setValue(dbToFaderPos(m_project.masterVolumeDb()));
    fader->setFixedWidth(16);
    fader->setToolTip(tr("Master volume"));
    faderCol->addWidget(fader, 1);
    m_masterFader = fader;
    auto *lock = new QToolButton(this);
    lock->setObjectName(QStringLiteral("masterLock"));
    lock->setIcon(IconFactory::iconFromSvgBody(IconFactory::svgLockFader(), 12));
    lock->setIconSize(QSize(12, 12));
    lock->setToolTip(tr("Lock Fader"));
    lock->setFixedSize(16, 16);
    lock->setAutoRaise(true);
    lock->setCheckable(true);
    lock->setFocusPolicy(Qt::NoFocus);
    faderCol->addWidget(lock, 0, Qt::AlignHCenter);
    m_masterLockBtn = lock;

    // Dragging pushes the new gain into the live mix on every step; the undo entry is
    // recorded once on release, so a drag is one edit rather than a hundred.
    connect(fader, &QSlider::valueChanged, this, [this](int pos) {
        m_project.setMasterVolumeDb(faderPosToDb(pos));
        if (m_audioEngine) {
            m_audioEngine->syncMixerLive();
        }
        if (m_mixingConsole) {
            m_mixingConsole->refreshFromProject();
        }
    });
    connect(fader, &QSlider::sliderPressed, this, [this]() { beginDocumentEdit(); });
    connect(fader, &QSlider::sliderReleased, this,
            [this]() { commitDocumentEdit(tr("Master Volume")); });
    // Lock is Vegas's guard against nudging the master by accident: the fader stops
    // taking input, and nothing about the gain itself changes.
    connect(lock, &QToolButton::toggled, this, [this](bool on) {
        if (m_masterFader) {
            m_masterFader->setEnabled(!on);
        }
    });
    body->addLayout(faderCol);

    auto *metersWrap = new QVBoxLayout();
    metersWrap->setContentsMargins(0, 0, 0, 0);
    metersWrap->setSpacing(2);

    auto *metersRow = new QHBoxLayout();
    metersRow->setContentsMargins(0, 0, 0, 0);
    metersRow->setSpacing(2);
    auto makeMeter = [this]() {
        auto *m = new QProgressBar(this);
        m->setOrientation(Qt::Vertical);
        m->setTextVisible(false);
        m->setRange(0, 100);
        m->setValue(0);
        m->setObjectName(QStringLiteral("masterMeter"));
        m->setFixedWidth(14);
        m->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        return m;
    };
    metersRow->addWidget(m_masterMeterL = makeMeter());
    metersRow->addWidget(new VuScaleWidget(this));
    metersRow->addWidget(m_masterMeterR = makeMeter());
    metersWrap->addLayout(metersRow, 1);

    auto *peaksRow = new QHBoxLayout();
    peaksRow->setContentsMargins(0, 0, 0, 0);
    peaksRow->setSpacing(2);
    auto makePeak = [this]() {
        auto *peak = new QLabel(QStringLiteral("-Inf"), this);
        peak->setObjectName(QStringLiteral("masterPeaks"));
        peak->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        peak->setFixedWidth(14);
        peak->setFixedHeight(14);
        return peak;
    };
    peaksRow->addWidget(m_masterPeakL = makePeak());
    peaksRow->addSpacing(22);
    peaksRow->addWidget(m_masterPeakR = makePeak());
    metersWrap->addLayout(peaksRow);

    body->addLayout(metersWrap, 1);
    layout->addLayout(body, 1);

    auto *tabRow = new QWidget(this);
    tabRow->setObjectName(QStringLiteral("masterBusFooter"));
    tabRow->setFixedHeight(22);
    auto *tabLay = new QHBoxLayout(tabRow);
    tabLay->setContentsMargins(4, 0, 2, 0);
    tabLay->setSpacing(0);
    auto *tab = new QLabel(tr("Master Bus"), tabRow);
    tab->setObjectName(QStringLiteral("masterBusTab"));
    tabLay->addWidget(tab, 1);
    auto *maxBtn = new QToolButton(tabRow);
    maxBtn->setObjectName(QStringLiteral("panelTabIco"));
    maxBtn->setText(QStringLiteral("▣"));
    maxBtn->setToolTip(tr("Maximize"));
    maxBtn->setFixedSize(16, 16);
    maxBtn->setAutoRaise(true);
    maxBtn->setFocusPolicy(Qt::NoFocus);
    auto *closeBtn = new QToolButton(tabRow);
    closeBtn->setObjectName(QStringLiteral("panelTabIco"));
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setToolTip(tr("Close"));
    closeBtn->setFixedSize(16, 16);
    closeBtn->setAutoRaise(true);
    closeBtn->setFocusPolicy(Qt::NoFocus);
    tabLay->addWidget(maxBtn);
    tabLay->addWidget(closeBtn);
    layout->addWidget(tabRow);
}

void MainWindow::cycleDownmixOutput()
{
    switch (m_downmixMode) {
    case DownmixOutputMode::Surround:
        m_downmixMode = DownmixOutputMode::Stereo;
        break;
    case DownmixOutputMode::Stereo:
        m_downmixMode = DownmixOutputMode::Mono;
        break;
    case DownmixOutputMode::Mono:
        m_downmixMode = DownmixOutputMode::Surround;
        break;
    }
    syncDownmixUi();
    QString modeName;
    switch (m_downmixMode) {
    case DownmixOutputMode::Surround:
        modeName = tr("5.1 surround");
        break;
    case DownmixOutputMode::Stereo:
        modeName = tr("Stereo");
        break;
    case DownmixOutputMode::Mono:
        modeName = tr("Mono");
        break;
    }
    statusBar()->showMessage(tr("Downmix Output: %1").arg(modeName), 2000);
}

void MainWindow::setDimOutput(bool on)
{
    if (m_dimOutput == on) {
        return;
    }
    m_dimOutput = on;
    syncDownmixUi();
    statusBar()->showMessage(on ? tr("Dim Output: −20 dB") : tr("Dim Output: off"), 2000);
}

void MainWindow::syncDownmixUi()
{
    QString svg;
    QString tip;
    // Vegas: button latched while playback is downmixed away from the project master layout.
    // Default projects are stereo — Mono (and explicit Surround listen) show as pressed;
    // Stereo matches the common “Downmix Output” highlight from Vegas screenshots.
    bool latched = (m_downmixMode != DownmixOutputMode::Surround);
    switch (m_downmixMode) {
    case DownmixOutputMode::Surround:
        svg = IconFactory::svgDownmixSurround();
        tip = tr("Downmix Output (5.1 surround)");
        break;
    case DownmixOutputMode::Stereo:
        svg = IconFactory::svgDownmix();
        tip = tr("Downmix Output (Stereo)");
        break;
    case DownmixOutputMode::Mono:
        svg = IconFactory::svgDownmixMono();
        tip = tr("Downmix Output (Mono)");
        break;
    }

    if (m_masterDownmixBtn) {
        m_masterDownmixBtn->blockSignals(true);
        m_masterDownmixBtn->setIcon(IconFactory::iconFromSvgBody(svg, 14));
        m_masterDownmixBtn->setToolTip(tip);
        m_masterDownmixBtn->setChecked(latched);
        m_masterDownmixBtn->blockSignals(false);
    }
    if (m_masterDimBtn) {
        m_masterDimBtn->blockSignals(true);
        m_masterDimBtn->setChecked(m_dimOutput);
        m_masterDimBtn->blockSignals(false);
    }
    if (m_mixingConsole) {
        m_mixingConsole->syncMonitorButtons(int(m_downmixMode), m_dimOutput);
    }
}

void MainWindow::setupTimelineTools()
{
    auto *layout = ui->timelineToolsLayout;
    clearLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Left column — same width as track headers / ruler corner (Vegas --track-header-w)
    m_rateCol = new QWidget(this);
    m_rateCol->setObjectName(QStringLiteral("timelineRateCol"));
    auto *rateLay = new QHBoxLayout(m_rateCol);
    rateLay->setContentsMargins(6, 2, 8, 2);
    rateLay->setSpacing(4);

    auto *rateLabel = new QLabel(tr("Rate:"), m_rateCol);
    rateLabel->setObjectName(QStringLiteral("rateLabel"));
    auto *rateVal = new QLabel(QStringLiteral("0,00"), m_rateCol);
    rateVal->setObjectName(QStringLiteral("rateValue"));
    rateVal->setFixedWidth(32);
    rateVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *rateSlider = new RateSlider(m_rateCol);
    connect(rateSlider, &RateSlider::rateChanged, this, [this, rateVal](double rate) {
        rateVal->setText(
            QString::number(rate, 'f', 2).replace(QLatin1Char('.'), QLatin1Char(',')));
        if (m_timeline) {
            m_timeline->setShuttleRate(rate);
        }
    });
    rateLay->addWidget(rateLabel);
    rateLay->addWidget(rateVal);
    rateLay->addWidget(rateSlider, 1);

    layout->addWidget(m_rateCol);

    auto *rest = new QWidget(this);
    rest->setObjectName(QStringLiteral("timelineToolsRest"));
    auto *restLay = new QHBoxLayout(rest);
    restLay->setContentsMargins(6, 1, 8, 1);
    restLay->setSpacing(2);

    auto addDisabled = [&](const QString &title, const QString &svg) {
        auto *btn = IconFactory::toolButton(rest, title, svg);
        btn->setEnabled(false);
        restLay->addWidget(btn);
        return btn;
    };

    restLay->addWidget(IconFactory::toolButton(rest, tr("Record into Track"), IconFactory::svgRecord()));
    auto *loopPlaybackBtn =
        IconFactory::toolButton(rest, tr("Loop Playback"), IconFactory::svgLoop(), true, true);
    loopPlaybackBtn->setChecked(m_project.loopPlaybackEnabled());
    connect(loopPlaybackBtn, &QToolButton::toggled, this, [this](bool on) {
        m_project.setLoopPlaybackEnabled(on);
        if (m_previewLoopBtn && m_previewLoopBtn->isChecked() != on) {
            QSignalBlocker b(m_previewLoopBtn);
            m_previewLoopBtn->setChecked(on);
        }
    });
    restLay->addWidget(loopPlaybackBtn);
    m_tlLoopBtn = loopPlaybackBtn;
    auto *playFromStart = IconFactory::toolButton(rest, tr("Play from Start"), IconFactory::svgPlayFromStart());
    auto *playBtn = IconFactory::toolButton(rest, tr("Play"), IconFactory::svgPlay());
    auto *pauseBtn = IconFactory::toolButton(rest, tr("Pause"), IconFactory::svgPause());
    auto *stopBtn = IconFactory::toolButton(rest, tr("Stop"), IconFactory::svgStop());
    restLay->addWidget(playFromStart);
    restLay->addWidget(playBtn);
    restLay->addWidget(pauseBtn);
    restLay->addWidget(stopBtn);
    auto *goStartBtn = IconFactory::toolButton(rest, tr("Go to Start"), IconFactory::svgGoStart());
    auto *goEndBtn = IconFactory::toolButton(rest, tr("Go to End"), IconFactory::svgGoEnd());
    auto *prevFrameBtn = IconFactory::toolButton(rest, tr("Previous Frame"), IconFactory::svgPrevFrame());
    auto *nextFrameBtn = IconFactory::toolButton(rest, tr("Next Frame"), IconFactory::svgNextFrame());
    restLay->addWidget(goStartBtn);
    restLay->addWidget(goEndBtn);
    restLay->addWidget(prevFrameBtn);
    restLay->addWidget(nextFrameBtn);
    m_tlPlayBtn = playBtn;
    m_tlPauseBtn = pauseBtn;
    m_tlStopBtn = stopBtn;
    m_tlPlayFromStartBtn = playFromStart;
    m_tlGoStartBtn = goStartBtn;
    m_tlGoEndBtn = goEndBtn;
    m_tlPrevFrameBtn = prevFrameBtn;
    m_tlNextFrameBtn = nextFrameBtn;
    addToolbarSep(restLay);

    auto *editGroup = new QButtonGroup(this);
    editGroup->setExclusive(true);
    auto *normal = IconFactory::toolButton(rest, tr("Normal Edit Tool"), IconFactory::svgEditNormal(), true, true);
    auto *env = IconFactory::toolButton(rest, tr("Envelope Edit Tool"), IconFactory::svgEnvelope(), true);
    auto *sel = IconFactory::toolButton(rest, tr("Selection Edit Tool"), IconFactory::svgSelection(), true);
    auto *zoom = IconFactory::toolButton(rest, tr("Zoom Edit Tool"), IconFactory::svgZoom(), true);
    for (QToolButton *b : {normal, env, sel, zoom}) {
        editGroup->addButton(b);
        restLay->addWidget(b);
    }
    addToolbarSep(restLay);

    auto *deleteBtn = IconFactory::toolButton(rest, tr("Delete"), IconFactory::svgDelete());
    connect(deleteBtn, &QToolButton::clicked, this, &MainWindow::onEditDelete);
    restLay->addWidget(deleteBtn);
    restLay->addWidget(IconFactory::toolButton(rest, tr("Trim"), IconFactory::svgTrim()));
    auto *trimStartBtn = IconFactory::toolButton(rest, tr("Trim Start"), IconFactory::svgTrimStart());
    connect(trimStartBtn, &QToolButton::clicked, this, &MainWindow::onEditTrimStart);
    restLay->addWidget(trimStartBtn);
    auto *trimEndBtn = IconFactory::toolButton(rest, tr("Trim End"), IconFactory::svgTrimEnd());
    connect(trimEndBtn, &QToolButton::clicked, this, &MainWindow::onEditTrimEnd);
    restLay->addWidget(trimEndBtn);
    auto *splitBtn = IconFactory::toolButton(rest, tr("Split"), IconFactory::svgSplit());
    connect(splitBtn, &QToolButton::clicked, this, &MainWindow::onEditSplit);
    restLay->addWidget(splitBtn);
    restLay->addWidget(IconFactory::toolButton(rest, tr("Heal"), IconFactory::svgHeal()));
    restLay->addWidget(IconFactory::toolButton(rest, tr("Lock"), IconFactory::svgLock(), true, true));
    addToolbarSep(restLay);

    auto *insertMarkerBtn = IconFactory::toolButton(rest, tr("Insert Marker"), IconFactory::svgMarker());
    connect(insertMarkerBtn, &QToolButton::clicked, this, [this]() {
        if (m_timeline) {
            m_timeline->insertMarkerAtPlayhead();
        }
    });
    restLay->addWidget(insertMarkerBtn);
    auto *insertRegionBtn = IconFactory::toolButton(rest, tr("Insert Region"), IconFactory::svgRegion());
    connect(insertRegionBtn, &QToolButton::clicked, this, [this]() {
        if (m_timeline) {
            m_timeline->insertLoopRegionAtPlayhead();
        }
    });
    restLay->addWidget(insertRegionBtn);
    addToolbarSep(restLay);

    restLay->addWidget(IconFactory::toolButton(rest, tr("Enable Snapping"), IconFactory::svgSnap(), true, true));
    auto *autoCfBtn = IconFactory::toolButton(rest, tr("Automatic Crossfades"), IconFactory::svgAutoCf(),
                                              true, m_project.automaticCrossfades());
    connect(autoCfBtn, &QToolButton::toggled, this, [this](bool on) {
        m_project.setAutomaticCrossfades(on);
    });
    restLay->addWidget(autoCfBtn);
    m_tlAutoCfBtn = autoCfBtn;
    restLay->addWidget(IconFactory::toolButton(rest, tr("Auto Ripple"), IconFactory::svgAutoRipple()));
    restLay->addWidget(
        IconFactory::toolButton(rest, tr("Lock Envelopes"), IconFactory::svgLockEnvelopes(), true, true));
    auto *ignoreGrouping =
        IconFactory::toolButton(rest, tr("Ignore Event Grouping"), IconFactory::svgIgnoreGrouping(), true);
    ignoreGrouping->setChecked(m_project.ignoreEventGrouping());
    connect(ignoreGrouping, &QToolButton::toggled, this, [this](bool on) {
        m_project.setIgnoreEventGrouping(on);
        if (m_timeline) {
            m_timeline->update();
        }
    });
    restLay->addWidget(ignoreGrouping);
    restLay->addWidget(
        IconFactory::toolButton(rest, tr("Video Output Color Grading"), IconFactory::svgColorGrade()));
    addToolbarSep(restLay);

    addDisabled(tr("Paste Attributes"), IconFactory::svgPasteAttr());
    addDisabled(tr("Copy Attributes"), IconFactory::svgCopyAttr());
    auto *groupBtn = IconFactory::toolButton(rest, tr("Group"), IconFactory::svgGroup());
    connect(groupBtn, &QToolButton::clicked, this, [this]() {
        runDocumentEdit(tr("Group"), [this]() { m_project.groupSelectedEvents(); });
        refreshTimeline();
    });
    restLay->addWidget(groupBtn);
    restLay->addStretch(1);

    m_tlTimecode = new QLabel(QStringLiteral("1.1.000"), rest);
    m_tlTimecode->setObjectName(QStringLiteral("timelineTimecode"));
    m_tlTimecode->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tlTimecode, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        showTimeDisplayContextMenu(m_tlTimecode->mapToGlobal(pos));
    });
    restLay->addWidget(m_tlTimecode);

    auto *miniVu = new QWidget(rest);
    miniVu->setObjectName(QStringLiteral("miniVu"));
    miniVu->setToolTip(tr("Master"));
    miniVu->setFixedSize(16, 18);
    auto *miniLay = new QHBoxLayout(miniVu);
    miniLay->setContentsMargins(2, 1, 2, 1);
    miniLay->setSpacing(2);
    auto makeMini = [miniVu](int pct) {
        auto *ch = new QFrame(miniVu);
        ch->setObjectName(QStringLiteral("miniVuCh"));
        ch->setFixedSize(4, 16);
        auto *fill = new QFrame(ch);
        fill->setObjectName(QStringLiteral("miniVuFill"));
        fill->setGeometry(0, 16 - qMax(1, 16 * pct / 100), 4, qMax(1, 16 * pct / 100));
        return ch;
    };
    miniLay->addWidget(makeMini(2));
    miniLay->addWidget(makeMini(2));
    restLay->addWidget(miniVu);

    layout->addWidget(rest, 1);

    // Sync width with timeline headers (and restore last size)
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const int savedW = settings.value(QStringLiteral("timeline/headerWidth"), 210).toInt();
    setTrackHeaderWidth(savedW);
    if (m_timeline) {
        connect(m_timeline, &TimelineView::headerWidthChanged, this, [this](int w) {
            if (m_rateCol) {
                m_rateCol->setFixedWidth(w);
            }
        });
        connect(m_timeline, &TimelineView::headerWidthEditFinished, this, [](int w) {
            QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
            settings.setValue(QStringLiteral("timeline/headerWidth"), w);
        });
    }
}

void MainWindow::setTrackHeaderWidth(int width)
{
    const int w = qBound(140, width, 480);
    if (m_timeline) {
        m_timeline->setHeaderWidth(w);
    }
    if (m_rateCol) {
        m_rateCol->setFixedWidth(m_timeline ? m_timeline->headerWidth() : w);
    }
}

void MainWindow::setupStatusBar()
{
    auto *bar = statusBar();
    bar->setObjectName(QStringLiteral("mainStatusBar"));
    bar->setSizeGripEnabled(true);
    bar->show();

    m_statusProject = new QLabel(bar);
    m_statusProject->setObjectName(QStringLiteral("statusProject"));
    m_statusProject->setMinimumWidth(160);

    m_statusAudio = new QLabel(bar);
    m_statusAudio->setObjectName(QStringLiteral("statusAudio"));
    m_statusAudio->setMinimumWidth(100);

    m_statusRecord = new QLabel(bar);
    m_statusRecord->setObjectName(QStringLiteral("statusRecord"));
    m_statusRecord->setMinimumWidth(220);

    bar->addPermanentWidget(m_statusProject);
    bar->addPermanentWidget(m_statusAudio);
    bar->addPermanentWidget(m_statusRecord);

    refreshStatusBar();
    bar->showMessage(tr("Ready"));
}

void MainWindow::refreshStatusBar()
{
    if (m_statusProject) {
        const QString fps =
            QString::number(m_project.frameRate(), 'f', 3).replace(QLatin1Char('.'), QLatin1Char(','));
        m_statusProject->setText(
            tr("%1×%2×32; %3p").arg(m_project.frameWidth()).arg(m_project.frameHeight()).arg(fps));
        m_statusProject->setToolTip(tr("Project video format"));
    }
    if (m_statusAudio) {
        m_statusAudio->setText(tr("%1 Hz").arg(m_project.sampleRate()));
        m_statusAudio->setToolTip(tr("Project audio sample rate"));
    }
    if (m_statusRecord) {
        // Vegas-style remaining record time readout (placeholder until capture I/O exists)
        m_statusRecord->setText(tr("Record Time (2 channels): 41:06:27:05"));
        m_statusRecord->setToolTip(tr("Estimated record time remaining"));
    }
}

void MainWindow::setupMediaBin()
{
    ui->mediaTree->clear();
    ui->mediaTree->setSpacing(1);
    // project-with-2-videos_static.html — Media By Type expanded
    ui->mediaTree->addItems({
        QStringLiteral("All Media"),
        QStringLiteral("Media By Type"),
        QStringLiteral("    Video"),
        QStringLiteral("    Audio"),
        QStringLiteral("    Still Image"),
        QStringLiteral("Tagged Media"),
        QStringLiteral("Custom Bins"),
        QStringLiteral("Smart Bins"),
        QStringLiteral("Storyboard Bin"),
        QStringLiteral("Main Timeline"),
    });
    ui->mediaTree->setCurrentRow(0);
    ui->mediaTree->setMaximumWidth(160);

    ui->mediaGrid->clear();
    ui->mediaGrid->setViewMode(QListView::IconMode);
    ui->mediaGrid->setIconSize(QSize(120, 68));
    ui->mediaGrid->setGridSize(QSize(132, 100));
    ui->mediaGrid->setSpacing(8);
    ui->mediaGrid->setResizeMode(QListView::Adjust);
    ui->mediaGrid->setMovement(QListView::Static);
    ui->mediaGrid->setWordWrap(true);
    ui->mediaGrid->setUniformItemSizes(true);
    ui->mediaGrid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->mediaGrid->setDragEnabled(true);
    ui->mediaGrid->setAcceptDrops(true);
    ui->mediaGrid->setDragDropMode(QAbstractItemView::DragDrop);
    ui->mediaGrid->setDefaultDropAction(Qt::CopyAction);
    connect(ui->mediaGrid, &MediaBinListWidget::filesDropped, this, [this](const QStringList &paths) {
        int added = 0;
        for (const QString &path : MediaMime::expandToMediaFiles(paths)) {
            const QString kind = MediaMime::guessKind(path);
            const QString name = QFileInfo(path).fileName();
            bool exists = false;
            for (const MediaItem &m : m_project.mediaPool()) {
                if (QDir::cleanPath(m.path).compare(QDir::cleanPath(path), Qt::CaseInsensitive) == 0) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                MediaItem item;
                item.path = path;
                item.displayName = name;
                item.kind = kind;
                item.missing = !QFileInfo::exists(path);
                m_project.mediaPool().push_back(item);
                addMediaCard(name, kind, defaultMetaForKind(kind), path);
                ++added;
            }
        }
        refreshMediaEmptyState();
        if (added > 0) {
            statusBar()->showMessage(tr("Imported %1 media file(s) into Project Media").arg(added), 3000);
        }
    });

    ui->mediaMeta->setObjectName(QStringLiteral("mediaMeta"));
    ui->mediaMeta->clear();

    // Empty / filled stack around the grid (Import CTA when empty)
    if (!ui->tabProjectMedia->findChild<QStackedWidget *>(QStringLiteral("mediaStack"))) {
        auto *rightLay = ui->mediaRightLayout;
        const int gridIdx = rightLay->indexOf(ui->mediaGrid);
        rightLay->removeWidget(ui->mediaGrid);

        auto *stack = new QStackedWidget(ui->tabProjectMedia);
        stack->setObjectName(QStringLiteral("mediaStack"));

        auto *emptyPage = new QWidget(stack);
        emptyPage->setObjectName(QStringLiteral("mediaEmptyPage"));
        auto *emptyLay = new QVBoxLayout(emptyPage);
        emptyLay->setContentsMargins(0, 0, 0, 0);
        emptyLay->addStretch(1);
        auto *cta = new QPushButton(tr("Import Media..."), emptyPage);
        cta->setObjectName(QStringLiteral("importMediaCta"));
        cta->setCursor(Qt::PointingHandCursor);
        cta->setFixedHeight(28);
        cta->setMinimumWidth(128);
        emptyLay->addWidget(cta, 0, Qt::AlignHCenter);
        emptyLay->addStretch(1);
        connect(cta, &QPushButton::clicked, this, [this]() { importMediaFiles(); });

        stack->addWidget(emptyPage);
        stack->addWidget(ui->mediaGrid);
        rightLay->insertWidget(gridIdx >= 0 ? gridIdx : 0, stack, 1);
    }

    // Footer: media meta line
    if (!ui->tabProjectMedia->findChild<QWidget *>(QStringLiteral("mediaBinFooter"))) {
        auto *rightLay = ui->mediaRightLayout;
        const int metaIdx = rightLay->indexOf(ui->mediaMeta);
        if (metaIdx >= 0) {
            rightLay->removeWidget(ui->mediaMeta);
        }
        auto *footer = new QWidget(ui->tabProjectMedia);
        footer->setObjectName(QStringLiteral("mediaBinFooter"));
        footer->setFixedHeight(24);
        auto *footerLay = new QHBoxLayout(footer);
        footerLay->setContentsMargins(4, 1, 4, 1);
        footerLay->setSpacing(6);
        ui->mediaMeta->setParent(footer);
        footerLay->addWidget(ui->mediaMeta, 1);
        rightLay->addWidget(footer);
    }

    connect(ui->mediaGrid, &::QListWidget::itemSelectionChanged, this, &MainWindow::updateMediaMeta);
    connect(ui->mediaGrid, &::QListWidget::itemDoubleClicked, this, [this](::QListWidgetItem *item) {
        if (!item) {
            return;
        }
        openMediaInTrimmer(item->data(Qt::UserRole + 2).toString(), item->text());
    });
    ui->mediaGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->mediaGrid, &QWidget::customContextMenuRequested, this,
            &MainWindow::showProjectMediaContextMenu);
    connect(&MediaThumbCache::instance(), &MediaThumbCache::thumbnailReady, this, [this](const QString &path) {
        if (!ui || !ui->mediaGrid) {
            return;
        }
        for (int i = 0; i < ui->mediaGrid->count(); ++i) {
            auto *item = ui->mediaGrid->item(i);
            if (!item) {
                continue;
            }
            if (QDir::cleanPath(item->data(Qt::UserRole + 2).toString())
                    .compare(QDir::cleanPath(path), Qt::CaseInsensitive)
                != 0) {
                continue;
            }
            const QString kind = item->data(Qt::UserRole).toString();
            item->setIcon(mediaThumbIcon(kind, i, path));
        }
    });

    const int genIdx = ui->mediaTabs->indexOf(ui->tabMediaGenerators);
    if (genIdx >= 0) {
        ui->mediaTabs->setTabText(genIdx, tr("Media Generator"));
    }

    // Vegas Pro dock order: Explorer, Project Media, Video FX, Media Generators, Transitions, Notes
    auto *bar = ui->mediaTabs->tabBar();
    const int explorer = ui->mediaTabs->indexOf(ui->tabExplorer);
    const int projectMedia = ui->mediaTabs->indexOf(ui->tabProjectMedia);
    if (explorer >= 0 && projectMedia >= 0 && explorer > projectMedia) {
        bar->moveTab(explorer, 0);
    }
    // Ensure Project Media is selected by default when no saved tab (restoreUiSettings may override)
    if (!QSettings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"))
             .contains(QStringLiteral("ui/mediaTab"))) {
        const int pm = ui->mediaTabs->indexOf(ui->tabProjectMedia);
        if (pm >= 0) {
            ui->mediaTabs->setCurrentIndex(pm);
        }
    }

    refreshMediaEmptyState();
}

void MainWindow::setupProjectNotes()
{
    if (ui->notesPlaceholder) {
        ui->notesLayout->removeWidget(ui->notesPlaceholder);
        ui->notesPlaceholder->hide();
    }
    ui->notesLayout->setContentsMargins(0, 0, 0, 0);
    ui->notesLayout->setSpacing(0);

    m_notes = new ProjectNotesPane(ui->tabProjectNotes);
    ui->notesLayout->addWidget(m_notes);
    m_notes->setPlayheadProvider([this]() { return m_project.playheadSec(); });

    connect(m_notes, &ProjectNotesPane::seekRequested, this, [this](double sec) {
        m_project.setPlayheadSec(sec);
        updateTimecodeLabels(sec);
        if (m_timeline) {
            m_timeline->update();
        }
        statusBar()->showMessage(tr("Moved playhead to note"), 2000);
    });
}

void MainWindow::setupTransitions()
{
    if (ui->transitionsPlaceholder) {
        ui->transitionsLayout->removeWidget(ui->transitionsPlaceholder);
        ui->transitionsPlaceholder->hide();
    }
    ui->transitionsLayout->setContentsMargins(0, 0, 0, 0);
    ui->transitionsLayout->setSpacing(0);

    m_transitions = new TransitionsPane(ui->tabTransitions);
    ui->transitionsLayout->addWidget(m_transitions);

    connect(m_transitions, &TransitionsPane::transitionActivated, this, [this](const QString &name) {
        statusBar()->showMessage(tr("Transition: %1").arg(name), 2500);
    });
    connect(m_transitions, &TransitionsPane::presetActivated, this,
            [this](const QString &trName, const QString &preset) {
                statusBar()->showMessage(tr("Preset «%1» — %2").arg(preset, trName), 2500);
            });
}

void MainWindow::setupMediaGenerator()
{
    if (ui->generatorsPlaceholder) {
        ui->generatorsLayout->removeWidget(ui->generatorsPlaceholder);
        ui->generatorsPlaceholder->hide();
    }
    ui->generatorsLayout->setContentsMargins(0, 0, 0, 0);
    ui->generatorsLayout->setSpacing(0);

    m_mediaGen = new MediaGeneratorPane(ui->tabMediaGenerators);
    ui->generatorsLayout->addWidget(m_mediaGen);

    connect(m_mediaGen, &MediaGeneratorPane::generatorActivated, this, [this](const QString &name) {
        if (name.compare(QStringLiteral("Titles & Text"), Qt::CaseInsensitive) == 0) {
            createTitlesTextEvent();
            return;
        }
        statusBar()->showMessage(tr("Media Generator: %1").arg(name), 2500);
    });
    connect(m_mediaGen, &MediaGeneratorPane::presetActivated, this,
            [this](const QString &gen, const QString &preset, const QString &animationKey) {
                if (gen.compare(QStringLiteral("Titles & Text"), Qt::CaseInsensitive) == 0) {
                    createTitlesTextEvent(animationKey, preset);
                    return;
                }
                statusBar()->showMessage(tr("Preset «%1» — %2").arg(preset, gen), 2500);
            });
}

void MainWindow::setupVideoFx()
{
    if (ui->videoFxPlaceholder) {
        ui->videoFxLayout->removeWidget(ui->videoFxPlaceholder);
        ui->videoFxPlaceholder->hide();
    }
    ui->videoFxLayout->setContentsMargins(0, 0, 0, 0);
    ui->videoFxLayout->setSpacing(0);

    m_videoFx = new VideoFxPane(&m_pluginScanner, ui->tabVideoFx);
    ui->videoFxLayout->addWidget(m_videoFx);

    connect(m_videoFx, &VideoFxPane::pluginActivated, this, [this](const QString &name) {
        statusBar()->showMessage(tr("Video FX: %1").arg(name), 2500);
    });
    connect(m_videoFx, &VideoFxPane::presetActivated, this,
            [this](const QString &plugin, const QString &preset) {
                statusBar()->showMessage(tr("Preset «%1» — %2").arg(preset, plugin), 2500);
            });
}

void MainWindow::setupExplorer()
{
    if (ui->explorerPlaceholder) {
        ui->explorerLayout->removeWidget(ui->explorerPlaceholder);
        ui->explorerPlaceholder->hide();
    }
    ui->explorerLayout->setContentsMargins(0, 0, 0, 0);
    ui->explorerLayout->setSpacing(0);

    m_explorer = new ExplorerPane(ui->tabExplorer);
    ui->explorerLayout->addWidget(m_explorer);

    connect(m_explorer, &ExplorerPane::importRequested, this, [this](const QStringList &paths) {
        int added = 0;
        for (const QString &path : MediaMime::expandToMediaFiles(paths)) {
            const QString kind = guessMediaKind(path);
            const QString name = QFileInfo(path).fileName();
            bool found = false;
            for (const MediaItem &m : m_project.mediaPool()) {
                if (QDir::cleanPath(m.path).compare(QDir::cleanPath(path), Qt::CaseInsensitive) == 0) {
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }
            MediaItem item;
            item.path = path;
            item.displayName = name;
            item.kind = kind;
            item.missing = !QFileInfo::exists(path);
            m_project.mediaPool().push_back(item);
            addMediaCard(name, kind, defaultMetaForKind(kind), path);
            ++added;
        }
        refreshMediaEmptyState();
        if (added > 0) {
            statusBar()->showMessage(tr("Imported %1 item(s) from Explorer").arg(added), 3000);
        }
    });
    connect(m_explorer, &ExplorerPane::addToTimelineRequested, this, &MainWindow::placeMediaOnTimeline);
    connect(m_explorer, &ExplorerPane::trimMediaRequested, this,
            [this](const QString &path) { openMediaInTrimmer(path); });
    connect(m_explorer, &ExplorerPane::previewMediaRequested, this, [this](const QString &path) {
        openMediaInTrimmer(path);
        statusBar()->showMessage(tr("Preview: %1").arg(QFileInfo(path).fileName()), 2500);
    });
}

void MainWindow::importMediaFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import Media"), QString(), tr("Media (*.mp4 *.mov *.mkv *.wav *.mp3 *.aif);;All (*.*)"));
    if (files.isEmpty()) {
        return;
    }
    beginDocumentEdit();
    for (const QString &f : files) {
        const QString kind = guessMediaKind(f);
        const QString name = QFileInfo(f).fileName();
        MediaItem item;
        item.path = f;
        item.displayName = name;
        item.kind = kind;
        item.missing = !QFileInfo::exists(f);
        m_project.mediaPool().push_back(item);
        addMediaCard(name, kind, defaultMetaForKind(kind), f);
    }
    commitDocumentEdit(tr("Import Media"));
    refreshMediaEmptyState();
}

QString MainWindow::guessMediaKind(const QString &pathOrName) const
{
    return MediaMime::guessKind(pathOrName);
}

QIcon MainWindow::mediaThumbIcon(const QString &kind, int variant, const QString &path) const
{
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        return MediaThumbCache::instance().iconFor(path, QSize(120, 68), kind);
    }

    QPixmap pm(120, 68);
    QPainter p(&pm);
    p.fillRect(pm.rect(), QColor(0x0a, 0x0a, 0x0a));

    if (kind == QLatin1String("audio")) {
        QLinearGradient g(0, 0, 0, 68);
        g.setColorAt(0, QColor(0x1a, 0x2a, 0x3a));
        g.setColorAt(1, QColor(0x0d, 0x15, 0x20));
        p.fillRect(pm.rect(), g);
        p.setPen(QPen(QColor(0x4a, 0x9b, 0xe8), 1.5));
        for (int x = 10; x < 110; x += 4) {
            const int h = 8 + ((x * 7 + variant * 13) % 28);
            p.drawLine(x, 34 - h / 2, x, 34 + h / 2);
        }
        p.setPen(QColor(0x88, 0xaa, 0xcc));
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("♪"));
    } else if (kind == QLatin1String("still")) {
        p.fillRect(pm.rect(), QColor(0x2a, 0x2a, 0x2a));
        p.setPen(QColor(0x66, 0x66, 0x66));
        p.drawRect(20, 12, 80, 44);
    } else {
        QLinearGradient g(0, 0, 120, 68);
        if (variant % 2 == 0) {
            g.setColorAt(0, QColor(0x2a, 0x4a, 0x2a));
            g.setColorAt(0.45, QColor(0x6a, 0x8a, 0x4a));
            g.setColorAt(1, QColor(0x1a, 0x30, 0x20));
        } else {
            g.setColorAt(0, QColor(0x3a, 0x2a, 0x4a));
            g.setColorAt(0.5, QColor(0x5a, 0x4a, 0x6a));
            g.setColorAt(1, QColor(0x20, 0x18, 0x28));
        }
        p.fillRect(pm.rect(), g);
        p.fillRect(0, 0, 120, 6, QColor(0x11, 0x11, 0x11));
        p.fillRect(0, 62, 120, 6, QColor(0x11, 0x11, 0x11));
        p.setPen(QColor(0x33, 0x33, 0x33));
        for (int x = 4; x < 120; x += 10) {
            p.fillRect(x, 1, 5, 4, QColor(0x55, 0x55, 0x55));
            p.fillRect(x, 63, 5, 4, QColor(0x55, 0x55, 0x55));
        }
        p.setBrush(QColor(0x00, 0x78, 0xd7));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(4, 8, 16, 12, 2, 2);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPointSize(7);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(4, 8, 16, 12), Qt::AlignCenter, QStringLiteral("AV"));
    }

    p.setPen(QColor(0x33, 0x33, 0x33));
    p.setBrush(Qt::NoBrush);
    p.drawRect(0, 0, 119, 67);
    p.end();
    return QIcon(pm);
}

void MainWindow::addMediaCard(const QString &name, const QString &kind, const QString &meta,
                              const QString &path, double lengthSec)
{
    const int variant = ui->mediaGrid->count();
    auto *item = new QListWidgetItem(mediaThumbIcon(kind, variant, path), name);
    item->setData(Qt::UserRole, kind);
    item->setData(Qt::UserRole + 1, meta.isEmpty() ? defaultMetaForKind(kind) : meta);
    item->setData(Qt::UserRole + 2, path);
    item->setData(Qt::UserRole + 3, lengthSec);
    item->setToolTip(path.isEmpty() ? name : path);
    item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    item->setSizeHint(QSize(132, 96));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    ui->mediaGrid->addItem(item);
}

void MainWindow::placeMediaOnTimeline(const QStringList &paths)
{
    if (!m_timeline) {
        return;
    }
    const QStringList media = MediaMime::expandToMediaFiles(paths);
    if (media.isEmpty()) {
        return;
    }
    const double t = m_project.playheadSec();
    beginDocumentEdit();
    for (const QString &path : media) {
        const QString name = QFileInfo(path).fileName();
        const QString kind = MediaMime::guessKind(path);
        const double len = MediaProbe::lengthForInsert(path, kind);
        bool found = false;
        for (const MediaItem &m : m_project.mediaPool()) {
            if (QDir::cleanPath(m.path).compare(QDir::cleanPath(path), Qt::CaseInsensitive) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            MediaItem item;
            item.path = path;
            item.displayName = name;
            item.kind = kind;
            item.missing = !QFileInfo::exists(path);
            m_project.mediaPool().push_back(item);
            addMediaCard(name, kind, defaultMetaForKind(kind), path, len);
        }
        m_project.addMediaAt(name, kind, t, len, -1, path);
    }
    commitDocumentEdit(tr("Add Media"));
    refreshMediaEmptyState();
    refreshTimeline();
    refreshPreviewFrame(m_project.playheadSec());
    statusBar()->showMessage(tr("Added %1 item(s) to timeline").arg(media.size()), 3000);
}

void MainWindow::openMediaInTrimmer(const QString &path, const QString &nameHint)
{
    if (!m_trimmer) {
        m_trimmer = new TrimmerWindow(this);
    }
    const QString name = nameHint.isEmpty() ? QFileInfo(path).fileName() : nameHint;
    const QString kindStr = MediaMime::guessKind(path.isEmpty() ? name : path);
    EventMediaKind kind = EventMediaKind::Video;
    if (kindStr == QLatin1String("audio")) {
        kind = EventMediaKind::Audio;
    } else if (kindStr == QLatin1String("still")) {
        kind = EventMediaKind::Still;
    }
    m_trimmer->setMedia(name, kind, defaultLengthForMediaKind(kindStr), path);
    m_trimmer->show();
    m_trimmer->raise();
    m_trimmer->activateWindow();
}

void MainWindow::showProjectMediaContextMenu(const QPoint &pos)
{
    if (!ui || !ui->mediaGrid) {
        return;
    }
    const QPoint global = ui->mediaGrid->viewport()->mapToGlobal(pos);
    auto *hit = ui->mediaGrid->itemAt(pos);
    if (hit && !hit->isSelected()) {
        ui->mediaGrid->setCurrentItem(hit);
    }
    const auto selected = ui->mediaGrid->selectedItems();
    QMenu menu(this);
    if (selected.isEmpty()) {
        menu.addAction(tr("Refresh"), this, [this]() {
            for (int i = 0; i < ui->mediaGrid->count(); ++i) {
                auto *item = ui->mediaGrid->item(i);
                if (!item) {
                    continue;
                }
                const QString path = item->data(Qt::UserRole + 2).toString();
                MediaThumbCache::instance().invalidate(path);
                item->setIcon(mediaThumbIcon(item->data(Qt::UserRole).toString(), i, path));
            }
        });
        menu.addAction(tr("Import Media..."), this, &MainWindow::onImportMedia);
        menu.exec(global);
        return;
    }

    QStringList paths;
    for (auto *item : selected) {
        const QString p = item->data(Qt::UserRole + 2).toString();
        if (!p.isEmpty()) {
            paths << p;
        }
    }
    const QString primaryPath = paths.value(0);
    const QString primaryName = selected.first()->text();

    menu.addAction(tr("Add to Timeline"), this, [this, paths]() { placeMediaOnTimeline(paths); });
    menu.addSeparator();
    auto *previewAct = menu.addAction(tr("Start Preview"), this, [this, primaryPath, primaryName]() {
        openMediaInTrimmer(primaryPath, primaryName);
    });
    previewAct->setShortcut(Qt::Key_Return);
    previewAct->setEnabled(selected.size() == 1);
    auto *trimAct = menu.addAction(tr("Trim"), this, [this, primaryPath, primaryName]() {
        openMediaInTrimmer(primaryPath, primaryName);
    });
    trimAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    trimAct->setEnabled(selected.size() == 1);
    menu.addSeparator();
    menu.addAction(tr("Refresh"), this, [this, selected]() {
        for (auto *item : selected) {
            const QString path = item->data(Qt::UserRole + 2).toString();
            MediaThumbCache::instance().invalidate(path);
            item->setIcon(mediaThumbIcon(item->data(Qt::UserRole).toString(), 0, path));
        }
    });
    auto *removeAct = menu.addAction(tr("Remove"), this, [this]() {
        const auto selected = ui->mediaGrid->selectedItems();
        if (selected.isEmpty()) {
            return;
        }
        beginDocumentEdit();
        for (auto *item : selected) {
            if (!item) {
                continue;
            }
            const QString path = item->data(Qt::UserRole + 2).toString();
            auto &pool = m_project.mediaPool();
            pool.erase(std::remove_if(pool.begin(), pool.end(),
                                      [&](const MediaItem &m) {
                                          return QDir::cleanPath(m.path).compare(QDir::cleanPath(path),
                                                                                 Qt::CaseInsensitive)
                                                 == 0;
                                      }),
                       pool.end());
            delete ui->mediaGrid->takeItem(ui->mediaGrid->row(item));
        }
        commitDocumentEdit(tr("Remove Media"));
        refreshMediaEmptyState();
    });
    removeAct->setShortcut(QKeySequence::Delete);
    menu.addSeparator();
    menu.addAction(tr("Explorer..."), this, [primaryPath]() {
        const QFileInfo fi(primaryPath);
        const QString target = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        if (!target.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
        }
    });
    auto *propsAct = menu.addAction(tr("Properties"), this, [this, primaryPath, primaryName]() {
        const QFileInfo fi(primaryPath);
        QMessageBox::information(
            this, tr("Properties"),
            tr("Name: %1\nPath: %2\nSize: %3 bytes")
                .arg(primaryName, primaryPath.isEmpty() ? tr("(none)") : primaryPath,
                     QString::number(fi.size())));
    });
    propsAct->setShortcut(QKeySequence(QStringLiteral("Alt+Enter")));
    propsAct->setEnabled(selected.size() == 1);
    menu.exec(global);
}

QString MainWindow::defaultMetaForKind(const QString &kind) const
{
    if (kind == QLatin1String("audio")) {
        return tr("Audio: 48,000 Hz; 24 Bit; Stereo; 0:05.036; Uncompressed");
    }
    if (kind == QLatin1String("still")) {
        return tr("Image · default length %1 s (Vegas)").arg(QString::number(kDefaultStillLengthSec, 'f', 0));
    }
    return tr("Video: 1920x1080x32 · 29.970 fps · Audio: 48,000 Hz; 16 bit; Stereo");
}

void MainWindow::updateMediaMeta()
{
    const auto selected = ui->mediaGrid->selectedItems();
    if (selected.isEmpty()) {
        ui->mediaMeta->clear();
        return;
    }
    ui->mediaMeta->setText(selected.first()->data(Qt::UserRole + 1).toString());
}

void MainWindow::refreshMediaEmptyState()
{
    if (auto *stack = ui->tabProjectMedia->findChild<QStackedWidget *>(QStringLiteral("mediaStack"))) {
        stack->setCurrentIndex(ui->mediaGrid->count() == 0 ? 0 : 1);
    }
    if (ui->mediaGrid->count() == 0) {
        ui->mediaMeta->clear();
    } else if (ui->mediaGrid->selectedItems().isEmpty()) {
        ui->mediaGrid->setCurrentRow(0);
    }
    updateMediaMeta();
}

void MainWindow::populateSampleProjectMedia()
{
    ui->mediaGrid->clear();
    const QString vp = SamplePaths::vegProjectDir();
    const QString samples = SamplePaths::samplesDir();
    auto resolveSample = [&](const QString &fileName) -> QString {
        for (const QString &dir : {vp, samples, QDir(samples).filePath(QStringLiteral("assets"))}) {
            if (dir.isEmpty()) {
                continue;
            }
            const QString cand = QDir(dir).filePath(fileName);
            if (QFileInfo::exists(cand)) {
                return cand;
            }
        }
        return {};
    };

    const QString wav = resolveSample(QStringLiteral("sample_for_project_audio.wav"));
    const QString mp4 = resolveSample(QStringLiteral("big-buck-bunny_video-60fps-4k.mp4"));
    const QString mp4Alt = resolveSample(QStringLiteral("sample_for_project_video.mp4"));

    addMediaCard(QStringLiteral("sample_for_project_audio.wav"), QStringLiteral("audio"),
                 tr("Audio: 48,000 Hz; 24 Bit; Stereo; 0:10.285; Uncompressed"), wav, 10.285);
    addMediaCard(mp4.isEmpty() ? QStringLiteral("sample_for_project_video.mp4")
                               : QFileInfo(mp4).fileName(),
                 QStringLiteral("video"),
                 tr("Video: 3840x2160x32 · 60.000 fps · Audio: 48,000 Hz; Stereo"),
                 mp4.isEmpty() ? mp4Alt : mp4, 8.0);
    refreshMediaEmptyState();
}

void MainWindow::setupTimeline()
{
    auto *host = new TimelineScrollHost(&m_project, ui->timelineHost);
    ui->timelineHostLayout->addWidget(host);
    m_timeline = host->timeline();

    connect(m_timeline, &TimelineView::eventDoubleClicked, this, &MainWindow::openEventProperties);
    connect(m_timeline, &TimelineView::eventContextMenuRequested, this, &MainWindow::showEventContextMenu);
    connect(m_timeline, &TimelineView::eventGeneratorRequested, this, [this](int eventId) {
        openTitlesTextEditor(m_project.findEvent(eventId));
    });
    connect(m_timeline, &TimelineView::eventFxRequested, this, [this](int eventId) {
        TrackEvent *ev = m_project.findEvent(eventId);
        if (!ev) {
            return;
        }
        if (isAudioFamily(ev->mediaKind)) {
            onAudioEventFx(eventId);
        } else {
            onVideoEventFx(eventId);
        }
    });
    connect(m_timeline, &TimelineView::eventFxMenuRequested, this,
            [this](int eventId, const QPoint &globalPos) {
                ContextMenuBuilder::showEventFxMenu(this, eventId, globalPos);
            });
    connect(m_timeline, &TimelineView::eventPanCropRequested, this, [this](int eventId) {
        onVideoEventFx(eventId);
    });
    connect(m_timeline, &TimelineView::eventMoreMenuRequested, this,
            [this](int eventId, const QPoint &globalPos) {
                ContextMenuBuilder::showEventMoreMenu(this, eventId, globalPos);
            });
    connect(m_timeline, &TimelineView::transitionPropertiesRequested, this,
            &MainWindow::openTransitionProperties);
    connect(m_timeline, &TimelineView::emptyAreaContextMenuRequested, this,
            &MainWindow::showTimelineEmptyContextMenu);
    connect(m_timeline, &TimelineView::trackHeaderContextMenuRequested, this,
            &MainWindow::showTrackHeaderContextMenu);
    connect(m_timeline, &TimelineView::trackMoreMenuRequested, this,
            [this](int trackIndex, const QPoint &globalPos) {
                ContextMenuBuilder::showTrackMoreMenu(this, trackIndex, globalPos);
            });
    connect(m_timeline, &TimelineView::trackFxRequested, this, &MainWindow::onTrackFx);
    connect(m_timeline, &TimelineView::trackEmptyContextMenuRequested, this,
            &MainWindow::showTrackEmptyContextMenu);
    connect(m_timeline, &TimelineView::rulerContextMenuRequested, this, &MainWindow::showRulerContextMenu);
    connect(m_timeline, &TimelineView::markerLaneContextMenuRequested, this,
            &MainWindow::showMarkerLaneContextMenu);
    connect(m_timeline, &TimelineView::markerContextMenuRequested, this,
            &MainWindow::showMarkerContextMenu);
    connect(m_timeline, &TimelineView::playheadChanged, this, &MainWindow::updateTimecodeLabels);
    connect(m_timeline, &TimelineView::documentEditBegan, this, &MainWindow::beginDocumentEdit);
    connect(m_timeline, &TimelineView::documentEditCommitted, this, &MainWindow::commitDocumentEdit);
    connect(m_timeline, &TimelineView::documentEditCommitted, this, [this](const QString &) {
        // Fades / gain / trims committed on the timeline — push into the live mix graph.
        if (m_audioEngine) {
            m_audioEngine->syncMixerLive();
        }
    });
    connect(m_timeline, &TimelineView::liveAudioParamsChanged, this, [this]() {
        if (m_audioEngine) {
            m_audioEngine->syncMixerLive();
        }
    });
    connect(m_timeline, &TimelineView::mediaDropRequested, this,
            [this](const QString &name, const QString &kindIn, double timeSec, int trackIndex,
                   double lengthSec, const QString &path, const QString &extra) {
                // Folders / multi-select from OS: path may already be expanded by MediaMime;
                // still re-expand when a directory slips through.
                QStringList placePaths;
                if (!path.isEmpty()) {
                    placePaths = MediaMime::expandToMediaFiles({path});
                    if (placePaths.isEmpty() && MediaMime::isMediaFile(path)) {
                        placePaths << path;
                    }
                }

                beginDocumentEdit();
                auto placeOne = [&](const QString &n, const QString &kIn, const QString &p,
                                    const QString &ex) {
                    const QString kind = kIn.isEmpty() ? MediaMime::guessKind(p.isEmpty() ? n : p) : kIn;
                    const double len = MediaProbe::lengthForInsert(p, kind, lengthSec);
                    if (!p.isEmpty()) {
                        bool found = false;
                        for (const MediaItem &m : m_project.mediaPool()) {
                            if (QDir::cleanPath(m.path).compare(QDir::cleanPath(p), Qt::CaseInsensitive)
                                == 0) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            MediaItem item;
                            item.path = p;
                            item.displayName = n;
                            item.kind = kind;
                            item.missing = !QFileInfo::exists(p);
                            m_project.mediaPool().push_back(item);
                            addMediaCard(n, kind, defaultMetaForKind(kind), p, len);
                            refreshMediaEmptyState();
                        }
                    }
                    m_project.addMediaAt(n, kind, timeSec, len, trackIndex, p, ex);
                };

                if (!placePaths.isEmpty()) {
                    // Stagger slightly when dropping a folderful of clips
                    double t = timeSec;
                    for (const QString &p : placePaths) {
                        const QString n = QFileInfo(p).fileName();
                        placeOne(n, MediaMime::guessKind(p), p, QString());
                        t += 0.0; // same start like Vegas multi-drop; keep aligned
                        Q_UNUSED(t);
                    }
                } else {
                    placeOne(name, kindIn, path, extra);
                }
                commitDocumentEdit(tr("Drop Media"));

                refreshTimeline();
                refreshPreviewFrame(m_project.playheadSec());
                if (kindIn.compare(QStringLiteral("titles"), Qt::CaseInsensitive) == 0) {
                    statusBar()->showMessage(
                        tr("Dropped «%1» — Titles & Text").arg(name.isEmpty() ? tr("Sample Text") : name),
                        3000);
                } else {
                    statusBar()->showMessage(
                        tr("Dropped media at %1").arg(m_project.formatRulerTime(timeSec)), 3000);
                }
            });
}

void MainWindow::wireTransportButtons()
{
    if (!m_timeline) {
        return;
    }
    auto wirePlay = [this](QToolButton *btn) {
        if (!btn) {
            return;
        }
        connect(btn, &QToolButton::clicked, this, [this]() { m_timeline->setPlaying(true); });
    };
    auto wirePause = [this](QToolButton *btn) {
        if (!btn) {
            return;
        }
        connect(btn, &QToolButton::clicked, this, [this]() { m_timeline->setPlaying(false); });
    };
    auto wireStop = [this](QToolButton *btn) {
        if (!btn) {
            return;
        }
        connect(btn, &QToolButton::clicked, this, [this]() {
            m_timeline->stopPlayback();
            m_timeline->seekPlayhead(0.0, true);
        });
    };
    wirePlay(m_tlPlayBtn);
    wirePlay(m_previewPlayBtn);
    wirePause(m_tlPauseBtn);
    wirePause(m_previewPauseBtn);
    wireStop(m_tlStopBtn);
    wireStop(m_previewStopBtn);
    if (m_tlPlayFromStartBtn) {
        connect(m_tlPlayFromStartBtn, &QToolButton::clicked, this, [this]() {
            m_timeline->seekPlayhead(0.0, false);
            m_timeline->setPlaying(true);
        });
    }
    if (m_tlGoStartBtn) {
        connect(m_tlGoStartBtn, &QToolButton::clicked, this,
                [this]() { m_timeline->seekPlayhead(0.0, true); });
    }
    if (m_tlGoEndBtn) {
        connect(m_tlGoEndBtn, &QToolButton::clicked, this, [this]() {
            m_timeline->seekPlayhead(m_project.timelineEndSec(), true);
        });
    }
    if (m_tlPrevFrameBtn) {
        connect(m_tlPrevFrameBtn, &QToolButton::clicked, this, [this]() { m_timeline->stepFrames(-1); });
    }
    if (m_tlNextFrameBtn) {
        connect(m_tlNextFrameBtn, &QToolButton::clicked, this, [this]() { m_timeline->stepFrames(1); });
    }

    connect(m_timeline, &TimelineView::playingChanged, this, &MainWindow::syncTransportUi);
    connect(m_timeline, &TimelineView::playingChanged, this, [this](bool playing) {
        if (!m_audioEngine) {
            return;
        }
        if (playing) {
            m_lastAvSyncFrame = -1;
            VideoFrameCache::setBucketFps(m_project.frameRate());
            m_audioEngine->syncGraphFromProject();
            m_audioEngine->play(m_project.playheadSec());
            // Prime video buffer ahead of the audio clock (MLT consumer pre-roll).
            const QSize vp = ui->previewViewport ? ui->previewViewport->size() : QSize(640, 360);
            VideoCompositor::prefetchAround(m_project, m_project.playheadSec(),
                                            QSize(std::max(160, vp.width()), std::max(90, vp.height())),
                                            0.15, 1.5);
        } else {
            m_audioEngine->stop();
            m_lastAvSyncFrame = -1;
        }
    });
    if (m_audioEngine) {
        // Engine may stop itself at timeline end — keep TimelineView transport in sync.
        connect(m_audioEngine.get(), &AudioEngine::playingChanged, this, [this](bool playing) {
            if (!playing && m_timeline && m_timeline->isPlaying()) {
                m_timeline->setPlaying(false);
            }
        });
        // Audio is the clock master (Kdenlive/MLT consumer model). Video presents on
        // project-frame ticks derived from audio position — not a separate wall timer.
        connect(m_audioEngine.get(), &AudioEngine::positionChanged, this, [this](double sec) {
            if (!m_timeline) {
                return;
            }
            const double end = m_project.timelineEndSec();
            const double clamped = std::min(sec, end);
            m_syncingPlayheadFromEngine = true;
            m_project.setPlayheadSec(clamped);
            m_timeline->update();
            updateTimecodeLabels(clamped);
            m_syncingPlayheadFromEngine = false;

            if (!m_timeline->isPlaying()) {
                refreshPreviewFrame(VideoCompositor::quantizeToFrame(clamped, m_project.frameRate()));
                return;
            }

            const double fps = std::clamp(m_project.frameRate(), 1.0, 120.0);
            const qint64 frame = qint64(std::floor(std::max(0.0, clamped) * fps + 1e-9));
            if (frame == m_lastAvSyncFrame) {
                return;
            }
            m_lastAvSyncFrame = frame;
            refreshPreviewFrame(VideoCompositor::quantizeToFrame(clamped, fps));
        });
    }
    connect(m_timeline, &TimelineView::playheadChanged, this, [this](double sec) {
        // User scrub / click-seek: always drive the audio clock (unless we are
        // mirroring AudioEngine → UI).
        if (m_audioEngine && !m_syncingPlayheadFromEngine) {
            m_audioEngine->seek(sec);
        }
        // During play, steady-state video is driven by AudioEngine::positionChanged.
        // Still refresh immediately so a click-seek updates the preview now.
        if (m_timeline && m_timeline->isPlaying()) {
            m_lastAvSyncFrame = -1;
            refreshPreviewFrame(sec);
            return;
        }
        m_lastAvSyncFrame = -1;
        refreshPreviewFrame(sec);
    });
    connect(&VideoFrameCache::instance(), &VideoFrameCache::frameReady, this,
            [this](const QString &) {
                if (m_timeline && m_timeline->isPlaying()) {
                    // Soft catch-up: redraw current audio-clock frame when a decode finishes.
                    refreshPreviewFrame(
                        VideoCompositor::quantizeToFrame(m_project.playheadSec(), m_project.frameRate()));
                } else {
                    refreshPreviewFrame(m_project.playheadSec());
                }
            });
    syncTransportUi(m_timeline->isPlaying());
    refreshPreviewFrame(m_project.playheadSec());
}

void MainWindow::syncTransportUi(bool playing)
{
    auto setEnabledPair = [&](QToolButton *play, QToolButton *pause) {
        if (play) {
            play->setEnabled(!playing);
        }
        if (pause) {
            pause->setEnabled(playing);
        }
    };
    setEnabledPair(m_tlPlayBtn, m_tlPauseBtn);
    setEnabledPair(m_previewPlayBtn, m_previewPauseBtn);
}

QRect MainWindow::previewContentRect(int viewportW, int viewportH) const
{
    const int w = std::max(1, viewportW);
    const int h = std::max(1, viewportH);
    const double ar = m_project.frameHeight() > 0
                          ? double(m_project.frameWidth()) / double(m_project.frameHeight())
                          : 16.0 / 9.0;
    int contentW = w;
    int contentH = h;
    const double viewAr = double(w) / double(h);
    if (viewAr > ar) {
        contentW = int(h * ar);
    } else {
        contentH = int(w / ar);
    }
    const int ox = (w - contentW) / 2;
    const int oy = (h - contentH) / 2;
    return QRect(ox, oy, contentW, contentH);
}

void MainWindow::refreshPreviewFrame(double sec)
{
    if (!ui->previewLabel) {
        return;
    }

    const bool playing = m_timeline && m_timeline->isPlaying();
    VideoFrameCache::setBucketFps(m_project.frameRate());

    const QSize vp = ui->previewViewport ? ui->previewViewport->size() : QSize(640, 360);
    const int w = std::max(160, vp.width());
    const int h = std::max(90, vp.height() - 4);
    const QSize out(w, h);

    // Forward-biased prefetch while playing so video stays near the audio clock.
    if (playing) {
        VideoCompositor::prefetchAround(m_project, sec, out, 0.15, 1.5);
    } else {
        VideoCompositor::prefetchAround(m_project, sec, out, 0.5, 0.5);
    }

    QImage composed = VideoCompositor::compose(m_project, sec, out, playing);
    if (composed.isNull()) {
        composed = m_lastPreviewFrame;
    } else {
        m_lastPreviewFrame = composed;
    }

    QPixmap px(w, h);
    // Match panel chrome (not pure black) for letterbox / pillarbox around the frame.
    px.fill(QColor(0x1e, 0x1e, 0x1e));
    QPainter p(&px);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!composed.isNull()) {
        p.drawImage(previewContentRect(w, h), composed);
    } else {
        p.setPen(QColor(90, 90, 100));
        QFont f = p.font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(px.rect(), Qt::AlignCenter, tr("No video at cursor"));
        p.setPen(QColor(70, 70, 80));
        f.setPointSize(9);
        p.setFont(f);
        p.drawText(QRect(0, h / 2 + 16, w, 20), Qt::AlignCenter, m_project.formatRulerTime(sec));
    }
    p.end();

    ui->previewLabel->setPixmap(px);
    ui->previewLabel->setScaledContents(false);
    ui->previewLabel->setAlignment(Qt::AlignCenter);
}

void MainWindow::updateTimecodeLabels(double sec)
{
    const QString tc = m_project.formatRulerTime(sec);
    if (m_tlTimecode) {
        m_tlTimecode->setText(tc);
    }
    if (m_mainTimecode) {
        m_mainTimecode->setText(tc);
    }
    updatePreviewDisplayMeta(sec);
}

void MainWindow::setOverlayGrid(bool on)
{
    if (m_overlayGrid == on) {
        return;
    }
    m_overlayGrid = on;
    if (m_overlayGridAct && m_overlayGridAct->isChecked() != on) {
        QSignalBlocker block(m_overlayGridAct);
        m_overlayGridAct->setChecked(on);
    }
    syncPreviewOverlays();
    updateOverlaysButton();
}

void MainWindow::setOverlaySafeAreas(bool on)
{
    if (m_overlaySafeAreas == on) {
        return;
    }
    m_overlaySafeAreas = on;
    if (m_overlaySafeAct && m_overlaySafeAct->isChecked() != on) {
        QSignalBlocker block(m_overlaySafeAct);
        m_overlaySafeAct->setChecked(on);
    }
    syncPreviewOverlays();
    updateOverlaysButton();
}

void MainWindow::syncPreviewOverlays()
{
    if (!m_previewOverlay) {
        return;
    }
    auto *layer = static_cast<PreviewOverlayLayer *>(m_previewOverlay);
    layer->setGeometry(ui->previewViewport->rect());
    layer->raise();
    layer->setOverlays(m_overlayGrid, m_overlaySafeAreas);
}

void MainWindow::updateOverlaysButton()
{
    if (!m_overlaysBtn) {
        return;
    }
    const bool any = m_overlayGrid || m_overlaySafeAreas;
    QSignalBlocker block(m_overlaysBtn);
    m_overlaysBtn->setChecked(any);
}

void MainWindow::syncTitlesTextOverlayGeometry()
{
    if (!m_titlesTextOverlay || !ui->previewViewport) {
        return;
    }
    const QSize vp = ui->previewViewport->size();
    m_titlesTextOverlay->setGeometry(previewContentRect(vp.width(), vp.height()));
    m_titlesTextOverlay->raise();
}

QString MainWindow::formatPreviewFps() const
{
    return QString::number(m_project.frameRate(), 'f', 3).replace(QLatin1Char('.'), QLatin1Char(','));
}

int MainWindow::previewResolutionDivisor() const
{
    return std::max(1, m_previewResDivisor);
}

void MainWindow::setPreviewQuality(const QString &level, const QString &resolution)
{
    Q_UNUSED(level);
    if (resolution == tr("Half")) {
        m_previewResDivisor = 2;
    } else if (resolution == tr("Quarter")) {
        m_previewResDivisor = 4;
    } else {
        // Auto / Full — Vegas status shows full project frame size
        m_previewResDivisor = 1;
    }
    refreshPreviewProjectMeta();
    refreshPreviewFrame(m_project.playheadSec());
}

void MainWindow::updatePreviewDisplayMeta(double sec)
{
    if (!m_previewRightMeta) {
        return;
    }
    const int frame = static_cast<int>(std::llround(sec * m_project.frameRate()));
    const int dw = ui->previewViewport ? ui->previewViewport->width() : 0;
    const int dh = ui->previewViewport ? ui->previewViewport->height() : 0;
    // Frame with thin space grouping like Vegas (e.g. 3 260)
    QString frameStr = QString::number(frame);
    if (frameStr.size() > 3) {
        frameStr.insert(frameStr.size() - 3, QChar(0x2009));
    }
    m_previewRightMeta->setText(tr("Frame: <b>%1</b><br>Display: %2x%3x32; %4p")
                                    .arg(frameStr)
                                    .arg(dw)
                                    .arg(dh)
                                    .arg(formatPreviewFps()));
}

void MainWindow::refreshPreviewProjectMeta()
{
    if (!m_previewLeftMeta) {
        return;
    }
    const int pw = m_project.frameWidth() > 0 ? m_project.frameWidth() : 1920;
    const int ph = m_project.frameHeight() > 0 ? m_project.frameHeight() : 1080;
    const int div = previewResolutionDivisor();
    const int prevW = std::max(1, pw / div);
    const int prevH = std::max(1, ph / div);
    const QString fps = formatPreviewFps();
    // Vegas: Preview matches Project for Auto/Full; reduced sizes shown in red
    const QString previewDims = QStringLiteral("%1x%2x32").arg(prevW).arg(prevH);
    const QString previewPart =
        (div > 1) ? QStringLiteral("<span style=\"color:#c43c3c\">%1</span>").arg(previewDims)
                  : previewDims;
    m_previewLeftMeta->setText(tr("Project: %1x%2x32; %3p<br>Preview: %4; %3p")
                                   .arg(pw)
                                   .arg(ph)
                                   .arg(fps)
                                   .arg(previewPart));
}

void MainWindow::onNewProject()
{
    clearUndoHistory();
    m_currentArchivePath.clear();
    m_project.loadEmptyProject();
    ui->mediaGrid->clear();
    refreshMediaEmptyState();
    if (m_timeline) {
        m_timeline->update();
    }
    if (m_mixingConsole) {
        m_mixingConsole->refreshFromProject();
    }
    setWindowTitle(tr("Untitled - OpenVegas"));
    refreshStatusBar();
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::onLoadDemoTimeline()
{
    clearUndoHistory();
    m_project.loadDemoProject();
    populateSampleProjectMedia();
    if (m_timeline) {
        m_timeline->update();
    }
    if (m_mixingConsole) {
        m_mixingConsole->refreshFromProject();
    }
    setWindowTitle(tr("Untitled * - OpenVegas"));
    refreshStatusBar();
    statusBar()->showMessage(tr("Demo timeline loaded"));
}

void MainWindow::onMixingConsole()
{
    if (!m_mixingConsole) {
        m_mixingConsole = new MixingConsoleWindow(this);
        m_mixingConsole->setProject(&m_project);
        m_mixingConsole->setPluginScanner(&m_pluginScanner);
        connect(m_mixingConsole, &MixingConsoleWindow::tracksChanged, this, [this]() {
            if (m_audioEngine) {
                m_audioEngine->syncMixerLive();
            }
            if (m_timeline) {
                m_timeline->update();
            }
            // The console's master strip edits the same gain as the fader on the main
            // window, so one has to follow the other or they disagree on screen.
            syncMasterFaderFromProject();
            refreshStatusBar();
        });
        connect(m_mixingConsole, &MixingConsoleWindow::documentEditBegan, this,
                &MainWindow::beginDocumentEdit);
        connect(m_mixingConsole, &MixingConsoleWindow::documentEditCommitted, this,
                [this](const QString &text) {
                    commitDocumentEdit(text);
                    // FX chain edits need full AudioGraph rebuild (plugin instances).
                    if (m_audioEngine
                        && (text == tr("Track FX") || text == tr("Assignable FX"))) {
                        m_audioEngine->syncGraphFromProject();
                    }
                });
        connect(m_mixingConsole, &MixingConsoleWindow::downmixOutputCycled, this,
                &MainWindow::cycleDownmixOutput);
        connect(m_mixingConsole, &MixingConsoleWindow::dimOutputChanged, this,
                &MainWindow::setDimOutput);
    }
    m_mixingConsole->refreshFromProject();
    syncDownmixUi();
    m_mixingConsole->show();
    m_mixingConsole->raise();
    m_mixingConsole->activateWindow();
}

void MainWindow::onOpenProject()
{
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QString startDir = settings.value(QStringLiteral("paths/lastProjectDir")).toString();
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        startDir = SamplePaths::vegProjectDir();
        if (startDir.isEmpty() || !QDir(startDir).exists()) {
            startDir = SamplePaths::samplesDir();
        }
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), startDir,
        tr("OpenVegas Project (*.ovp *.ozp);;Vegas Project (*.veg);;"
           "OpenVegas Project Archive (project.json);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("paths/lastProjectDir"), QFileInfo(path).absolutePath());
    openProjectPath(path);
}

void MainWindow::openProjectPath(const QString &path)
{
    // OpenVegas's own project archive is a folder (project.json + media_list.txt — see
    // MARKDOWN/PROJECT_ARCHIVE_FORMAT.md); "Open" lets you pick either the folder itself
    // or the project.json manifest inside it.
    const QFileInfo fi(path);

    // Single-file projects first: they are files, and the folder checks below would take a
    // file path for a folder that simply holds no project.json.
    const QString suffix = fi.suffix().toLower();
    if (suffix == ProjectFile::singleFileSuffix() || suffix == ProjectFile::zipSuffix()
        || (fi.isFile() && ProjectFile::looksLikeZip(path))) {
        QString fileError;
        const bool zipped =
            suffix == ProjectFile::zipSuffix() || ProjectFile::looksLikeZip(path);
        const bool ok = zipped ? ProjectFile::loadOzp(path, &m_project, &fileError)
                               : ProjectFile::loadOvp(path, &m_project, &fileError);
        if (!ok) {
            QMessageBox::warning(this, tr("Open Project"), fileError);
            return;
        }
        m_currentProjectFile = path;
        m_currentArchivePath.clear();
        clearUndoHistory();
        applyProjectToUi();
        rememberRecentFile(path);
        setWindowTitle(tr("%1 - OpenVegas").arg(m_project.projectTitle()));
        statusBar()->showMessage(tr("Opened: %1").arg(path), 5000);
        return;
    }

    const QString archiveDir =
        fi.fileName().compare(QStringLiteral("project.json"), Qt::CaseInsensitive) == 0
            ? fi.absolutePath()
            : path;
    if (ProjectInterchange::isProjectArchive(archiveDir)) {
        QString archiveError;
        if (!ProjectInterchange::importProjectArchive(archiveDir, &m_project, &archiveError)) {
            QMessageBox::warning(this, tr("Open Project"), archiveError);
            return;
        }
        m_currentArchivePath = archiveDir;
        clearUndoHistory();
        applyProjectToUi();
        rememberRecentFile(archiveDir);
        if (m_project.hasMixerExtras()) {
            onMixingConsole();
        }
        setWindowTitle(tr("%1 - OpenVegas").arg(m_project.projectTitle()));
        statusBar()->showMessage(tr("Opened project archive: %1").arg(archiveDir), 8000);
        return;
    }
    m_currentArchivePath.clear(); // a .veg is a different, read-only source — see onSaveProject()

    const QString resolved = SamplePaths::resolveProjectPath(path);
    QString error;
    const VegOpenResult veg = VegReader::open(resolved, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Open Project"), error);
        return;
    }

    const bool usedEdl = m_project.applyVegImport(veg, resolved);
    clearUndoHistory();
    applyProjectToUi();
    rememberRecentFile(resolved);

    if (m_project.hasMixerExtras()) {
        onMixingConsole();
    }

    // Zoom timeline so imported content roughly fills the view
    if (m_timeline) {
        double maxEnd = 8.0;
        for (const Track &t : m_project.tracks()) {
            for (const TrackEvent &ev : t.events) {
                maxEnd = std::max(maxEnd, ev.startSec + ev.lengthSec);
            }
        }
        const int viewW = std::max(400, m_timeline->width() - m_timeline->headerWidth());
        const double targetPps = std::clamp((viewW * 0.85) / maxEnd, 0.5, 120.0);
        m_project.setPixelsPerSecond(targetPps);
        m_timeline->setScrollX(0);
        m_timeline->refreshLayout();
    }

    int eventCount = 0;
    for (const Track &t : m_project.tracks()) {
        eventCount += t.events.size();
    }

    QStringList notes;
    notes << tr("VEGAS Pro %1 · %2 Hz · %3 fps · %4×%5")
                 .arg(veg.header.vegasVersion)
                 .arg(veg.header.sampleRate)
                 .arg(veg.header.frameRate, 0, 'f', 3)
                 .arg(m_project.frameWidth())
                 .arg(m_project.frameHeight());
    notes << tr("Media: %1").arg(m_project.mediaPool().size());
    if (usedEdl) {
        notes << tr("Timeline: Vegas EDL sidecar (%1 events)").arg(eventCount);
    } else if (veg.hasTimelineTimings) {
        notes << tr("Timeline: %1 event block(s) from .veg").arg(veg.events.size());
    } else {
        notes << tr("Timeline: heuristic (%1 events)").arg(eventCount);
    }
    if (!veg.eventLabels.isEmpty()) {
        notes << tr("Labels: %1").arg(veg.eventLabels.join(QStringLiteral(", ")));
    }

    QStringList missingPaths;
    for (const MediaItem &m : m_project.mediaPool()) {
        if (m.missing) {
            missingPaths << m.path;
        }
    }
    if (!missingPaths.isEmpty()) {
        notes << tr("%1 media missing").arg(missingPaths.size());
    }
    for (const QString &w : veg.warnings) {
        notes << w;
    }
    statusBar()->showMessage(notes.join(QStringLiteral("  |  ")), 16000);

    if (!missingPaths.isEmpty()) {
        resolveMissingMedia();
    }
}

void MainWindow::resolveMissingMedia()
{
    bool ignoreAll = false;
    // Re-read each round so relinks update the list.
    while (true) {
        const QStringList missing = m_project.missingMediaPaths();
        if (missing.isEmpty() || ignoreAll) {
            break;
        }
        const QString path = missing.first();

        MissingFileDialog choice(path, this);
        if (choice.exec() != QDialog::Accepted) {
            break;
        }

        switch (choice.action()) {
        case MissingFileAction::IgnoreAll:
            ignoreAll = true;
            break;
        case MissingFileAction::Ignore: {
            for (MediaItem &m : m_project.mediaPool()) {
                if (QDir::cleanPath(m.path).compare(QDir::cleanPath(path), Qt::CaseInsensitive)
                    == 0) {
                    m.missing = false; // offline, no further prompts
                    break;
                }
            }
            break;
        }
        case MissingFileAction::Search: {
            SearchMissingFilesDialog search(path, this);
            if (search.exec() == QDialog::Accepted && !search.selectedPath().isEmpty()) {
                m_project.relinkMedia(path, search.selectedPath());
                VideoFrameCache::instance().invalidate();
                MediaThumbCache::instance().invalidate(search.selectedPath());
            }
            // Cancel → re-show the choice dialog for the same file.
            break;
        }
        case MissingFileAction::Specify: {
            FindMissingFileDialog find(path, this);
            if (find.exec() == QDialog::Accepted && !find.selectedPath().isEmpty()) {
                m_project.relinkMedia(path, find.selectedPath());
                VideoFrameCache::instance().invalidate();
                MediaThumbCache::instance().invalidate(find.selectedPath());
            }
            break;
        }
        case MissingFileAction::Cancel:
        default:
            return;
        }
    }
    refreshMediaPoolUi();
    refreshTimeline();
    refreshPreviewFrame(m_project.playheadSec());
}

void MainWindow::refreshMediaPoolUi()
{
    if (!ui || !ui->mediaGrid) {
        return;
    }
    ui->mediaGrid->clear();
    int i = 0;
    for (const MediaItem &m : m_project.mediaPool()) {
        addMediaCard(m.displayName.isEmpty() ? QFileInfo(m.path).fileName() : m.displayName, m.kind,
                     m.missing || !QFileInfo::exists(m.path) ? tr("Offline") : defaultMetaForKind(m.kind),
                     m.path);
        ++i;
        Q_UNUSED(i);
    }
    refreshMediaEmptyState();
    updateMediaMeta();
}

void MainWindow::rememberRecentFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    const QString abs = QFileInfo(path).absoluteFilePath();
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QStringList recent = settings.value(QStringLiteral("recent/files")).toStringList();
    recent.removeAll(abs);
    recent.prepend(abs);
    while (recent.size() > 9) {
        recent.removeLast();
    }
    settings.setValue(QStringLiteral("recent/files"), recent);
}

void MainWindow::onImportMedia()
{
    importMediaFiles();
}

void MainWindow::applyInterchangeImport(const InterchangeResult &result, const QString &statusLabel,
                                        bool addEventsToTimeline)
{
    beginDocumentEdit();
    int addedMedia = 0;
    for (const InterchangeMediaRef &ref : result.media) {
        MediaItem item;
        item.path = ref.path;
        item.displayName = ref.displayName.isEmpty() ? QFileInfo(ref.path).fileName() : ref.displayName;
        item.kind = ref.kind.isEmpty() ? guessMediaKind(item.path) : ref.kind;
        item.missing = !QFileInfo::exists(item.path);
        bool exists = false;
        for (const MediaItem &m : m_project.mediaPool()) {
            if (QDir::cleanPath(m.path).compare(QDir::cleanPath(item.path), Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_project.mediaPool().push_back(item);
            ++addedMedia;
        }
        addMediaCard(item.displayName, item.kind,
                     item.missing ? tr("Missing: %1").arg(item.path)
                                  : tr("%1 · %2").arg(item.kind, item.path),
                     item.path);
    }

    int addedEvents = 0;
    int addedMarkers = 0;
    if (addEventsToTimeline) {
        InterchangeResult timeline = result;
        timeline.events.clear();
        for (const InterchangeEvent &ev : result.events) {
            if (ev.kind == QLatin1String("caption")) {
                m_project.addMarkerAt(ev.startSec, ev.name);
                ++addedMarkers;
            } else {
                timeline.events.push_back(ev);
            }
        }
        // Prefer resolving relative/copied media against the first known media path's folder.
        QString resolveAgainst = m_project.projectPath();
        if (resolveAgainst.isEmpty() && !result.media.isEmpty()) {
            resolveAgainst = result.media.first().path;
        }
        addedEvents = m_project.applyInterchangeEvents(timeline, resolveAgainst);
    }
    commitDocumentEdit(tr("Import"));

    refreshMediaEmptyState();
    if (m_timeline && (addedEvents > 0 || addedMarkers > 0)) {
        m_timeline->refreshLayout();
    }

    QStringList notes;
    notes << statusLabel;
    if (addedMedia > 0) {
        notes << tr("%1 media").arg(addedMedia);
    }
    if (addedEvents > 0) {
        notes << tr("%1 timeline events").arg(addedEvents);
    }
    if (addedMarkers > 0) {
        notes << tr("%1 markers").arg(addedMarkers);
    }
    for (const QString &w : result.warnings) {
        notes << w;
    }
    statusBar()->showMessage(notes.join(QStringLiteral("  |  ")), 10000);
}

void MainWindow::onImportMediaFromProject()
{
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QString startDir = settings.value(QStringLiteral("paths/lastProjectDir")).toString();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Media from Project"), startDir,
        tr("Vegas Project (*.veg);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const InterchangeResult result = ProjectInterchange::importMediaFromProject(path, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import Media from Project"), error);
        return;
    }
    applyInterchangeImport(result, tr("Imported media from %1").arg(QFileInfo(path).fileName()),
                           false);
}

void MainWindow::onImportPremiere()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Premiere/After Effects Project"), QString(),
        tr("Premiere Project (*.prproj);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const InterchangeResult result = ProjectInterchange::importPremiereProject(path, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import Premiere"), error);
        return;
    }
    if (result.media.isEmpty()) {
        QMessageBox::information(
            this, tr("Import Premiere"),
            tr("No media paths could be extracted from \"%1\".\n"
               "OpenVegas supports best-effort path scraping only.")
                .arg(QFileInfo(path).fileName()));
        return;
    }
    applyInterchangeImport(result, tr("Premiere media from %1").arg(QFileInfo(path).fileName()),
                           false);
}

void MainWindow::onImportFinalCutXml()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Final Cut Pro 7 / DaVinci Resolve XML"), QString(),
        tr("Final Cut XML (*.xml);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const InterchangeResult result = ProjectInterchange::importFinalCutXml(path, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import XML"), error);
        return;
    }
    applyInterchangeImport(result, tr("Imported FCP/Resolve XML"), true);
}

void MainWindow::onImportFcpxml()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Final Cut Pro X"), QString(),
        tr("Final Cut Pro X (*.fcpxml);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const InterchangeResult result = ProjectInterchange::importFinalCutXml(path, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import FCPXML"), error);
        return;
    }
    applyInterchangeImport(result, tr("Imported FCPXML"), true);
}

void MainWindow::onImportEdl()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import EDL Text File"), QString(),
                                                      tr("EDL (*.edl *.txt);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const InterchangeResult result =
        ProjectInterchange::importEdl(path, m_project.frameRate(), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import EDL"), error);
        return;
    }
    applyInterchangeImport(result, tr("Imported EDL"), true);
}

void MainWindow::onImportBroadcastWave()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import Broadcast Wave Format"), QString(),
        tr("Broadcast Wave (*.wav *.bwf);;Wave (*.wav);;All files (*.*)"));
    if (files.isEmpty()) {
        return;
    }
    const InterchangeResult result = ProjectInterchange::importBroadcastWave(files);
    applyInterchangeImport(result, tr("Imported Broadcast Wave"), false);
}

void MainWindow::onImportClosedCaptions()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Closed Captioning"), QString(),
        tr("Captions (*.srt *.vtt *.scc);;SubRip (*.srt);;WebVTT (*.vtt);;SCC (*.scc);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const InterchangeResult result = ProjectInterchange::importClosedCaptions(path, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import Captions"), error);
        return;
    }
    applyInterchangeImport(result, tr("Imported captions as markers"), true);
}

void MainWindow::onExportProjectArchive()
{
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QString startDir = settings.value(QStringLiteral("paths/lastExportDir")).toString();
    if (startDir.isEmpty()) {
        startDir = QFileInfo(m_project.projectPath()).absolutePath();
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Export VEGAS Project Archive — choose destination folder"), startDir);
    if (dir.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("paths/lastExportDir"), dir);

    const QString archiveName = m_project.projectTitle().isEmpty()
                                    ? QStringLiteral("OpenVegas_Archive")
                                    : (m_project.projectTitle() + QStringLiteral("_Archive"));
    const QString archiveDir = QDir(dir).filePath(archiveName);

    const auto reply = QMessageBox::question(
        this, tr("Project Archive"),
        tr("Create archive in:\n%1\n\nCopy media files into the archive Media folder?")
            .arg(QDir::toNativeSeparators(archiveDir)),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
    if (reply == QMessageBox::Cancel) {
        return;
    }
    const bool copyMedia = (reply == QMessageBox::Yes);

    QString error;
    captureFxStateForSave();
    if (!ProjectInterchange::exportProjectArchive(m_project, archiveDir, copyMedia, &error)) {
        QMessageBox::warning(this, tr("Export Archive"), error);
        return;
    }
    statusBar()->showMessage(tr("Project archive written to %1").arg(archiveDir), 8000);
    QMessageBox::information(this, tr("Export Archive"),
                             tr("Archive created:\n%1\n\nContains project.json, media_list.txt%2.")
                                 .arg(QDir::toNativeSeparators(archiveDir),
                                      copyMedia ? tr(", and Media/") : QString()));
}

void MainWindow::captureFxStateForSave()
{
    auto &host = CompositePluginHost::instance();
    for (Track &t : m_project.tracks()) {
        host.captureChainState(&t.fxChain);
        for (TrackEvent &ev : t.events) {
            host.captureChainState(&ev.fxChain);
        }
    }
}

void MainWindow::onSaveProject()
{
    captureFxStateForSave();
    QString error;
    // A project opened or saved as a single file goes back to that file; a folder archive
    // still saves as a folder, so an existing habit is not taken away.
    if (!m_currentProjectFile.isEmpty()) {
        if (!saveProjectToPath(m_currentProjectFile, &error)) {
            QMessageBox::warning(this, tr("Save Project"), error);
            return;
        }
        statusBar()->showMessage(tr("Saved: %1").arg(m_currentProjectFile), 5000);
        return;
    }
    if (m_currentArchivePath.isEmpty()) {
        onSaveProjectAs();
        return;
    }
    if (!ProjectInterchange::exportProjectArchive(m_project, m_currentArchivePath,
                                                  /*copyMedia=*/false, &error)) {
        QMessageBox::warning(this, tr("Save Project"), error);
        return;
    }
    statusBar()->showMessage(tr("Saved: %1").arg(m_currentArchivePath), 5000);
}

void MainWindow::onSaveProjectAs()
{
    // Saving used to mean picking a destination folder and then typing a name, because the
    // only native format was a folder. A project you cannot double-click, mail, or keep in
    // one place beside the footage is awkward for everyday work, so this is a plain file
    // dialog now; the folder form stays available under Export.
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QString startDir = settings.value(QStringLiteral("paths/lastSaveDir")).toString();
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        startDir = QFileInfo(m_project.projectPath()).absolutePath();
    }
    const QString defaultName =
        m_project.projectTitle().isEmpty() ? QStringLiteral("Untitled") : m_project.projectTitle();
    const QString suggested = QDir(startDir).filePath(defaultName + QStringLiteral(".ovp"));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project"), suggested,
        tr("OpenVegas Project (*.ovp);;OpenVegas Zip Archive (*.ozp)"));
    if (path.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("paths/lastSaveDir"), QFileInfo(path).absolutePath());

    captureFxStateForSave();
    QString error;
    if (!saveProjectToPath(path, &error)) {
        QMessageBox::warning(this, tr("Save Project"), error);
        return;
    }
    m_currentProjectFile = path;
    m_currentArchivePath.clear();
    setWindowTitle(tr("%1 - OpenVegas").arg(m_project.projectTitle()));
    statusBar()->showMessage(tr("Saved: %1").arg(path), 5000);
}

bool MainWindow::saveProjectToPath(const QString &path, QString *error)
{
    // The suffix picks the format. A zip carries the media inside it, which is what makes
    // it the one to hand to someone else; the plain file only points at where the media
    // lives, so it stays small and stays valid only while that media does.
    if (QFileInfo(path).suffix().compare(ProjectFile::zipSuffix(), Qt::CaseInsensitive) == 0) {
        return ProjectFile::saveOzp(m_project, path, /*includeMedia=*/true, error);
    }
    return ProjectFile::saveOvp(m_project, path, error);
}

void MainWindow::onExportPremiere()
{
    QMessageBox::information(
        this, tr("Export Premiere/After Effects"),
        tr("Native .prproj export is not available yet.\n\n"
           "Use Export → Final Cut Pro 7/DaVinci Resolve (*.xml) or EDL, "
           "which Premiere and After Effects can import."));
}

void MainWindow::onExportFinalCutXml()
{
    QString startName = m_project.projectTitle().isEmpty() ? QStringLiteral("timeline")
                                                          : m_project.projectTitle();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Final Cut Pro 7 / DaVinci Resolve XML"), startName + QStringLiteral(".xml"),
        tr("Final Cut XML (*.xml)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!ProjectInterchange::exportFinalCutXml(m_project, path, &error)) {
        QMessageBox::warning(this, tr("Export XML"), error);
        return;
    }
    statusBar()->showMessage(tr("Exported FCP/Resolve XML: %1").arg(path), 8000);
}

void MainWindow::onExportFcpxml()
{
    QString startName = m_project.projectTitle().isEmpty() ? QStringLiteral("timeline")
                                                          : m_project.projectTitle();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Final Cut Pro X"), startName + QStringLiteral(".fcpxml"),
        tr("Final Cut Pro X (*.fcpxml)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!ProjectInterchange::exportFcpxml(m_project, path, &error)) {
        QMessageBox::warning(this, tr("Export FCPXML"), error);
        return;
    }
    statusBar()->showMessage(tr("Exported FCPXML: %1").arg(path), 8000);
}

void MainWindow::onExportEdl()
{
    QString startName = m_project.projectTitle().isEmpty() ? QStringLiteral("timeline")
                                                          : m_project.projectTitle();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export EDL Text File"), startName + QStringLiteral(".txt"),
        tr("Vegas EDL Text (*.txt);;CMX3600 EDL (*.edl);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    const bool vegasCsv = path.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive)
                          || !path.endsWith(QLatin1String(".edl"), Qt::CaseInsensitive);
    const bool ok = vegasCsv ? ProjectInterchange::exportVegasCsvEdl(m_project, path, &error)
                             : ProjectInterchange::exportEdl(m_project, path, &error);
    if (!ok) {
        QMessageBox::warning(this, tr("Export EDL"), error);
        return;
    }
    statusBar()->showMessage(tr("Exported EDL: %1").arg(path), 8000);
}

void MainWindow::applyProjectToUi()
{
    ui->mediaGrid->clear();
    for (const MediaItem &m : m_project.mediaPool()) {
        addMediaCard(m.displayName, m.kind,
                     m.missing ? tr("Missing: %1").arg(m.path)
                               : tr("%1 · %2").arg(m.kind, m.path),
                     m.path);
    }
    refreshMediaEmptyState();

    refreshPreviewProjectMeta();
    updatePreviewDisplayMeta(m_project.playheadSec());
    refreshPreviewFrame(m_project.playheadSec());

    if (m_timeline) {
        m_timeline->refreshLayout();
    }
    setWindowTitle(tr("%1 - OpenVegas").arg(QFileInfo(m_project.projectPath()).fileName()));
    refreshStatusBar();
}

void MainWindow::onWelcome()
{
    WelcomeDialog dlg(this);
    connect(&dlg, &WelcomeDialog::newProjectRequested, this, &MainWindow::onNewProject);
    connect(&dlg, &WelcomeDialog::openProjectRequested, this, &MainWindow::onOpenProject);
    connect(&dlg, &WelcomeDialog::openProjectPathRequested, this, &MainWindow::openProjectPath);
    connect(&dlg, &WelcomeDialog::advancedSettingsRequested, this, &MainWindow::onProjectProperties);
    dlg.exec();
}

void MainWindow::onProjectProperties()
{
    beginDocumentEdit();
    ProjectPropertiesDialog dlg(&m_project, this);
    if (dlg.exec() == QDialog::Accepted) {
        commitDocumentEdit(tr("Project Properties"));
        refreshPreviewProjectMeta();
        refreshStatusBar();
        if (m_timeline) {
            m_timeline->update();
        }
    } else if (m_editCaptureOpen) {
        // Revert Apply clicks if the dialog was cancelled.
        m_editBefore.apply(m_project);
        discardDocumentEdit();
        refreshPreviewProjectMeta();
        refreshStatusBar();
        if (m_timeline) {
            m_timeline->update();
        }
    }
}

void MainWindow::onRenderAs()
{
    RenderAsDialog dlg(&m_project, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString path = dlg.outputPath();
    if (path.isEmpty()) {
        return;
    }
    if (!m_audioEngine) {
        statusBar()->showMessage(tr("Audio engine unavailable"), 5000);
        return;
    }
    if (m_timeline && m_timeline->isPlaying()) {
        m_timeline->stopPlayback();
    }
    if (m_audioEngine) {
        m_audioEngine->stop();
    }
    m_audioEngine->syncGraphFromProject();
    double start = 0.0;
    double len = std::max(1.0, m_project.timelineEndSec());
    if (dlg.optionLoopRegionOnly() && m_project.hasLoopRegion()) {
        start = m_project.loopRegion().startSec;
        len = std::max(0.05, m_project.loopRegion().endSec - m_project.loopRegion().startSec);
    }

    MediaRenderRequest req;
    req.outputPath = path;
    req.formatName = dlg.selectedFormat();
    req.templateName = dlg.selectedTemplate();
    req.startSec = start;
    req.lengthSec = len;

    // Rough size estimate for progress UI
    qint64 estBytes = 0;
    RenderFormat fmt;
    RenderTemplate tpl;
    if (MediaEngine::resolveTemplate(req.formatName, req.templateName, &fmt, &tpl)) {
        const bool video = !(fmt.audioOnly || tpl.audioOnly);
        const int br = tpl.bitrateKbps > 0 ? tpl.bitrateKbps : (video ? 8000 : 192);
        estBytes = qint64((br + (video ? 192 : 0)) * 1000.0 / 8.0 * len) + 65536;
    }

    RenderingProgressDialog progress(path, this);
    progress.beginRender(len, estBytes);
    progress.show();
    QCoreApplication::processEvents();

    req.onProgress = [&](const MediaRenderProgress &p) {
        progress.applyProgress(p);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return !progress.wasCanceled();
    };
    req.onPreviewFrame = [&](const QImage &frame, double t) {
        if (!progress.showVideoInPreview()) {
            return;
        }
        showRenderPreviewFrame(frame, t);
        if (!m_syncingPlayheadFromEngine) {
            m_syncingPlayheadFromEngine = true;
            m_project.setPlayheadSec(t);
            if (m_timeline) {
                m_timeline->update();
            }
            updateTimecodeLabels(t);
            m_syncingPlayheadFromEngine = false;
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    };

    statusBar()->showMessage(tr("Rendering…"));
    const MediaRenderResult r = MediaEngine::renderProject(m_project, m_audioEngine.get(), req);

    if (r.canceled || progress.wasCanceled()) {
        progress.finishFailure(tr("Canceled"));
        statusBar()->showMessage(tr("Render canceled"), 5000);
        if (!progress.closeWhenDone()) {
            progress.exec();
        }
        return;
    }
    if (r.ok) {
        progress.finishSuccess(r.message.isEmpty() ? tr("Rendered to %1").arg(path) : r.message);
        statusBar()->showMessage(r.message.isEmpty() ? tr("Rendered to %1").arg(path) : r.message,
                                 8000);
        if (!progress.closeWhenDone()) {
            progress.exec();
        }
    } else {
        progress.finishFailure(r.error.isEmpty() ? path : r.error);
        statusBar()->showMessage(tr("Render failed"), 5000);
        QMessageBox::warning(this, tr("Render As"),
                             tr("Render failed:\n%1").arg(r.error.isEmpty() ? path : r.error));
        progress.exec();
    }
}

void MainWindow::showRenderPreviewFrame(const QImage &frame, double sec)
{
    if (!ui->previewLabel || frame.isNull()) {
        return;
    }

    const QSize vp = ui->previewViewport ? ui->previewViewport->size() : QSize(640, 360);
    const int w = std::max(160, vp.width());
    const int h = std::max(90, vp.height() - 4);

    m_lastPreviewFrame = frame;

    QPixmap px(w, h);
    px.fill(QColor(0x1e, 0x1e, 0x1e));
    QPainter p(&px);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(previewContentRect(w, h), frame);
    p.end();

    ui->previewLabel->setPixmap(px);
    ui->previewLabel->setScaledContents(false);
    ui->previewLabel->setAlignment(Qt::AlignCenter);
    Q_UNUSED(sec);
}

void MainWindow::onBounceAudioMixdown()
{
    if (!m_audioEngine) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Bounce Audio Mixdown"), QString(), tr("WAV (*.wav)"));
    if (path.isEmpty()) {
        return;
    }
    m_audioEngine->syncGraphFromProject();
    const double len = std::max(1.0, m_project.timelineEndSec());
    const bool ok = m_audioEngine->renderToWav(path, 0.0, len);
    statusBar()->showMessage(ok ? tr("Bounced mixdown to %1").arg(path)
                                : tr("Bounce failed"),
                             6000);
}

void MainWindow::onExtractAudioFromCd()
{
    ExtractAudioFromCdDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const auto files = dlg.extractedFiles();
    if (files.isEmpty()) {
        return;
    }
    beginDocumentEdit();
    for (const auto &f : files) {
        MediaItem item;
        item.path = f.path;
        item.displayName = f.displayName;
        item.kind = QStringLiteral("audio");
        item.missing = !QFileInfo::exists(f.path);
        bool exists = false;
        for (const MediaItem &m : m_project.mediaPool()) {
            if (QDir::cleanPath(m.path).compare(QDir::cleanPath(f.path), Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_project.mediaPool().push_back(item);
        }
        addMediaCard(item.displayName, item.kind, tr("WAV · CDDA · %1 s").arg(f.lengthSec, 0, 'f', 1),
                     f.path, f.lengthSec);
    }
    commitDocumentEdit(tr("Extract Audio from CD"));
    refreshMediaEmptyState();
    statusBar()->showMessage(tr("Extracted %1 track(s) from CD to Project Media").arg(files.size()),
                             6000);
}

void MainWindow::onPreferences()
{
    PreferencesDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        m_pluginScanner.setPreferredPath(dlg.ofxPath());
        m_pluginScanner.setVegasProPath(dlg.vegasProPath());
        AudioPluginRegistry::instance().refresh();
    }
}

void MainWindow::onCustomizeKeyboard()
{
    CustomizeKeyboardDialog dlg(this);
    dlg.exec();
}

void MainWindow::applyKeyboardMap()
{
    qDeleteAll(m_keyboardShortcuts);
    m_keyboardShortcuts.clear();

    // Only bind commands not already owned by QAction menu shortcuts (avoid double-fire).
    static const QSet<QString> kAppShortcuts = {
        QStringLiteral("Transport.PlayToggle"),
        QStringLiteral("Transport.Play"),
        QStringLiteral("Transport.PlayFromStart"),
        QStringLiteral("Transport.Pause"),
        QStringLiteral("Transport.Stop"),
        QStringLiteral("Transport.LoopPlayback"),
        QStringLiteral("Transport.ScrubLeft"),
        QStringLiteral("Transport.ScrubPause"),
        QStringLiteral("Transport.ScrubRight"),
        QStringLiteral("Transport.Record"),
        QStringLiteral("CursorTo.Home"),
        QStringLiteral("CursorTo.End"),
        QStringLiteral("CursorTo.LeftByFrame"),
        QStringLiteral("CursorTo.RightByFrame"),
        QStringLiteral("CursorTo.LeftByPixel"),
        QStringLiteral("CursorTo.RightByPixel"),
        QStringLiteral("Options.LoopPlayback"),
        QStringLiteral("Options.IgnoreEventGrouping"),
        QStringLiteral("Options.EnableSnapping"),
        QStringLiteral("Marker.Insert"),
        QStringLiteral("LoopRegion.Insert"),
        QStringLiteral("Track.ToggleMute"),
        QStringLiteral("Track.ToggleSolo"),
        QStringLiteral("Edit.Split"),
        QStringLiteral("Group.CreateNew"),
        QStringLiteral("Group.Clear"),
        QStringLiteral("Help.CustomizeKeyboard"),
        QStringLiteral("View.MixingConsole"),
        QStringLiteral("View.EventMediaMarkers"),
    };

    QSet<QString> boundSeqs;
    for (const KeyboardCommand &c : KeyboardMap::instance().commandsFor(KeyboardContext::Global)) {
        if (!kAppShortcuts.contains(c.id)) {
            continue;
        }
        for (const QKeySequence &seq : c.shortcuts) {
            if (seq.isEmpty()) {
                continue;
            }
            const QString portable = seq.toString(QKeySequence::PortableText);
            if (boundSeqs.contains(portable)) {
                continue;
            }
            boundSeqs.insert(portable);
            auto *sc = new QShortcut(seq, this);
            sc->setContext(Qt::ApplicationShortcut);
            const QString id = c.id;
            connect(sc, &QShortcut::activated, this, [this, id]() { invokeKeyboardCommand(id); });
            m_keyboardShortcuts.push_back(sc);
        }
    }
}

void MainWindow::invokeKeyboardCommand(const QString &commandId)
{
    if (commandId == QLatin1String("Transport.PlayToggle")) {
        if (m_timeline) {
            m_timeline->togglePlaying();
        }
        return;
    }
    if (commandId == QLatin1String("Transport.Play")) {
        if (m_timeline) {
            m_timeline->setPlaying(true);
        }
        return;
    }
    if (commandId == QLatin1String("Transport.Pause")) {
        if (m_timeline) {
            m_timeline->setPlaying(false);
        }
        return;
    }
    if (commandId == QLatin1String("Transport.PlayFromStart")
        || commandId == QLatin1String("Transport.TogglePlayFromStart")) {
        if (m_timeline) {
            m_timeline->seekPlayhead(0.0, false);
            m_timeline->setPlaying(true);
        }
        return;
    }
    if (commandId == QLatin1String("Transport.Stop")) {
        if (m_timeline) {
            m_timeline->stopPlayback();
            m_timeline->seekPlayhead(0.0, true);
        }
        return;
    }
    if (commandId == QLatin1String("Transport.ScrubLeft")) {
        if (m_timeline) {
            m_timeline->setShuttleRate(-1.0);
        }
        return;
    }
    if (commandId == QLatin1String("Transport.ScrubRight")) {
        if (m_timeline) {
            m_timeline->setShuttleRate(1.0);
        }
        return;
    }
    if (commandId == QLatin1String("Transport.ScrubPause")) {
        if (m_timeline) {
            m_timeline->setShuttleRate(0.0);
            m_timeline->setPlaying(false);
        }
        return;
    }
    if (commandId == QLatin1String("Transport.LoopPlayback")
        || commandId == QLatin1String("Options.LoopPlayback")) {
        setLoopPlaybackEnabled(!m_project.loopPlaybackEnabled());
        return;
    }
    if (commandId == QLatin1String("Transport.Record")) {
        statusBar()->showMessage(tr("Record — not implemented yet"), 2500);
        return;
    }
    if (commandId == QLatin1String("CursorTo.Home")) {
        onGoToStart();
        return;
    }
    if (commandId == QLatin1String("CursorTo.End")) {
        onGoToEnd();
        return;
    }
    if (commandId == QLatin1String("CursorTo.LeftByFrame")
        || commandId == QLatin1String("CursorTo.LeftByPixel")
        || commandId == QLatin1String("Transport.StepBack")) {
        if (m_timeline) {
            m_timeline->stepFrames(-1);
        }
        return;
    }
    if (commandId == QLatin1String("CursorTo.RightByFrame")
        || commandId == QLatin1String("CursorTo.RightByPixel")
        || commandId == QLatin1String("Transport.StepForward")) {
        if (m_timeline) {
            m_timeline->stepFrames(1);
        }
        return;
    }
    if (commandId == QLatin1String("Edit.Undo")) {
        if (m_undoStack) {
            m_undoStack->undo();
        }
        return;
    }
    if (commandId == QLatin1String("Edit.Redo")) {
        if (m_undoStack) {
            m_undoStack->redo();
        }
        return;
    }
    if (commandId == QLatin1String("Edit.Cut")) {
        onEditCut();
        return;
    }
    if (commandId == QLatin1String("Edit.Copy")) {
        onEditCopy();
        return;
    }
    if (commandId == QLatin1String("Edit.Paste")) {
        onEditPaste();
        return;
    }
    if (commandId == QLatin1String("Edit.Delete")) {
        onEditDelete();
        return;
    }
    if (commandId == QLatin1String("Edit.Split")) {
        onEditSplit();
        return;
    }
    if (commandId == QLatin1String("Group.CreateNew")) {
        runDocumentEdit(tr("Group"), [this]() { m_project.groupSelectedEvents(); });
        refreshTimeline();
        return;
    }
    if (commandId == QLatin1String("Group.Clear")) {
        // Global hotkey has no single right-clicked event to anchor on (unlike the
        // context menu's per-event "Clear") — dissolve the group of every selected event.
        QSet<int> groupIds;
        for (int id : m_project.selectedEventIds()) {
            if (const TrackEvent *ev = m_project.findEvent(id); ev && ev->groupId > 0) {
                groupIds.insert(ev->groupId);
            }
        }
        if (groupIds.isEmpty()) {
            return;
        }
        runDocumentEdit(tr("Clear Group"), [this, &groupIds]() {
            for (int gid : groupIds) {
                m_project.clearGroup(gid);
            }
        });
        refreshTimeline();
        return;
    }
    if (commandId == QLatin1String("Select.All") || commandId == QLatin1String("Edit.SelectAll")) {
        onSelectAll();
        return;
    }
    if (commandId == QLatin1String("File.New")) {
        onNewProject();
        return;
    }
    if (commandId == QLatin1String("File.Open")) {
        onOpenProject();
        return;
    }
    if (commandId == QLatin1String("File.Preferences")) {
        onPreferences();
        return;
    }
    if (commandId == QLatin1String("Help.CustomizeKeyboard")) {
        onCustomizeKeyboard();
        return;
    }
    if (commandId == QLatin1String("View.MixingConsole")) {
        onMixingConsole();
        return;
    }
    if (commandId == QLatin1String("Options.IgnoreEventGrouping")) {
        m_project.setIgnoreEventGrouping(!m_project.ignoreEventGrouping());
        return;
    }
    if (commandId == QLatin1String("Options.EnableSnapping")) {
        m_project.setSnappingEnabled(!m_project.snappingEnabled());
        return;
    }
    if (commandId == QLatin1String("Marker.Insert")) {
        if (m_timeline) {
            m_timeline->insertMarkerAtPlayhead();
        }
        return;
    }
    if (commandId == QLatin1String("LoopRegion.Insert")) {
        if (m_timeline) {
            m_timeline->insertLoopRegionAtPlayhead();
        }
        return;
    }
    if (commandId == QLatin1String("Track.ToggleMute")) {
        int ti = -1;
        for (int i = 0; i < m_project.tracks().size(); ++i) {
            for (const TrackEvent &ev : m_project.tracks()[i].events) {
                if (ev.selected) {
                    ti = i;
                    break;
                }
            }
            if (ti >= 0) {
                break;
            }
        }
        if (ti < 0 && !m_project.tracks().isEmpty()) {
            ti = 0;
        }
        if (ti >= 0) {
            m_project.tracks()[ti].muted = !m_project.tracks()[ti].muted;
            if (m_audioEngine) {
                m_audioEngine->syncMixerLive();
            }
            refreshTimeline();
        }
        return;
    }
    if (commandId == QLatin1String("Track.ToggleSolo")) {
        int ti = -1;
        for (int i = 0; i < m_project.tracks().size(); ++i) {
            for (const TrackEvent &ev : m_project.tracks()[i].events) {
                if (ev.selected) {
                    ti = i;
                    break;
                }
            }
            if (ti >= 0) {
                break;
            }
        }
        if (ti < 0 && !m_project.tracks().isEmpty()) {
            ti = 0;
        }
        if (ti >= 0) {
            m_project.tracks()[ti].solo = !m_project.tracks()[ti].solo;
            if (m_audioEngine) {
                m_audioEngine->syncMixerLive();
            }
            refreshTimeline();
        }
        return;
    }
    if (commandId == QLatin1String("View.EventMediaMarkers")) {
        m_project.setShowEventMediaMarkers(!m_project.showEventMediaMarkers());
        if (m_trimmer) {
            m_trimmer->setMarkersVisible(m_project.showEventMediaMarkers());
        }
        if (m_timeline) {
            m_timeline->update();
        }
        return;
    }
}

void MainWindow::onPluginChooser()
{
    PluginChooserDialog dlg(&m_pluginScanner, this);
    dlg.setAudioMode(false);
    dlg.exec();
}

void MainWindow::onVideoEventFx(int eventId)
{
    TrackEvent *ev = m_project.findEvent(eventId);
    if (!ev || isAudioFamily(ev->mediaKind)) {
        return;
    }
    ensureFxFirst(ev->fxChain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
    ev->panCrop.ensureDefault(m_project.frameWidth(), m_project.frameHeight());

    if (!m_videoEventFx) {
        m_videoEventFx = new VideoEventFxDialogExact(this);
        m_videoEventFx->setPluginScanner(&m_pluginScanner);
        connect(m_videoEventFx, &QDialog::finished, this, [this](int) {
            commitDocumentEdit(tr("Video Event FX"));
            if (m_timeline) {
                m_timeline->update();
            }
        });
    } else if (m_videoEventFx->isVisible()) {
        commitDocumentEdit(tr("Video Event FX"));
    }

    beginDocumentEdit();
    const double localPh = std::max(0.0, m_project.playheadSec() - ev->startSec);
    m_videoEventFx->setEvent(ev, m_project.frameWidth(), m_project.frameHeight(), localPh);
    m_videoEventFx->show();
    m_videoEventFx->raise();
    m_videoEventFx->activateWindow();
}

void MainWindow::openTransitionProperties(int eventId, bool fadeIn)
{
    TrackEvent *ev = m_project.findEvent(eventId);
    if (!ev) {
        return;
    }
    const TransitionInstance &t = fadeIn ? ev->transitionIn : ev->transitionOut;
    if (!t.isValid()) {
        return;
    }
    if (!m_transitionProps) {
        m_transitionProps = new TransitionPropertiesDialog(this);
        connect(m_transitionProps, &TransitionPropertiesDialog::transitionChanged, this,
                [this](int id, bool isFadeIn, const TransitionInstance &edited) {
                    TrackEvent *target = m_project.findEvent(id);
                    if (!target) {
                        return;
                    }
                    (isFadeIn ? target->transitionIn : target->transitionOut) = edited;
                    refreshPreviewFrame(m_project.playheadSec());
                    if (m_timeline) {
                        m_timeline->update();
                    }
                });
        connect(m_transitionProps, &QDialog::finished, this, [this](int) {
            commitDocumentEdit(tr("Transition Properties"));
        });
    } else if (m_transitionProps->isVisible()) {
        commitDocumentEdit(tr("Transition Properties"));
    }

    beginDocumentEdit();
    m_transitionProps->setTransition(t, eventId, fadeIn, ev->name);
    m_transitionProps->show();
    m_transitionProps->raise();
    m_transitionProps->activateWindow();
}

void MainWindow::openTitlesTextEditor(TrackEvent *ev)
{
    if (!ev) {
        return;
    }
    if (!m_titlesTextEditor) {
        m_titlesTextEditor = new TitlesTextEditorDialog(this);
        connect(m_titlesTextEditor, &TitlesTextEditorDialog::previewInvalidated, this, [this] {
            refreshPreviewFrame(m_project.playheadSec());
        });
        connect(m_titlesTextEditor, &TitlesTextEditorDialog::durationChanged, this, [this] {
            if (m_timeline) {
                m_timeline->update();
            }
        });
        connect(m_titlesTextEditor, &QDialog::finished, this, [this](int) {
            commitDocumentEdit(tr("Titles & Text"));
            if (m_titlesTextOverlay) {
                static_cast<TitlesTextOverlayLayer *>(m_titlesTextOverlay)->clearActiveEvent();
            }
            if (m_timeline) {
                m_timeline->update();
            }
        });
    } else if (m_titlesTextEditor->isVisible()) {
        commitDocumentEdit(tr("Titles & Text"));
    }

    beginDocumentEdit();
    m_titlesTextEditor->setEvent(ev, m_project.frameWidth(), m_project.frameHeight(),
                                 m_project.frameRate());
    m_titlesTextEditor->show();
    m_titlesTextEditor->raise();
    m_titlesTextEditor->activateWindow();

    if (m_titlesTextOverlay) {
        syncTitlesTextOverlayGeometry();
        static_cast<TitlesTextOverlayLayer *>(m_titlesTextOverlay)->setActiveEvent(&m_project, ev->id);
    }
}

void MainWindow::createTitlesTextEvent(const QString &animationKey, const QString &sampleText)
{
    int newEventId = -1;
    runDocumentEdit(tr("Add Titles & Text"), [this, &newEventId, &animationKey, &sampleText]() {
        newEventId = m_project.addTitlesTextEvent(animationKey, sampleText);
    });
    refreshTimeline();
    TrackEvent *ev = m_project.findEvent(newEventId);
    if (ev) {
        openTitlesTextEditor(ev);
    }
}

void MainWindow::ensureAudioFxDialog()
{
    if (m_audioEventFx) {
        return;
    }
    m_audioEventFx = new AudioEventFxDialog(this);
    m_audioEventFx->setPluginScanner(&m_pluginScanner);
    connect(m_audioEventFx, &QDialog::finished, this, [this](int) {
        commitDocumentEdit(m_audioFxCommitLabel.isEmpty() ? tr("Event FX") : m_audioFxCommitLabel);
        if (m_audioEngine) {
            m_audioEngine->syncGraphFromProject();
        }
        if (m_timeline) {
            m_timeline->update();
        }
    });
}

void MainWindow::onAudioEventFx(int eventId)
{
    TrackEvent *ev = m_project.findEvent(eventId);
    if (!ev || !isAudioFamily(ev->mediaKind)) {
        return;
    }

    ensureAudioFxDialog();
    if (m_audioEventFx->isVisible()) {
        commitDocumentEdit(m_audioFxCommitLabel.isEmpty() ? tr("Event FX") : m_audioFxCommitLabel);
        if (m_audioEngine) {
            m_audioEngine->syncGraphFromProject();
        }
    }

    beginDocumentEdit();
    m_audioFxCommitLabel = tr("Event FX");
    m_audioEventFx->setEvent(ev);
    m_audioEventFx->show();
    m_audioEventFx->raise();
    m_audioEventFx->activateWindow();
    // Vegas-style: empty Event FX → open Plug-In Chooser after the chain window is up.
    if (ev->fxChain.isEmpty()) {
        QTimer::singleShot(0, m_audioEventFx, [this]() {
            if (m_audioEventFx && m_audioEventFx->isVisible()) {
                m_audioEventFx->addPlugins();
            }
        });
    }
}

void MainWindow::onTrackFx(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size()) {
        return;
    }
    Track &track = m_project.tracks()[trackIndex];
    if (track.kind == TrackKind::Video) {
        if (!m_videoTrackFx) {
            m_videoTrackFx = new VideoTrackFxDialog(this);
            m_videoTrackFx->setPluginScanner(&m_pluginScanner);
            connect(m_videoTrackFx, &QDialog::finished, this, [this](int) {
                commitDocumentEdit(tr("Video Track FX"));
            });
        } else if (m_videoTrackFx->isVisible()) {
            commitDocumentEdit(tr("Video Track FX"));
        }
        beginDocumentEdit();
        m_videoTrackFx->setTrack(&track, m_project.timelineEndSec(), m_project.playheadSec());
        m_videoTrackFx->show();
        m_videoTrackFx->raise();
        m_videoTrackFx->activateWindow();
        return;
    }
    if (track.kind == TrackKind::Audio) {
        if (track.fxChain.isEmpty()) {
            track.fxChain = BuiltinAudioCatalog::defaultTrackFxChain();
        }
    }

    ensureAudioFxDialog();
    if (m_audioEventFx->isVisible()) {
        commitDocumentEdit(m_audioFxCommitLabel.isEmpty() ? tr("Track FX") : m_audioFxCommitLabel);
        if (m_audioEngine) {
            m_audioEngine->syncGraphFromProject();
        }
    }

    beginDocumentEdit();
    m_audioFxCommitLabel = tr("Track FX");
    m_audioEventFx->setTrack(&track);
    m_audioEventFx->show();
    m_audioEventFx->raise();
    m_audioEventFx->activateWindow();
}

void MainWindow::onColorGrading(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size()) {
        return;
    }
    Track &track = m_project.tracks()[trackIndex];
    if (track.kind != TrackKind::Video) {
        return;
    }
    if (indexOfFxName(track.fxChain, QStringLiteral("Color Grading")) < 0) {
        track.fxChain.push_back(
            makeFxSlot(QStringLiteral("Color Grading"), PluginFormat::Builtin,
                       QStringLiteral("builtin:Color Grading")));
    }

    ensureAudioFxDialog();
    if (m_audioEventFx->isVisible()) {
        commitDocumentEdit(m_audioFxCommitLabel.isEmpty() ? tr("Color Grading")
                                                           : m_audioFxCommitLabel);
    }

    beginDocumentEdit();
    m_audioFxCommitLabel = tr("Color Grading");
    m_audioEventFx->setTrack(&track);
    m_audioEventFx->selectByName(QStringLiteral("Color Grading"));
    m_audioEventFx->show();
    m_audioEventFx->raise();
    m_audioEventFx->activateWindow();
}

void MainWindow::onTrackMotion(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size()) {
        return;
    }
    Track &track = m_project.tracks()[trackIndex];
    if (track.kind != TrackKind::Video) {
        return;
    }
    double dur = 10.0;
    for (const TrackEvent &ev : track.events) {
        dur = std::max(dur, ev.startSec + ev.lengthSec);
    }
    const double aspect = (m_project.frameHeight() > 0)
                              ? double(m_project.frameWidth()) / double(m_project.frameHeight())
                              : (16.0 / 9.0);
    track.motion.ensureDefault(aspect);

    // Non-modal, like the Event FX windows: in VEGAS you leave Track Motion open and keep
    // scrubbing and editing the timeline behind it. Running it with exec() also meant the
    // preview never moved while it was up, so the effect of a change could not be seen.
    if (!m_trackMotion) {
        m_trackMotion = new TrackMotionDialog(this);
        connect(m_trackMotion, &QDialog::finished, this, [this](int) {
            commitDocumentEdit(tr("Track Motion"));
            if (m_timeline) {
                m_timeline->update();
            }
        });
        connect(m_trackMotion, &TrackMotionDialog::motionChanged, this, [this]() {
            refreshPreviewFrame(m_project.playheadSec());
            if (m_timeline) {
                m_timeline->update();
            }
        });
    } else if (m_trackMotion->isVisible()) {
        // Switching to another track closes off the previous track's edit as its own.
        commitDocumentEdit(tr("Track Motion"));
    }

    beginDocumentEdit();
    m_trackMotion->setTrack(&track, m_project.frameWidth(), m_project.frameHeight(), dur,
                            m_project.playheadSec());
    m_trackMotion->show();
    m_trackMotion->raise();
    m_trackMotion->activateWindow();
}

void MainWindow::onSelectAll()
{
    m_project.selectAllEvents();
    if (m_timeline) {
        m_timeline->update();
    }
}

void MainWindow::beginDocumentEdit()
{
    if (m_editCaptureOpen) {
        return;
    }
    m_editBefore = ProjectSnapshot::capture(m_project);
    m_editCaptureOpen = true;
}

void MainWindow::commitDocumentEdit(const QString &text)
{
    if (!m_editCaptureOpen) {
        return;
    }
    m_editCaptureOpen = false;
    const ProjectSnapshot after = ProjectSnapshot::capture(m_project);
    if (after == m_editBefore || !m_undoStack) {
        return;
    }
    m_undoStack->push(new SnapshotCommand(&m_project, m_editBefore, after, text, [this]() {
        onDocumentRestored();
    }));
}

void MainWindow::discardDocumentEdit()
{
    m_editCaptureOpen = false;
}

void MainWindow::runDocumentEdit(const QString &text, const std::function<void()> &mutate)
{
    beginDocumentEdit();
    if (mutate) {
        mutate();
    }
    commitDocumentEdit(text);
}

void MainWindow::clearUndoHistory()
{
    discardDocumentEdit();
    if (m_undoStack) {
        m_undoStack->clear();
    }
}

void MainWindow::onDocumentRestored()
{
    ui->mediaGrid->clear();
    for (const MediaItem &m : m_project.mediaPool()) {
        addMediaCard(m.displayName, m.kind,
                     m.missing ? tr("Missing: %1").arg(m.path)
                               : tr("%1 · %2").arg(m.kind, m.path),
                     m.path);
    }
    refreshMediaEmptyState();
    if (m_mixingConsole) {
        m_mixingConsole->refreshFromProject();
    }
    refreshTimeline();
    refreshPreviewFrame(m_project.playheadSec());
    refreshStatusBar();
}

void MainWindow::onEditCut()
{
    if (m_project.selectedEventIds().isEmpty()) {
        statusBar()->showMessage(tr("Nothing to cut"), 2000);
        return;
    }
    runDocumentEdit(tr("Cut"), [this]() { m_project.cutSelectedEvents(); });
    refreshTimeline();
    refreshPreviewFrame(m_project.playheadSec());
    statusBar()->showMessage(tr("Cut"), 2000);
}

void MainWindow::onEditCopy()
{
    if (m_project.selectedEventIds().isEmpty()) {
        statusBar()->showMessage(tr("Nothing to copy"), 2000);
        return;
    }
    m_project.copySelectedEvents();
    statusBar()->showMessage(tr("Copied %1 event(s)").arg(m_project.eventClipboard().items.size()),
                             2000);
}

void MainWindow::onEditPaste()
{
    if (!m_project.hasEventClipboard()) {
        statusBar()->showMessage(tr("Clipboard is empty"), 2000);
        return;
    }
    int n = 0;
    runDocumentEdit(tr("Paste"), [this, &n]() { n = m_project.pasteEventsAt(m_project.playheadSec()); });
    refreshTimeline();
    refreshPreviewFrame(m_project.playheadSec());
    statusBar()->showMessage(tr("Pasted %1 event(s)").arg(n), 2000);
}

void MainWindow::onEditDelete()
{
    bool ok = false;
    runDocumentEdit(tr("Delete"), [this, &ok]() { ok = m_project.deleteSelectedEvents(); });
    if (!ok) {
        statusBar()->showMessage(tr("Nothing to delete"), 2000);
        return;
    }
    refreshTimeline();
    refreshPreviewFrame(m_project.playheadSec());
}

void MainWindow::onEditSplit()
{
    bool ok = false;
    runDocumentEdit(tr("Split"), [this, &ok]() {
        const double t = m_project.playheadSec();
        // Vegas: with an explicit selection, split just that; with nothing selected
        // (the common "position the cursor and hit S" workflow), split every event
        // under the cursor across every track instead of requiring a click first.
        ok = m_project.selectedEventIds().isEmpty() ? m_project.splitAllAt(t)
                                                     : m_project.splitSelectedAt(t);
    });
    if (!ok) {
        statusBar()->showMessage(tr("Split: place the cursor inside a clip"), 2500);
        return;
    }
    refreshTimeline();
}

void MainWindow::onEditTrimStart()
{
    bool ok = false;
    runDocumentEdit(tr("Trim Start"),
                    [this, &ok]() { ok = m_project.trimSelectedStartTo(m_project.playheadSec()); });
    if (!ok) {
        statusBar()->showMessage(tr("Trim Start: place playhead inside a selected event"), 2500);
        return;
    }
    refreshTimeline();
}

void MainWindow::onEditTrimEnd()
{
    bool ok = false;
    runDocumentEdit(tr("Trim End"),
                    [this, &ok]() { ok = m_project.trimSelectedEndTo(m_project.playheadSec()); });
    if (!ok) {
        statusBar()->showMessage(tr("Trim End: place playhead inside a selected event"), 2500);
        return;
    }
    refreshTimeline();
}

void MainWindow::onGoToStart()
{
    if (m_timeline) {
        m_timeline->seekPlayhead(0.0, true);
    }
}

void MainWindow::onGoToEnd()
{
    if (m_timeline) {
        m_timeline->seekPlayhead(m_project.timelineEndSec(), true);
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About OpenVegas"),
                       tr("OpenVegas 0.1 — GPL-3.0-or-later\n"
                          "C++17 / Qt 6.8 — UI chrome from SAMPLES mockups.\n\n"
                          "Vegas Pro plug-ins remain MAGIX property."));
}

void MainWindow::openEventProperties(int eventId)
{
    TrackEvent *ev = m_project.findEvent(eventId);
    if (!ev) {
        return;
    }
    if (!ev->fxChain.isEmpty() && isTitlesTextName(ev->fxChain.first().displayName)) {
        openTitlesTextEditor(ev);
        return;
    }
    EventPropertiesDialog dlg(this);
    dlg.setEvent(ev->name, ev->startSec, ev->lengthSec);
    if (dlg.exec() == QDialog::Accepted) {
        runDocumentEdit(tr("Event Properties"), [ev, &dlg]() {
            ev->name = dlg.eventName();
            ev->startSec = dlg.startSec();
            ev->lengthSec = dlg.lengthSec();
        });
        if (m_timeline) {
            m_timeline->update();
        }
    }
}

void MainWindow::openTrimmer(int eventId)
{
    TrackEvent *ev = m_project.findEvent(eventId);
    if (!ev) {
        return;
    }
    if (!m_trimmer) {
        m_trimmer = new TrimmerWindow(this);
        m_trimmer->onMarkersChanged = [this](const QString &mediaPath,
                                             const QVector<TimelineMarker> &markers) {
            MediaItem *item = m_project.ensureMediaItem(mediaPath);
            if (!item && !mediaPath.isEmpty()) {
                item = m_project.ensureMediaItem(mediaPath, QFileInfo(mediaPath).fileName());
            }
            if (item) {
                item->markers = markers;
            }
            if (m_timeline) {
                m_timeline->update();
            }
        };
    }
    const QString path = m_project.mediaPathForEvent(*ev);
    MediaItem *item = m_project.ensureMediaItem(
        path, ev->name,
        isAudioFamily(ev->mediaKind) ? QStringLiteral("audio") : QStringLiteral("video"));
    const QVector<TimelineMarker> markers = item ? item->markers : QVector<TimelineMarker>{};
    double dur = ev->mediaLengthSec > 1e-6 ? ev->mediaLengthSec : ev->lengthSec;
    if (dur < 0.5) {
        dur = 0.5;
    }
    if (isAudioFamily(ev->mediaKind) && !path.isEmpty()) {
        const WaveformPeaks peaks = MediaWaveformCache::instance().peaksFor(path);
        if (peaks.isValid() && peaks.durationSec > 0.5) {
            dur = peaks.durationSec;
        }
    }
    QString title = ev->name;
    if (title.isEmpty()) {
        title = QFileInfo(path).fileName();
    }
    m_trimmer->setMedia(title, ev->mediaKind, dur, path, markers, ev->reversed);
    m_trimmer->show();
    m_trimmer->raise();
    m_trimmer->activateWindow();
}

void MainWindow::beginTrackRename(int trackIndex)
{
    if (m_timeline) {
        m_timeline->beginTrackRename(trackIndex);
    }
}

void MainWindow::refreshTimeline()
{
    if (m_timeline) {
        m_timeline->refreshLayout();
        m_timeline->update();
    }
    if (m_audioEngine) {
        m_audioEngine->syncGraphFromProject();
    }
    if (m_mixingConsole) {
        m_mixingConsole->refreshFromProject();
    }
    syncMasterFaderFromProject();
}

void MainWindow::syncMasterFaderFromProject()
{
    if (!m_masterFader) {
        return;
    }
    // The mixing console edits the same gain, and so does opening a project. Signals are
    // blocked so following the model does not read as the user moving the fader, which
    // would push the value straight back and start an edit.
    const QSignalBlocker block(m_masterFader);
    m_masterFader->setValue(dbToFaderPos(m_project.masterVolumeDb()));
}

void MainWindow::refreshTimecodeLabels()
{
    updateTimecodeLabels(m_project.playheadSec());
}

void MainWindow::showEventContextMenu(int eventId, const QPoint &globalPos)
{
    ContextMenuBuilder::showEventMenu(this, eventId, globalPos);
}

void MainWindow::showTimelineEmptyContextMenu(const QPoint &globalPos)
{
    ContextMenuBuilder::showTimelineEmptyMenu(this, globalPos);
}

void MainWindow::showTrackHeaderContextMenu(int trackIndex, const QPoint &globalPos)
{
    ContextMenuBuilder::showTrackHeaderMenu(this, trackIndex, globalPos);
}

void MainWindow::showTrackEmptyContextMenu(int trackIndex, const QPoint &globalPos)
{
    ContextMenuBuilder::showTrackEmptyMenu(this, trackIndex, globalPos);
}

void MainWindow::showRulerContextMenu(const QPoint &globalPos)
{
    ContextMenuBuilder::showRulerMenu(this, globalPos);
}

void MainWindow::showMarkerLaneContextMenu(const QPoint &globalPos)
{
    ContextMenuBuilder::showMarkerLaneMenu(this, globalPos);
}

void MainWindow::showMarkerContextMenu(int markerId, const QPoint &globalPos)
{
    ContextMenuBuilder::showMarkerMenu(this, markerId, globalPos);
}

void MainWindow::setLoopPlaybackEnabled(bool on)
{
    m_project.setLoopPlaybackEnabled(on);
    if (m_tlLoopBtn) {
        QSignalBlocker b(m_tlLoopBtn);
        m_tlLoopBtn->setChecked(on);
    }
    if (m_previewLoopBtn) {
        QSignalBlocker b(m_previewLoopBtn);
        m_previewLoopBtn->setChecked(on);
    }
}

void MainWindow::showPreviewContextMenu(const QPoint &globalPos)
{
    ContextMenuBuilder::showPreviewMenu(this, globalPos);
}

void MainWindow::showTimeDisplayContextMenu(const QPoint &globalPos)
{
    ContextMenuBuilder::showTimeDisplayMenu(this, globalPos);
}

} // namespace openvegas
