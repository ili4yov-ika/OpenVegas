#include "ui/ColorGradingEditor.h"
#include "ui/IconFactory.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFrame>
#include <QDataStream>
#include <QIODevice>
#include <QVariantMap>
#include <QVariantList>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

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

struct WheelValues {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double y = 0.0;
};

WheelValues readWheel(const QVariantMap &m, const QString &prefix, double defRgb, double defY)
{
    WheelValues v;
    v.r = mapGet(m, prefix + QStringLiteral(".r"), defRgb);
    v.g = mapGet(m, prefix + QStringLiteral(".g"), defRgb);
    v.b = mapGet(m, prefix + QStringLiteral(".b"), defRgb);
    v.y = mapGet(m, prefix + QStringLiteral(".y"), defY);
    return v;
}

void writeWheel(QVariantMap *m, const QString &prefix, const WheelValues &v)
{
    m->insert(prefix + QStringLiteral(".r"), v.r);
    m->insert(prefix + QStringLiteral(".g"), v.g);
    m->insert(prefix + QStringLiteral(".b"), v.b);
    m->insert(prefix + QStringLiteral(".y"), v.y);
}

class ColorWheelWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheelWidget(const QString &title, bool unityDefault, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_title(title)
        , m_unity(unityDefault)
    {
        setObjectName(QStringLiteral("cgWheel"));
        setMinimumSize(168, 200);
        setMouseTracking(true);
        if (m_unity) {
            m_r = m_g = m_b = m_y = 1.0;
        }
    }

    void setValues(double r, double g, double b, double y)
    {
        m_r = r;
        m_g = g;
        m_b = b;
        m_y = y;
        update();
        emit valuesChanged();
    }

    double r() const { return m_r; }
    double g() const { return m_g; }
    double b() const { return m_b; }
    double y() const { return m_y; }

signals:
    void valuesChanged();

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(0x2a, 0x2a, 0x2a));

        QFont f = font();
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0xe0, 0xe0, 0xe0));
        p.drawText(QRect(8, 4, width() - 40, 18), Qt::AlignLeft | Qt::AlignVCenter, m_title);

        const QRect wheel = wheelRect();
        const QPointF c = wheel.center();
        const double radius = wheel.width() * 0.5;

        QConicalGradient grad(c, 0.0);
        for (int i = 0; i <= 12; ++i) {
            const double h = (i / 12.0) * 360.0;
            grad.setColorAt(1.0 - i / 12.0, QColor::fromHsvF(std::fmod(h / 360.0, 1.0), 1.0, 1.0));
        }
        p.setBrush(grad);
        p.setPen(QPen(QColor(0x10, 0x10, 0x10), 1));
        p.drawEllipse(wheel);

        // Desaturate center
        QRadialGradient soft(c, radius);
        soft.setColorAt(0.0, QColor(180, 180, 180, 220));
        soft.setColorAt(0.35, QColor(180, 180, 180, 40));
        soft.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setBrush(soft);
        p.setPen(Qt::NoPen);
        p.drawEllipse(wheel);

        // Crosshair from RGB as polar offset
        const double maxAbs = m_unity ? 2.0 : 1.0;
        const double nx = std::clamp((m_r - m_g) * 0.5, -1.0, 1.0);
        const double ny = std::clamp((m_b - (m_r + m_g) * 0.5) * 0.55, -1.0, 1.0);
        const QPointF tip(c.x() + nx * radius * 0.85, c.y() - ny * radius * 0.85);
        p.setPen(QPen(Qt::white, 1.2));
        p.drawLine(QPointF(tip.x() - 6, tip.y()), QPointF(tip.x() + 6, tip.y()));
        p.drawLine(QPointF(tip.x(), tip.y() - 6), QPointF(tip.x(), tip.y() + 6));
        p.setBrush(Qt::white);
        p.drawEllipse(tip, 3.0, 3.0);

        Q_UNUSED(maxAbs);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        applyPos(event->position());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            applyPos(event->position());
        }
    }

private:
    QRect wheelRect() const
    {
        const int side = std::min(width() - 16, height() - 36);
        return QRect((width() - side) / 2, 26, side, side);
    }

    void applyPos(QPointF pos)
    {
        const QRect w = wheelRect();
        const QPointF c = w.center();
        QPointF d = pos - c;
        const double radius = w.width() * 0.5;
        const double len = std::hypot(d.x(), d.y());
        if (len > radius && len > 1e-6) {
            d *= radius / len;
        }
        const double nx = d.x() / (radius * 0.85);
        const double ny = -d.y() / (radius * 0.85);
        // Map joystick to RGB deltas around default
        const double base = m_unity ? 1.0 : 0.0;
        const double span = m_unity ? 0.5 : 0.35;
        m_r = base + nx * span - ny * span * 0.25;
        m_g = base - nx * span - ny * span * 0.25;
        m_b = base + ny * span;
        if (m_unity) {
            m_r = std::clamp(m_r, 0.0, 2.0);
            m_g = std::clamp(m_g, 0.0, 2.0);
            m_b = std::clamp(m_b, 0.0, 2.0);
        } else {
            m_r = std::clamp(m_r, -1.0, 1.0);
            m_g = std::clamp(m_g, -1.0, 1.0);
            m_b = std::clamp(m_b, -1.0, 1.0);
        }
        update();
        emit valuesChanged();
    }

    QString m_title;
    bool m_unity = false;
    double m_r = 0.0;
    double m_g = 0.0;
    double m_b = 0.0;
    double m_y = 0.0;
};

class CurveEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditorWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("cgCurve"));
        setMinimumSize(240, 240);
        m_points = {{0.0, 0.0}, {1.0, 1.0}};
    }

    void setPoints(const QVector<QPointF> &pts)
    {
        m_points = pts;
        if (m_points.size() < 2) {
            m_points = {{0.0, 0.0}, {1.0, 1.0}};
        }
        update();
    }

    QVector<QPointF> points() const { return m_points; }

signals:
    void pointsChanged();

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x22, 0x22, 0x22));
        const QRect plot = rect().adjusted(10, 10, -10, -10);
        p.fillRect(plot, QColor(0x18, 0x18, 0x18));
        p.setPen(QColor(0x40, 0x40, 0x40));
        for (int i = 1; i < 4; ++i) {
            const int x = plot.left() + plot.width() * i / 4;
            const int y = plot.top() + plot.height() * i / 4;
            p.drawLine(x, plot.top(), x, plot.bottom());
            p.drawLine(plot.left(), y, plot.right(), y);
        }
        p.setPen(QColor(0x60, 0x60, 0x60));
        p.drawRect(plot.adjusted(0, 0, -1, -1));

        // Diagonal reference
        p.setPen(QPen(QColor(0x50, 0x50, 0x50), 1, Qt::DashLine));
        p.drawLine(plot.bottomLeft(), plot.topRight());

        QPainterPath path;
        for (int i = 0; i < m_points.size(); ++i) {
            const QPointF pt = toScreen(m_points[i], plot);
            if (i == 0) {
                path.moveTo(pt);
            } else {
                path.lineTo(pt);
            }
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setBrush(QColor(0xf0, 0xc0, 0x20));
        p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
        for (const QPointF &pt : m_points) {
            const QPointF s = toScreen(pt, plot);
            p.drawEllipse(s, 4.0, 4.0);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const QRect plot = rect().adjusted(10, 10, -10, -10);
        m_drag = hitPoint(event->position(), plot);
        if (m_drag < 0 && event->button() == Qt::LeftButton) {
            QPointF n = fromScreen(event->position(), plot);
            m_points.push_back(n);
            std::sort(m_points.begin(), m_points.end(),
                      [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
            m_drag = hitPoint(event->position(), plot);
            emit pointsChanged();
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_drag < 0) {
            return;
        }
        const QRect plot = rect().adjusted(10, 10, -10, -10);
        QPointF n = fromScreen(event->position(), plot);
        if (m_drag == 0) {
            n.setX(0.0);
        } else if (m_drag == m_points.size() - 1) {
            n.setX(1.0);
        }
        m_points[m_drag] = n;
        std::sort(m_points.begin(), m_points.end(),
                  [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
        // re-find after sort
        m_drag = hitPoint(toScreen(n, plot), plot);
        emit pointsChanged();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_drag = -1; }

private:
    static QPointF toScreen(const QPointF &n, const QRect &plot)
    {
        return QPointF(plot.left() + n.x() * plot.width(),
                       plot.bottom() - n.y() * plot.height());
    }

    static QPointF fromScreen(const QPointF &s, const QRect &plot)
    {
        const double x = std::clamp((s.x() - plot.left()) / double(plot.width()), 0.0, 1.0);
        const double y = std::clamp((plot.bottom() - s.y()) / double(plot.height()), 0.0, 1.0);
        return QPointF(x, y);
    }

    int hitPoint(const QPointF &s, const QRect &plot) const
    {
        for (int i = 0; i < m_points.size(); ++i) {
            const QPointF p = toScreen(m_points[i], plot);
            if (QLineF(p, s).length() <= 8.0) {
                return i;
            }
        }
        return -1;
    }

    QVector<QPointF> m_points;
    int m_drag = -1;
};

QWidget *makeStubTab(const QString &text, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *lay = new QVBoxLayout(w);
    auto *lab = new QLabel(text, w);
    lab->setWordWrap(true);
    lab->setAlignment(Qt::AlignCenter);
    lab->setStyleSheet(QStringLiteral("color:#aaa;"));
    lay->addStretch(1);
    lay->addWidget(lab);
    lay->addStretch(1);
    return w;
}

} // namespace

ColorGradingEditor::ColorGradingEditor(FxSlot *slot, QWidget *parent)
    : QWidget(parent)
    , m_slot(slot)
{
    setObjectName(QStringLiteral("colorGradingEditor"));
    buildUi();
    loadFromSlot();
}

void ColorGradingEditor::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("aefxPluginTitleBar"));
    auto *tb = new QHBoxLayout(titleBar);
    tb->setContentsMargins(10, 6, 10, 6);
    auto *title = new QLabel(tr("Color Grading"), titleBar);
    title->setObjectName(QStringLiteral("aefxPluginTitle"));
    QFont tf = title->font();
    tf.setPointSize(11);
    tf.setBold(true);
    title->setFont(tf);
    tb->addWidget(title);
    tb->addStretch(1);
    auto *about = new QLabel(QStringLiteral("<a href='#' style='color:#7ab4ff;'>%1</a>")
                                 .arg(tr("About")),
                             titleBar);
    about->setTextFormat(Qt::RichText);
    about->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    tb->addWidget(about);
    auto *help = new QToolButton(titleBar);
    help->setAutoRaise(true);
    help->setText(QStringLiteral("?"));
    help->setFixedSize(22, 22);
    tb->addWidget(help);
    root->addWidget(titleBar);

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("cgBody"));
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(6, 4, 6, 6);
    bodyLay->setSpacing(8);

    m_leftTabs = new QTabWidget(body);
    m_leftTabs->setObjectName(QStringLiteral("cgLeftTabs"));
    m_wheelsHost = new QWidget(m_leftTabs);
    m_leftTabs->addTab(makeStubTab(tr("Input LUT controls coming soon."), m_leftTabs),
                       tr("Input LUT"));
    m_leftTabs->addTab(m_wheelsHost, tr("Color Wheels"));
    m_leftTabs->addTab(makeStubTab(tr("Highlight/shadow wheels coming soon."), m_leftTabs),
                       tr("HL Color Wheels"));
    m_leftTabs->addTab(makeStubTab(tr("Utilities coming soon."), m_leftTabs), tr("Utilities"));
    m_leftTabs->setCurrentIndex(1);

    m_rightTabs = new QTabWidget(body);
    m_rightTabs->setObjectName(QStringLiteral("cgRightTabs"));
    m_curvesHost = new QWidget(m_rightTabs);
    m_rightTabs->addTab(m_curvesHost, tr("Color Curves"));
    m_rightTabs->addTab(makeStubTab(tr("HSL curves coming soon."), m_rightTabs), tr("HSL Curves"));
    m_rightTabs->addTab(makeStubTab(tr("HSL controls coming soon."), m_rightTabs), tr("HSL"));
    m_rightTabs->addTab(makeStubTab(tr("Look LUT coming soon."), m_rightTabs), tr("Look LUT"));

    bodyLay->addWidget(m_leftTabs, 3);
    bodyLay->addWidget(m_rightTabs, 2);
    root->addWidget(body, 1);

    rebuildWheelsPage();
    rebuildCurvesPage();
}

void ColorGradingEditor::rebuildWheelsPage()
{
    if (auto *old = m_wheelsHost->layout()) {
        QLayoutItem *it = nullptr;
        while ((it = old->takeAt(0)) != nullptr) {
            if (it->widget()) {
                it->widget()->deleteLater();
            }
            delete it;
        }
        delete old;
    }

    auto *lay = new QHBoxLayout(m_wheelsHost);
    lay->setContentsMargins(6, 8, 6, 6);
    lay->setSpacing(8);

    const QVariantMap p0 = m_slot ? loadParams(*m_slot) : QVariantMap{};

    struct Spec {
        QString title;
        QString key;
        bool unity;
        double defRgb;
        double defY;
    };
    const Spec specs[] = {
        {tr("Lift"), QStringLiteral("lift"), false, 0.0, 0.0},
        {tr("Gamma"), QStringLiteral("gamma"), true, 1.0, 1.0},
        {tr("Gain"), QStringLiteral("gain"), true, 1.0, 1.0},
        {tr("Offset"), QStringLiteral("offset"), false, 0.0, 0.0},
    };

    for (const Spec &sp : specs) {
        auto *col = new QWidget(m_wheelsHost);
        auto *colLay = new QVBoxLayout(col);
        colLay->setContentsMargins(0, 0, 0, 0);
        colLay->setSpacing(4);

        auto *head = new QHBoxLayout();
        head->addStretch(1);
        auto *reset = new QToolButton(col);
        reset->setAutoRaise(true);
        reset->setToolTip(tr("Reset"));
        reset->setIcon(IconFactory::iconFromSvgBody(
            QStringLiteral("<path d='M12.5 8a4.5 4.5 0 11-1.3-3.1M11 2.5v3h3' fill='none' "
                           "stroke='currentColor' stroke-width='1.3'/>"),
            12));
        reset->setFixedSize(20, 20);
        head->addWidget(reset);
        colLay->addLayout(head);

        auto *wheel = new ColorWheelWidget(sp.title, sp.unity, col);
        const WheelValues wv = readWheel(p0, sp.key, sp.defRgb, sp.defY);
        wheel->setValues(wv.r, wv.g, wv.b, wv.y);

        auto *spins = new QGridLayout();
        spins->setHorizontalSpacing(4);
        spins->setVerticalSpacing(2);
        const char *labels[] = {"R", "G", "B", "Y"};
        QDoubleSpinBox *boxes[4] = {};
        for (int i = 0; i < 4; ++i) {
            auto *lab = new QLabel(QString::fromLatin1(labels[i]), col);
            lab->setFixedWidth(12);
            auto *spin = new QDoubleSpinBox(col);
            spin->setDecimals(3);
            spin->setSingleStep(0.01);
            if (sp.unity) {
                spin->setRange(0.0, 2.0);
            } else {
                spin->setRange(-1.0, 1.0);
            }
            spin->setFixedWidth(72);
            boxes[i] = spin;
            spins->addWidget(lab, i, 0);
            spins->addWidget(spin, i, 1);
        }
        boxes[0]->setValue(wv.r);
        boxes[1]->setValue(wv.g);
        boxes[2]->setValue(wv.b);
        boxes[3]->setValue(wv.y);

        auto syncSpinsFromWheel = [=]() {
            boxes[0]->blockSignals(true);
            boxes[1]->blockSignals(true);
            boxes[2]->blockSignals(true);
            boxes[0]->setValue(wheel->r());
            boxes[1]->setValue(wheel->g());
            boxes[2]->setValue(wheel->b());
            boxes[0]->blockSignals(false);
            boxes[1]->blockSignals(false);
            boxes[2]->blockSignals(false);
            saveToSlot();
        };
        connect(wheel, &ColorWheelWidget::valuesChanged, this, syncSpinsFromWheel);

        auto syncWheelFromSpins = [=]() {
            wheel->blockSignals(true);
            wheel->setValues(boxes[0]->value(), boxes[1]->value(), boxes[2]->value(),
                             boxes[3]->value());
            wheel->blockSignals(false);
            saveToSlot();
        };
        for (QDoubleSpinBox *b : boxes) {
            connect(b, qOverload<double>(&QDoubleSpinBox::valueChanged), this, syncWheelFromSpins);
        }

        connect(reset, &QToolButton::clicked, this, [=]() {
            wheel->setValues(sp.defRgb, sp.defRgb, sp.defRgb, sp.defY);
            boxes[0]->setValue(sp.defRgb);
            boxes[1]->setValue(sp.defRgb);
            boxes[2]->setValue(sp.defRgb);
            boxes[3]->setValue(sp.defY);
            saveToSlot();
        });

        auto *row = new QHBoxLayout();
        row->addWidget(wheel, 1);
        row->addLayout(spins);
        colLay->addLayout(row, 1);
        lay->addWidget(col, 1);

        // stash key on wheel via property for saveToSlot
        wheel->setProperty("cgKey", sp.key);
        wheel->setProperty("cgUnity", sp.unity);
        for (int i = 0; i < 4; ++i) {
            boxes[i]->setProperty("cgKey", sp.key);
            boxes[i]->setProperty("cgChan", i);
        }
    }
}

void ColorGradingEditor::rebuildCurvesPage()
{
    if (auto *old = m_curvesHost->layout()) {
        QLayoutItem *it = nullptr;
        while ((it = old->takeAt(0)) != nullptr) {
            if (it->widget()) {
                it->widget()->deleteLater();
            }
            delete it;
        }
        delete old;
    }

    auto *lay = new QVBoxLayout(m_curvesHost);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);

    auto *top = new QHBoxLayout();
    auto *channel = new QComboBox(m_curvesHost);
    channel->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue")});
    top->addWidget(channel);
    top->addStretch(1);
    auto *reset = new QToolButton(m_curvesHost);
    reset->setAutoRaise(true);
    reset->setText(tr("Reset"));
    top->addWidget(reset);
    lay->addLayout(top);

    auto *curve = new CurveEditorWidget(m_curvesHost);
    const QVariantMap p0 = m_slot ? loadParams(*m_slot) : QVariantMap{};
    QVector<QPointF> pts;
    const QVariantList list = p0.value(QStringLiteral("curve.rgb")).toList();
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        pts.push_back(QPointF(m.value(QStringLiteral("x")).toDouble(),
                              m.value(QStringLiteral("y")).toDouble()));
    }
    if (pts.size() >= 2) {
        curve->setPoints(pts);
    }
    lay->addWidget(curve, 1);

    connect(curve, &CurveEditorWidget::pointsChanged, this, [this, curve]() {
        if (!m_slot) {
            return;
        }
        QVariantMap m = loadParams(*m_slot);
        QVariantList list;
        for (const QPointF &pt : curve->points()) {
            QVariantMap item;
            item.insert(QStringLiteral("x"), pt.x());
            item.insert(QStringLiteral("y"), pt.y());
            list.push_back(item);
        }
        m.insert(QStringLiteral("curve.rgb"), list);
        saveParams(m_slot, m);
    });
    connect(reset, &QToolButton::clicked, this, [this, curve]() {
        curve->setPoints({{0.0, 0.0}, {1.0, 1.0}});
        if (!m_slot) {
            return;
        }
        QVariantMap m = loadParams(*m_slot);
        m.remove(QStringLiteral("curve.rgb"));
        saveParams(m_slot, m);
    });
}

void ColorGradingEditor::loadFromSlot()
{
    // Wheels/curves already seeded in rebuild*; nothing else.
}

void ColorGradingEditor::saveToSlot()
{
    if (!m_slot || !m_wheelsHost) {
        return;
    }
    QVariantMap m = loadParams(*m_slot);
    const auto wheels = m_wheelsHost->findChildren<ColorWheelWidget *>();
    for (ColorWheelWidget *w : wheels) {
        const QString key = w->property("cgKey").toString();
        if (key.isEmpty()) {
            continue;
        }
        writeWheel(&m, key, {w->r(), w->g(), w->b(), w->y()});
    }
    // Prefer spin Y values if present
    for (QDoubleSpinBox *spin : m_wheelsHost->findChildren<QDoubleSpinBox *>()) {
        const QString key = spin->property("cgKey").toString();
        const int chan = spin->property("cgChan").toInt();
        if (key.isEmpty()) {
            continue;
        }
        const char *suffix[] = {".r", ".g", ".b", ".y"};
        if (chan >= 0 && chan < 4) {
            m.insert(key + QString::fromLatin1(suffix[chan]), spin->value());
        }
    }
    saveParams(m_slot, m);
}

} // namespace openvegas

#include "ColorGradingEditor.moc"
