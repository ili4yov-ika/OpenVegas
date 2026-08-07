#include "ui/VideoTrackFxDialog.h"

#include "audio/BuiltinDsp.h"
#include "plugins/VegasVideoPluginCatalog.h"
#include "ui/AudioEventFxDialog.h"
#include "ui/KeyframeLaneWidgets.h"
#include "ui/PluginChooserDialog.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

QPushButton *makeIcoBtn(QWidget *parent, const QString &text, const QString &tip)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QStringLiteral("pcIcoBtn"));
    b->setToolTip(tip);
    b->setFixedHeight(20);
    b->setMinimumWidth(22);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

} // namespace

VideoTrackFxDialog::VideoTrackFxDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Video Track FX"));
    resize(760, 520);
    buildUi();
}

void VideoTrackFxDialog::setTrack(Track *track, double durationSec, double playheadSec)
{
    m_track = track;
    m_durationSec = std::max(0.5, durationSec);
    m_playheadSec = std::clamp(playheadSec, 0.0, m_durationSec);
    m_selectedFx = 0;
    m_kfFocusFx = -1;
    m_kfParamKey.clear();
    if (m_subtitle && track) {
        m_subtitle->setText(tr("Video Track FX: %1").arg(track->name));
    }
    rebuildChain();
}

void VideoTrackFxDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *sub = new QWidget(this);
    sub->setObjectName(QStringLiteral("pcSubheader"));
    sub->setFixedHeight(28);
    auto *subLay = new QHBoxLayout(sub);
    subLay->setContentsMargins(8, 0, 8, 0);
    subLay->setSpacing(8);
    m_subtitle = new QLabel(tr("Video Track FX: "), sub);
    m_subtitle->setObjectName(QStringLiteral("pcFxName"));
    subLay->addWidget(m_subtitle, 1);
    auto *addBtn = makeIcoBtn(sub, QStringLiteral("fx+"), tr("Add Plug-In"));
    auto *remBtn = makeIcoBtn(sub, QStringLiteral("fx×"), tr("Remove Plug-In"));
    subLay->addWidget(addBtn);
    subLay->addWidget(remBtn);
    root->addWidget(sub);

    auto *chainRow = new QWidget(this);
    chainRow->setObjectName(QStringLiteral("aefxChainRow"));
    chainRow->setFixedHeight(34);
    auto *chainRowLay = new QHBoxLayout(chainRow);
    chainRowLay->setContentsMargins(4, 2, 4, 2);
    m_chainScroll = new QScrollArea(chainRow);
    m_chainScroll->setObjectName(QStringLiteral("aefxChainScroll"));
    m_chainScroll->setWidgetResizable(true);
    m_chainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chainScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chainScroll->setFixedHeight(30);
    m_chainScroll->setFrameShape(QFrame::NoFrame);
    m_chainHost = new QWidget(m_chainScroll);
    m_chainHost->setObjectName(QStringLiteral("aefxChainHost"));
    m_chainLay = new QHBoxLayout(m_chainHost);
    m_chainLay->setContentsMargins(6, 2, 6, 2);
    m_chainLay->setSpacing(0);
    m_chainLay->addStretch(1);
    m_chainScroll->setWidget(m_chainHost);
    chainRowLay->addWidget(m_chainScroll, 1);
    root->addWidget(chainRow);

    m_genericTitle = new QLabel(tr("Plug-In"), this);
    m_genericTitle->setObjectName(QStringLiteral("pcFxName"));
    m_genericHint = new QLabel(this);
    m_genericHint->setWordWrap(true);

    m_paramsHost = new QWidget(this);
    m_paramsLay = new QVBoxLayout(m_paramsHost);
    m_paramsLay->setContentsMargins(16, 16, 16, 16);
    m_paramsLay->addWidget(m_genericTitle);
    m_paramsLay->addWidget(m_genericHint);
    m_paramsLay->addSpacing(8);
    root->addWidget(m_paramsHost, 1);

    root->addWidget(buildKeyframePanel(), 0);

    connect(addBtn, &QPushButton::clicked, this, &VideoTrackFxDialog::addPlugins);
    connect(remBtn, &QPushButton::clicked, this, &VideoTrackFxDialog::removeSelected);
}

QWidget *VideoTrackFxDialog::buildKeyframePanel()
{
    auto *kf = new QWidget(this);
    kf->setObjectName(QStringLiteral("pcKf"));
    kf->setMinimumHeight(120);
    m_kfPanel = kf;
    auto *root = new QVBoxLayout(kf);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *headers = new QWidget(kf);
    headers->setFixedHeight(20);
    auto *hLay = new QHBoxLayout(headers);
    hLay->setContentsMargins(0, 0, 0, 0);
    auto *corner = new QWidget(headers);
    corner->setObjectName(QStringLiteral("pcKfCorner"));
    corner->setFixedWidth(140);
    hLay->addWidget(corner);
    m_kfRuler = new PanCropKeyframeRuler(headers);
    m_kfRuler->setOnScrub([this](double t) { setPlayheadSec(t); });
    hLay->addWidget(m_kfRuler, 1);
    root->addWidget(headers);

    m_kfLanesHost = new QWidget(kf);
    m_kfLanesLay = new QVBoxLayout(m_kfLanesHost);
    m_kfLanesLay->setContentsMargins(0, 0, 0, 0);
    m_kfLanesLay->setSpacing(0);
    root->addWidget(m_kfLanesHost, 1);

    auto *tb = new QWidget(kf);
    tb->setObjectName(QStringLiteral("pcKfToolbar"));
    auto *tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(8, 2, 8, 2);
    tbLay->setSpacing(4);
    auto *btnFirst = makeIcoBtn(tb, QStringLiteral("⏮"), tr("First Keyframe"));
    auto *btnPrev = makeIcoBtn(tb, QStringLiteral("◀"), tr("Previous Keyframe"));
    auto *btnNext = makeIcoBtn(tb, QStringLiteral("▶"), tr("Next Keyframe"));
    auto *btnLast = makeIcoBtn(tb, QStringLiteral("⏭"), tr("Last Keyframe"));
    auto *btnAdd = makeIcoBtn(tb, QStringLiteral("◆+"), tr("Create Keyframe"));
    auto *btnDel = makeIcoBtn(tb, QStringLiteral("◆×"), tr("Delete Keyframe"));
    for (auto *b : {btnFirst, btnPrev, btnNext, btnLast, btnAdd, btnDel}) {
        tbLay->addWidget(b);
    }
    tbLay->addSpacing(8);
    m_btnLanes = makeIcoBtn(tb, QStringLiteral("Lanes"), tr("Lanes"));
    m_btnCurves = makeIcoBtn(tb, QStringLiteral("Curves"), tr("Curves"));
    m_btnLanes->setCheckable(true);
    m_btnCurves->setCheckable(true);
    m_btnCurves->setChecked(true);
    m_btnLanes->setObjectName(QStringLiteral("pcKfModeBtn"));
    m_btnCurves->setObjectName(QStringLiteral("pcKfModeBtn"));
    tbLay->addWidget(m_btnLanes);
    tbLay->addWidget(m_btnCurves);
    tbLay->addStretch(1);
    m_kfTc = new QLabel(QStringLiteral("00:00:00,00"), tb);
    m_kfTc->setObjectName(QStringLiteral("pcKfTc"));
    tbLay->addWidget(m_kfTc);
    root->addWidget(tb);

    connect(btnFirst, &QPushButton::clicked, this, &VideoTrackFxDialog::navigateFirst);
    connect(btnPrev, &QPushButton::clicked, this, &VideoTrackFxDialog::navigatePrev);
    connect(btnNext, &QPushButton::clicked, this, &VideoTrackFxDialog::navigateNext);
    connect(btnLast, &QPushButton::clicked, this, &VideoTrackFxDialog::navigateLast);
    connect(btnAdd, &QPushButton::clicked, this, &VideoTrackFxDialog::addKeyframeAtPlayhead);
    connect(btnDel, &QPushButton::clicked, this, &VideoTrackFxDialog::deleteSelectedKeyframe);
    connect(m_btnLanes, &QPushButton::clicked, this, [this]() { setCurvesMode(false); });
    connect(m_btnCurves, &QPushButton::clicked, this, [this]() { setCurvesMode(true); });

    return kf;
}

void VideoTrackFxDialog::rebuildChain()
{
    if (!m_chainLay) {
        return;
    }
    while (QLayoutItem *it = m_chainLay->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    m_nodes.clear();
    if (!m_track || m_track->fxChain.isEmpty()) {
        m_selectedFx = -1;
        auto *hint = new QLabel(tr("(empty chain)"), m_chainHost);
        hint->setObjectName(QStringLiteral("aefxChainEmpty"));
        m_chainLay->addWidget(hint);
        m_chainLay->addStretch(1);
        rebuildParamsUi();
        rebuildKeyframeLanes();
        refreshKeyframeLanes();
        return;
    }
    auto *startDot = new QLabel(m_chainHost);
    startDot->setObjectName(QStringLiteral("aefxChainDot"));
    startDot->setFixedSize(8, 8);
    m_chainLay->addWidget(startDot, 0, Qt::AlignVCenter);
    auto *startLine = new QFrame(m_chainHost);
    startLine->setObjectName(QStringLiteral("aefxChainLine"));
    startLine->setFixedSize(10, 1);
    m_chainLay->addWidget(startLine, 0, Qt::AlignVCenter);
    for (int i = 0; i < m_track->fxChain.size(); ++i) {
        if (i > 0) {
            auto *line = new QFrame(m_chainHost);
            line->setObjectName(QStringLiteral("aefxChainLine"));
            line->setFixedSize(12, 1);
            m_chainLay->addWidget(line, 0, Qt::AlignVCenter);
        }
        auto *node = new FxChainNodeWidget(i, m_track->fxChain[i], m_chainHost);
        connect(node, &FxChainNodeWidget::selected, this, &VideoTrackFxDialog::selectPlugin);
        connect(node, &FxChainNodeWidget::bypassToggled, this, &VideoTrackFxDialog::setBypass);
        connect(node, &FxChainNodeWidget::moveRequested, this, &VideoTrackFxDialog::movePlugin);
        m_chainLay->addWidget(node, 0, Qt::AlignVCenter);
        m_nodes.push_back(node);
    }
    auto *endLine = new QFrame(m_chainHost);
    endLine->setObjectName(QStringLiteral("aefxChainLine"));
    endLine->setFixedSize(10, 1);
    m_chainLay->addWidget(endLine, 0, Qt::AlignVCenter);
    auto *endDot = new QLabel(m_chainHost);
    endDot->setObjectName(QStringLiteral("aefxChainDot"));
    endDot->setFixedSize(8, 8);
    m_chainLay->addWidget(endDot, 0, Qt::AlignVCenter);
    m_chainLay->addStretch(1);
    if (m_selectedFx < 0 || m_selectedFx >= m_track->fxChain.size()) {
        m_selectedFx = 0;
    }
    for (FxChainNodeWidget *n : m_nodes) {
        n->setSelected(n->index() == m_selectedFx);
    }
    rebuildParamsUi();
    rebuildKeyframeLanes();
    refreshKeyframeLanes();
}

void VideoTrackFxDialog::selectPlugin(int index)
{
    if (!m_track || index < 0 || index >= m_track->fxChain.size()) {
        return;
    }
    m_selectedFx = index;
    m_kfFocusFx = m_selectedFx;
    const auto infos = VegasVideoPluginCatalog::paramsInfoForSlot(m_track->fxChain[m_selectedFx]);
    if (m_kfParamKey.isEmpty() && !infos.isEmpty()) {
        m_kfParamKey = infos.first().name;
    }
    for (FxChainNodeWidget *n : m_nodes) {
        n->setSelected(n->index() == m_selectedFx);
    }
    rebuildParamsUi();
    rebuildKeyframeLanes();
    refreshKeyframeLanes();
}

void VideoTrackFxDialog::setBypass(int index, bool bypass)
{
    if (!m_track || index < 0 || index >= m_track->fxChain.size()) {
        return;
    }
    m_track->fxChain[index].bypass = bypass;
}

void VideoTrackFxDialog::movePlugin(int from, int insertBefore)
{
    if (!m_track || from < 0 || from >= m_track->fxChain.size()) {
        return;
    }
    insertBefore = std::clamp(insertBefore, 0, int(m_track->fxChain.size()));
    if (insertBefore == from || insertBefore == from + 1) {
        return;
    }
    const FxSlot slot = m_track->fxChain[from];
    m_track->fxChain.removeAt(from);
    if (insertBefore > from) {
        --insertBefore;
    }
    m_track->fxChain.insert(insertBefore, slot);
    m_selectedFx = insertBefore;
    rebuildChain();
}

void VideoTrackFxDialog::addPlugins()
{
    if (!m_track) {
        return;
    }
    PluginChooserDialog dlg(m_pluginScanner, this);
    dlg.setAudioMode(false);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dlg.selectedPluginName();
    if (name.isEmpty()) {
        return;
    }
    m_track->fxChain.push_back(VegasVideoPluginCatalog::slotFromDisplayName(name));
    rebuildChain();
    selectPlugin(m_track->fxChain.size() - 1);
}

void VideoTrackFxDialog::removeSelected()
{
    if (!m_track || m_selectedFx < 0 || m_selectedFx >= m_track->fxChain.size()) {
        return;
    }
    m_track->fxChain.removeAt(m_selectedFx);
    if (m_selectedFx >= m_track->fxChain.size()) {
        m_selectedFx = m_track->fxChain.size() - 1;
    }
    rebuildChain();
}

void VideoTrackFxDialog::rebuildParamsUi()
{
    if (!m_paramsLay) {
        return;
    }
    // Clear everything after title (index 0) and hint (index 1).
    while (m_paramsLay->count() > 2) {
        QLayoutItem *it = m_paramsLay->takeAt(2);
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    if (!m_track || m_selectedFx < 0 || m_selectedFx >= m_track->fxChain.size()) {
        if (m_genericTitle) {
            m_genericTitle->setText(tr("(no plug-in selected)"));
        }
        if (m_genericHint) {
            m_genericHint->clear();
        }
        return;
    }

    FxSlot &slot = m_track->fxChain[m_selectedFx];
    if (m_genericTitle) {
        m_genericTitle->setText(slot.displayName);
    }
    QVariantMap p = unpackFxParams(slot.state);
    const QVector<OfxParamInfo> infos = VegasVideoPluginCatalog::paramsInfoForSlot(slot);

    auto addSlider = [&](const QString &label, const QString &key, double def, double minV,
                         double maxV, int decimals) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, m_paramsHost));
        auto *sl = new QSlider(Qt::Horizontal, m_paramsHost);
        sl->setRange(0, 1000);
        const double cur = p.value(key, def).toDouble();
        sl->setValue(int(std::lround((cur - minV) / (maxV - minV) * 1000.0)));
        auto *spin = new QDoubleSpinBox(m_paramsHost);
        spin->setRange(minV, maxV);
        spin->setDecimals(decimals);
        spin->setValue(cur);
        spin->setFixedWidth(90);
        FxSlot *slotPtr = &slot;
        QObject::connect(sl, &QSlider::valueChanged, m_paramsHost,
                         [spin, minV, maxV, slotPtr, key](int v) {
                             const double x = minV + (maxV - minV) * (v / 1000.0);
                             spin->blockSignals(true);
                             spin->setValue(x);
                             spin->blockSignals(false);
                             QVariantMap m = unpackFxParams(slotPtr->state);
                             m.insert(key, x);
                             slotPtr->state = packFxParams(m);
                         });
        QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_paramsHost,
                         [sl, minV, maxV, slotPtr, key](double x) {
                             const int v = int(std::lround((x - minV) / (maxV - minV) * 1000.0));
                             sl->blockSignals(true);
                             sl->setValue(std::clamp(v, 0, 1000));
                             sl->blockSignals(false);
                             QVariantMap m = unpackFxParams(slotPtr->state);
                             m.insert(key, x);
                             slotPtr->state = packFxParams(m);
                         });
        row->addWidget(sl, 1);
        row->addWidget(spin);
        m_paramsLay->addLayout(row);
    };

    if (m_genericHint) {
        const bool usingRealParams =
            slot.format == PluginFormat::Ofx && !OfxHost::instance().paramsForSlot(slot).isEmpty();
        m_genericHint->setText(usingRealParams
            ? tr("Parameters from the installed OFX plug-in — applied in Video Preview.")
            : infos.isEmpty()
                ? tr("Effect is kept in the chain. If a matching OFX binary is found it is "
                     "processed; otherwise OpenVegas applies a CPU fallback when available.")
                : tr("Approximate parameters — applied via CPU fallback in Video Preview."));
    }
    for (const OfxParamInfo &info : infos) {
        addSlider(info.label, info.name, info.defaultValue, info.minValue, info.maxValue, 2);
    }
    m_paramsLay->addStretch(1);
}

QString VideoTrackFxDialog::fxMasterAutomationId(const FxSlot &slot) const
{
    return QStringLiteral("fx:%1:_master").arg(slot.hostKey.isEmpty() ? slot.pluginId : slot.hostKey);
}

QString VideoTrackFxDialog::fxParamAutomationId(const FxSlot &slot, const QString &paramKey) const
{
    return QStringLiteral("fx:%1:%2")
        .arg(slot.hostKey.isEmpty() ? slot.pluginId : slot.hostKey, paramKey);
}

AutomationLane *VideoTrackFxDialog::findAutomationLane(const QString &targetId)
{
    if (!m_track) {
        return nullptr;
    }
    for (AutomationLane &lane : m_track->automationLanes) {
        if (lane.targetId == targetId) {
            return &lane;
        }
    }
    return nullptr;
}

AutomationLane &VideoTrackFxDialog::ensureAutomationLane(const QString &targetId)
{
    if (AutomationLane *existing = findAutomationLane(targetId)) {
        return *existing;
    }
    AutomationLane lane;
    lane.targetId = targetId;
    m_track->automationLanes.push_back(lane);
    return m_track->automationLanes.last();
}

double VideoTrackFxDialog::currentParamValue(const FxSlot &slot, const QString &paramKey) const
{
    double def = 0.5;
    for (const OfxParamInfo &info : VegasVideoPluginCatalog::paramsInfoForSlot(slot)) {
        if (info.name == paramKey) {
            def = info.defaultValue;
            break;
        }
    }
    const QVariantMap m = unpackFxParams(slot.state);
    return m.value(paramKey, def).toDouble();
}

void VideoTrackFxDialog::setPlayheadSec(double sec)
{
    m_playheadSec = std::clamp(sec, 0.0, m_durationSec);
    if (m_kfTc) {
        m_kfTc->setText(formatTc(m_playheadSec));
    }
    if (m_kfRuler) {
        m_kfRuler->setRange(m_durationSec, m_playheadSec);
    }
    refreshKeyframeLanes();
}

void VideoTrackFxDialog::setCurvesMode(bool curves)
{
    m_kfCurves = curves;
    if (m_btnLanes) {
        m_btnLanes->setChecked(!curves);
    }
    if (m_btnCurves) {
        m_btnCurves->setChecked(curves);
    }
    rebuildKeyframeLanes();
    refreshKeyframeLanes();
}

void VideoTrackFxDialog::rebuildKeyframeLanes()
{
    if (!m_kfLanesLay || !m_track) {
        return;
    }
    while (QLayoutItem *it = m_kfLanesLay->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }

    if (m_kfFocusFx < 0 || m_kfFocusFx >= m_track->fxChain.size()) {
        m_kfFocusFx = m_selectedFx;
    }

    auto addLaneRow = [&](int fxIndex, const QString &label, const QString &paramKey, bool active,
                          bool paramRow) {
        auto *row = new QWidget(m_kfLanesHost);
        row->setObjectName(active ? QStringLiteral("pcKfRowActive") : QStringLiteral("pcKfRow"));
        row->setMinimumHeight(m_kfCurves && paramRow ? 48 : 28);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        auto *labHost = new QWidget(row);
        labHost->setObjectName(active ? QStringLiteral("pcKfLabelActive")
                                       : QStringLiteral("pcKfLabel"));
        labHost->setFixedWidth(140);
        auto *labLay = new QHBoxLayout(labHost);
        labLay->setContentsMargins(paramRow ? 18 : 8, 0, 8, 0);
        auto *name = new QLabel(label, labHost);
        name->setObjectName(QStringLiteral("pcKfLaneName"));
        labLay->addWidget(name, 1);
        rowLay->addWidget(labHost);
        auto *lane = new KeyframeLane(row);
        lane->setProperty("fxIndex", fxIndex);
        lane->setProperty("paramKey", paramKey);
        const int captureFx = fxIndex;
        const QString captureParam = paramKey;
        lane->setOnSelect([this, captureFx, captureParam](int i) {
            const bool focusChanged =
                (m_kfFocusFx != captureFx) || (m_kfParamKey != captureParam);
            m_kfFocusFx = captureFx;
            m_kfParamKey = captureParam;
            m_selectedFx = captureFx;
            for (FxChainNodeWidget *n : m_nodes) {
                n->setSelected(n->index() == m_selectedFx);
            }
            rebuildParamsUi();
            if (focusChanged) {
                rebuildKeyframeLanes();
            }
            selectKeyframeIndex(i);
        });
        lane->setOnScrub([this](double t) { setPlayheadSec(t); });
        lane->setOnMove([this, captureFx, captureParam](int i, double t, bool fin) {
            m_kfFocusFx = captureFx;
            m_kfParamKey = captureParam;
            moveKeyframe(i, t, fin);
        });
        lane->setOnCreateAt([this, captureFx, captureParam](double t) {
            m_kfFocusFx = captureFx;
            m_kfParamKey = captureParam;
            selectPlugin(captureFx);
            addKeyframeAtTime(t);
        });
        lane->setOnDeleteSelected([this, captureFx, captureParam]() {
            m_kfFocusFx = captureFx;
            m_kfParamKey = captureParam;
            deleteSelectedKeyframe();
        });
        rowLay->addWidget(lane, 1);
        m_kfLanesLay->addWidget(row);
        labHost->installEventFilter(new RowClickFilter(
            [this, captureFx, captureParam, paramRow]() {
                m_kfFocusFx = captureFx;
                if (!paramRow) {
                    const auto infos =
                        VegasVideoPluginCatalog::paramsInfoForSlot(m_track->fxChain[captureFx]);
                    if (!infos.isEmpty()
                        && (m_kfParamKey.isEmpty() || m_kfFocusFx != captureFx)) {
                        m_kfParamKey = infos.first().name;
                    }
                } else {
                    m_kfParamKey = captureParam;
                }
                selectPlugin(captureFx);
                rebuildKeyframeLanes();
                refreshKeyframeLanes();
            },
            labHost));
    };

    for (int i = 0; i < m_track->fxChain.size(); ++i) {
        const FxSlot &slot = m_track->fxChain[i];
        const bool active = (i == m_kfFocusFx);
        addLaneRow(i, slot.displayName, QStringLiteral("_master"), active, false);
        if (active) {
            for (const OfxParamInfo &info : VegasVideoPluginCatalog::paramsInfoForSlot(slot)) {
                const bool paramActive = (m_kfParamKey == info.name);
                addLaneRow(i, info.label, info.name, paramActive, true);
            }
        }
    }

    if (m_kfPanel) {
        const int n = m_kfLanesLay->count();
        m_kfPanel->setFixedHeight(
            std::clamp(20 + 28 * std::max(1, n) + 28 + (m_kfCurves ? 24 : 0), 120, 280));
    }
}

void VideoTrackFxDialog::refreshKeyframeLanes()
{
    if (!m_track || !m_kfLanesHost || !m_kfPanel) {
        return;
    }
    if (m_kfRuler) {
        m_kfRuler->setRange(m_durationSec, m_playheadSec);
    }
    if (m_kfTc) {
        m_kfTc->setText(formatTc(m_playheadSec));
    }

    const auto widgets = m_kfLanesHost->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (w->objectName() != QLatin1String("pcKfLane")) {
            continue;
        }
        auto *lane = static_cast<KeyframeLane *>(w);
        const int fxIndex = lane->property("fxIndex").toInt();
        const QString paramKey = lane->property("paramKey").toString();
        if (fxIndex < 0 || fxIndex >= m_track->fxChain.size()) {
            continue;
        }
        const FxSlot &slot = m_track->fxChain[fxIndex];
        const QString target = (paramKey == QLatin1String("_master"))
                                   ? fxMasterAutomationId(slot)
                                   : fxParamAutomationId(slot, paramKey);
        const AutomationLane *al = findAutomationLane(target);
        QVector<double> times;
        QVector<double> values;
        QVector<int> types;
        if (al) {
            for (const AutomationPoint &pt : al->points) {
                times.push_back(pt.timeSec);
                values.push_back(pt.value);
                types.push_back(int(VideoKeyframeType::Linear));
            }
        }
        const bool focus =
            (fxIndex == m_kfFocusFx
             && (paramKey == m_kfParamKey
                 || (paramKey == QLatin1String("_master") && m_kfParamKey.isEmpty())));
        const int sel = focus ? m_kfIndex : -1;
        const bool showCurve = m_kfCurves && paramKey != QLatin1String("_master") && !times.isEmpty();
        if (showCurve) {
            lane->setCurve(times, values, types, m_durationSec, sel, m_playheadSec, true);
        } else {
            lane->setTimes(times, types, m_durationSec, sel, m_playheadSec);
        }
    }
}

void VideoTrackFxDialog::selectKeyframeIndex(int pointIndex)
{
    m_kfIndex = std::max(0, pointIndex);
    if (!m_track || m_kfFocusFx < 0 || m_kfFocusFx >= m_track->fxChain.size()) {
        refreshKeyframeLanes();
        return;
    }
    const FxSlot &slot = m_track->fxChain[m_kfFocusFx];
    const QString key = m_kfParamKey.isEmpty() ? QStringLiteral("_master") : m_kfParamKey;
    const QString target =
        (key == QLatin1String("_master")) ? fxMasterAutomationId(slot) : fxParamAutomationId(slot, key);
    if (const AutomationLane *al = findAutomationLane(target)) {
        if (m_kfIndex >= 0 && m_kfIndex < al->points.size()) {
            setPlayheadSec(al->points[m_kfIndex].timeSec);
            return;
        }
    }
    refreshKeyframeLanes();
}

void VideoTrackFxDialog::navigateFirst()
{
    selectKeyframeIndex(0);
}

void VideoTrackFxDialog::navigatePrev()
{
    selectKeyframeIndex(std::max(0, m_kfIndex - 1));
}

void VideoTrackFxDialog::navigateNext()
{
    selectKeyframeIndex(m_kfIndex + 1);
}

void VideoTrackFxDialog::navigateLast()
{
    if (!m_track || m_kfFocusFx < 0 || m_kfFocusFx >= m_track->fxChain.size()) {
        return;
    }
    const FxSlot &slot = m_track->fxChain[m_kfFocusFx];
    const QString key = m_kfParamKey.isEmpty() ? QStringLiteral("_master") : m_kfParamKey;
    const QString target =
        (key == QLatin1String("_master")) ? fxMasterAutomationId(slot) : fxParamAutomationId(slot, key);
    const AutomationLane *al = findAutomationLane(target);
    if (!al || al->points.isEmpty()) {
        return;
    }
    selectKeyframeIndex(al->points.size() - 1);
}

void VideoTrackFxDialog::addKeyframeAtPlayhead()
{
    addKeyframeAtTime(m_playheadSec);
}

void VideoTrackFxDialog::addKeyframeAtTime(double timeSec)
{
    if (!m_track || m_kfFocusFx < 0 || m_kfFocusFx >= m_track->fxChain.size()) {
        return;
    }
    timeSec = std::clamp(timeSec, 0.0, m_durationSec);
    FxSlot &slot = m_track->fxChain[m_kfFocusFx];
    ensureFxHostKey(&slot);
    QString key = m_kfParamKey;
    if (key.isEmpty()) {
        const auto infos = VegasVideoPluginCatalog::paramsInfoForSlot(slot);
        key = infos.isEmpty() ? QStringLiteral("_master") : infos.first().name;
        m_kfParamKey = key;
    }
    // Always stamp a master marker too (Vegas-style plugin row diamonds).
    {
        AutomationLane &master = ensureAutomationLane(fxMasterAutomationId(slot));
        bool near = false;
        for (const AutomationPoint &pt : master.points) {
            if (std::abs(pt.timeSec - timeSec) < 1e-3) {
                near = true;
                break;
            }
        }
        if (!near) {
            AutomationPoint pt;
            pt.timeSec = timeSec;
            pt.value = 1.0;
            master.points.push_back(pt);
            std::sort(master.points.begin(), master.points.end(),
                      [](const AutomationPoint &a, const AutomationPoint &b) {
                          return a.timeSec < b.timeSec;
                      });
        }
    }
    if (key != QLatin1String("_master")) {
        AutomationLane &lane = ensureAutomationLane(fxParamAutomationId(slot, key));
        const double value = currentParamValue(slot, key);
        int replace = -1;
        for (int i = 0; i < lane.points.size(); ++i) {
            if (std::abs(lane.points[i].timeSec - timeSec) < 1e-3) {
                replace = i;
                break;
            }
        }
        if (replace >= 0) {
            lane.points[replace].value = value;
            m_kfIndex = replace;
        } else {
            AutomationPoint pt;
            pt.timeSec = timeSec;
            pt.value = value;
            lane.points.push_back(pt);
            std::sort(lane.points.begin(), lane.points.end(),
                      [](const AutomationPoint &a, const AutomationPoint &b) {
                          return a.timeSec < b.timeSec;
                      });
            for (int i = 0; i < lane.points.size(); ++i) {
                if (std::abs(lane.points[i].timeSec - timeSec) < 1e-3) {
                    m_kfIndex = i;
                    break;
                }
            }
        }
    }
    m_playheadSec = timeSec;
    rebuildKeyframeLanes();
    refreshKeyframeLanes();
}

void VideoTrackFxDialog::deleteSelectedKeyframe()
{
    if (!m_track || m_kfFocusFx < 0 || m_kfFocusFx >= m_track->fxChain.size()) {
        return;
    }
    const FxSlot &slot = m_track->fxChain[m_kfFocusFx];
    const QString key = m_kfParamKey.isEmpty() ? QStringLiteral("_master") : m_kfParamKey;
    const QString target =
        (key == QLatin1String("_master")) ? fxMasterAutomationId(slot) : fxParamAutomationId(slot, key);
    AutomationLane *al = findAutomationLane(target);
    if (!al || m_kfIndex < 0 || m_kfIndex >= al->points.size()) {
        return;
    }
    const double t = al->points[m_kfIndex].timeSec;
    al->points.removeAt(m_kfIndex);
    if (AutomationLane *master = findAutomationLane(fxMasterAutomationId(slot))) {
        for (int i = master->points.size() - 1; i >= 0; --i) {
            if (std::abs(master->points[i].timeSec - t) < 1e-3) {
                master->points.removeAt(i);
            }
        }
    }
    m_kfIndex = al->points.isEmpty() ? 0 : std::min(m_kfIndex, int(al->points.size()) - 1);
    rebuildKeyframeLanes();
    refreshKeyframeLanes();
}

void VideoTrackFxDialog::moveKeyframe(int pointIndex, double timeSec, bool finalize)
{
    if (!m_track || m_kfFocusFx < 0 || m_kfFocusFx >= m_track->fxChain.size()) {
        return;
    }
    timeSec = std::clamp(timeSec, 0.0, m_durationSec);
    const FxSlot &slot = m_track->fxChain[m_kfFocusFx];
    const QString key = m_kfParamKey.isEmpty() ? QStringLiteral("_master") : m_kfParamKey;
    const QString target =
        (key == QLatin1String("_master")) ? fxMasterAutomationId(slot) : fxParamAutomationId(slot, key);
    AutomationLane *al = findAutomationLane(target);
    if (!al || pointIndex < 0 || pointIndex >= al->points.size()) {
        return;
    }
    const double oldT = al->points[pointIndex].timeSec;
    al->points[pointIndex].timeSec = timeSec;
    if (finalize) {
        std::sort(al->points.begin(), al->points.end(),
                  [](const AutomationPoint &a, const AutomationPoint &b) {
                      return a.timeSec < b.timeSec;
                  });
        for (int i = 0; i < al->points.size(); ++i) {
            if (std::abs(al->points[i].timeSec - timeSec) < 1e-6) {
                m_kfIndex = i;
                break;
            }
        }
        if (AutomationLane *master = findAutomationLane(fxMasterAutomationId(slot))) {
            for (AutomationPoint &pt : master->points) {
                if (std::abs(pt.timeSec - oldT) < 1e-3) {
                    pt.timeSec = timeSec;
                }
            }
            std::sort(master->points.begin(), master->points.end(),
                      [](const AutomationPoint &a, const AutomationPoint &b) {
                          return a.timeSec < b.timeSec;
                      });
        }
    }
    m_playheadSec = timeSec;
    if (m_kfTc) {
        m_kfTc->setText(formatTc(m_playheadSec));
    }
    if (m_kfRuler) {
        m_kfRuler->setRange(m_durationSec, m_playheadSec);
    }
    if (finalize) {
        refreshKeyframeLanes();
    }
}

} // namespace openvegas
