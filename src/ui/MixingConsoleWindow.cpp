#include "ui/MixingConsoleWindow.h"

#include "audio/AudioUtil.h"
#include "audio/CompositePluginHost.h"
#include "ui/IconFactory.h"
#include "ui/PluginChooserDialog.h"
#include "ui/AudioEventFxDialog.h"
#include "model/ProjectModel.h"
#include "plugins/AudioPluginHost.h"
#include "plugins/AudioPluginTypes.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QSignalBlocker>
#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace openvegas {

namespace {

constexpr int kStripNarrow = 72;
constexpr int kStripDefault = 92;
constexpr int kStripWide = 118;
constexpr int kMasterExtra = 22;

QColor audioTrackSwatch(int oneBasedIndex)
{
    static const QColor kColors[] = {
        QColor(0xc0, 0x6a, 0x8a), // pink
        QColor(0xc0, 0x48, 0x48), // red
        QColor(0xd0, 0x88, 0x40), // orange
        QColor(0xd0, 0xb8, 0x40), // yellow
        QColor(0x68, 0xa8, 0x68), // green
        QColor(0x58, 0x88, 0xc0), // blue
    };
    const int n = int(sizeof(kColors) / sizeof(kColors[0]));
    return kColors[(qMax(1, oneBasedIndex) - 1) % n];
}

QPixmap colorBadge(const QColor &c, int size, const QString &text = QString())
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(0, 0, size, size, c);
    p.setPen(QColor(0x22, 0x22, 0x22));
    p.drawRect(0, 0, size - 1, size - 1);
    if (!text.isEmpty()) {
        QFont f;
        f.setPixelSize(qMax(8, size - 6));
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0x10, 0x10, 0x10));
        p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, text);
    }
    return pm;
}

class MixerDbScale : public QWidget
{
public:
    explicit MixerDbScale(bool showTicks, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_showTicks(showTicks)
    {
        setFixedWidth(14);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setShowTicks(bool on)
    {
        m_showTicks = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_showTicks) {
            return;
        }
        QPainter p(this);
        QFont f = font();
        f.setPixelSize(7);
        p.setFont(f);
        p.setPen(QColor(0xa0, 0xa0, 0xa0));
        // Vegas default meter labels every 6 dB from 6…84
        static const int kMarks[] = {6, 12, 18, 24, 30, 36, 42, 48, 54, 60, 66, 72, 78, 84};
        constexpr int n = int(sizeof(kMarks) / sizeof(kMarks[0]));
        const int top = 2;
        const int bottom = height() - 2;
        const int span = qMax(1, bottom - top);
        const QFontMetrics fm(f);
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / double(n - 1);
            const int y = top + int(std::lround(t * span));
            const QString num = QString::number(kMarks[i]);
            const int tw = fm.horizontalAdvance(num);
            p.drawText((width() - tw) / 2, y + fm.ascent() / 2 - 1, num);
        }
    }

private:
    bool m_showTicks = true;
};

QString svgPhaseInvert()
{
    return QStringLiteral(
        "<path d=\"M2.5 8c0-3 2.5-5.5 5.5-5.5S13.5 5 13.5 8\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.3\"/>"
        "<path d=\"M13.5 8c0 3-2.5 5.5-5.5 5.5S2.5 11 2.5 8\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.3\" stroke-dasharray=\"2 1.5\"/>");
}

QString svgTouchHand()
{
    return QStringLiteral(
        "<path d=\"M7 2.5v6.2M5.2 5.2l1.8 1.8 1.8-1.8M4 11.5h8v2H4z\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>");
}

QToolButton *makeStripBtn(QWidget *parent, const QString &text, const QString &tip, bool checkable = false)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("mixerStripBtn"));
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setFixedSize(20, 16);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QToolButton *makeStripIconBtn(QWidget *parent, const QString &svg, const QString &tip, bool checkable = false,
                              const QColor &tint = QColor(0xe0, 0xe0, 0xe0))
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("mixerStripBtn"));
    b->setIcon(IconFactory::iconFromSvgBody(svg, 11, tint));
    b->setIconSize(QSize(11, 11));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setFixedSize(20, 16);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QComboBox *makeStripCombo(QWidget *parent, const QStringList &items, int current = 0)
{
    auto *c = new QComboBox(parent);
    c->setObjectName(QStringLiteral("mixerStripCombo"));
    c->addItems(items);
    c->setCurrentIndex(qBound(0, current, items.size() - 1));
    c->setFixedHeight(16);
    c->setFocusPolicy(Qt::NoFocus);
    c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return c;
}

} // namespace

class MixerChannelStrip : public QFrame
{
public:
    enum class Kind { AudioTrack, MasterBus, PreviewBus, AssignableFx, AudioBus, InputBus };

    MixerChannelStrip(const QString &title, const QString &subtitle, const QString &route,
                      const QColor &swatch, Kind kind, int trackNumber, bool faderTicks,
                      int baseW, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_kind(kind)
        , m_baseW(baseW)
    {
        const bool master = (kind == Kind::MasterBus);
        const bool fxBus = (kind == Kind::AssignableFx);
        const bool audioBus = (kind == Kind::AudioBus || kind == Kind::InputBus);
        setObjectName(master     ? QStringLiteral("mixerStripMaster")
                      : fxBus    ? QStringLiteral("mixerStripFx")
                      : audioBus ? QStringLiteral("mixerStripBus")
                                 : QStringLiteral("mixerStrip"));
        setFixedWidth(baseW);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(2, 2, 2, 0);
        root->setSpacing(1);

        // Header: color badge with number + title
        auto *hdr = new QHBoxLayout();
        hdr->setSpacing(3);
        hdr->setContentsMargins(0, 0, 0, 0);
        auto *badge = new QLabel(this);
        badge->setFixedSize(16, 16);
        QString badgeText;
        if (kind == Kind::AudioBus || kind == Kind::InputBus) {
            // trackNumber holds letterIndex (0 → A)
            if (trackNumber >= 0 && trackNumber < 26) {
                badgeText = QChar(QLatin1Char('A' + trackNumber));
            } else if (trackNumber >= 0) {
                badgeText = QString::number(trackNumber + 1);
            }
        } else if (trackNumber > 0) {
            badgeText = QString::number(trackNumber);
        }
        badge->setPixmap(colorBadge(swatch, 16, badgeText));
        badge->setToolTip(title);
        hdr->addWidget(badge);
        auto *name = new QLabel(title, this);
        name->setObjectName(QStringLiteral("mixerStripTitle"));
        name->setTextInteractionFlags(Qt::NoTextInteraction);
        hdr->addWidget(name, 1);
        root->addLayout(hdr);

        // I/O region (Vegas: device + bus).
        if (kind == Kind::InputBus) {
            root->addWidget(makeStripCombo(
                this, {tr("Input Off"), tr("Microsoft Sound Mapper"), tr("Microsoft Sound Mapper - Left"),
                       tr("Microsoft Sound Mapper - Right")}));
            root->addWidget(makeStripCombo(
                this, {tr("Output Off"), tr("Master (Microsoft Sound Mapper)"), tr("Microsoft Sound Mapper")}));
        } else if (kind == Kind::AssignableFx || kind == Kind::AudioBus) {
            root->addWidget(makeStripCombo(this, {tr("Master"), tr("Output Off"), tr("Bus A")}));
        } else {
            if (!subtitle.isEmpty()) {
                root->addWidget(makeStripCombo(this, {subtitle, tr("Input Off"), tr("Microsoft Sound Mapper")}));
            }
            if (!route.isEmpty()) {
                root->addWidget(makeStripCombo(this, {route, tr("Output Off"), tr("Master"), tr("Bus A")}));
            } else if (master) {
                root->addWidget(makeStripCombo(this, {tr("Microsoft Sound Mapper"), tr("Output Off")}));
            }
        }

        // Automation mode
        auto *autoRow = new QHBoxLayout();
        autoRow->setContentsMargins(0, 0, 0, 0);
        autoRow->setSpacing(2);
        auto *autoIcon = new QLabel(this);
        autoIcon->setPixmap(IconFactory::iconFromSvgBody(svgTouchHand(), 12, QColor(0xd0, 0xd0, 0xd0)).pixmap(12, 12));
        autoIcon->setFixedSize(12, 12);
        autoRow->addWidget(autoIcon);
        auto *autoMode = makeStripCombo(this, {tr("Touch"), tr("Latch"), tr("Write"), tr("Read"), tr("Off")}, 0);
        autoRow->addWidget(autoMode, 1);
        root->addLayout(autoRow);

        // Pan
        auto *panLab = new QLabel(tr("Center"), this);
        panLab->setObjectName(QStringLiteral("mixerPanLabel"));
        panLab->setAlignment(Qt::AlignHCenter);
        panLab->setFixedHeight(12);
        root->addWidget(panLab);

        m_pan = new QSlider(Qt::Horizontal, this);
        m_pan->setObjectName(QStringLiteral("mixerPan"));
        m_pan->setRange(0, 100);
        m_pan->setValue(50);
        m_pan->setFixedHeight(12);
        m_pan->setToolTip(tr("Pan"));
        root->addWidget(m_pan);

        // Buttons | fader | scale | meters
        auto *body = new QHBoxLayout();
        body->setSpacing(1);
        body->setContentsMargins(0, 1, 0, 0);

        auto *btns = new QVBoxLayout();
        btns->setSpacing(1);
        btns->setContentsMargins(0, 0, 0, 0);
        m_muteBtn = makeStripBtn(this, QStringLiteral("M"), tr("Mute"), true);
        m_soloBtn = makeStripBtn(this, QStringLiteral("S"), tr("Solo"), true);
        btns->addWidget(m_muteBtn);
        btns->addWidget(m_soloBtn);
        btns->addWidget(makeStripBtn(this, QStringLiteral("fx"), tr("Track FX")));
        btns->addWidget(makeStripIconBtn(this, svgPhaseInvert(), tr("Phase Invert"), true));
        if (kind == Kind::AudioTrack) {
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgRecord(), tr("Arm for Record"), true,
                                            QColor(0xc4, 0x2b, 0x1c)));
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgAudioDevice(), tr("Input Monitor"), true));
        } else if (kind == Kind::AssignableFx) {
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgFx(), tr("Edit FX Chain")));
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgGroup(), tr("FX routing")));
        } else if (kind == Kind::InputBus) {
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgRecord(), tr("Arm for Record"), true,
                                            QColor(0xc4, 0x2b, 0x1c)));
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgAudioDevice(), tr("Input Monitor"), true));
        } else if (kind == Kind::AudioBus) {
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgFx(), tr("Bus FX")));
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgAudioDevice(), tr("Monitor"), true));
        } else {
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgFx(), tr("Assignable FX")));
            btns->addWidget(makeStripIconBtn(this, IconFactory::svgLockFader(), tr("Lock Fader"), true));
        }
        btns->addStretch(1);
        body->addLayout(btns);

        auto makeFader = [this, master]() {
            auto *f = new QSlider(Qt::Vertical, this);
            f->setObjectName(QStringLiteral("mixerFader"));
            f->setRange(0, 100);
            f->setValue(master ? 75 : 70);
            f->setFixedWidth(14);
            return f;
        };

        m_fader = makeFader();
        if (master) {
            body->addWidget(m_fader, 0, Qt::AlignHCenter);
            body->addWidget(makeFader(), 0, Qt::AlignHCenter);
        } else {
            body->addWidget(m_fader, 0, Qt::AlignHCenter);
        }

        m_scale = new MixerDbScale(faderTicks, this);
        body->addWidget(m_scale);

        auto *meterCol = new QVBoxLayout();
        meterCol->setSpacing(1);
        meterCol->setContentsMargins(0, 0, 0, 0);
        auto *meters = new QHBoxLayout();
        meters->setSpacing(1);
        auto makeMeter = [this]() {
            auto *m = new QProgressBar(this);
            m->setOrientation(Qt::Vertical);
            m->setTextVisible(false);
            m->setRange(0, 100);
            m->setValue(0);
            m->setObjectName(QStringLiteral("mixerMeter"));
            m->setFixedWidth(8);
            return m;
        };
        m_meterL = makeMeter();
        m_meterR = makeMeter();
        meters->addWidget(m_meterL);
        meters->addWidget(m_meterR);
        meterCol->addLayout(meters, 1);

        m_peakLabel = new QLabel(QStringLiteral("0,0"), this);
        m_peakLabel->setObjectName(QStringLiteral("mixerPeak"));
        m_peakLabel->setAlignment(Qt::AlignHCenter);
        m_peakLabel->setFixedHeight(13);
        meterCol->addWidget(m_peakLabel);
        body->addLayout(meterCol);

        root->addLayout(body, 1);

        if (master || kind == Kind::AudioBus || kind == Kind::InputBus || kind == Kind::AssignableFx) {
            // FX footer = plugin name (subtitle); Bus footer = bus name (subtitle) or title
            QString footText = title;
            if ((kind == Kind::AssignableFx || kind == Kind::AudioBus || kind == Kind::InputBus)
                && !subtitle.isEmpty()) {
                footText = subtitle;
            }
            auto *foot = new QLabel(footText, this);
            foot->setObjectName(QStringLiteral("mixerStripFooter"));
            foot->setAlignment(Qt::AlignCenter);
            foot->setFixedHeight(15);
            root->addWidget(foot);
        }
    }

    Kind kind() const { return m_kind; }
    int baseWidth() const { return m_baseW; }
    void setBaseWidth(int w)
    {
        m_baseW = w;
        setFixedWidth(w);
    }
    void setFaderTicks(bool on)
    {
        if (m_scale) {
            m_scale->setShowTicks(on);
        }
    }

    void setModelIds(int trackId, int busId, bool isMaster)
    {
        m_trackId = trackId;
        m_busId = busId;
        m_isMaster = isMaster;
    }
    int trackId() const { return m_trackId; }
    int busId() const { return m_busId; }
    bool isMaster() const { return m_isMaster; }

    void bindToModel(ProjectModel *project, MixingConsoleWindow *owner)
    {
        if (!project || !owner) {
            return;
        }
        auto applyLive = [this, project]() {
            if (m_isMaster) {
                project->setMasterVolumeDb(faderPosToDb(m_fader ? m_fader->value() : 70));
            } else if (m_trackId >= 0) {
                for (Track &t : project->tracks()) {
                    if (t.id != m_trackId) {
                        continue;
                    }
                    if (m_fader) {
                        t.volumeDb = faderPosToDb(m_fader->value());
                    }
                    if (m_pan) {
                        t.pan = float(m_pan->value() - 50) / 50.f;
                    }
                    if (m_muteBtn) {
                        t.muted = m_muteBtn->isChecked();
                    }
                    if (m_soloBtn) {
                        t.solo = m_soloBtn->isChecked();
                    }
                    break;
                }
            } else if (m_busId >= 0) {
                for (MixerBus &b : project->mixerBuses()) {
                    if (b.id != m_busId) {
                        continue;
                    }
                    if (m_fader) {
                        b.volumeDb = faderPosToDb(m_fader->value());
                    }
                    if (m_pan) {
                        b.pan = float(m_pan->value() - 50) / 50.f;
                    }
                    if (m_muteBtn) {
                        b.muted = m_muteBtn->isChecked();
                    }
                    if (m_soloBtn) {
                        b.solo = m_soloBtn->isChecked();
                    }
                    break;
                }
            }
        };
        auto commitUndo = [owner, applyLive]() {
            emit owner->documentEditBegan();
            applyLive();
            emit owner->documentEditCommitted(QObject::tr("Mixer"));
            emit owner->tracksChanged();
        };
        auto liveOnly = [owner, applyLive]() {
            applyLive();
            emit owner->tracksChanged();
        };
        if (m_fader) {
            QObject::connect(m_fader, &QSlider::valueChanged, owner, [liveOnly](int) { liveOnly(); });
            QObject::connect(m_fader, &QSlider::sliderReleased, owner, commitUndo);
        }
        if (m_pan) {
            QObject::connect(m_pan, &QSlider::valueChanged, owner, [liveOnly](int) { liveOnly(); });
            QObject::connect(m_pan, &QSlider::sliderReleased, owner, commitUndo);
        }
        if (m_muteBtn) {
            QObject::connect(m_muteBtn, &QToolButton::toggled, owner, [commitUndo](bool) { commitUndo(); });
        }
        if (m_soloBtn) {
            QObject::connect(m_soloBtn, &QToolButton::toggled, owner, [commitUndo](bool) { commitUndo(); });
        }
    }

    void loadFromModel(const ProjectModel *project)
    {
        if (!project) {
            return;
        }
        if (m_isMaster) {
            if (m_fader) {
                QSignalBlocker b(m_fader);
                m_fader->setValue(dbToFaderPos(project->masterVolumeDb()));
            }
            return;
        }
        if (m_trackId >= 0) {
            for (const Track &t : project->tracks()) {
                if (t.id != m_trackId) {
                    continue;
                }
                if (m_fader) {
                    QSignalBlocker b(m_fader);
                    m_fader->setValue(dbToFaderPos(t.volumeDb));
                }
                if (m_pan) {
                    QSignalBlocker b(m_pan);
                    m_pan->setValue(int(std::lround((t.pan + 1.f) * 50.f)));
                }
                if (m_muteBtn) {
                    QSignalBlocker b(m_muteBtn);
                    m_muteBtn->setChecked(t.muted);
                }
                if (m_soloBtn) {
                    QSignalBlocker b(m_soloBtn);
                    m_soloBtn->setChecked(t.solo);
                }
                break;
            }
        }
    }

    void setMeters(float peakL, float peakR)
    {
        auto toPct = [](float p) {
            if (p <= 1e-6f) {
                return 0;
            }
            const double db = linearToDb(p);
            return int(std::clamp((db + 60.0) / 72.0 * 100.0, 0.0, 100.0));
        };
        if (m_meterL) {
            m_meterL->setValue(toPct(peakL));
        }
        if (m_meterR) {
            m_meterR->setValue(toPct(peakR));
        }
        if (m_peakLabel) {
            m_peakLabel->setText(QStringLiteral("%1")
                                     .arg(linearToDb(std::max(peakL, peakR)), 0, 'f', 1));
        }
    }

private:
    Kind m_kind;
    int m_baseW = kStripDefault;
    MixerDbScale *m_scale = nullptr;
    QSlider *m_fader = nullptr;
    QSlider *m_pan = nullptr;
    QToolButton *m_muteBtn = nullptr;
    QToolButton *m_soloBtn = nullptr;
    QProgressBar *m_meterL = nullptr;
    QProgressBar *m_meterR = nullptr;
    QLabel *m_peakLabel = nullptr;
    int m_trackId = -1;
    int m_busId = -1;
    bool m_isMaster = false;
};

MixingConsoleWindow::MixingConsoleWindow(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mixingConsoleWindow"));
    setWindowTitle(tr("Mixing Console"));
    setWindowFlags(Qt::Window);
    resize(860, 480);
    buildUi();
}

void MixingConsoleWindow::setProject(ProjectModel *project)
{
    m_project = project;
}

void MixingConsoleWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshFromProject();
}

void MixingConsoleWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top toolbar — Vegas: format | view/gear/monitor | Insert…
    auto *top = new QWidget(this);
    top->setObjectName(QStringLiteral("mixerTopBar"));
    top->setFixedHeight(26);
    auto *topLay = new QHBoxLayout(top);
    topLay->setContentsMargins(6, 2, 4, 2);
    topLay->setSpacing(3);

    auto *fmt = new QLabel(tr("48 000 Hz; 16 Bit"), top);
    fmt->setObjectName(QStringLiteral("mixerFormatLabel"));
    topLay->addWidget(fmt);
    topLay->addStretch(1);

    auto addIconBtn = [top, topLay](const QString &tip, const QString &svg) {
        QToolButton *b = IconFactory::toolButton(top, tip, svg);
        b->setFixedSize(22, 22);
        b->setIconSize(QSize(13, 13));
        topLay->addWidget(b);
        return b;
    };

    auto *viewBtn = addIconBtn(tr("View"), IconFactory::svgViews());
    auto *gearBtn = addIconBtn(tr("Mixing Console Properties"), IconFactory::svgGear());
    addIconBtn(tr("Downmix Monitor Output"), IconFactory::svgDownmix());
    addIconBtn(tr("Dim Output"), IconFactory::svgAudioDevice());

    auto addInsertBtn = [top, topLay](const QString &text, const QString &svg) {
        auto *b = new QToolButton(top);
        b->setObjectName(QStringLiteral("mixerInsertBtn"));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIcon(IconFactory::iconFromSvgBody(svg, 12));
        b->setIconSize(QSize(12, 12));
        b->setText(text);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        topLay->addWidget(b);
        return b;
    };
    auto *insertAudioBtn = addInsertBtn(tr("Insert Audio Track"), IconFactory::svgWaveform());
    auto *insertFxBtn = addInsertBtn(tr("Insert Assignable FX..."), IconFactory::svgFx());
    auto *insertBusBtn = addInsertBtn(tr("Insert Bus"), IconFactory::svgGroup());
    auto *insertInputBusBtn = addInsertBtn(tr("Insert Input Bus"), IconFactory::svgAudioDevice());
    connect(insertAudioBtn, &QToolButton::clicked, this, &MixingConsoleWindow::insertAudioTrack);
    connect(insertFxBtn, &QToolButton::clicked, this, &MixingConsoleWindow::insertAssignableFx);
    connect(insertBusBtn, &QToolButton::clicked, this, &MixingConsoleWindow::insertBus);
    connect(insertInputBusBtn, &QToolButton::clicked, this, &MixingConsoleWindow::insertInputBus);
    root->addWidget(top);

    // View menu (Channel List / View Controls)
    auto *viewMenu = new QMenu(this);
    m_actChannelList = viewMenu->addAction(tr("Channel List"));
    m_actChannelList->setCheckable(true);
    m_actChannelList->setChecked(true);
    m_actChannelList->setShortcut(QKeySequence(QStringLiteral("Shift+C")));
    m_actViewControls = viewMenu->addAction(tr("View Controls"));
    m_actViewControls->setCheckable(true);
    m_actViewControls->setChecked(true);
    m_actViewControls->setShortcut(QKeySequence(QStringLiteral("Shift+R")));
    viewBtn->setMenu(viewMenu);
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    connect(m_actChannelList, &QAction::toggled, this, [this](bool on) {
        m_showChannelList = on;
        applyChromeVisibility();
    });
    connect(m_actViewControls, &QAction::toggled, this, [this](bool on) {
        m_showViewControls = on;
        applyChromeVisibility();
    });

    // Gear / properties menu
    auto *gearMenu = new QMenu(this);
    gearMenu->addAction(tr("Audio Properties..."));
    auto *insertMenu = gearMenu->addMenu(tr("Insert"));
    insertMenu->addAction(tr("Audio Track"), this, &MixingConsoleWindow::insertAudioTrack);
    insertMenu->addAction(tr("Assignable FX..."), this, &MixingConsoleWindow::insertAssignableFx);
    insertMenu->addAction(tr("Bus"), this, &MixingConsoleWindow::insertBus);
    insertMenu->addAction(tr("Input Bus"), this, &MixingConsoleWindow::insertInputBus);
    gearMenu->addSeparator();
    auto *actDownmix = gearMenu->addAction(tr("Downmix Monitor Output"));
    actDownmix->setCheckable(true);
    auto *actDim = gearMenu->addAction(tr("Dim Output"));
    actDim->setCheckable(true);
    gearMenu->addSeparator();

    auto *showChMenu = gearMenu->addMenu(tr("Show Channels"));
    showChMenu->addAction(tr("All"), this, &MixingConsoleWindow::showAllChannels);
    showChMenu->addAction(tr("Audio Tracks"));
    showChMenu->addAction(tr("Audio Busses"));
    showChMenu->addAction(tr("Input Busses"));
    showChMenu->addAction(tr("Assignable FX Busses"));
    showChMenu->addAction(tr("Master Bus"));
    showChMenu->addAction(tr("Preview Bus"));

    auto *regionsMenu = gearMenu->addMenu(tr("Show Control Regions"));
    for (const QString &r : {tr("Insert FX"), tr("Sends"), tr("I/O"), tr("VU Meters"), tr("Peak Meters"),
                             tr("Faders")}) {
        auto *a = regionsMenu->addAction(r);
        a->setCheckable(true);
        a->setChecked(r == tr("I/O") || r == tr("Faders") || r == tr("Peak Meters"));
    }

    auto *actLabel = gearMenu->addAction(tr("Label Control Regions"));
    actLabel->setShortcut(QKeySequence(QStringLiteral("Shift+L")));
    actLabel->setCheckable(true);

    m_actFaderTicks = gearMenu->addAction(tr("Fader Ticks"));
    m_actFaderTicks->setCheckable(true);
    m_actFaderTicks->setChecked(true);
    m_actFaderTicks->setShortcut(QKeySequence(QStringLiteral("Shift+T")));
    connect(m_actFaderTicks, &QAction::toggled, this, [this](bool on) {
        m_faderTicks = on;
        for (MixerChannelStrip *s : m_strips) {
            if (s) {
                s->setFaderTicks(on);
            }
        }
    });

    gearMenu->addSeparator();
    auto *widthMenu = gearMenu->addMenu(tr("Channel Width"));
    m_widthGroup = new QActionGroup(this);
    m_widthGroup->setExclusive(true);
    auto *actNarrow = widthMenu->addAction(tr("Narrow"));
    actNarrow->setCheckable(true);
    actNarrow->setShortcut(QKeySequence(QStringLiteral("N")));
    auto *actDefault = widthMenu->addAction(tr("Default"));
    actDefault->setCheckable(true);
    actDefault->setChecked(true);
    actDefault->setShortcut(QKeySequence(QStringLiteral("D")));
    auto *actWide = widthMenu->addAction(tr("Wide"));
    actWide->setCheckable(true);
    actWide->setShortcut(QKeySequence(QStringLiteral("W")));
    m_widthGroup->addAction(actNarrow);
    m_widthGroup->addAction(actDefault);
    m_widthGroup->addAction(actWide);
    connect(actNarrow, &QAction::triggered, this, [this]() {
        m_channelWidth = ChannelWidth::Narrow;
        applyChannelWidth();
    });
    connect(actDefault, &QAction::triggered, this, [this]() {
        m_channelWidth = ChannelWidth::Default;
        applyChannelWidth();
    });
    connect(actWide, &QAction::triggered, this, [this]() {
        m_channelWidth = ChannelWidth::Wide;
        applyChannelWidth();
    });

    auto *meterMenu = gearMenu->addMenu(tr("Meter Layout"));
    meterMenu->addAction(tr("Reset Meter Clip"));
    meterMenu->addSeparator();
    auto *rangeGroup = new QActionGroup(this);
    rangeGroup->setExclusive(true);
    for (const QString &rng : {tr("-12 to 0 dB"), tr("-24 to 0 dB"), tr("-42 to 0 dB"), tr("-60 to 0 dB"),
                               tr("-90 to 0 dB"), tr("-120 to 0 dB"), tr("-138 to 0 dB")}) {
        auto *a = meterMenu->addAction(rng);
        a->setCheckable(true);
        if (rng.contains(QStringLiteral("-90"))) {
            a->setChecked(true);
        }
        rangeGroup->addAction(a);
    }
    meterMenu->addSeparator();
    for (const QString &t : {tr("Show Labels"), tr("Show Peaks"), tr("Hold Peaks"), tr("Hold Valleys")}) {
        auto *a = meterMenu->addAction(t);
        a->setCheckable(true);
        a->setChecked(t != tr("Hold Valleys"));
    }

    gearBtn->setMenu(gearMenu);
    gearBtn->setPopupMode(QToolButton::InstantPopup);

    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    // Left sidebar
    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName(QStringLiteral("mixerSidebar"));
    m_sidebar->setFixedWidth(138);
    auto *sideLay = new QVBoxLayout(m_sidebar);
    sideLay->setContentsMargins(3, 3, 3, 3);
    sideLay->setSpacing(2);

    m_channelList = new QListWidget(m_sidebar);
    m_channelList->setObjectName(QStringLiteral("mixerChannelList"));
    m_channelList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_channelList->setUniformItemSizes(true);
    m_channelList->setIconSize(QSize(14, 14));
    sideLay->addWidget(m_channelList, 1);
    connect(m_channelList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        const int busId = item->data(Qt::UserRole).toInt();
        if (busId > 0) {
            openAssignableFx(busId);
        }
    });

    m_viewControls = new QWidget(m_sidebar);
    m_viewControls->setObjectName(QStringLiteral("mixerViewControls"));
    auto *vcLay = new QVBoxLayout(m_viewControls);
    vcLay->setContentsMargins(0, 0, 0, 0);
    vcLay->setSpacing(2);

    auto *showAllBtn = new QPushButton(tr("Show All"), m_viewControls);
    showAllBtn->setObjectName(QStringLiteral("mixerShowAllBtn"));
    showAllBtn->setCursor(Qt::PointingHandCursor);
    showAllBtn->setFocusPolicy(Qt::NoFocus);
    vcLay->addWidget(showAllBtn);
    connect(showAllBtn, &QPushButton::clicked, this, &MixingConsoleWindow::showAllChannels);

    m_filterGroup = new QButtonGroup(this);
    m_filterGroup->setExclusive(false);
    const QStringList filters = {tr("Audio Tracks"), tr("Audio Busses"), tr("Input Busses"),
                                 tr("Assignable FX"), tr("Master Bus"), tr("Preview Bus")};
    for (int i = 0; i < filters.size(); ++i) {
        auto *b = new QPushButton(filters.at(i), m_viewControls);
        b->setObjectName(QStringLiteral("mixerFilterBtn"));
        b->setCheckable(true);
        b->setChecked(i == 0 || i == 4); // Audio Tracks + Master Bus
        if (i == 5) {
            b->setEnabled(false); // Preview Bus — reserved like Vegas when unused
        }
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        m_filterGroup->addButton(b, i);
        vcLay->addWidget(b);
        connect(b, &QPushButton::toggled, this, &MixingConsoleWindow::applyFilter);
    }
    sideLay->addWidget(m_viewControls);

    auto *zoomRow = new QHBoxLayout();
    zoomRow->setSpacing(2);
    auto *zOut = IconFactory::toolButton(m_sidebar, tr("Zoom Out"), IconFactory::svgZoom());
    zOut->setFixedSize(18, 18);
    zOut->setIconSize(QSize(11, 11));
    m_zoomSlider = new QSlider(Qt::Horizontal, m_sidebar);
    m_zoomSlider->setObjectName(QStringLiteral("mixerZoom"));
    m_zoomSlider->setRange(70, 140);
    m_zoomSlider->setValue(100);
    auto *zIn = IconFactory::toolButton(m_sidebar, tr("Zoom In"), IconFactory::svgZoom());
    zIn->setFixedSize(18, 18);
    zIn->setIconSize(QSize(11, 11));
    zoomRow->addWidget(zOut);
    zoomRow->addWidget(m_zoomSlider, 1);
    zoomRow->addWidget(zIn);
    sideLay->addLayout(zoomRow);
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int) { applyChannelWidth(); });
    connect(zOut, &QToolButton::clicked, this, [this]() {
        m_zoomSlider->setValue(m_zoomSlider->value() - 10);
    });
    connect(zIn, &QToolButton::clicked, this, [this]() {
        m_zoomSlider->setValue(m_zoomSlider->value() + 10);
    });

    body->addWidget(m_sidebar);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("mixerStripsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_stripsHost = new QWidget(scroll);
    m_stripsHost->setObjectName(QStringLiteral("mixerStripsHost"));
    m_stripsLay = new QHBoxLayout(m_stripsHost);
    m_stripsLay->setContentsMargins(2, 2, 2, 2);
    m_stripsLay->setSpacing(2);
    m_stripsLay->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_stripsLay->addStretch(1);
    scroll->setWidget(m_stripsHost);
    body->addWidget(scroll, 1);

    m_insertMarker = new QFrame(m_stripsHost);
    m_insertMarker->setObjectName(QStringLiteral("mixerInsertMarker"));
    m_insertMarker->setFixedWidth(3);
    m_insertMarker->setStyleSheet(QStringLiteral("background:#0078d7;"));
    m_insertMarker->hide();

    root->addLayout(body, 1);
}

void MixingConsoleWindow::refreshFromProject()
{
    rebuildStrips();
}

void MixingConsoleWindow::setMasterMeter(float peakL, float peakR)
{
    for (MixerChannelStrip *s : m_strips) {
        if (s && s->isMaster()) {
            s->setMeters(peakL, peakR);
            break;
        }
    }
}

void MixingConsoleWindow::setTrackMeter(int trackId, float peakL, float peakR)
{
    for (MixerChannelStrip *s : m_strips) {
        if (s && s->trackId() == trackId) {
            s->setMeters(peakL, peakR);
            break;
        }
    }
}

void MixingConsoleWindow::applyChannelWidth()
{
    int base = kStripDefault;
    switch (m_channelWidth) {
    case ChannelWidth::Narrow:
        base = kStripNarrow;
        break;
    case ChannelWidth::Wide:
        base = kStripWide;
        break;
    case ChannelWidth::Default:
    default:
        base = kStripDefault;
        break;
    }
    const double zoom = m_zoomSlider ? (m_zoomSlider->value() / 100.0) : 1.0;
    for (MixerChannelStrip *strip : m_strips) {
        if (!strip) {
            continue;
        }
        const int w = int((strip->kind() == MixerChannelStrip::Kind::MasterBus ? base + kMasterExtra : base) * zoom);
        strip->setBaseWidth(w);
    }
}

void MixingConsoleWindow::applyChromeVisibility()
{
    if (m_channelList) {
        m_channelList->setVisible(m_showChannelList);
    }
    if (m_viewControls) {
        m_viewControls->setVisible(m_showViewControls);
    }
    if (m_sidebar) {
        m_sidebar->setVisible(m_showChannelList || m_showViewControls);
    }
}

void MixingConsoleWindow::showAllChannels()
{
    if (!m_filterGroup) {
        return;
    }
    for (QAbstractButton *b : m_filterGroup->buttons()) {
        if (b && b->isEnabled()) {
            b->setChecked(true);
        }
    }
}

void MixingConsoleWindow::insertAudioTrack()
{
    if (!m_project) {
        return;
    }
    emit documentEditBegan();
    m_project->addTrack(TrackKind::Audio);
    emit documentEditCommitted(tr("Insert Audio Track"));
    if (m_filterGroup && m_filterGroup->button(0) && !m_filterGroup->button(0)->isChecked()) {
        m_filterGroup->button(0)->setChecked(true);
    }
    rebuildStrips();
    emit tracksChanged();
}

void MixingConsoleWindow::insertAssignableFx()
{
    if (!m_project) {
        return;
    }
    const int nextNum = m_project->assignableFxBuses().size() + 1;
    PluginChooserDialog chooser(nullptr, this);
    chooser.setAudioMode(true);
    chooser.setWindowTitle(tr("Plug-In Chooser - FX %1").arg(nextNum));
    if (chooser.exec() != QDialog::Accepted) {
        return;
    }
    const QVector<AudioPluginDesc> picked = chooser.selectedAudioPlugins();
    if (picked.isEmpty()) {
        return;
    }

    QVector<FxSlot> chain;
    chain.reserve(picked.size());
    for (const AudioPluginDesc &d : picked) {
        FxSlot slot;
        CompositePluginHost::instance().createInstance(d, &slot);
        chain.push_back(slot);
    }

    emit documentEditBegan();
    const int idx = m_project->addAssignableFxBus(chain);
    emit documentEditCommitted(tr("Insert Assignable FX"));
    if (m_filterGroup && m_filterGroup->button(3) && !m_filterGroup->button(3)->isChecked()) {
        m_filterGroup->button(3)->setChecked(true);
    }
    rebuildStrips();

    if (idx >= 0 && idx < m_project->assignableFxBuses().size()) {
        openAssignableFx(m_project->assignableFxBuses()[idx].id);
    }
}

void MixingConsoleWindow::insertBus()
{
    if (!m_project) {
        return;
    }
    emit documentEditBegan();
    m_project->addMixerBus();
    emit documentEditCommitted(tr("Insert Bus"));
    // Audio Busses filter (index 1)
    if (m_filterGroup && m_filterGroup->button(1) && !m_filterGroup->button(1)->isChecked()) {
        m_filterGroup->button(1)->setChecked(true);
    }
    rebuildStrips();
}

void MixingConsoleWindow::insertInputBus()
{
    if (!m_project) {
        return;
    }
    emit documentEditBegan();
    m_project->addMixerInputBus();
    emit documentEditCommitted(tr("Insert Input Bus"));
    // Input Busses filter (index 2)
    if (m_filterGroup && m_filterGroup->button(2) && !m_filterGroup->button(2)->isChecked()) {
        m_filterGroup->button(2)->setChecked(true);
    }
    rebuildStrips();
}

void MixingConsoleWindow::openAssignableFx(int busId)
{
    if (!m_project) {
        return;
    }
    AssignableFxBus *bus = m_project->findAssignableFxBus(busId);
    if (!bus) {
        return;
    }
    emit documentEditBegan();
    AudioEventFxDialog dlg(this);
    dlg.setChain(&bus->fxChain, QStringLiteral("%1  %2").arg(bus->number).arg(bus->name));
    dlg.exec();
    if (!bus->fxChain.isEmpty()) {
        bus->name = bus->fxChain.first().displayName;
    }
    emit documentEditCommitted(tr("Assignable FX"));
    rebuildStrips();
}

void MixingConsoleWindow::rebuildStrips()
{
    clearReorderUi();
    while (QLayoutItem *it = m_stripsLay->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            if (w == m_insertMarker) {
                continue;
            }
            w->deleteLater();
        }
        delete it;
    }
    m_strips.clear();
    m_channelList->clear();

    if (!m_project) {
        auto *strip = addStrip(tr("Audio"), tr("Sound Mapper"), tr("Master"), audioTrackSwatch(1),
                               int(MixerChannelStrip::Kind::AudioTrack), 1);
        strip->setProperty("filterKind", int(MixerChannelStrip::Kind::AudioTrack));
        m_stripsLay->addStretch(1);
        applyFilter();
        applyChannelWidth();
        return;
    }

    m_project->ensureMixerStripOrder();

    // Show bus/input/FX filters when those channels exist
    if (m_filterGroup) {
        if (!m_project->mixerBuses().isEmpty() && m_filterGroup->button(1)) {
            m_filterGroup->button(1)->setChecked(true);
        }
        if (!m_project->mixerInputBuses().isEmpty() && m_filterGroup->button(2)) {
            m_filterGroup->button(2)->setChecked(true);
        }
        if (!m_project->assignableFxBuses().isEmpty() && m_filterGroup->button(3)) {
            m_filterGroup->button(3)->setChecked(true);
        }
    }

    int audioOrdinal = 0;
    const QColor busSw = QColor(0x48, 0x78, 0xb0);
    const QColor inSw = QColor(0x3a, 0x6a, 0xa0);
    const QColor fxSw = QColor(0x50, 0x88, 0xc8);

    for (const MixerStripRef &ref : m_project->mixerStripOrder()) {
        MixerChannelStrip *strip = nullptr;
        QListWidgetItem *item = nullptr;

        if (ref.kind == MixerStripKind::AudioTrack) {
            const Track *audioTrack = nullptr;
            for (const Track &t : m_project->tracks()) {
                if (t.kind == TrackKind::Audio && t.id == ref.id) {
                    audioTrack = &t;
                    break;
                }
            }
            if (!audioTrack) {
                continue;
            }
            ++audioOrdinal;
            const QColor sw = audioTrackSwatch(audioOrdinal);
            const QString listName =
                audioTrack->name.isEmpty() ? tr("Track %1").arg(audioOrdinal) : audioTrack->name;
            strip = addStrip(tr("Audio"), tr("Sound Mapper"), tr("Master"), sw,
                             int(MixerChannelStrip::Kind::AudioTrack), audioOrdinal);
            strip->setProperty("filterKind", int(MixerChannelStrip::Kind::AudioTrack));
            strip->setProperty("trackId", audioTrack->id);
            item = new QListWidgetItem(QStringLiteral("%1  %2").arg(audioOrdinal).arg(listName));
            item->setIcon(QIcon(colorBadge(sw, 14, QString::number(audioOrdinal))));
        } else if (ref.kind == MixerStripKind::AudioBus) {
            const MixerBus *bus = m_project->findMixerBus(ref.id);
            if (!bus) {
                continue;
            }
            strip = addStrip(tr("Bus"), bus->name, tr("Master"), busSw,
                             int(MixerChannelStrip::Kind::AudioBus), bus->letterIndex);
            strip->setProperty("filterKind", int(MixerChannelStrip::Kind::AudioBus));
            strip->setProperty("mixerBusId", bus->id);
            const QString letter = (bus->letterIndex >= 0 && bus->letterIndex < 26)
                                       ? QString(QChar(QLatin1Char('A' + bus->letterIndex)))
                                       : QString::number(bus->letterIndex + 1);
            item = new QListWidgetItem(QStringLiteral("%1  %2").arg(letter, bus->name));
            item->setIcon(QIcon(colorBadge(busSw, 14, letter)));
        } else if (ref.kind == MixerStripKind::InputBus) {
            const MixerInputBus *bus = m_project->findMixerInputBus(ref.id);
            if (!bus) {
                continue;
            }
            strip = addStrip(tr("Input"), bus->name, QString(), inSw,
                             int(MixerChannelStrip::Kind::InputBus), bus->letterIndex);
            strip->setProperty("filterKind", int(MixerChannelStrip::Kind::InputBus));
            strip->setProperty("mixerInputBusId", bus->id);
            const QString letter = (bus->letterIndex >= 0 && bus->letterIndex < 26)
                                       ? QString(QChar(QLatin1Char('A' + bus->letterIndex)))
                                       : QString::number(bus->letterIndex + 1);
            item = new QListWidgetItem(QStringLiteral("%1  %2").arg(letter, bus->name));
            item->setIcon(QIcon(colorBadge(inSw, 14, letter)));
        } else if (ref.kind == MixerStripKind::AssignableFx) {
            const AssignableFxBus *bus = m_project->findAssignableFxBus(ref.id);
            if (!bus) {
                continue;
            }
            strip = addStrip(tr("FX"), bus->name, tr("Master"), fxSw,
                             int(MixerChannelStrip::Kind::AssignableFx), bus->number);
            strip->setProperty("filterKind", int(MixerChannelStrip::Kind::AssignableFx));
            strip->setProperty("assignableFxId", bus->id);
            item = new QListWidgetItem(QStringLiteral("%1  %2").arg(bus->number).arg(bus->name));
            item->setIcon(QIcon(colorBadge(fxSw, 14, QString::number(bus->number))));
            item->setData(Qt::UserRole, bus->id);
        } else if (ref.kind == MixerStripKind::Master) {
            strip = addStrip(tr("Master"), QString(), QString(), QColor(0xe0, 0xe0, 0xe0),
                             int(MixerChannelStrip::Kind::MasterBus));
            strip->setProperty("filterKind", int(MixerChannelStrip::Kind::MasterBus));
            item = new QListWidgetItem(tr("Master"));
            item->setIcon(QIcon(colorBadge(QColor(0x40, 0x70, 0xb0), 14)));
        }

        if (!strip || !item) {
            continue;
        }
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        m_channelList->addItem(item);

        if (ref.kind == MixerStripKind::AudioTrack) {
            strip->setModelIds(strip->property("trackId").toInt(), -1, false);
        } else if (ref.kind == MixerStripKind::AudioBus) {
            strip->setModelIds(-1, strip->property("mixerBusId").toInt(), false);
        } else if (ref.kind == MixerStripKind::Master) {
            strip->setModelIds(-1, -1, true);
        }
        strip->loadFromModel(m_project);
        strip->bindToModel(m_project, this);

        strip->setProperty("orderIndex", m_strips.size() - 1);
        strip->setCursor(Qt::SizeHorCursor);
        strip->installEventFilter(this);
    }

    if (m_strips.isEmpty()) {
        auto *strip = addStrip(tr("Audio"), tr("Sound Mapper"), tr("Master"), audioTrackSwatch(1),
                               int(MixerChannelStrip::Kind::AudioTrack), 1);
        strip->setProperty("filterKind", int(MixerChannelStrip::Kind::AudioTrack));
    }

    if (m_insertMarker) {
        m_insertMarker->setParent(m_stripsHost);
        m_insertMarker->hide();
    }
    m_stripsLay->addStretch(1);
    applyFilter();
    applyChannelWidth();
}

MixerChannelStrip *MixingConsoleWindow::addStrip(const QString &title, const QString &subtitle,
                                                 const QString &route, const QColor &swatch, int kind,
                                                 int trackNumber)
{
    int base = kStripDefault;
    switch (m_channelWidth) {
    case ChannelWidth::Narrow:
        base = kStripNarrow;
        break;
    case ChannelWidth::Wide:
        base = kStripWide;
        break;
    default:
        break;
    }
    const auto k = static_cast<MixerChannelStrip::Kind>(kind);
    if (k == MixerChannelStrip::Kind::MasterBus) {
        base += kMasterExtra;
    }

    auto *strip = new MixerChannelStrip(title, subtitle, route, swatch, k, trackNumber, m_faderTicks,
                                        base, m_stripsHost);
    m_strips.push_back(strip);
    // Insert before trailing stretch
    int stretchAt = m_stripsLay->count();
    for (int i = 0; i < m_stripsLay->count(); ++i) {
        if (m_stripsLay->itemAt(i)->spacerItem()) {
            stretchAt = i;
            break;
        }
    }
    m_stripsLay->insertWidget(stretchAt, strip);
    return strip;
}

void MixingConsoleWindow::applyFilter()
{
    const bool showTracks = m_filterGroup->button(0) && m_filterGroup->button(0)->isChecked();
    const bool showBusses = m_filterGroup->button(1) && m_filterGroup->button(1)->isChecked();
    const bool showInputBusses = m_filterGroup->button(2) && m_filterGroup->button(2)->isChecked();
    const bool showFx = m_filterGroup->button(3) && m_filterGroup->button(3)->isChecked();
    const bool showMaster = m_filterGroup->button(4) && m_filterGroup->button(4)->isChecked();
    for (MixerChannelStrip *strip : m_strips) {
        if (!strip) {
            continue;
        }
        const auto kind = static_cast<MixerChannelStrip::Kind>(strip->property("filterKind").toInt());
        bool visible = true;
        if (kind == MixerChannelStrip::Kind::AudioTrack) {
            visible = showTracks;
        } else if (kind == MixerChannelStrip::Kind::AudioBus) {
            visible = showBusses;
        } else if (kind == MixerChannelStrip::Kind::InputBus) {
            visible = showInputBusses;
        } else if (kind == MixerChannelStrip::Kind::AssignableFx) {
            visible = showFx;
        } else if (kind == MixerChannelStrip::Kind::MasterBus) {
            visible = showMaster;
        }
        strip->setVisible(visible);
    }
}

int MixingConsoleWindow::stripIndexAtX(int x) const
{
    for (int i = 0; i < m_strips.size(); ++i) {
        MixerChannelStrip *s = m_strips[i];
        if (!s || !s->isVisible()) {
            continue;
        }
        const QRect g = s->geometry();
        if (x >= g.left() && x < g.right()) {
            return i;
        }
    }
    return -1;
}

int MixingConsoleWindow::insertIndexAtX(int x) const
{
    // Insertion index in full order (including hidden) based on visible strip midpoints
    int visibleBefore = 0;
    for (int i = 0; i < m_strips.size(); ++i) {
        MixerChannelStrip *s = m_strips[i];
        if (!s || !s->isVisible()) {
            continue;
        }
        const int mid = s->geometry().center().x();
        if (x < mid) {
            return i;
        }
        visibleBefore = i + 1;
    }
    return m_strips.size();
    Q_UNUSED(visibleBefore);
}

void MixingConsoleWindow::updateReorderGhost(int insertIndex)
{
    if (!m_insertMarker || !m_stripsHost) {
        return;
    }
    int x = 2;
    if (insertIndex <= 0) {
        if (!m_strips.isEmpty() && m_strips.first()) {
            x = m_strips.first()->geometry().left();
        }
    } else if (insertIndex >= m_strips.size()) {
        for (int i = m_strips.size() - 1; i >= 0; --i) {
            if (m_strips[i] && m_strips[i]->isVisible()) {
                x = m_strips[i]->geometry().right();
                break;
            }
        }
    } else if (m_strips[insertIndex]) {
        x = m_strips[insertIndex]->geometry().left();
    }
    m_insertMarker->setGeometry(x - 1, 2, 3, qMax(40, m_stripsHost->height() - 4));
    m_insertMarker->show();
    m_insertMarker->raise();
}

void MixingConsoleWindow::clearReorderUi()
{
    m_reordering = false;
    m_reorderFrom = -1;
    m_pressStrip = nullptr;
    if (m_insertMarker) {
        m_insertMarker->hide();
    }
}

bool MixingConsoleWindow::eventFilter(QObject *watched, QEvent *event)
{
    MixerChannelStrip *strip = nullptr;
    for (MixerChannelStrip *s : m_strips) {
        if (s == watched) {
            strip = s;
            break;
        }
    }
    if (!strip || !m_project) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            m_pressPos = me->globalPosition().toPoint();
            m_pressStrip = strip;
            m_reorderFrom = m_strips.indexOf(strip);
            m_reordering = false;
        }
    } else if (event->type() == QEvent::MouseMove && m_pressStrip == strip) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!(me->buttons() & Qt::LeftButton)) {
            return QWidget::eventFilter(watched, event);
        }
        if (!m_reordering
            && (me->globalPosition().toPoint() - m_pressPos).manhattanLength()
                   >= QApplication::startDragDistance()) {
            m_reordering = true;
        }
        if (m_reordering) {
            const QPoint local = m_stripsHost->mapFromGlobal(me->globalPosition().toPoint());
            updateReorderGhost(insertIndexAtX(local.x()));
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && m_pressStrip == strip) {
            const bool didReorder = m_reordering && m_reorderFrom >= 0;
            if (didReorder) {
                const QPoint local = m_stripsHost->mapFromGlobal(me->globalPosition().toPoint());
                const int insertAt = insertIndexAtX(local.x());
                const int from = m_reorderFrom;
                clearReorderUi();
                emit documentEditBegan();
                if (m_project->moveMixerStrip(from, insertAt)) {
                    emit documentEditCommitted(tr("Reorder Mixer"));
                    rebuildStrips();
                } else {
                    emit documentEditCommitted(tr("Reorder Mixer"));
                }
                return true;
            }
            clearReorderUi();
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace openvegas
