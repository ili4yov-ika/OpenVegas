#include "ui/TitlesTextKeyframePane.h"

#include "ui/IconFactory.h"
#include "ui/KeyframeLaneWidgets.h"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

constexpr int kTreeWidth = 168;
constexpr int kRulerHeight = 20;

/** Per-parameter curve colour; X/Y keep Vegas's red/cyan pairing. */
QColor curveColorForKey(const QString &key)
{
    if (key == QLatin1String("locationX")) {
        return QColor(0xe0, 0x30, 0x30);
    }
    if (key == QLatin1String("locationY")) {
        return QColor(0x30, 0xd0, 0xd0);
    }
    if (key == QLatin1String("scale")) {
        return QColor(0xf0, 0xc0, 0x30);
    }
    if (key == QLatin1String("tracking")) {
        return QColor(0x80, 0xd0, 0x50);
    }
    if (key == QLatin1String("lineSpacing")) {
        return QColor(0x50, 0xa0, 0xf0);
    }
    if (key == QLatin1String("outlineWidth")) {
        return QColor(0xd0, 0x70, 0xf0);
    }
    if (key == QLatin1String("shadowOffsetX")) {
        return QColor(0xf0, 0x90, 0x40);
    }
    if (key == QLatin1String("shadowOffsetY")) {
        return QColor(0x40, 0xc0, 0x90);
    }
    return QColor(0xc0, 0xc0, 0xc8);
}

/** Value range a lane is drawn against — auto-fit so every curve uses the full height,
 *  which is what makes two parameters with very different scales both readable. */
void laneValueRange(const TitlesTextParamLane &lane, double *outMin, double *outMax)
{
    double vmin = lane.keys.first().value;
    double vmax = vmin;
    for (const TitlesTextKeyframe &kf : lane.keys) {
        vmin = std::min(vmin, kf.value);
        vmax = std::max(vmax, kf.value);
    }
    if (std::abs(vmax - vmin) < 1e-9) {
        vmin -= 0.5;
        vmax += 0.5;
    }
    const double pad = (vmax - vmin) * 0.12;
    *outMin = vmin - pad;
    *outMax = vmax + pad;
}

} // namespace

/** Keyframe drawing surface (right of the tree, below the ruler). */
class TitlesTextKeyframeGraph : public QWidget {
public:
    explicit TitlesTextKeyframeGraph(TitlesTextKeyframePane *pane)
        : QWidget(pane)
        , m_pane(pane)
    {
        setObjectName(QStringLiteral("ttKfGraph"));
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        setMinimumHeight(90);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x1e, 0x1e, 0x22));
        p.setRenderHint(QPainter::Antialiasing, true);

        if (m_pane->m_curvesMode) {
            paintCurves(p);
        } else {
            paintLanes(p);
        }

        const double phX = timeToX(m_pane->m_playheadSec);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(m_pane->m_curvesMode ? QColor(0xf0, 0xf0, 0xf0) : QColor(0xe0, 0x30, 0x30), 1));
        p.drawLine(QPointF(phX, 0), QPointF(phX, height()));

        if (m_tooltipVisible) {
            paintTooltip(p);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        const int hit = hitTestSelectedLane(event->position());
        if (hit >= 0) {
            m_dragIndex = hit;
            m_dragging = true;
            showTooltipFor(hit);
            update();
            return;
        }
        scrubTo(event->position().x());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && m_dragIndex >= 0) {
            dragKeyframeTo(event->position());
            return;
        }
        if (event->buttons() & Qt::LeftButton) {
            scrubTo(event->position().x());
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (m_dragging) {
            m_dragging = false;
            m_dragIndex = -1;
            m_tooltipVisible = false;
            update();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        // Double-click on empty graph adds a keyframe for the selected parameter, at the
        // clicked time and (in Curves mode) the clicked value.
        const QString key = m_pane->selectedParamKey();
        if (key.isEmpty()) {
            return;
        }
        const double t = std::clamp(xToTime(event->position().x()), 0.0, m_pane->m_lengthSec);
        double value = titlesTextParamValue(m_pane->m_params, key);
        if (m_pane->m_curvesMode) {
            if (const TitlesTextParamLane *lane = titlesTextFindLane(m_pane->m_params, key)) {
                double vmin = 0.0;
                double vmax = 1.0;
                laneValueRange(*lane, &vmin, &vmax);
                value = yToValue(event->position().y(), vmin, vmax);
            }
        }
        titlesTextSetKeyframe(&m_pane->m_params, key, t, value);
        m_pane->commit();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            m_pane->deleteKeyframeAtPlayhead();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    double timeToX(double t) const
    {
        const double len = std::max(0.001, m_pane->m_lengthSec);
        return std::clamp(t / len, 0.0, 1.0) * width();
    }
    double xToTime(double x) const
    {
        const double len = std::max(0.001, m_pane->m_lengthSec);
        return std::clamp(double(x) / std::max(1.0, double(width())), 0.0, 1.0) * len;
    }
    double valueToY(double v, double vmin, double vmax) const
    {
        const double span = (vmax - vmin) > 1e-9 ? (vmax - vmin) : 1.0;
        const double norm = (v - vmin) / span;
        return height() - 8.0 - norm * (height() - 16.0);
    }
    double yToValue(double y, double vmin, double vmax) const
    {
        const double usable = std::max(1.0, height() - 16.0);
        const double norm = std::clamp((height() - 8.0 - y) / usable, 0.0, 1.0);
        return vmin + (vmax - vmin) * norm;
    }

    void paintCurves(QPainter &p)
    {
        const QString selected = m_pane->selectedParamKey();
        // Selected curve last so it lands on top of the dimmed ones.
        QVector<const TitlesTextParamLane *> order;
        for (const TitlesTextParamLane &lane : m_pane->m_params.lanes) {
            if (lane.paramKey != selected) {
                order.push_back(&lane);
            }
        }
        for (const TitlesTextParamLane &lane : m_pane->m_params.lanes) {
            if (lane.paramKey == selected) {
                order.push_back(&lane);
            }
        }

        for (const TitlesTextParamLane *lane : order) {
            if (lane->keys.isEmpty()) {
                continue;
            }
            const bool isSelected = lane->paramKey == selected;
            double vmin = 0.0;
            double vmax = 1.0;
            laneValueRange(*lane, &vmin, &vmax);

            QColor color = curveColorForKey(lane->paramKey);
            if (!isSelected) {
                color.setAlpha(110);
            }
            QPainterPath path;
            for (int i = 0; i < lane->keys.size(); ++i) {
                const QPointF pt(timeToX(lane->keys[i].timeSec),
                                 valueToY(lane->keys[i].value, vmin, vmax));
                if (i == 0) {
                    path.moveTo(pt);
                } else {
                    path.lineTo(pt);
                }
            }
            // Vegas holds the last value out to the end of the event — draw that tail so
            // the curve visually matches what titlesTextAtTime() actually evaluates.
            const QPointF lastPt(timeToX(lane->keys.last().timeSec),
                                 valueToY(lane->keys.last().value, vmin, vmax));
            path.lineTo(QPointF(width(), lastPt.y()));
            p.setPen(QPen(color, isSelected ? 2.0 : 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);

            if (!isSelected) {
                continue;
            }
            for (int i = 0; i < lane->keys.size(); ++i) {
                const QPointF pt(timeToX(lane->keys[i].timeSec),
                                 valueToY(lane->keys[i].value, vmin, vmax));
                p.setBrush(QColor(0xff, 0xff, 0xff));
                p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
                p.drawEllipse(pt, i == m_dragIndex ? 6.0 : 5.0, i == m_dragIndex ? 6.0 : 5.0);
            }
        }
    }

    void paintLanes(QPainter &p)
    {
        QTreeWidget *tree = m_pane->m_tree;
        if (!tree) {
            return;
        }
        // Every keyframe time in the generator, for the aggregated group/root rows.
        QVector<double> allTimes;
        for (const TitlesTextParamLane &lane : m_pane->m_params.lanes) {
            for (const TitlesTextKeyframe &kf : lane.keys) {
                if (!allTimes.contains(kf.timeSec)) {
                    allTimes.push_back(kf.timeSec);
                }
            }
        }

        QTreeWidgetItemIterator it(tree);
        while (*it) {
            QTreeWidgetItem *item = *it;
            ++it;
            if (item->isHidden()) {
                continue;
            }
            const QRect rowRect = tree->visualItemRect(item);
            if (rowRect.isNull() || rowRect.bottom() < 0 || rowRect.top() > height()) {
                continue;
            }
            const double y = rowRect.center().y();
            const QString key = item->data(0, Qt::UserRole).toString();

            if (key.isEmpty()) {
                // Group / root row: Vegas shows an aggregated diamond per keyframe time.
                p.setBrush(QColor(0xd0, 0xd0, 0xd8));
                p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
                for (double t : allTimes) {
                    const double x = timeToX(t);
                    QPolygonF dia;
                    dia << QPointF(x, y - 5) << QPointF(x + 5, y) << QPointF(x, y + 5)
                        << QPointF(x - 5, y);
                    p.drawPolygon(dia);
                }
                continue;
            }

            const TitlesTextParamLane *lane = titlesTextFindLane(m_pane->m_params, key);
            if (!lane || lane->keys.isEmpty()) {
                continue;
            }
            p.setPen(QPen(QColor(0x90, 0x90, 0x98), 1));
            for (int i = 0; i + 1 < lane->keys.size(); ++i) {
                p.drawLine(QPointF(timeToX(lane->keys[i].timeSec), y),
                           QPointF(timeToX(lane->keys[i + 1].timeSec), y));
            }
            const bool selectedRow = key == m_pane->selectedParamKey();
            for (const TitlesTextKeyframe &kf : lane->keys) {
                p.setBrush(selectedRow ? QColor(0xff, 0xff, 0xff) : QColor(0xb0, 0xb0, 0xb8));
                p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
                p.drawEllipse(QPointF(timeToX(kf.timeSec), y), 4.5, 4.5);
            }
        }
    }

    void paintTooltip(QPainter &p)
    {
        p.setRenderHint(QPainter::Antialiasing, false);
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int w = std::max(fm.horizontalAdvance(m_tooltipLine1),
                               fm.horizontalAdvance(m_tooltipLine2))
                      + 10;
        const int h = fm.height() * 2 + 6;
        int x = int(std::clamp(m_tooltipPos.x() + 8.0, 0.0, double(width() - w)));
        int y = int(std::clamp(m_tooltipPos.y() + 8.0, 0.0, double(height() - h)));
        const QRect box(x, y, w, h);
        p.fillRect(box, QColor(0xff, 0xff, 0xff));
        p.setPen(QColor(0x40, 0x40, 0x40));
        p.drawRect(box.adjusted(0, 0, -1, -1));
        p.setPen(QColor(0xc0, 0x30, 0x30));
        p.drawText(QRect(box.x() + 5, box.y() + 2, box.width(), fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_tooltipLine1);
        p.setPen(QColor(0x20, 0x20, 0x20));
        p.drawText(QRect(box.x() + 5, box.y() + 2 + fm.height(), box.width(), fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_tooltipLine2);
    }

    int hitTestSelectedLane(const QPointF &pos) const
    {
        const QString key = m_pane->selectedParamKey();
        const TitlesTextParamLane *lane = titlesTextFindLane(m_pane->m_params, key);
        if (!lane || lane->keys.isEmpty()) {
            return -1;
        }
        double vmin = 0.0;
        double vmax = 1.0;
        laneValueRange(*lane, &vmin, &vmax);
        for (int i = 0; i < lane->keys.size(); ++i) {
            const double x = timeToX(lane->keys[i].timeSec);
            if (m_pane->m_curvesMode) {
                const double y = valueToY(lane->keys[i].value, vmin, vmax);
                if (std::hypot(pos.x() - x, pos.y() - y) <= 8.0) {
                    return i;
                }
            } else if (std::abs(pos.x() - x) <= 6.0) {
                return i;
            }
        }
        return -1;
    }

    void dragKeyframeTo(const QPointF &pos)
    {
        const QString key = m_pane->selectedParamKey();
        TitlesTextParamLane *lane = nullptr;
        for (TitlesTextParamLane &l : m_pane->m_params.lanes) {
            if (l.paramKey == key) {
                lane = &l;
                break;
            }
        }
        if (!lane || m_dragIndex < 0 || m_dragIndex >= lane->keys.size()) {
            return;
        }
        double vmin = 0.0;
        double vmax = 1.0;
        laneValueRange(*lane, &vmin, &vmax);

        lane->keys[m_dragIndex].timeSec = std::clamp(xToTime(pos.x()), 0.0, m_pane->m_lengthSec);
        if (m_pane->m_curvesMode) {
            lane->keys[m_dragIndex].value = yToValue(pos.y(), vmin, vmax);
        }
        // Keep the drag anchored to the same keyframe after a reorder past a neighbour.
        const TitlesTextKeyframe dragged = lane->keys[m_dragIndex];
        std::sort(lane->keys.begin(), lane->keys.end(),
                  [](const TitlesTextKeyframe &a, const TitlesTextKeyframe &b) {
                      return a.timeSec < b.timeSec;
                  });
        for (int i = 0; i < lane->keys.size(); ++i) {
            if (qFuzzyCompare(lane->keys[i].timeSec, dragged.timeSec)
                && qFuzzyCompare(lane->keys[i].value, dragged.value)) {
                m_dragIndex = i;
                break;
            }
        }
        showTooltipFor(m_dragIndex);
        m_pane->commit();
    }

    void showTooltipFor(int index)
    {
        const QString key = m_pane->selectedParamKey();
        const TitlesTextParamLane *lane = titlesTextFindLane(m_pane->m_params, key);
        if (!lane || index < 0 || index >= lane->keys.size()) {
            m_tooltipVisible = false;
            return;
        }
        double vmin = 0.0;
        double vmax = 1.0;
        laneValueRange(*lane, &vmin, &vmax);
        const TitlesTextKeyframe &kf = lane->keys[index];
        m_tooltipLine1 = formatTc(kf.timeSec);
        m_tooltipLine2 = QString::number(kf.value, 'f', 4);
        m_tooltipPos = QPointF(timeToX(kf.timeSec), valueToY(kf.value, vmin, vmax));
        m_tooltipVisible = true;
    }

    void scrubTo(double x)
    {
        m_pane->setPlayheadSec(xToTime(x));
        emit m_pane->playheadMoved(m_pane->playheadSec());
    }

    TitlesTextKeyframePane *m_pane = nullptr;
    bool m_dragging = false;
    int m_dragIndex = -1;
    bool m_tooltipVisible = false;
    QString m_tooltipLine1;
    QString m_tooltipLine2;
    QPointF m_tooltipPos;
};

TitlesTextKeyframePane::TitlesTextKeyframePane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ttKeyframePane"));
    buildUi();
}

void TitlesTextKeyframePane::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    // Corner spacer keeps the tree's first row level with the top of the graph, so the
    // Lanes-mode rows can be painted straight off the tree's own row rectangles.
    auto *corner = new QWidget(this);
    corner->setFixedSize(kTreeWidth, kRulerHeight);
    grid->addWidget(corner, 0, 0);

    auto *ruler = new PanCropKeyframeRuler(this);
    ruler->setOnScrub([this](double t) {
        setPlayheadSec(t);
        emit playheadMoved(m_playheadSec);
    });
    grid->addWidget(ruler, 0, 1);
    m_ruler = ruler;

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("ttKfTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setFixedWidth(kTreeWidth);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid->addWidget(m_tree, 1, 0);

    m_graph = new TitlesTextKeyframeGraph(this);
    grid->addWidget(m_graph, 1, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(1, 1);
    root->addLayout(grid, 1);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(4, 3, 4, 3);
    toolbar->setSpacing(3);
    toolbar->addStretch(1);

    m_syncBtn = IconFactory::toolButton(this, tr("Sync Cursor to Media Timeline"),
                                        IconFactory::svgSnap(), true, true);
    connect(m_syncBtn, &QToolButton::toggled, this, [this](bool on) { m_syncCursor = on; });
    toolbar->addWidget(m_syncBtn);

    m_firstBtn = IconFactory::toolButton(this, tr("First Keyframe"), IconFactory::svgGoStart());
    connect(m_firstBtn, &QToolButton::clicked, this, [this]() { goToKeyframe(-1, true); });
    toolbar->addWidget(m_firstBtn);
    m_prevBtn = IconFactory::toolButton(this, tr("Previous Keyframe"), IconFactory::svgPrevFrame());
    connect(m_prevBtn, &QToolButton::clicked, this, [this]() { goToKeyframe(-1, false); });
    toolbar->addWidget(m_prevBtn);
    m_nextBtn = IconFactory::toolButton(this, tr("Next Keyframe"), IconFactory::svgNextFrame());
    connect(m_nextBtn, &QToolButton::clicked, this, [this]() { goToKeyframe(1, false); });
    toolbar->addWidget(m_nextBtn);
    m_lastBtn = IconFactory::toolButton(this, tr("Last Keyframe"), IconFactory::svgGoEnd());
    connect(m_lastBtn, &QToolButton::clicked, this, [this]() { goToKeyframe(1, true); });
    toolbar->addWidget(m_lastBtn);

    m_addBtn = IconFactory::toolButton(this, tr("Add Keyframe"), IconFactory::svgMarker());
    connect(m_addBtn, &QToolButton::clicked, this, [this]() { addKeyframeAtPlayhead(); });
    toolbar->addWidget(m_addBtn);
    m_deleteBtn = IconFactory::toolButton(this, tr("Delete Keyframe"), IconFactory::svgDelete());
    connect(m_deleteBtn, &QToolButton::clicked, this, [this]() { deleteKeyframeAtPlayhead(); });
    toolbar->addWidget(m_deleteBtn);

    m_timecodeEdit = new QLineEdit(this);
    m_timecodeEdit->setFixedWidth(96);
    m_timecodeEdit->setReadOnly(true);
    m_timecodeEdit->setAlignment(Qt::AlignCenter);
    toolbar->addWidget(m_timecodeEdit);

    m_lanesBtn = new QToolButton(this);
    m_lanesBtn->setText(tr("Lanes"));
    m_lanesBtn->setCheckable(true);
    m_curvesBtn = new QToolButton(this);
    m_curvesBtn->setText(tr("Curves"));
    m_curvesBtn->setCheckable(true);
    m_curvesBtn->setChecked(true);
    connect(m_lanesBtn, &QToolButton::clicked, this, [this]() {
        m_curvesMode = false;
        m_lanesBtn->setChecked(true);
        m_curvesBtn->setChecked(false);
        m_graph->update();
    });
    connect(m_curvesBtn, &QToolButton::clicked, this, [this]() {
        m_curvesMode = true;
        m_curvesBtn->setChecked(true);
        m_lanesBtn->setChecked(false);
        m_graph->update();
    });
    toolbar->addWidget(m_lanesBtn);
    toolbar->addWidget(m_curvesBtn);
    root->addLayout(toolbar);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
                m_selectedKey = cur ? cur->data(0, Qt::UserRole).toString() : QString();
                syncToolbarState();
                m_graph->update();
            });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *) { m_graph->update(); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *) { m_graph->update(); });

    rebuildTree();
}

void TitlesTextKeyframePane::rebuildTree()
{
    m_tree->clear();
    m_paramItems.clear();

    auto *root = new QTreeWidgetItem(m_tree);
    root->setText(0, tr("Titles & Text"));
    root->setExpanded(true);

    QHash<QString, QTreeWidgetItem *> groups;
    for (const TitlesTextAnimatableParam &ap : titlesTextAnimatableParams()) {
        QTreeWidgetItem *parent = root;
        if (!ap.group.isEmpty()) {
            auto found = groups.constFind(ap.group);
            if (found == groups.cend()) {
                auto *groupItem = new QTreeWidgetItem(root);
                groupItem->setText(0, ap.group);
                groupItem->setExpanded(true);
                groups.insert(ap.group, groupItem);
                parent = groupItem;
            } else {
                parent = *found;
            }
        }
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, ap.label);
        item->setData(0, Qt::UserRole, ap.key);
        m_paramItems.insert(ap.key, item);
    }

    if (m_selectedKey.isEmpty() && !titlesTextAnimatableParams().isEmpty()) {
        m_selectedKey = titlesTextAnimatableParams().first().key;
    }
    if (QTreeWidgetItem *item = m_paramItems.value(m_selectedKey)) {
        m_tree->setCurrentItem(item);
    }
}

void TitlesTextKeyframePane::setParams(const TitlesTextParams &params, double lengthSec,
                                       double frameRateFps)
{
    m_params = params;
    m_lengthSec = std::max(0.05, lengthSec);
    m_frameRateFps = frameRateFps > 0.001 ? frameRateFps : 30.0;
    // Colour the tree rows that actually carry keyframes, so the animated parameters are
    // findable without clicking through every row.
    for (auto it = m_paramItems.constBegin(); it != m_paramItems.constEnd(); ++it) {
        const bool animated = titlesTextFindLane(m_params, it.key()) != nullptr;
        it.value()->setForeground(0, animated ? QBrush(curveColorForKey(it.key()))
                                              : QBrush(QColor(0xc0, 0xc0, 0xc8)));
    }
    if (m_ruler) {
        m_ruler->setRange(m_lengthSec, m_playheadSec);
    }
    syncToolbarState();
    m_graph->update();
}

void TitlesTextKeyframePane::setPlayheadSec(double localSec)
{
    m_playheadSec = std::clamp(localSec, 0.0, m_lengthSec);
    if (m_ruler) {
        m_ruler->setRange(m_lengthSec, m_playheadSec);
    }
    syncToolbarState();
    m_graph->update();
}

bool TitlesTextKeyframePane::isAnimated(const QString &key) const
{
    return titlesTextFindLane(m_params, key) != nullptr;
}

void TitlesTextKeyframePane::selectParam(const QString &key)
{
    if (QTreeWidgetItem *item = m_paramItems.value(key)) {
        m_tree->setCurrentItem(item);
    }
}

void TitlesTextKeyframePane::addKeyframeForParam(const QString &key)
{
    if (key.isEmpty()) {
        return;
    }
    selectParam(key);
    titlesTextSetKeyframe(&m_params, key, m_playheadSec, titlesTextParamValue(m_params, key));
    commit();
}

QVector<double> TitlesTextKeyframePane::navigationTimes() const
{
    QVector<double> times;
    const QString key = m_selectedKey;
    if (!key.isEmpty()) {
        if (const TitlesTextParamLane *lane = titlesTextFindLane(m_params, key)) {
            for (const TitlesTextKeyframe &kf : lane->keys) {
                times.push_back(kf.timeSec);
            }
        }
    } else {
        // Group / root row selected: Vegas steps through every keyframe below it.
        for (const TitlesTextParamLane &lane : m_params.lanes) {
            for (const TitlesTextKeyframe &kf : lane.keys) {
                if (!times.contains(kf.timeSec)) {
                    times.push_back(kf.timeSec);
                }
            }
        }
    }
    std::sort(times.begin(), times.end());
    return times;
}

void TitlesTextKeyframePane::goToKeyframe(int direction, bool toEnd)
{
    const QVector<double> times = navigationTimes();
    if (times.isEmpty()) {
        return;
    }
    double target = m_playheadSec;
    if (toEnd) {
        target = direction < 0 ? times.first() : times.last();
    } else if (direction < 0) {
        for (int i = times.size() - 1; i >= 0; --i) {
            if (times[i] < m_playheadSec - 1e-4) {
                target = times[i];
                break;
            }
        }
    } else {
        for (double t : times) {
            if (t > m_playheadSec + 1e-4) {
                target = t;
                break;
            }
        }
    }
    setPlayheadSec(target);
    emit playheadMoved(m_playheadSec);
}

void TitlesTextKeyframePane::addKeyframeAtPlayhead()
{
    if (m_selectedKey.isEmpty()) {
        return;
    }
    titlesTextSetKeyframe(&m_params, m_selectedKey, m_playheadSec,
                          titlesTextParamValue(m_params, m_selectedKey));
    commit();
}

void TitlesTextKeyframePane::deleteKeyframeAtPlayhead()
{
    if (m_selectedKey.isEmpty()) {
        return;
    }
    if (titlesTextRemoveKeyframe(&m_params, m_selectedKey, m_playheadSec)) {
        commit();
    }
}

void TitlesTextKeyframePane::syncToolbarState()
{
    if (m_timecodeEdit) {
        m_timecodeEdit->setText(formatTc(m_playheadSec));
    }
    const bool hasParam = !m_selectedKey.isEmpty();
    const QVector<double> times = navigationTimes();
    const bool hasKeys = !times.isEmpty();
    if (m_addBtn) {
        m_addBtn->setEnabled(hasParam);
    }
    if (m_deleteBtn) {
        bool onKeyframe = false;
        if (hasParam) {
            if (const TitlesTextParamLane *lane = titlesTextFindLane(m_params, m_selectedKey)) {
                for (const TitlesTextKeyframe &kf : lane->keys) {
                    if (std::abs(kf.timeSec - m_playheadSec) <= 0.02) {
                        onKeyframe = true;
                        break;
                    }
                }
            }
        }
        m_deleteBtn->setEnabled(onKeyframe);
    }
    for (QToolButton *b : {m_firstBtn, m_prevBtn, m_nextBtn, m_lastBtn}) {
        if (b) {
            b->setEnabled(hasKeys);
        }
    }
}

void TitlesTextKeyframePane::commit()
{
    // Repaint from the pane's own copy first so dragging stays smooth even if the owner
    // takes a slower path (event write + preview re-render) on paramsEdited.
    for (auto it = m_paramItems.constBegin(); it != m_paramItems.constEnd(); ++it) {
        const bool animated = titlesTextFindLane(m_params, it.key()) != nullptr;
        it.value()->setForeground(0, animated ? QBrush(curveColorForKey(it.key()))
                                              : QBrush(QColor(0xc0, 0xc0, 0xc8)));
    }
    syncToolbarState();
    m_graph->update();
    emit paramsEdited(m_params);
}

} // namespace openvegas
