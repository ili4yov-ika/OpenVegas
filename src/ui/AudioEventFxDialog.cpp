#include "ui/AudioEventFxDialog.h"
#include "ui/ColorGradingEditor.h"
#include "ui/PluginChooserDialog.h"
#include "ui/IconFactory.h"
#include "plugins/AudioPluginHost.h"
#include "audio/CompositePluginHost.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QCheckBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QFrame>
#include <QScrollBar>
#include <QVariantMap>
#include <QDataStream>
#include <QBuffer>
#include <QFormLayout>
#include <QStyle>
#include <QTabWidget>
#include <QButtonGroup>
#include <QPainterPath>
#include <QProgressBar>
#include <QSizePolicy>
#include <QFrame>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

QString formatName(PluginFormat f)
{
    switch (f) {
    case PluginFormat::Vst3:
        return QStringLiteral("VST3, 64 Bit");
    case PluginFormat::Vst2:
        return QStringLiteral("VST2, 64 Bit");
    case PluginFormat::Vst1:
        return QStringLiteral("VST, 64 Bit");
    case PluginFormat::Ofx:
        return QStringLiteral("OFX");
    case PluginFormat::DirectShow:
        return QStringLiteral("VEGAS Shared");
    default:
        return {};
    }
}

QVariantMap loadParams(const FxSlot &slot)
{
    QVariantMap m;
    if (slot.state.isEmpty()) {
        return m;
    }
    QDataStream in(slot.state);
    in.setVersion(QDataStream::Qt_6_0);
    in >> m;
    return m;
}

void saveParams(FxSlot *slot, const QVariantMap &m)
{
    if (!slot) {
        return;
    }
    QByteArray ba;
    QDataStream out(&ba, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << m;
    slot->state = ba;
}

double mapGet(const QVariantMap &m, const QString &k, double def)
{
    return m.contains(k) ? m.value(k).toDouble() : def;
}

bool mapGetB(const QVariantMap &m, const QString &k, bool def)
{
    return m.contains(k) ? m.value(k).toBool() : def;
}

int mapGetI(const QVariantMap &m, const QString &k, int def)
{
    return m.contains(k) ? m.value(k).toInt() : def;
}

void setParam(FxSlot *slot, const QString &key, const QVariant &value)
{
    QVariantMap m = loadParams(*slot);
    m.insert(key, value);
    saveParams(slot, m);
}

QToolButton *makeIconBtn(QWidget *parent, const QString &tip, const QString &svg)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("aefxIconBtn"));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setIcon(IconFactory::iconFromSvgBody(svg, 14));
    b->setIconSize(QSize(14, 14));
    b->setFixedSize(22, 22);
    return b;
}

QSlider *makeVFader(QWidget *parent)
{
    auto *s = new QSlider(Qt::Vertical, parent);
    s->setObjectName(QStringLiteral("aefxVFader"));
    s->setRange(0, 1000);
    s->setFixedHeight(120);
    s->setFixedWidth(28);
    return s;
}

} // namespace

FxChainNodeWidget::FxChainNodeWidget(int index, const FxSlot &slot, QWidget *parent)
    : QWidget(parent)
    , m_index(index)
{
    setObjectName(QStringLiteral("aefxChainNode"));
    setCursor(Qt::OpenHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setAcceptDrops(true);
    setToolTip(tr("Drag to reorder"));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 2, 8, 2);
    lay->setSpacing(4);

    m_enabled = new QCheckBox(this);
    m_enabled->setObjectName(QStringLiteral("aefxChainCheck"));
    m_enabled->setChecked(!slot.bypass);
    m_enabled->setToolTip(tr("Enable / bypass"));
    lay->addWidget(m_enabled);

    m_name = new QLabel(AudioEventFxDialog::formatSlotLabel(slot), this);
    m_name->setObjectName(QStringLiteral("aefxChainName"));
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    lay->addWidget(m_name);

    connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) {
        emit bypassToggled(m_index, !on);
    });

    adjustSize();
    setFixedHeight(24);
}

void FxChainNodeWidget::setSelected(bool on)
{
    if (m_selected == on) {
        return;
    }
    m_selected = on;
    setProperty("selected", on);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

bool FxChainNodeWidget::isBypassed() const
{
    return m_enabled && !m_enabled->isChecked();
}

int FxChainNodeWidget::insertBeforeFromPos(const QPoint &pos) const
{
    return (pos.x() < width() / 2) ? m_index : (m_index + 1);
}

void FxChainNodeWidget::clearDropIndicator()
{
    if (m_dropSide < 0) {
        return;
    }
    m_dropSide = -1;
    update();
}

void FxChainNodeWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit selected(m_index);
        // Bypass checkbox handles its own click — don't start a drag from it
        if (m_enabled && m_enabled->geometry().contains(event->position().toPoint())) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_pressActive = true;
        m_dragStart = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void FxChainNodeWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || !m_pressActive) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if ((event->position().toPoint() - m_dragStart).manhattanLength()
        < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    m_pressActive = false;

    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMime), QByteArray::number(m_index));

    QPixmap pm(size());
    pm.fill(Qt::transparent);
    {
        QPainter pp(&pm);
        pp.fillRect(rect(), m_selected ? QColor(0x00, 0x78, 0xd7, 200) : QColor(0x40, 0x40, 0x40, 200));
        pp.setPen(QColor(0xe0, 0xe0, 0xe0));
        pp.drawText(rect().adjusted(20, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    m_name ? m_name->text() : QString());
        pp.setPen(QPen(QColor(0x00, 0xa0, 0xff), 1, Qt::DashLine));
        pp.setBrush(Qt::NoBrush);
        pp.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    QDrag drag(this);
    drag.setMimeData(mime);
    drag.setPixmap(pm);
    drag.setHotSpot(event->position().toPoint());
    setProperty("dragging", true);
    style()->unpolish(this);
    style()->polish(this);
    drag.exec(Qt::MoveAction);
    setProperty("dragging", false);
    style()->unpolish(this);
    style()->polish(this);
    setCursor(Qt::OpenHandCursor);
    clearDropIndicator();
}

void FxChainNodeWidget::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressActive = false;
    setCursor(Qt::OpenHandCursor);
    QWidget::mouseReleaseEvent(event);
}

void FxChainNodeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_selected) {
        p.fillRect(rect(), QColor(0x00, 0x78, 0xd7));
    }
    if (m_dropSide >= 0) {
        p.setPen(QPen(QColor(0xff, 0xc0, 0x40), 3));
        const int x = (m_dropSide == 0) ? 1 : (width() - 2);
        p.drawLine(x, 1, x, height() - 2);
    }
}

void FxChainNodeWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasFormat(QString::fromLatin1(kMime))) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void FxChainNodeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event->mimeData()->hasFormat(QString::fromLatin1(kMime))) {
        event->ignore();
        return;
    }
    const int side = (event->position().x() < width() / 2.0) ? 0 : 1;
    if (side != m_dropSide) {
        m_dropSide = side;
        update();
    }
    event->acceptProposedAction();
}

void FxChainNodeWidget::dragLeaveEvent(QDragLeaveEvent *)
{
    clearDropIndicator();
}

void FxChainNodeWidget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(QString::fromLatin1(kMime))) {
        event->ignore();
        clearDropIndicator();
        return;
    }
    const int from = event->mimeData()->data(QString::fromLatin1(kMime)).toInt();
    const int insertBefore = insertBeforeFromPos(event->position().toPoint());
    clearDropIndicator();
    event->acceptProposedAction();
    if (from != insertBefore && from + 1 != insertBefore) {
        emit moveRequested(from, insertBefore);
    }
}

AudioEventFxDialog::AudioEventFxDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("AudioEventFxDialog"));
    setWindowTitle(tr("Audio Event FX"));
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setWindowModality(Qt::NonModal);
    setMinimumSize(720, 520);
    resize(900, 640);
    buildUi();
}

void AudioEventFxDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("aefxHeader"));
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(8, 6, 8, 4);
    headerLay->setSpacing(8);

    m_eventIcon = new QLabel(header);
    m_eventIcon->setObjectName(QStringLiteral("aefxEventIcon"));
    m_eventIcon->setFixedSize(18, 18);
    m_eventIcon->setPixmap(IconFactory::iconFromSvgBody(IconFactory::svgWaveform(), 16).pixmap(16, 16));

    m_eventName = new QLabel(header);
    m_eventName->setObjectName(QStringLiteral("aefxEventName"));
    QFont nf = m_eventName->font();
    nf.setBold(true);
    m_eventName->setFont(nf);

    auto *addBtn = new QToolButton(header);
    addBtn->setObjectName(QStringLiteral("aefxAddBtn"));
    addBtn->setToolTip(tr("Add plug-in to chain"));
    addBtn->setText(QStringLiteral("fx+"));
    addBtn->setAutoRaise(true);
    addBtn->setFixedHeight(22);

    auto *remBtn = makeIconBtn(header, tr("Remove selected plug-in"), IconFactory::svgDelete());
    remBtn->setObjectName(QStringLiteral("aefxRemoveBtn"));

    headerLay->addWidget(m_eventIcon);
    headerLay->addWidget(m_eventName, 1);
    headerLay->addWidget(addBtn);
    headerLay->addWidget(remBtn);
    root->addWidget(header);

    auto *chainRow = new QWidget(this);
    chainRow->setObjectName(QStringLiteral("aefxChainRow"));
    auto *chainRowLay = new QHBoxLayout(chainRow);
    chainRowLay->setContentsMargins(4, 2, 4, 2);
    chainRowLay->setSpacing(2);

    auto *scrollLeft = makeIconBtn(chainRow, tr("Scroll left"), IconFactory::svgPrevFrame());
    auto *scrollRight = makeIconBtn(chainRow, tr("Scroll right"), IconFactory::svgNextFrame());

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
    chainRowLay->addWidget(scrollLeft);
    chainRowLay->addWidget(scrollRight);
    root->addWidget(chainRow);

    auto *preset = new QWidget(this);
    preset->setObjectName(QStringLiteral("aefxPresetBar"));
    auto *presetLay = new QHBoxLayout(preset);
    presetLay->setContentsMargins(8, 4, 8, 4);
    presetLay->setSpacing(6);

    auto *presetLabel = new QLabel(tr("Preset:"), preset);
    presetLabel->setObjectName(QStringLiteral("aefxPresetLabel"));
    m_presetCombo = new QComboBox(preset);
    m_presetCombo->setObjectName(QStringLiteral("aefxPresetCombo"));
    m_presetCombo->addItem(tr("(Untitled)"));
    m_presetCombo->addItem(tr("Default"));

    presetLay->addWidget(presetLabel);
    presetLay->addWidget(m_presetCombo, 1);
    presetLay->addWidget(makeIconBtn(preset, tr("Save preset"), IconFactory::svgSave()));
    presetLay->addWidget(makeIconBtn(preset, tr("Delete preset"), IconFactory::svgDelete()));
    presetLay->addWidget(makeIconBtn(preset, tr("Help"), IconFactory::svgSearch()));
    root->addWidget(preset);

    m_viewport = new QStackedWidget(this);
    m_viewport->setObjectName(QStringLiteral("aefxViewport"));
    m_emptyHint = new QLabel(tr("Add a plug-in to the chain to edit its parameters."), m_viewport);
    m_emptyHint->setObjectName(QStringLiteral("aefxEmptyHint"));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    m_viewport->addWidget(m_emptyHint);
    root->addWidget(m_viewport, 1);

    connect(addBtn, &QToolButton::clicked, this, &AudioEventFxDialog::addPlugins);
    connect(remBtn, &QToolButton::clicked, this, &AudioEventFxDialog::removeSelected);
    connect(scrollLeft, &QToolButton::clicked, this, [this]() { scrollChain(-120); });
    connect(scrollRight, &QToolButton::clicked, this, [this]() { scrollChain(120); });
}

QString AudioEventFxDialog::formatSlotLabel(const FxSlot &s)
{
    const QString name =
        (s.format == PluginFormat::Builtin) ? builtinFxDisplayName(s.displayName) : s.displayName;
    const QString fmt = formatName(s.format);
    if (fmt.isEmpty()) {
        return name;
    }
    return QStringLiteral("%1(%2)").arg(name, fmt);
}

void AudioEventFxDialog::setEvent(TrackEvent *ev)
{
    m_mode = Mode::Event;
    m_event = ev;
    m_track = nullptr;
    m_chain = nullptr;
    setWindowTitle(tr("Audio Event FX"));
    m_eventName->setText(eventTitle());
    m_eventIcon->setPixmap(IconFactory::iconFromSvgBody(IconFactory::svgWaveform(), 16).pixmap(16, 16));
    rebuildChain();
}

void AudioEventFxDialog::setTrack(Track *track)
{
    m_mode = Mode::Track;
    m_track = track;
    m_event = nullptr;
    m_chain = nullptr;
    if (track && track->kind == TrackKind::Video) {
        setWindowTitle(tr("Video Track FX"));
        m_eventName->setText(tr("Video Track FX: %1").arg(eventTitle()));
        m_eventIcon->setPixmap(
            IconFactory::iconFromSvgBody(IconFactory::svgFx(), 16).pixmap(16, 16));
        resize(std::max(width(), 1100), std::max(height(), 620));
    } else {
        setWindowTitle(tr("Audio Track FX"));
        m_eventName->setText(eventTitle());
        m_eventIcon->setPixmap(
            IconFactory::iconFromSvgBody(IconFactory::svgAudioDevice(), 16).pixmap(16, 16));
    }
    rebuildChain();
}

void AudioEventFxDialog::setChain(QVector<FxSlot> *chain, const QString &title)
{
    m_mode = Mode::Chain;
    m_chain = chain;
    m_event = nullptr;
    m_track = nullptr;
    setWindowTitle(tr("Assignable FX"));
    m_eventName->setText(title);
    m_eventIcon->setPixmap(IconFactory::iconFromSvgBody(IconFactory::svgFx(), 16).pixmap(16, 16));
    rebuildChain();
}

QString AudioEventFxDialog::eventTitle() const
{
    if (m_mode == Mode::Event && m_event) {
        return m_event->name;
    }
    if (m_mode == Mode::Track && m_track) {
        return m_track->name;
    }
    return {};
}

QVector<FxSlot> *AudioEventFxDialog::chain()
{
    if (m_mode == Mode::Event && m_event) {
        return &m_event->fxChain;
    }
    if (m_mode == Mode::Track && m_track) {
        return &m_track->fxChain;
    }
    if (m_mode == Mode::Chain) {
        return m_chain;
    }
    return nullptr;
}

void AudioEventFxDialog::rebuildChain()
{
    if (QVector<FxSlot> *c = chain()) {
        for (FxSlot &s : *c) {
            normalizeBuiltinFxSlot(&s);
        }
        CompositePluginHost::instance().ensureChainLoaded(c);
    }
    while (QLayoutItem *it = m_chainLay->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    m_nodes.clear();

    QVector<FxSlot> *c = chain();
    if (!c || c->isEmpty()) {
        m_selected = -1;
        auto *hint = new QLabel(tr("(empty chain)"), m_chainHost);
        hint->setObjectName(QStringLiteral("aefxChainEmpty"));
        m_chainLay->addWidget(hint);
        m_chainLay->addStretch(1);
        refreshViewport();
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

    for (int i = 0; i < c->size(); ++i) {
        if (i > 0) {
            auto *line = new QFrame(m_chainHost);
            line->setObjectName(QStringLiteral("aefxChainLine"));
            line->setFixedSize(12, 1);
            m_chainLay->addWidget(line, 0, Qt::AlignVCenter);
        }
        auto *node = new FxChainNodeWidget(i, (*c)[i], m_chainHost);
        connect(node, &FxChainNodeWidget::selected, this, &AudioEventFxDialog::selectPlugin);
        connect(node, &FxChainNodeWidget::bypassToggled, this, &AudioEventFxDialog::setBypass);
        connect(node, &FxChainNodeWidget::moveRequested, this, &AudioEventFxDialog::movePlugin);
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

    int sel = m_selected;
    if (sel < 0 || sel >= c->size()) {
        sel = 0;
    }
    selectPlugin(sel);
}

void AudioEventFxDialog::selectPlugin(int index)
{
    QVector<FxSlot> *c = chain();
    if (!c || index < 0 || index >= c->size()) {
        m_selected = -1;
        refreshViewport();
        return;
    }
    m_selected = index;
    for (FxChainNodeWidget *n : m_nodes) {
        n->setSelected(n->index() == index);
    }
    m_presetCombo->setCurrentIndex(0);
    refreshViewport();
}

void AudioEventFxDialog::selectByName(const QString &displayName)
{
    QVector<FxSlot> *c = chain();
    if (!c) {
        return;
    }
    const int idx = indexOfFxName(*c, displayName);
    if (idx >= 0) {
        selectPlugin(idx);
    }
}

void AudioEventFxDialog::refreshViewport()
{
    while (m_viewport->count() > 1) {
        QWidget *w = m_viewport->widget(1);
        m_viewport->removeWidget(w);
        w->deleteLater();
    }

    QVector<FxSlot> *c = chain();
    if (!c || m_selected < 0 || m_selected >= c->size()) {
        m_viewport->setCurrentIndex(0);
        return;
    }

    FxSlot &slot = (*c)[m_selected];
    QWidget *page = nullptr;
    if (slot.format == PluginFormat::Builtin) {
        page = buildBuiltinEditor(slot);
    } else {
        page = buildVstEditorPage(slot);
    }
    m_viewport->addWidget(page);
    m_viewport->setCurrentWidget(page);
}

QWidget *AudioEventFxDialog::buildBuiltinEditor(FxSlot &slot)
{
    const QString n = slot.displayName;
    if (n.contains(QLatin1String("color grading"), Qt::CaseInsensitive)
        || n.compare(QLatin1String("ColorGrading"), Qt::CaseInsensitive) == 0) {
        return buildColorGradingEditor(slot);
    }
    if (n.contains(QLatin1String("chorus"), Qt::CaseInsensitive)) {
        return buildChorusEditor(slot);
    }
    if (n.contains(QLatin1String("reverb"), Qt::CaseInsensitive)) {
        return buildReverbEditor(slot);
    }
    if (n.contains(QLatin1String("delay"), Qt::CaseInsensitive)) {
        return buildDelayEditor(slot);
    }
    if (n.contains(QLatin1String("noise gate"), Qt::CaseInsensitive)
        || n.compare(QLatin1String("Noise Gate"), Qt::CaseInsensitive) == 0) {
        return buildNoiseGateEditor(slot);
    }
    if (n.contains(QLatin1String("Track EQ"), Qt::CaseInsensitive)
        || n.compare(QLatin1String("TrackEQ"), Qt::CaseInsensitive) == 0
        || n.contains(QLatin1String("ExpressFX EQ"), Qt::CaseInsensitive)) {
        return buildTrackEqEditor(slot);
    }
    if (n.contains(QLatin1String("compressor"), Qt::CaseInsensitive)) {
        return buildTrackCompressorEditor(slot);
    }
    return buildGenericBuiltinEditor(slot);
}

QWidget *AudioEventFxDialog::buildColorGradingEditor(FxSlot &slot)
{
    return new ColorGradingEditor(&slot, this);
}

QWidget *AudioEventFxDialog::buildDelayEditor(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    const QVariantMap p0 = loadParams(slot);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxBuiltinPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);

    auto *titleBar = new QWidget(page);
    titleBar->setObjectName(QStringLiteral("aefxPluginTitleBar"));
    auto *tbLay = new QHBoxLayout(titleBar);
    tbLay->setContentsMargins(8, 4, 8, 4);
    auto *title = new QLabel(slot.displayName, titleBar);
    title->setObjectName(QStringLiteral("aefxPluginTitle"));
    tbLay->addWidget(title);
    tbLay->addStretch(1);
    root->addWidget(titleBar);

    auto *body = new QWidget(page);
    body->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(16, 16, 16, 16);
    bodyLay->setSpacing(10);

    auto addParam = [body, bodyLay, slotPtr, &p0](const QString &label, const QString &key,
                                                  double def, double minV, double maxV, int decimals) {
        auto *row = new QHBoxLayout();
        auto *lab = new QLabel(label, body);
        auto *sl = new QSlider(Qt::Horizontal, body);
        sl->setObjectName(QStringLiteral("aefxHSlider"));
        sl->setRange(0, 1000);
        const double cur = mapGet(p0, key, def);
        sl->setValue(int(std::lround((cur - minV) / (maxV - minV) * 1000.0)));
        auto *spin = new QDoubleSpinBox(body);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(minV, maxV);
        spin->setDecimals(decimals);
        spin->setValue(cur);
        spin->setFixedWidth(90);
        connect(sl, &QSlider::valueChanged, body, [spin, minV, maxV, slotPtr, key](int v) {
            const double x = minV + (maxV - minV) * (v / 1000.0);
            spin->blockSignals(true);
            spin->setValue(x);
            spin->blockSignals(false);
            setParam(slotPtr, key, x);
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), body,
                [sl, minV, maxV, slotPtr, key](double x) {
                    const int v = int(std::lround((x - minV) / (maxV - minV) * 1000.0));
                    sl->blockSignals(true);
                    sl->setValue(std::clamp(v, 0, 1000));
                    sl->blockSignals(false);
                    setParam(slotPtr, key, x);
                });
        row->addWidget(lab);
        row->addWidget(sl, 1);
        row->addWidget(spin);
        bodyLay->addLayout(row);
    };
    addParam(tr("Delay (ms)"), QStringLiteral("delayMs"), 250.0, 1.0, 2000.0, 0);
    addParam(tr("Feedback"), QStringLiteral("feedback"), 0.35, 0.0, 0.95, 2);
    addParam(tr("Mix"), QStringLiteral("mix"), 0.4, 0.0, 1.0, 2);
    bodyLay->addStretch(1);
    root->addWidget(body, 1);
    return page;
}

QWidget *AudioEventFxDialog::buildReverbEditor(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    const QVariantMap p0 = loadParams(slot);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxBuiltinPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);

    auto *titleBar = new QWidget(page);
    titleBar->setObjectName(QStringLiteral("aefxPluginTitleBar"));
    auto *tbLay = new QHBoxLayout(titleBar);
    tbLay->setContentsMargins(8, 4, 8, 4);
    auto *title = new QLabel(slot.displayName, titleBar);
    title->setObjectName(QStringLiteral("aefxPluginTitle"));
    tbLay->addWidget(title);
    tbLay->addStretch(1);
    root->addWidget(titleBar);

    auto *body = new QWidget(page);
    body->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(16, 16, 16, 16);
    bodyLay->setSpacing(10);

    auto addParam = [body, bodyLay, slotPtr, &p0](const QString &label, const QString &key,
                                                  double def, double minV, double maxV, int decimals) {
        auto *row = new QHBoxLayout();
        auto *lab = new QLabel(label, body);
        auto *sl = new QSlider(Qt::Horizontal, body);
        sl->setObjectName(QStringLiteral("aefxHSlider"));
        sl->setRange(0, 1000);
        const double cur = mapGet(p0, key, def);
        sl->setValue(int(std::lround((cur - minV) / (maxV - minV) * 1000.0)));
        auto *spin = new QDoubleSpinBox(body);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(minV, maxV);
        spin->setDecimals(decimals);
        spin->setValue(cur);
        spin->setFixedWidth(90);
        connect(sl, &QSlider::valueChanged, body, [spin, minV, maxV, slotPtr, key](int v) {
            const double x = minV + (maxV - minV) * (v / 1000.0);
            spin->blockSignals(true);
            spin->setValue(x);
            spin->blockSignals(false);
            setParam(slotPtr, key, x);
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), body,
                [sl, minV, maxV, slotPtr, key](double x) {
                    const int v = int(std::lround((x - minV) / (maxV - minV) * 1000.0));
                    sl->blockSignals(true);
                    sl->setValue(std::clamp(v, 0, 1000));
                    sl->blockSignals(false);
                    setParam(slotPtr, key, x);
                });
        row->addWidget(lab);
        row->addWidget(sl, 1);
        row->addWidget(spin);
        bodyLay->addLayout(row);
    };
    addParam(tr("Room size"), QStringLiteral("roomSize"), 0.55, 0.0, 1.0, 2);
    addParam(tr("Damp"), QStringLiteral("damp"), 0.45, 0.0, 1.0, 2);
    addParam(tr("Mix"), QStringLiteral("mix"), 0.35, 0.0, 1.0, 2);
    bodyLay->addStretch(1);
    root->addWidget(body, 1);
    return page;
}

QWidget *AudioEventFxDialog::buildChorusEditor(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    const QVariantMap p0 = loadParams(slot);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxBuiltinPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *titleBar = new QWidget(page);
    titleBar->setObjectName(QStringLiteral("aefxPluginTitleBar"));
    auto *tbLay = new QHBoxLayout(titleBar);
    tbLay->setContentsMargins(8, 4, 8, 4);
    auto *title = new QLabel(slot.displayName, titleBar);
    title->setObjectName(QStringLiteral("aefxPluginTitle"));
    tbLay->addWidget(title);
    tbLay->addStretch(1);
    tbLay->addWidget(makeIconBtn(titleBar, tr("Help"), IconFactory::svgSearch()));
    root->addWidget(titleBar);

    auto *body = new QWidget(page);
    body->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(12, 10, 12, 10);
    bodyLay->setSpacing(10);

    auto *top = new QHBoxLayout();
    top->setSpacing(16);

    auto addFader = [&](const QString &label, const QString &range, const QString &key, double def,
                       double minDb, double maxDb) {
        auto *col = new QVBoxLayout();
        col->setSpacing(2);
        auto *val = new QLabel(body);
        val->setObjectName(QStringLiteral("aefxFaderValue"));
        val->setAlignment(Qt::AlignCenter);
        auto *fader = makeVFader(body);
        const double cur = mapGet(p0, key, def);
        fader->setValue(std::clamp(int(std::lround((cur - minDb) / (maxDb - minDb) * 1000.0)), 0, 1000));
        auto *name = new QLabel(label, body);
        name->setObjectName(QStringLiteral("aefxFaderLabel"));
        name->setAlignment(Qt::AlignCenter);
        auto *rng = new QLabel(range, body);
        rng->setObjectName(QStringLiteral("aefxFaderRange"));
        rng->setAlignment(Qt::AlignCenter);
        auto updateVal = [val, minDb, maxDb](int v) {
            const double db = minDb + (maxDb - minDb) * (v / 1000.0);
            val->setText(db <= minDb + 0.05 ? QStringLiteral("-Inf")
                                            : QStringLiteral("%1 dB").arg(db, 0, 'f', 1));
        };
        updateVal(fader->value());
        connect(fader, &QSlider::valueChanged, body, [slotPtr, key, minDb, maxDb, updateVal](int v) {
            updateVal(v);
            setParam(slotPtr, key, minDb + (maxDb - minDb) * (v / 1000.0));
        });
        col->addWidget(val);
        col->addWidget(fader, 0, Qt::AlignHCenter);
        col->addWidget(name);
        col->addWidget(rng);
        top->addLayout(col);
    };

    addFader(tr("Input gain"), tr("-Inf. to 12.0 dB"), QStringLiteral("inputGain"), 0.0, -60.0, 12.0);
    addFader(tr("Dry out"), tr("-Inf. to 0.0 dB"), QStringLiteral("dryOut"), -6.0, -60.0, 0.0);
    addFader(tr("Chorus out"), tr("-Inf. to 0.0 dB"), QStringLiteral("chorusOut"), -4.4, -60.0, 0.0);

    auto *right = new QVBoxLayout();
    right->setSpacing(6);
    auto *invChorus = new QCheckBox(tr("Invert chorus phase"), body);
    invChorus->setObjectName(QStringLiteral("aefxCheck"));
    invChorus->setChecked(mapGetB(p0, QStringLiteral("invChorus"), false));
    auto *invFb = new QCheckBox(tr("Invert feedback phase"), body);
    invFb->setObjectName(QStringLiteral("aefxCheck"));
    invFb->setChecked(mapGetB(p0, QStringLiteral("invFeedback"), false));
    connect(invChorus, &QCheckBox::toggled, body, [slotPtr](bool on) {
        setParam(slotPtr, QStringLiteral("invChorus"), on);
    });
    connect(invFb, &QCheckBox::toggled, body, [slotPtr](bool on) {
        setParam(slotPtr, QStringLiteral("invFeedback"), on);
    });

    auto *sizeRow = new QHBoxLayout();
    auto *sizeLabel = new QLabel(tr("Chorus size (1 to 3)"), body);
    sizeLabel->setObjectName(QStringLiteral("aefxParamLabel"));
    auto *sizeSlider = new QSlider(Qt::Horizontal, body);
    sizeSlider->setObjectName(QStringLiteral("aefxHSlider"));
    sizeSlider->setRange(1, 3);
    sizeSlider->setValue(mapGetI(p0, QStringLiteral("chorusSize"), 3));
    auto *sizeVal = new QLabel(QString::number(sizeSlider->value()), body);
    sizeVal->setObjectName(QStringLiteral("aefxParamValue"));
    sizeVal->setFixedWidth(28);
    connect(sizeSlider, &QSlider::valueChanged, body, [slotPtr, sizeVal](int v) {
        sizeVal->setText(QString::number(v));
        setParam(slotPtr, QStringLiteral("chorusSize"), v);
    });
    sizeRow->addWidget(sizeLabel);
    sizeRow->addWidget(sizeSlider, 1);
    sizeRow->addWidget(sizeVal);

    right->addWidget(invChorus);
    right->addWidget(invFb);
    right->addSpacing(8);
    right->addLayout(sizeRow);
    right->addStretch(1);
    top->addLayout(right, 1);
    bodyLay->addLayout(top);

    auto addHParam = [&](const QString &label, const QString &key, double def, double minV, double maxV,
                         const QString &suffix, int decimals) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *lab = new QLabel(label, body);
        lab->setObjectName(QStringLiteral("aefxParamLabel"));
        lab->setMinimumWidth(260);
        auto *sl = new QSlider(Qt::Horizontal, body);
        sl->setObjectName(QStringLiteral("aefxHSlider"));
        sl->setRange(0, 1000);
        const double cur = mapGet(p0, key, def);
        sl->setValue(int(std::lround((cur - minV) / (maxV - minV) * 1000.0)));
        auto *spin = new QDoubleSpinBox(body);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(minV, maxV);
        spin->setDecimals(decimals);
        spin->setSuffix(suffix);
        spin->setValue(cur);
        spin->setFixedWidth(90);
        connect(sl, &QSlider::valueChanged, body, [spin, minV, maxV, slotPtr, key](int v) {
            const double x = minV + (maxV - minV) * (v / 1000.0);
            spin->blockSignals(true);
            spin->setValue(x);
            spin->blockSignals(false);
            setParam(slotPtr, key, x);
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), body,
                [sl, minV, maxV, slotPtr, key](double x) {
                    const int v = int(std::lround((x - minV) / (maxV - minV) * 1000.0));
                    sl->blockSignals(true);
                    sl->setValue(std::clamp(v, 0, 1000));
                    sl->blockSignals(false);
                    setParam(slotPtr, key, x);
                });
        row->addWidget(lab);
        row->addWidget(sl, 1);
        row->addWidget(spin);
        bodyLay->addLayout(row);
    };

    addHParam(tr("Modulation rate (0.001 to 20.0 Hz)"), QStringLiteral("modRate"), 0.8, 0.001, 20.0,
              QString(), 3);
    addHParam(tr("Modulation depth (1 to 100 %)"), QStringLiteral("modDepth"), 3.0, 1.0, 100.0,
              QStringLiteral(" %"), 0);
    addHParam(tr("Feedback (0 to 100 %)"), QStringLiteral("feedback"), 0.0, 0.0, 100.0,
              QStringLiteral(" %"), 0);
    addHParam(tr("Chorus out delay (0.1 to 100.0 ms)"), QStringLiteral("delayMs"), 40.0, 0.1, 100.0,
              QString(), 1);

    {
        auto *row = new QHBoxLayout();
        auto *cb = new QCheckBox(tr("Attenuate high frequencies above (Hz)"), body);
        cb->setObjectName(QStringLiteral("aefxCheck"));
        cb->setChecked(mapGetB(p0, QStringLiteral("attenHf"), false));
        auto *sl = new QSlider(Qt::Horizontal, body);
        sl->setObjectName(QStringLiteral("aefxHSlider"));
        sl->setRange(100, 20000);
        sl->setValue(int(mapGet(p0, QStringLiteral("attenHz"), 5000)));
        auto *spin = new QDoubleSpinBox(body);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(100, 20000);
        spin->setDecimals(0);
        spin->setValue(mapGet(p0, QStringLiteral("attenHz"), 5000));
        spin->setFixedWidth(90);
        sl->setEnabled(cb->isChecked());
        spin->setEnabled(cb->isChecked());
        connect(cb, &QCheckBox::toggled, body, [sl, spin, slotPtr](bool on) {
            sl->setEnabled(on);
            spin->setEnabled(on);
            setParam(slotPtr, QStringLiteral("attenHf"), on);
        });
        connect(sl, &QSlider::valueChanged, body, [spin, slotPtr](int v) {
            spin->blockSignals(true);
            spin->setValue(v);
            spin->blockSignals(false);
            setParam(slotPtr, QStringLiteral("attenHz"), double(v));
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), body,
                [sl, slotPtr](double v) {
                    sl->blockSignals(true);
                    sl->setValue(int(v));
                    sl->blockSignals(false);
                    setParam(slotPtr, QStringLiteral("attenHz"), v);
                });
        row->addWidget(cb);
        row->addWidget(sl, 1);
        row->addWidget(spin);
        bodyLay->addLayout(row);
    }

    {
        auto *cb = new QCheckBox(tr("Tempo sync"), body);
        cb->setObjectName(QStringLiteral("aefxCheck"));
        cb->setChecked(mapGetB(p0, QStringLiteral("tempoSync"), false));
        connect(cb, &QCheckBox::toggled, body, [slotPtr](bool on) {
            setParam(slotPtr, QStringLiteral("tempoSync"), on);
        });
        bodyLay->addWidget(cb);
        auto *syncRow = new QHBoxLayout();
        auto *lab = new QLabel(tr("Modulation period"), body);
        lab->setObjectName(QStringLiteral("aefxParamLabel"));
        auto *spin = new QDoubleSpinBox(body);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(0.0625, 16.0);
        spin->setDecimals(3);
        spin->setValue(mapGet(p0, QStringLiteral("modPeriod"), 1.0));
        spin->setEnabled(cb->isChecked());
        auto *unit = new QComboBox(body);
        unit->setObjectName(QStringLiteral("aefxPresetCombo"));
        unit->addItems({tr("Measures"), tr("Beats"), tr("Notes")});
        unit->setEnabled(cb->isChecked());
        connect(cb, &QCheckBox::toggled, spin, &QWidget::setEnabled);
        connect(cb, &QCheckBox::toggled, unit, &QWidget::setEnabled);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), body, [slotPtr](double v) {
            setParam(slotPtr, QStringLiteral("modPeriod"), v);
        });
        syncRow->addSpacing(24);
        syncRow->addWidget(lab);
        syncRow->addWidget(spin);
        syncRow->addWidget(unit);
        syncRow->addStretch(1);
        bodyLay->addLayout(syncRow);
    }

    bodyLay->addStretch(1);
    root->addWidget(body, 1);
    return page;
}

namespace {

class EqCurveWidget : public QWidget {
public:
    explicit EqCurveWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("aefxEqCurve"));
        setMinimumHeight(160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        for (int i = 0; i < 4; ++i) {
            m_enabled[i] = true;
            m_type[i] = (i == 0) ? 0 : (i == 3 ? 2 : 1);
            m_freq[i] = (i == 0) ? 100.0 : (i == 1 ? 1000.0 : (i == 2 ? 4000.0 : 10000.0));
            m_gain[i] = 0.0;
        }
        m_active = 0;
    }

    void setBand(int i, bool en, int type, double freq, double gain)
    {
        if (i < 0 || i >= 4) {
            return;
        }
        m_enabled[i] = en;
        m_type[i] = type;
        m_freq[i] = freq;
        m_gain[i] = gain;
        update();
    }

    void setActive(int i)
    {
        m_active = std::clamp(i, 0, 3);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xf4, 0xf4, 0xf4));
        const QRect plot = rect().adjusted(36, 8, -10, -22);
        p.fillRect(plot, Qt::white);
        p.setPen(QColor(0xd0, 0xd0, 0xd0));
        // H grid ±15 dB
        for (int g = -15; g <= 15; g += 5) {
            const double t = (15.0 - g) / 30.0;
            const int y = plot.top() + int(t * plot.height());
            p.drawLine(plot.left(), y, plot.right(), y);
            p.setPen(QColor(0x60, 0x60, 0x60));
            p.drawText(QRect(2, y - 8, 32, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(g));
            p.setPen(QColor(0xd0, 0xd0, 0xd0));
        }
        // Freq labels
        const double freqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
        p.setPen(QColor(0x60, 0x60, 0x60));
        for (double f : freqs) {
            const double t = std::log10(f / 20.0) / std::log10(20000.0 / 20.0);
            const int x = plot.left() + int(t * plot.width());
            p.setPen(QColor(0xe0, 0xe0, 0xe0));
            p.drawLine(x, plot.top(), x, plot.bottom());
            p.setPen(QColor(0x60, 0x60, 0x60));
            QString lab = f >= 1000 ? QStringLiteral("%1k").arg(f / 1000.0, 0, 'f', f >= 10000 ? 0 : 0)
                                    : QString::number(int(f));
            if (f == 1000) {
                lab = QStringLiteral("1k");
            } else if (f == 2000) {
                lab = QStringLiteral("2k");
            } else if (f == 5000) {
                lab = QStringLiteral("5k");
            } else if (f == 10000) {
                lab = QStringLiteral("10k");
            } else if (f == 20000) {
                lab = QStringLiteral("20k");
            }
            p.drawText(QRect(x - 16, plot.bottom() + 2, 32, 16), Qt::AlignCenter, lab);
        }

        auto responseDb = [&](double hz) {
            double sum = 0.0;
            for (int i = 0; i < 4; ++i) {
                if (!m_enabled[i]) {
                    continue;
                }
                const double f0 = std::max(20.0, m_freq[i]);
                const double g = m_gain[i];
                const double x = hz / f0;
                if (m_type[i] == 0) { // low shelf
                    sum += g / (1.0 + x * x);
                } else if (m_type[i] == 2) { // high shelf
                    sum += g * (x * x) / (1.0 + x * x);
                } else { // peak
                    const double w = std::log(hz / f0) / 0.35;
                    sum += g * std::exp(-w * w);
                }
            }
            return std::clamp(sum, -15.0, 15.0);
        };

        QPainterPath path;
        for (int px = 0; px <= plot.width(); ++px) {
            const double t = px / double(std::max(1, plot.width()));
            const double hz = 20.0 * std::pow(1000.0, t); // 20..20k
            const double db = responseDb(hz);
            const double ty = (15.0 - db) / 30.0;
            const QPointF pt(plot.left() + px, plot.top() + ty * plot.height());
            if (px == 0) {
                path.moveTo(pt);
            } else {
                path.lineTo(pt);
            }
        }
        p.setPen(QPen(QColor(0x20, 0x20, 0x20), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        // Band markers
        const QColor colors[4] = {QColor(0xc0, 0x30, 0x30), QColor(0x30, 0x30, 0xc0),
                                  QColor(0x30, 0xa0, 0x30), QColor(0xa0, 0x60, 0x20)};
        for (int i = 0; i < 4; ++i) {
            const double t = std::log10(m_freq[i] / 20.0) / std::log10(1000.0);
            const int x = plot.left() + int(std::clamp(t, 0.0, 1.0) * plot.width());
            const double ty = (15.0 - m_gain[i]) / 30.0;
            const int y = plot.top() + int(std::clamp(ty, 0.0, 1.0) * plot.height());
            p.setBrush(colors[i]);
            p.setPen(i == m_active ? QPen(Qt::black, 2) : QPen(Qt::black, 1));
            p.drawEllipse(QPoint(x, y), i == m_active ? 7 : 5, i == m_active ? 7 : 5);
            p.setPen(Qt::white);
            p.drawText(QRect(x - 6, y - 6, 12, 12), Qt::AlignCenter, QString::number(i + 1));
        }
    }

private:
    bool m_enabled[4]{};
    int m_type[4]{};
    double m_freq[4]{};
    double m_gain[4]{};
    int m_active = 0;
};

QWidget *makePluginChrome(QWidget *page, const QString &title)
{
    auto *titleBar = new QWidget(page);
    titleBar->setObjectName(QStringLiteral("aefxPluginTitleBar"));
    auto *tbLay = new QHBoxLayout(titleBar);
    tbLay->setContentsMargins(14, 8, 14, 8);
    tbLay->setSpacing(10);

    auto *accent = new QFrame(titleBar);
    accent->setObjectName(QStringLiteral("aefxTitleAccent"));
    accent->setFixedSize(3, 18);

    auto *lab = new QLabel(builtinFxDisplayName(title), titleBar);
    lab->setObjectName(QStringLiteral("aefxPluginTitle"));

    auto *badge = new QLabel(QObject::tr("OpenVegas"), titleBar);
    badge->setObjectName(QStringLiteral("aefxOpenVegasBadge"));

    tbLay->addWidget(accent, 0, Qt::AlignVCenter);
    tbLay->addWidget(lab, 0, Qt::AlignVCenter);
    tbLay->addStretch(1);
    tbLay->addWidget(badge, 0, Qt::AlignVCenter);
    return titleBar;
}

} // namespace

QWidget *AudioEventFxDialog::buildNoiseGateEditor(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    const QVariantMap p0 = loadParams(slot);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxBuiltinPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(makePluginChrome(page, slot.displayName));

    auto *body = new QWidget(page);
    body->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(20, 16, 20, 16);
    bodyLay->setSpacing(28);

    // Threshold vertical fader (−Inf … 0 dB)
    {
        auto *col = new QVBoxLayout();
        col->setSpacing(6);
        auto *val = new QLabel(body);
        val->setObjectName(QStringLiteral("aefxFaderValue"));
        val->setAlignment(Qt::AlignCenter);
        auto *fader = makeVFader(body);
        fader->setFixedHeight(180);
        const double cur = mapGet(p0, QStringLiteral("thresholdDb"), -60.0);
        // Map −60..0 → 1000..0 (top = −Inf)
        fader->setValue(std::clamp(int(std::lround((0.0 - cur) / 60.0 * 1000.0)), 0, 1000));
        auto updateVal = [val](int v) {
            const double db = -60.0 * (v / 1000.0);
            val->setText(db <= -59.5 ? QStringLiteral("-Inf")
                                     : QStringLiteral("%1 dB").arg(db, 0, 'f', 1));
        };
        updateVal(fader->value());
        connect(fader, &QSlider::valueChanged, body, [slotPtr, updateVal](int v) {
            updateVal(v);
            setParam(slotPtr, QStringLiteral("thresholdDb"), -60.0 * (v / 1000.0));
        });
        auto *name = new QLabel(tr("Threshold level\n(-Inf to 0 dB)"), body);
        name->setObjectName(QStringLiteral("aefxFaderLabel"));
        name->setAlignment(Qt::AlignCenter);
        col->addWidget(val);
        col->addWidget(fader, 0, Qt::AlignHCenter);
        col->addWidget(name);
        bodyLay->addLayout(col);
    }

    auto *right = new QVBoxLayout();
    right->setSpacing(14);
    auto addMs = [&](const QString &label, const QString &key, double def, double minV, double maxV) {
        auto *row = new QHBoxLayout();
        auto *lab = new QLabel(label, body);
        lab->setObjectName(QStringLiteral("aefxParamLabel"));
        lab->setMinimumWidth(200);
        auto *sl = new QSlider(Qt::Horizontal, body);
        sl->setObjectName(QStringLiteral("aefxHSlider"));
        sl->setRange(0, 1000);
        const double cur = mapGet(p0, key, def);
        sl->setValue(int(std::lround((cur - minV) / (maxV - minV) * 1000.0)));
        auto *spin = new QDoubleSpinBox(body);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(minV, maxV);
        spin->setDecimals(cur < 10 ? 0 : 0);
        spin->setValue(cur);
        spin->setFixedWidth(90);
        connect(sl, &QSlider::valueChanged, body, [spin, minV, maxV, slotPtr, key](int v) {
            const double x = minV + (maxV - minV) * (v / 1000.0);
            spin->blockSignals(true);
            spin->setValue(x);
            spin->blockSignals(false);
            setParam(slotPtr, key, x);
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), body,
                [sl, minV, maxV, slotPtr, key](double x) {
                    const int v = int(std::lround((x - minV) / (maxV - minV) * 1000.0));
                    sl->blockSignals(true);
                    sl->setValue(std::clamp(v, 0, 1000));
                    sl->blockSignals(false);
                    setParam(slotPtr, key, x);
                });
        row->addWidget(lab);
        row->addWidget(sl, 1);
        row->addWidget(spin);
        right->addLayout(row);
    };
    addMs(tr("Attack time (0 to 500 ms)"), QStringLiteral("attackMs"), 3.0, 0.0, 500.0);
    addMs(tr("Release time (1 to 5,000 ms)"), QStringLiteral("releaseMs"), 100.0, 1.0, 5000.0);
    right->addStretch(1);
    bodyLay->addLayout(right, 1);

    root->addWidget(body, 1);
    return page;
}

QWidget *AudioEventFxDialog::buildTrackEqEditor(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    QVariantMap p0 = loadParams(slot);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxBuiltinPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(makePluginChrome(page, slot.displayName));

    auto *tabs = new QTabWidget(page);
    tabs->setObjectName(QStringLiteral("aefxEqTabs"));

    auto *eqPage = new QWidget;
    eqPage->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *eqLay = new QVBoxLayout(eqPage);
    eqLay->setContentsMargins(10, 10, 10, 8);
    eqLay->setSpacing(8);

    auto *curve = new EqCurveWidget(eqPage);
    eqLay->addWidget(curve, 1);

    auto *bandBar = new QWidget(eqPage);
    auto *bandLay = new QHBoxLayout(bandBar);
    bandLay->setContentsMargins(0, 0, 0, 0);
    bandLay->setSpacing(4);
    QButtonGroup *bandGroup = new QButtonGroup(bandBar);
    QVector<QToolButton *> bandBtns;
    for (int i = 0; i < 4; ++i) {
        auto *b = new QToolButton(bandBar);
        b->setObjectName(QStringLiteral("aefxBandBtn"));
        b->setText(QString::number(i + 1));
        b->setCheckable(true);
        b->setFixedSize(28, 22);
        bandGroup->addButton(b, i);
        bandLay->addWidget(b);
        bandBtns.push_back(b);
    }
    bandLay->addStretch(1);
    bandBtns[0]->setChecked(true);
    eqLay->addWidget(bandBar);

    auto *controls = new QWidget(eqPage);
    auto *form = new QFormLayout(controls);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);

    auto *enabled = new QCheckBox(tr("Enabled"), controls);
    enabled->setObjectName(QStringLiteral("aefxCheck"));
    auto *type = new QComboBox(controls);
    type->setObjectName(QStringLiteral("aefxPresetCombo"));
    type->addItems({tr("Low Shelf"), tr("Peak"), tr("High Shelf")});
    auto *freq = new QDoubleSpinBox(controls);
    freq->setObjectName(QStringLiteral("aefxSpin"));
    freq->setRange(20, 20000);
    freq->setDecimals(0);
    freq->setSuffix(QStringLiteral(" Hz"));
    auto *freqSl = new QSlider(Qt::Horizontal, controls);
    freqSl->setObjectName(QStringLiteral("aefxHSlider"));
    freqSl->setRange(0, 1000);
    auto *gain = new QDoubleSpinBox(controls);
    gain->setObjectName(QStringLiteral("aefxSpin"));
    gain->setRange(-15, 15);
    gain->setDecimals(1);
    gain->setSuffix(QStringLiteral(" dB"));
    auto *gainSl = new QSlider(Qt::Horizontal, controls);
    gainSl->setObjectName(QStringLiteral("aefxHSlider"));
    gainSl->setRange(-150, 150);
    auto *rolloff = new QDoubleSpinBox(controls);
    rolloff->setObjectName(QStringLiteral("aefxSpin"));
    rolloff->setRange(6, 24);
    rolloff->setSingleStep(6);
    rolloff->setDecimals(0);
    rolloff->setSuffix(QStringLiteral(" dB/oct"));
    auto *rollSl = new QSlider(Qt::Horizontal, controls);
    rollSl->setObjectName(QStringLiteral("aefxHSlider"));
    rollSl->setRange(6, 24);

    form->addRow(QString(), enabled);
    form->addRow(tr("Type"), type);
    {
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(freqSl, 1);
        hl->addWidget(freq);
        form->addRow(tr("Frequency (Hz)"), row);
    }
    {
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(gainSl, 1);
        hl->addWidget(gain);
        form->addRow(tr("Gain (dB)"), row);
    }
    {
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(rollSl, 1);
        hl->addWidget(rolloff);
        form->addRow(tr("Rolloff (dB/oct)"), row);
    }
    eqLay->addWidget(controls);

    auto bandKey = [](int i, const char *field) {
        return QStringLiteral("band%1.%2").arg(i).arg(QLatin1String(field));
    };
    auto syncCurve = [curve, slotPtr, bandKey]() {
        const QVariantMap m = loadParams(*slotPtr);
        for (int i = 0; i < 4; ++i) {
            curve->setBand(i, mapGetB(m, bandKey(i, "enabled"), true),
                           mapGetI(m, bandKey(i, "type"), i == 0 ? 0 : (i == 3 ? 2 : 1)),
                           mapGet(m, bandKey(i, "freq"),
                                  i == 0 ? 100.0 : (i == 1 ? 1000.0 : (i == 2 ? 4000.0 : 10000.0))),
                           mapGet(m, bandKey(i, "gain"), 0.0));
        }
    };
    auto loadBandUi = [=](int bi) {
        const QVariantMap m = loadParams(*slotPtr);
        enabled->blockSignals(true);
        type->blockSignals(true);
        freq->blockSignals(true);
        gain->blockSignals(true);
        rolloff->blockSignals(true);
        freqSl->blockSignals(true);
        gainSl->blockSignals(true);
        rollSl->blockSignals(true);

        enabled->setChecked(mapGetB(m, bandKey(bi, "enabled"), true));
        type->setCurrentIndex(mapGetI(m, bandKey(bi, "type"), bi == 0 ? 0 : (bi == 3 ? 2 : 1)));
        const double f = mapGet(m, bandKey(bi, "freq"),
                                bi == 0 ? 100.0 : (bi == 1 ? 1000.0 : (bi == 2 ? 4000.0 : 10000.0)));
        const double g = mapGet(m, bandKey(bi, "gain"), 0.0);
        const double r = mapGet(m, bandKey(bi, "rolloff"), 12.0);
        freq->setValue(f);
        gain->setValue(g);
        rolloff->setValue(r);
        const double t = std::log10(std::max(20.0, f) / 20.0) / std::log10(1000.0);
        freqSl->setValue(int(std::lround(std::clamp(t, 0.0, 1.0) * 1000.0)));
        gainSl->setValue(int(std::lround(g * 10.0)));
        rollSl->setValue(int(r));

        enabled->blockSignals(false);
        type->blockSignals(false);
        freq->blockSignals(false);
        gain->blockSignals(false);
        rolloff->blockSignals(false);
        freqSl->blockSignals(false);
        gainSl->blockSignals(false);
        rollSl->blockSignals(false);
        curve->setActive(bi);
        syncCurve();
    };

    int *activeBand = new int(0);
    // Keep activeBand alive with page
    QObject::connect(page, &QObject::destroyed, page, [activeBand]() { delete activeBand; });

    QObject::connect(bandGroup, &QButtonGroup::idClicked, page, [=](int id) {
        *activeBand = id;
        loadBandUi(id);
    });
    auto writeField = [=](const char *field, const QVariant &v) {
        setParam(slotPtr, bandKey(*activeBand, field), v);
        syncCurve();
    };
    QObject::connect(enabled, &QCheckBox::toggled, page, [=](bool on) { writeField("enabled", on); });
    QObject::connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), page,
                     [=](int i) { writeField("type", i); });
    QObject::connect(freq, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page, [=](double v) {
        const double t = std::log10(std::max(20.0, v) / 20.0) / std::log10(1000.0);
        freqSl->blockSignals(true);
        freqSl->setValue(int(std::lround(std::clamp(t, 0.0, 1.0) * 1000.0)));
        freqSl->blockSignals(false);
        writeField("freq", v);
    });
    QObject::connect(freqSl, &QSlider::valueChanged, page, [=](int v) {
        const double hz = 20.0 * std::pow(1000.0, v / 1000.0);
        freq->blockSignals(true);
        freq->setValue(hz);
        freq->blockSignals(false);
        writeField("freq", hz);
    });
    QObject::connect(gain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page, [=](double v) {
        gainSl->blockSignals(true);
        gainSl->setValue(int(std::lround(v * 10.0)));
        gainSl->blockSignals(false);
        writeField("gain", v);
    });
    QObject::connect(gainSl, &QSlider::valueChanged, page, [=](int v) {
        const double g = v / 10.0;
        gain->blockSignals(true);
        gain->setValue(g);
        gain->blockSignals(false);
        writeField("gain", g);
    });
    QObject::connect(rolloff, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
                     [=](double v) {
                         rollSl->blockSignals(true);
                         rollSl->setValue(int(v));
                         rollSl->blockSignals(false);
                         writeField("rolloff", v);
                     });
    QObject::connect(rollSl, &QSlider::valueChanged, page, [=](int v) {
        rolloff->blockSignals(true);
        rolloff->setValue(v);
        rolloff->blockSignals(false);
        writeField("rolloff", double(v));
    });

    loadBandUi(0);

    auto *chPage = new QWidget;
    chPage->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *chLay = new QVBoxLayout(chPage);
    auto *chHint = new QLabel(tr("Channel routing follows the track (stereo L/R)."), chPage);
    chHint->setObjectName(QStringLiteral("aefxParamLabel"));
    chHint->setWordWrap(true);
    chLay->addWidget(chHint);
    chLay->addStretch(1);

    tabs->addTab(eqPage, tr("EQ"));
    tabs->addTab(chPage, tr("Channels"));
    root->addWidget(tabs, 1);
    return page;
}

QWidget *AudioEventFxDialog::buildTrackCompressorEditor(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    const QVariantMap p0 = loadParams(slot);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxBuiltinPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(makePluginChrome(page, slot.displayName));

    auto *body = new QWidget(page);
    body->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(16, 14, 16, 14);
    bodyLay->setSpacing(10);

    auto addMeter = [&](const QString &caption, int fromDb, int /*toDb*/) {
        auto *lab = new QLabel(caption, body);
        lab->setObjectName(QStringLiteral("aefxParamLabel"));
        bodyLay->addWidget(lab);
        auto *meter = new QProgressBar(body);
        meter->setObjectName(QStringLiteral("aefxMeter"));
        meter->setRange(0, 100);
        meter->setValue(0);
        meter->setTextVisible(false);
        meter->setFixedHeight(14);
        meter->setToolTip(tr("%1 … %2 dB").arg(fromDb).arg(3));
        bodyLay->addWidget(meter);
        return meter;
    };
    addMeter(tr("Input"), 39, 3);
    auto addGainRow = [&](const QString &label, const QString &key, double def) {
        auto *row = new QHBoxLayout();
        auto *lab = new QLabel(label, body);
        lab->setObjectName(QStringLiteral("aefxParamLabel"));
        lab->setMinimumWidth(140);
        auto *sl = new QSlider(Qt::Horizontal, body);
        sl->setObjectName(QStringLiteral("aefxHSlider"));
        sl->setRange(-240, 240); // −24…+24 dB ×10
        const double cur = mapGet(p0, key, def);
        sl->setValue(int(std::lround(cur * 10.0)));
        auto *val = new QLabel(body);
        val->setObjectName(QStringLiteral("aefxParamValue"));
        val->setFixedWidth(56);
        auto upd = [val](double db) {
            val->setText(QStringLiteral("%1 dB").arg(db, 0, 'f', 0));
        };
        upd(cur);
        connect(sl, &QSlider::valueChanged, body, [slotPtr, key, upd](int v) {
            const double db = v / 10.0;
            upd(db);
            setParam(slotPtr, key, db);
        });
        row->addWidget(lab);
        row->addWidget(sl, 1);
        row->addWidget(val);
        bodyLay->addLayout(row);
    };
    addGainRow(tr("Input gain (dB)"), QStringLiteral("inputGain"), 0.0);
    addMeter(tr("Output"), 39, 3);
    addGainRow(tr("Output gain (dB)"), QStringLiteral("outputGain"), 0.0);

    {
        auto *row = new QHBoxLayout();
        auto *lab = new QLabel(tr("Reduction (dB)"), body);
        lab->setObjectName(QStringLiteral("aefxParamLabel"));
        auto *meter = new QProgressBar(body);
        meter->setObjectName(QStringLiteral("aefxMeter"));
        meter->setRange(0, 100);
        meter->setValue(0);
        meter->setTextVisible(false);
        meter->setFixedHeight(14);
        auto *val = new QLabel(QStringLiteral("0,0"), body);
        val->setObjectName(QStringLiteral("aefxParamValue"));
        val->setFixedWidth(40);
        row->addWidget(lab);
        row->addWidget(meter, 1);
        row->addWidget(val);
        bodyLay->addLayout(row);
    }

    auto addParam = [&](const QString &label, const QString &key, double def, double minV, double maxV,
                        int decimals) {
        auto *row = new QHBoxLayout();
        auto *lab = new QLabel(label, body);
        lab->setObjectName(QStringLiteral("aefxParamLabel"));
        lab->setMinimumWidth(140);
        auto *sl = new QSlider(Qt::Horizontal, body);
        sl->setObjectName(QStringLiteral("aefxHSlider"));
        sl->setRange(0, 1000);
        const double cur = mapGet(p0, key, def);
        sl->setValue(int(std::lround((cur - minV) / (maxV - minV) * 1000.0)));
        auto *spin = new QDoubleSpinBox(body);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(minV, maxV);
        spin->setDecimals(decimals);
        spin->setValue(cur);
        spin->setFixedWidth(90);
        connect(sl, &QSlider::valueChanged, body, [spin, minV, maxV, slotPtr, key](int v) {
            const double x = minV + (maxV - minV) * (v / 1000.0);
            spin->blockSignals(true);
            spin->setValue(x);
            spin->blockSignals(false);
            setParam(slotPtr, key, x);
        });
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), body,
                [sl, minV, maxV, slotPtr, key](double x) {
                    const int v = int(std::lround((x - minV) / (maxV - minV) * 1000.0));
                    sl->blockSignals(true);
                    sl->setValue(std::clamp(v, 0, 1000));
                    sl->blockSignals(false);
                    setParam(slotPtr, key, x);
                });
        row->addWidget(lab);
        row->addWidget(sl, 1);
        row->addWidget(spin);
        bodyLay->addLayout(row);
    };
    addParam(tr("Threshold (dB)"), QStringLiteral("threshold"), 0.0, -60.0, 0.0, 1);
    addParam(tr("Amount (x:1)"), QStringLiteral("amount"), 1.0, 1.0, 20.0, 1);
    addParam(tr("Attack (ms)"), QStringLiteral("attackMs"), 15.0, 0.0, 500.0, 0);
    addParam(tr("Release (ms)"), QStringLiteral("releaseMs"), 250.0, 1.0, 5000.0, 0);

    auto *autoGain = new QCheckBox(tr("Auto gain compensation"), body);
    autoGain->setObjectName(QStringLiteral("aefxCheck"));
    autoGain->setChecked(mapGetB(p0, QStringLiteral("autoGain"), true));
    connect(autoGain, &QCheckBox::toggled, body, [slotPtr](bool on) {
        setParam(slotPtr, QStringLiteral("autoGain"), on);
    });
    auto *smooth = new QCheckBox(tr("Smooth saturation"), body);
    smooth->setObjectName(QStringLiteral("aefxCheck"));
    smooth->setChecked(mapGetB(p0, QStringLiteral("smoothSat"), false));
    connect(smooth, &QCheckBox::toggled, body, [slotPtr](bool on) {
        setParam(slotPtr, QStringLiteral("smoothSat"), on);
    });
    bodyLay->addWidget(autoGain);
    bodyLay->addWidget(smooth);
    bodyLay->addStretch(1);

    root->addWidget(body, 1);
    return page;
}

QWidget *AudioEventFxDialog::buildGenericBuiltinEditor(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    const QVariantMap p0 = loadParams(slot);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxBuiltinPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);

    auto *titleBar = new QWidget(page);
    titleBar->setObjectName(QStringLiteral("aefxPluginTitleBar"));
    auto *tbLay = new QHBoxLayout(titleBar);
    tbLay->setContentsMargins(8, 4, 8, 4);
    auto *title = new QLabel(slot.displayName, titleBar);
    title->setObjectName(QStringLiteral("aefxPluginTitle"));
    tbLay->addWidget(title);
    tbLay->addStretch(1);
    root->addWidget(titleBar);

    auto *body = new QWidget(page);
    body->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *form = new QFormLayout(body);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(10);

    auto *gain = new QDoubleSpinBox(body);
    gain->setObjectName(QStringLiteral("aefxSpin"));
    gain->setRange(-60, 12);
    gain->setDecimals(1);
    gain->setSuffix(QStringLiteral(" dB"));
    gain->setValue(mapGet(p0, QStringLiteral("gain"), 0.0));
    form->addRow(tr("Gain"), gain);

    auto *mix = new QSlider(Qt::Horizontal, body);
    mix->setObjectName(QStringLiteral("aefxHSlider"));
    mix->setRange(0, 100);
    mix->setValue(mapGetI(p0, QStringLiteral("mix"), 100));
    form->addRow(tr("Dry/Wet %"), mix);

    connect(gain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page, [slotPtr](double v) {
        setParam(slotPtr, QStringLiteral("gain"), v);
    });
    connect(mix, &QSlider::valueChanged, page, [slotPtr](int v) {
        setParam(slotPtr, QStringLiteral("mix"), v);
    });

    root->addWidget(body, 1);
    return page;
}

QWidget *AudioEventFxDialog::buildVstEditorPage(FxSlot &slot)
{
    FxSlot *slotPtr = &slot;
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("aefxVstPage"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *chrome = new QWidget(page);
    chrome->setObjectName(QStringLiteral("aefxVstChrome"));
    auto *chLay = new QHBoxLayout(chrome);
    chLay->setContentsMargins(8, 4, 8, 4);
    chLay->setSpacing(6);
    auto *logo = new QLabel(slot.displayName, chrome);
    logo->setObjectName(QStringLiteral("aefxVstChromeTitle"));
    auto *fmt = new QLabel(formatName(slot.format), chrome);
    fmt->setObjectName(QStringLiteral("aefxVstFmt"));
    chLay->addWidget(logo);
    chLay->addWidget(fmt);
    chLay->addStretch(1);
    root->addWidget(chrome);

    auto *status = new QLabel(page);
    status->setObjectName(QStringLiteral("aefxVstHint"));
    status->setWordWrap(true);
    status->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    root->addWidget(status);

    auto *embed = new QWidget(page);
    embed->setObjectName(QStringLiteral("aefxVstEmbed"));
    embed->setMinimumSize(400, 280);
    embed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *embedLay = new QVBoxLayout(embed);
    embedLay->setContentsMargins(0, 0, 0, 0);
    root->addWidget(embed, 1);

    auto *paramsHost = new QWidget(page);
    paramsHost->setObjectName(QStringLiteral("aefxBuiltinBody"));
    auto *paramsLay = new QVBoxLayout(paramsHost);
    paramsLay->setContentsMargins(12, 8, 12, 8);
    root->addWidget(paramsHost);

    const auto rebuildParams = [slotPtr, paramsHost, paramsLay]() {
        while (QLayoutItem *it = paramsLay->takeAt(0)) {
            if (it->widget()) {
                it->widget()->deleteLater();
            }
            delete it;
        }
        auto &host = CompositePluginHost::instance();
        const int n = host.parameterCount(slotPtr);
        if (n <= 0) {
            paramsHost->hide();
            return;
        }
        paramsHost->show();
        auto *title = new QLabel(QObject::tr("Parameters"), paramsHost);
        paramsLay->addWidget(title);
        const int showN = std::min(n, 24);
        for (int i = 0; i < showN; ++i) {
            QString name;
            float mn = 0.f, mx = 1.f, step = 0.f;
            if (!host.parameterInfo(slotPtr, i, &name, &mn, &mx, &step)) {
                continue;
            }
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(name.isEmpty() ? QString::number(i) : name, paramsHost));
            auto *sl = new QSlider(Qt::Horizontal, paramsHost);
            sl->setRange(0, 1000);
            const float v = host.getParameter(slotPtr, i);
            sl->setValue(int(std::lround(std::clamp(double(v), 0.0, 1.0) * 1000.0)));
            row->addWidget(sl, 1);
            const int idx = i;
            QObject::connect(sl, &QSlider::valueChanged, paramsHost, [slotPtr, idx](int x) {
                CompositePluginHost::instance().setParameter(slotPtr, idx, float(x) / 1000.f);
            });
            paramsLay->addLayout(row);
        }
    };

    QString err;
    const bool loaded = CompositePluginHost::instance().ensureInstance(slotPtr, &err);
    if (!loaded) {
        status->setText(err.isEmpty()
                            ? tr("Plug-in DLL not found. Add VST paths in Preferences → Plug-Ins.")
                            : err);
        status->show();
        paramsHost->hide();
        return page;
    }

    status->setText(tr("Loading native editor…"));
    QTimer::singleShot(0, page, [slotPtr, embed, status, rebuildParams, page]() {
        auto &host = CompositePluginHost::instance();
        host.prepare(slotPtr, 48000.0, 512);
        const bool opened = host.openEditor(slotPtr, embed);
        if (opened) {
            status->setText(tr("Native editor connected — audio is processed in the FX chain."));
            embed->show();
        } else {
            embed->hide();
            status->setText(
                tr("Native GUI not available for this plug-in — use parameter sliders below. "
                   "Audio is still processed when Play is active."));
            rebuildParams();
            if (host.parameterCount(slotPtr) <= 0) {
                status->setText(
                    tr("Native GUI not available and no automatable parameters exposed. "
                       "Audio is still processed when Play is active."));
            }
        }
        Q_UNUSED(page);
    });

    return page;
}

void AudioEventFxDialog::addPlugins()
{
    QVector<FxSlot> *c = chain();
    if (!c) {
        return;
    }
    PluginChooserDialog dlg(m_pluginScanner, this);
    dlg.setAudioMode(true);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QVector<AudioPluginDesc> picked = dlg.selectedAudioPlugins();
    for (const AudioPluginDesc &d : picked) {
        FxSlot slot;
        CompositePluginHost::instance().createInstance(d, &slot);
        c->push_back(slot);
    }
    rebuildChain();
    if (!picked.isEmpty()) {
        selectPlugin(c->size() - 1);
    }
}

void AudioEventFxDialog::removeSelected()
{
    QVector<FxSlot> *c = chain();
    if (!c || m_selected < 0 || m_selected >= c->size()) {
        return;
    }
    CompositePluginHost::instance().releaseInstance(&(*c)[m_selected]);
    c->removeAt(m_selected);
    if (m_selected >= c->size()) {
        m_selected = c->size() - 1;
    }
    rebuildChain();
}

void AudioEventFxDialog::setBypass(int index, bool bypass)
{
    QVector<FxSlot> *c = chain();
    if (!c || index < 0 || index >= c->size()) {
        return;
    }
    (*c)[index].bypass = bypass;
}

void AudioEventFxDialog::movePlugin(int from, int insertBefore)
{
    QVector<FxSlot> *c = chain();
    if (!c || from < 0 || from >= c->size()) {
        return;
    }
    if (insertBefore < 0) {
        insertBefore = 0;
    }
    if (insertBefore > c->size()) {
        insertBefore = c->size();
    }
    // No-op when dropping onto own edges
    if (insertBefore == from || insertBefore == from + 1) {
        return;
    }
    const FxSlot slot = (*c)[from];
    c->removeAt(from);
    if (insertBefore > from) {
        --insertBefore;
    }
    c->insert(insertBefore, slot);
    m_selected = insertBefore;
    rebuildChain();
}

void AudioEventFxDialog::scrollChain(int dx)
{
    if (!m_chainScroll) {
        return;
    }
    QScrollBar *bar = m_chainScroll->horizontalScrollBar();
    bar->setValue(bar->value() + dx);
}

} // namespace openvegas
