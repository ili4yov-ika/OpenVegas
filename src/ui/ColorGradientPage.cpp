#include "ui/ColorGradientPage.h"

#include "io/VegLegacyGenerators.h"
#include "ui/VegasColorPicker.h"
#include "video/VegasGenerators.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {

constexpr double kPi = 3.14159265358979323846;

QColor pointColour(const QVariantMap &pt)
{
    const QVariantList c = pt.value(QStringLiteral("Color")).toList();
    if (c.size() < 4) {
        return Qt::black;
    }
    auto v = [](const QVariant &x) { return float(std::clamp(x.toDouble(), 0.0, 1.0)); };
    return QColor::fromRgbF(v(c[0]), v(c[1]), v(c[2]), v(c[3]));
}

void checker(QPainter &p, const QRect &r, int cell)
{
    for (int y = r.top(); y <= r.bottom(); y += cell) {
        for (int x = r.left(); x <= r.right(); x += cell) {
            const bool dark = (((x - r.left()) / cell) + ((y - r.top()) / cell)) % 2 == 0;
            p.fillRect(QRect(x, y, cell, cell).intersected(r),
                       dark ? QColor(0x99, 0x99, 0x99) : QColor(0xdd, 0xdd, 0xdd));
        }
    }
}

} // namespace

/** The gradient itself with its control points on it, numbered, draggable. */
class GradientCanvas : public QWidget {
public:
    explicit GradientCanvas(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(218, 158);
        setMouseTracking(true);
    }
    QVariantMap params;
    int selected = 0;
    std::function<void(int)> onSelect;
    std::function<void(int, double, double)> onMove;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        checker(p, r, 8);
        p.drawImage(r.topLeft(), renderVegasGenerator(vegColorGradientClsid(), params, r.size()));
        p.setRenderHint(QPainter::Antialiasing, true);
        // The centre the distances are measured from.
        const QPointF c(r.width() * 0.5, r.height() * 0.5);
        p.setPen(QPen(Qt::white, 1));
        p.drawLine(c + QPointF(-6, 0), c + QPointF(6, 0));
        p.drawLine(c + QPointF(0, -6), c + QPointF(0, 6));
        const QVariantList pts = params.value(QStringLiteral("Points")).toList();
        QFont f = font();
        f.setPixelSize(9);
        f.setBold(true);
        p.setFont(f);
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF at = pointAt(pts[i].toMap());
            p.setPen(QPen(i == selected ? QColor(0xff, 0xd0, 0x40) : QColor(0x20, 0x20, 0x20), 1.5));
            p.setBrush(Qt::white);
            p.drawEllipse(at, 6.5, 6.5);
            p.setPen(Qt::black);
            p.drawText(QRectF(at.x() - 6, at.y() - 6, 12, 12), Qt::AlignCenter, QString::number(i + 1));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const QVariantList pts = params.value(QStringLiteral("Points")).toList();
        int best = -1;
        double bestD = 12.0;
        for (int i = 0; i < pts.size(); ++i) {
            const double d = QLineF(pointAt(pts[i].toMap()), e->position()).length();
            if (d < bestD) {
                best = i;
                bestD = d;
            }
        }
        m_dragging = best;
        if (best >= 0 && onSelect) {
            onSelect(best);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging >= 0 && (e->buttons() & Qt::LeftButton) && onMove) {
            onMove(m_dragging, std::clamp(e->position().x() / (width() - 1), 0.0, 1.0),
                   std::clamp(e->position().y() / (height() - 1), 0.0, 1.0));
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_dragging = -1; }

private:
    QPointF pointAt(const QVariantMap &pt) const
    {
        return {pt.value(QStringLiteral("X")).toDouble() * (width() - 1),
                pt.value(QStringLiteral("Y")).toDouble() * (height() - 1)};
    }
    int m_dragging = -1;
};

/**
 * The DirectX plug-ins' colour picker: a red, a green and a blue bar, each shaded from its
 * colour with that channel off to on, with a marker at the value, and a spectrum to pick
 * from. The R, G, B and A boxes beside it carry the numbers.
 */
class DxtColorBars : public QWidget {
public:
    explicit DxtColorBars(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(190, 96);
    }
    QColor colour = Qt::white;
    std::function<void(const QColor &)> onPick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int ch = 0; ch < 3; ++ch) {
            const QRect bar = barRect(ch);
            QColor from = colour, to = colour;
            from.setAlpha(255);
            to.setAlpha(255);
            if (ch == 0) {
                from.setRed(0);
                to.setRed(255);
            } else if (ch == 1) {
                from.setGreen(0);
                to.setGreen(255);
            } else {
                from.setBlue(0);
                to.setBlue(255);
            }
            QLinearGradient g(bar.topLeft(), bar.topRight());
            g.setColorAt(0, from);
            g.setColorAt(1, to);
            p.fillRect(bar, g);
            p.setPen(QColor(0x40, 0x40, 0x40));
            p.drawRect(bar);
            const int v = ch == 0 ? colour.red() : ch == 1 ? colour.green() : colour.blue();
            const double x = bar.left() + v / 255.0 * bar.width();
            QPolygonF tri;
            tri << QPointF(x, bar.bottom() + 1) << QPointF(x - 4, bar.bottom() + 7)
                << QPointF(x + 4, bar.bottom() + 7);
            p.setBrush(Qt::white);
            p.drawPolygon(tri);
            const QString name = QStringLiteral("RGB").mid(ch, 1);
            p.setPen(palette().color(QPalette::WindowText));
            p.drawText(QRect(0, bar.top() - 2, 12, bar.height() + 4), Qt::AlignCenter, name);
        }
        // The spectrum: hues across, light to dark down.
        const QRect sp = spectrumRect();
        QImage img(sp.size(), QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                img.setPixel(x, y, spectrumAt(x, y, img.size()).rgb());
            }
        }
        p.drawImage(sp.topLeft(), img);
    }
    void mousePressEvent(QMouseEvent *e) override { pick(e->position().toPoint()); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton) {
            pick(e->position().toPoint());
        }
    }

private:
    QRect barRect(int ch) const { return QRect(14, 2 + ch * 22, width() - 18, 10); }
    QRect spectrumRect() const { return QRect(14, 68, width() - 18, height() - 70); }
    static QColor spectrumAt(int x, int y, const QSize &s)
    {
        const float h = float(x) / std::max(1, s.width() - 1);
        const float l = 1.0f - float(y) / std::max(1, s.height() - 1);
        return QColor::fromHslF(std::min(h, 0.9999f), 1.0f, 0.15f + 0.7f * l);
    }
    void pick(const QPoint &pos)
    {
        QColor c = colour;
        for (int ch = 0; ch < 3; ++ch) {
            const QRect bar = barRect(ch).adjusted(0, -2, 0, 9);
            if (bar.contains(pos)) {
                const int v = std::clamp(int(std::lround((pos.x() - bar.left()) * 255.0 / bar.width())), 0, 255);
                if (ch == 0) c.setRed(v); else if (ch == 1) c.setGreen(v); else c.setBlue(v);
            }
        }
        const QRect sp = spectrumRect();
        if (sp.contains(pos)) {
            const QColor s = spectrumAt(pos.x() - sp.left(), pos.y() - sp.top(), sp.size());
            c.setRgb(s.red(), s.green(), s.blue(), colour.alpha());
        }
        if (c != colour && onPick) {
            onPick(c);
        }
    }
};

ColorGradientPage::ColorGradientPage(QWidget *parent)
    : GeneratorPage(parent)
{
    auto *root = new QVBoxLayout(this);
    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("&Gradient Type:"), this));
    m_type = new QComboBox(this);
    m_type->addItems({tr("Linear"), tr("Elliptical"), tr("Rectangular")});
    top->addWidget(m_type);
    top->addSpacing(12);
    top->addWidget(new QLabel(tr("&Aspect Ratio Angle:"), this));
    m_angle = new QLineEdit(this);
    m_angle->setFixedWidth(56);
    top->addWidget(m_angle);
    top->addStretch(1);
    root->addLayout(top);

    auto *body = new QHBoxLayout();
    auto *left = new QVBoxLayout();
    left->addWidget(new QLabel(tr("Control Points:"), this));
    m_canvas = new GradientCanvas(this);
    left->addWidget(m_canvas);
    auto *buttons = new QHBoxLayout();
    auto button = [this, buttons](const QString &text, const QString &tip, std::function<void()> fn) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, std::move(fn));
        buttons->addWidget(b);
    };
    button(QStringLiteral("⊕"), tr("Add a new gradient control point"), [this] {
        QVariantList pts = points();
        QVariantMap pt;
        pt.insert(QStringLiteral("X"), 0.5);
        pt.insert(QStringLiteral("Y"), 0.5);
        pt.insert(QStringLiteral("Color"), QVariantList{1.0, 1.0, 1.0, 1.0});
        if (pts.size() >= 9) {
            return; // the plug-in keeps nine
        }
        pts.insert(m_selected + 1, pt);
        setPoints(pts);
        select(m_selected + 1);
    });
    button(QStringLiteral("⊖"), tr("Remove currently selected gradient control point"), [this] {
        QVariantList pts = points();
        if (pts.size() <= 2) {
            return;
        }
        pts.removeAt(m_selected);
        setPoints(pts);
        select(std::min(m_selected, int(pts.size()) - 1));
    });
    button(QStringLiteral("←"), tr("Select previous gradient control point"),
           [this] { select(std::max(0, m_selected - 1)); });
    button(QStringLiteral("→"), tr("Select next gradient control point"),
           [this] { select(std::min(int(points().size()) - 1, m_selected + 1)); });
    button(QStringLiteral("⇈"), tr("Increase gradient control point position number"), [this] {
        QVariantList pts = points();
        if (m_selected + 1 < pts.size()) {
            pts.swapItemsAt(m_selected, m_selected + 1);
            setPoints(pts);
            select(m_selected + 1);
        }
    });
    button(QStringLiteral("⇊"), tr("Decrease gradient control point position number"), [this] {
        QVariantList pts = points();
        if (m_selected > 0) {
            pts.swapItemsAt(m_selected, m_selected - 1);
            setPoints(pts);
            select(m_selected - 1);
        }
    });
    buttons->addStretch(1);
    left->addLayout(buttons);
    left->addStretch(1);
    body->addLayout(left);

    auto *props = new QGroupBox(tr("Control Point Properties"), this);
    auto *pl = new QGridLayout(props);
    pl->addWidget(new QLabel(tr("&Distance:"), props), 0, 0);
    m_distance = new QLineEdit(props);
    m_distance->setFixedWidth(80);
    pl->addWidget(m_distance, 0, 1, Qt::AlignLeft);
    pl->addWidget(new QLabel(tr("Color:"), props), 1, 0, 1, 2);
    m_bars = new DxtColorBars(props);
    pl->addWidget(m_bars, 2, 0, 1, 2);
    auto *rgba = new QHBoxLayout();
    auto spin = [props, rgba](const QString &name) {
        rgba->addWidget(new QLabel(name, props));
        auto *s = new QSpinBox(props);
        s->setRange(0, 255);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(36);
        rgba->addWidget(s);
        return s;
    };
    m_r = spin(QStringLiteral("R"));
    m_g = spin(QStringLiteral("G"));
    m_b = spin(QStringLiteral("B"));
    m_a = spin(QStringLiteral("A"));
    auto *eyedropper = new QToolButton(props);
    eyedropper->setText(QStringLiteral("⌖"));
    eyedropper->setToolTip(tr("Pick a color from the screen"));
    rgba->addWidget(eyedropper);
    pl->addLayout(rgba, 3, 0, 1, 2);
    pl->setRowStretch(4, 1);
    body->addWidget(props, 1);
    root->addLayout(body);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);
    root->addStretch(1);

    connect(m_type, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_block && i >= 0) {
            m_params.insert(QStringLiteral("GradientType"), i);
            edited();
        }
    });
    connect(m_angle, &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const double deg = m_angle->text().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (ok) {
            m_params.insert(QStringLiteral("AspectAngle"), deg * kPi / 180.0);
            edited();
        } else {
            refresh();
        }
    });
    connect(m_distance, &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const double d = m_distance->text().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (ok) {
            setDistance(d);
        } else {
            refresh();
        }
    });
    m_canvas->onSelect = [this](int i) { select(i); };
    m_canvas->onMove = [this](int i, double x, double y) {
        QVariantList pts = points();
        if (i < 0 || i >= pts.size()) {
            return;
        }
        QVariantMap pt = pts[i].toMap();
        pt.insert(QStringLiteral("X"), x);
        pt.insert(QStringLiteral("Y"), y);
        pts[i] = pt;
        setPoints(pts);
    };
    m_bars->onPick = [this](const QColor &c) { setPointColor(c); };
    for (QSpinBox *s : {m_r, m_g, m_b, m_a}) {
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (!m_block) {
                setPointColor(QColor(m_r->value(), m_g->value(), m_b->value(), m_a->value()));
            }
        });
    }
    connect(eyedropper, &QToolButton::clicked, this, [this] {
        pickScreenColor([this](const QColor &c) {
            QColor picked = c;
            picked.setAlpha(m_a->value());
            setPointColor(picked);
        });
    });
}

void ColorGradientPage::setParams(const QVariantMap &params)
{
    m_params = params;
    m_selected = std::clamp(m_selected, 0, std::max(0, int(points().size()) - 1));
    refresh();
}

QVariantList ColorGradientPage::points() const
{
    return m_params.value(QStringLiteral("Points")).toList();
}

void ColorGradientPage::setPoints(const QVariantList &points)
{
    m_params.insert(QStringLiteral("Points"), points);
    edited();
}

void ColorGradientPage::select(int index)
{
    m_selected = std::clamp(index, 0, std::max(0, int(points().size()) - 1));
    refresh();
}

double ColorGradientPage::distanceOf(const QVariantMap &pt) const
{
    const double a = m_params.value(QStringLiteral("AspectAngle")).toDouble();
    const double du = pt.value(QStringLiteral("X")).toDouble() - 0.5;
    const double dv = pt.value(QStringLiteral("Y")).toDouble() - 0.5;
    if (m_params.value(QStringLiteral("GradientType")).toInt() == 0) {
        return du * std::cos(a) + dv * std::sin(a);
    }
    return std::hypot(du, dv);
}

void ColorGradientPage::setDistance(double d)
{
    QVariantList pts = points();
    if (m_selected >= pts.size()) {
        return;
    }
    QVariantMap pt = pts[m_selected].toMap();
    const double a = m_params.value(QStringLiteral("AspectAngle")).toDouble();
    double ux = std::cos(a), uy = std::sin(a);
    if (m_params.value(QStringLiteral("GradientType")).toInt() != 0) {
        // Radial: keep the point's direction from the centre, change how far out it is.
        const double du = pt.value(QStringLiteral("X")).toDouble() - 0.5;
        const double dv = pt.value(QStringLiteral("Y")).toDouble() - 0.5;
        const double len = std::hypot(du, dv);
        if (len > 1e-9) {
            ux = du / len;
            uy = dv / len;
        }
    }
    pt.insert(QStringLiteral("X"), 0.5 + d * ux);
    pt.insert(QStringLiteral("Y"), 0.5 + d * uy);
    pts[m_selected] = pt;
    setPoints(pts);
}

void ColorGradientPage::setPointColor(const QColor &c)
{
    QVariantList pts = points();
    if (m_selected >= pts.size()) {
        return;
    }
    QVariantMap pt = pts[m_selected].toMap();
    pt.insert(QStringLiteral("Color"),
              QVariantList{double(c.redF()), double(c.greenF()), double(c.blueF()), double(c.alphaF())});
    pts[m_selected] = pt;
    setPoints(pts);
}

void ColorGradientPage::edited()
{
    refresh();
    emit paramsEdited(m_params);
}

void ColorGradientPage::refresh()
{
    m_block = true;
    m_type->setCurrentIndex(std::clamp(m_params.value(QStringLiteral("GradientType")).toInt(), 0, 2));
    m_angle->setText(QString::number(m_params.value(QStringLiteral("AspectAngle")).toDouble() * 180.0 / kPi,
                                     'f', 1));
    m_canvas->params = m_params;
    m_canvas->selected = m_selected;
    m_canvas->update();
    const QVariantList pts = points();
    if (m_selected < pts.size()) {
        const QVariantMap pt = pts[m_selected].toMap();
        m_distance->setText(QString::number(distanceOf(pt), 'f', 3));
        const QColor c = pointColour(pt);
        m_bars->colour = c;
        m_bars->update();
        m_r->setValue(c.red());
        m_g->setValue(c.green());
        m_b->setValue(c.blue());
        m_a->setValue(c.alpha());
    }
    m_hint->setText(tr("Point %1 of %2").arg(m_selected + 1).arg(pts.size()));
    m_block = false;
}

} // namespace openvegas
