#include "ui/VegasColorPicker.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace openvegas {

namespace {

/** A colour's channels in the picker's terms: three in 0..1 for the model, plus alpha. */
struct Channels {
    double c[3] = {0, 0, 0};
};

Channels channelsOf(const QColor &c, bool hsl)
{
    Channels ch;
    if (hsl) {
        float h = 0, s = 0, l = 0, a = 0;
        c.getHslF(&h, &s, &l, &a);
        ch.c[0] = std::max(0.0f, h);
        ch.c[1] = s;
        ch.c[2] = l;
    } else {
        ch.c[0] = c.redF();
        ch.c[1] = c.greenF();
        ch.c[2] = c.blueF();
    }
    return ch;
}

QColor colourOf(const Channels &ch, bool hsl, double alpha)
{
    auto cl = [](double v) { return float(std::clamp(v, 0.0, 1.0)); };
    return hsl ? QColor::fromHslF(cl(ch.c[0]), cl(ch.c[1]), cl(ch.c[2]), cl(alpha))
               : QColor::fromRgbF(cl(ch.c[0]), cl(ch.c[1]), cl(ch.c[2]), cl(alpha));
}

/** The plane: which channel runs across (x), which up (y), which is on the strip. */
struct PlaneAxes {
    int x = 1, y = 2, slider = 0;
};

PlaneAxes axesFor(int planeIndex)
{
    switch (planeIndex) {
    case 1: return {0, 2, 1}; // (RxB) G
    case 2: return {0, 1, 2}; // (RxG) B
    default: return {1, 2, 0}; // (GxB) R
    }
}

void paintChecker(QPainter &p, const QRect &r, int cell = 5)
{
    p.save();
    p.setClipRect(r);
    for (int y = r.top(); y < r.bottom() + 1; y += cell) {
        for (int x = r.left(); x < r.right() + 1; x += cell) {
            const bool dark = (((x - r.left()) / cell) + ((y - r.top()) / cell)) % 2 == 0;
            p.fillRect(x, y, cell, cell, dark ? QColor(0x9a, 0x9a, 0x9a) : QColor(0xde, 0xde, 0xde));
        }
    }
    p.restore();
}

} // namespace

/** Two channels as a square, the third held at the picker's value. */
class VegasColorPlane : public QWidget {
public:
    explicit VegasColorPlane(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(96, 96);
        setCursor(Qt::CrossCursor);
    }
    Channels channels;
    PlaneAxes axes;
    bool hsl = false;
    std::function<void(const Channels &)> onPick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QImage img(size(), QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                Channels ch = channels;
                ch.c[axes.x] = double(x) / (img.width() - 1);
                ch.c[axes.y] = 1.0 - double(y) / (img.height() - 1);
                line[x] = colourOf(ch, hsl, 1.0).rgb();
            }
        }
        QPainter p(this);
        p.drawImage(0, 0, img);
        // The current colour: a cross in a ring, white over a black outline.
        const QPointF at(channels.c[axes.x] * (width() - 1), (1.0 - channels.c[axes.y]) * (height() - 1));
        p.setRenderHint(QPainter::Antialiasing, true);
        for (const auto &[pen, w] : {std::pair<QColor, double>{Qt::black, 3.0}, {Qt::white, 1.4}}) {
            p.setPen(QPen(pen, w));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, 6.0, 6.0);
            p.drawLine(at + QPointF(-11, 0), at + QPointF(-7, 0));
            p.drawLine(at + QPointF(7, 0), at + QPointF(11, 0));
            p.drawLine(at + QPointF(0, -11), at + QPointF(0, -7));
            p.drawLine(at + QPointF(0, 7), at + QPointF(0, 11));
        }
    }
    void mousePressEvent(QMouseEvent *e) override { pick(e->position()); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton) {
            pick(e->position());
        }
    }

private:
    void pick(const QPointF &pos)
    {
        channels.c[axes.x] = std::clamp(pos.x() / (width() - 1), 0.0, 1.0);
        channels.c[axes.y] = std::clamp(1.0 - pos.y() / (height() - 1), 0.0, 1.0);
        update();
        if (onPick) {
            onPick(channels);
        }
    }
};

/** A vertical strip for one value: the third channel, or alpha over a checkerboard. */
class VegasColorStrip : public QWidget {
public:
    explicit VegasColorStrip(bool alpha, QWidget *parent)
        : QWidget(parent)
        , m_alpha(alpha)
    {
        setFixedSize(20, 112);
        setCursor(Qt::PointingHandCursor);
    }
    Channels channels;
    PlaneAxes axes;
    bool hsl = false;
    double alphaValue = 1.0;
    std::function<void(double)> onPick;

    double value() const { return m_alpha ? alphaValue : channels.c[axes.slider]; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect bar(3, 8, width() - 6, height() - 16);
        if (m_alpha) {
            paintChecker(p, bar, 4);
        }
        QLinearGradient g(bar.topLeft(), bar.bottomLeft());
        for (int i = 0; i <= 8; ++i) {
            const double v = 1.0 - i / 8.0;
            Channels ch = channels;
            QColor c;
            if (m_alpha) {
                c = colourOf(ch, hsl, v);
            } else {
                ch.c[axes.slider] = v;
                c = colourOf(ch, hsl, 1.0);
            }
            g.setColorAt(i / 8.0, c);
        }
        p.fillRect(bar, g);
        p.setPen(QColor(0x50, 0x50, 0x50));
        p.drawRect(bar.adjusted(0, 0, -1, -1));
        // The handle: a rounded tab across the strip at the value.
        const double y = bar.top() + (1.0 - value()) * (bar.height() - 1);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QColor(0x30, 0x30, 0x30));
        p.setBrush(QColor(0xc8, 0xc8, 0xc8));
        p.drawRoundedRect(QRectF(0.5, y - 5, width() - 1, 10), 3, 3);
    }
    void mousePressEvent(QMouseEvent *e) override { pick(e->position().y()); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton) {
            pick(e->position().y());
        }
    }

private:
    void pick(double y)
    {
        const double v = std::clamp(1.0 - (y - 8.0) / (height() - 17.0), 0.0, 1.0);
        if (m_alpha) {
            alphaValue = v;
        } else {
            channels.c[axes.slider] = v;
        }
        update();
        if (onPick) {
            onPick(v);
        }
    }
    bool m_alpha = false;
};

namespace {

/** One-shot screen colour pick: the next click anywhere samples the pixel under it. */
class EyedropperFilter : public QObject {
public:
    explicit EyedropperFilter(std::function<void(const QColor &)> done)
        : m_done(std::move(done))
    {
        qApp->installEventFilter(this);
        QApplication::setOverrideCursor(Qt::CrossCursor);
    }

protected:
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress) {
            const QPoint at = QCursor::pos();
            QColor c;
            if (QScreen *screen = QGuiApplication::screenAt(at)) {
                const QPoint local = at - screen->geometry().topLeft();
                const QImage px = screen->grabWindow(0, local.x(), local.y(), 1, 1).toImage();
                if (!px.isNull()) {
                    c = px.pixelColor(0, 0);
                }
            }
            finish(c);
            return true;
        }
        if (e->type() == QEvent::KeyPress) {
            finish({});
            return true;
        }
        return false;
    }

private:
    void finish(const QColor &c)
    {
        qApp->removeEventFilter(this);
        QApplication::restoreOverrideCursor();
        if (c.isValid() && m_done) {
            m_done(c);
        }
        deleteLater();
    }
    std::function<void(const QColor &)> m_done;
};

QString channelText(const QColor &c)
{
    auto f = [](double v) { return QString::number(v, 'f', 1); };
    // VEGAS writes one decimal unless more are needed to say the value.
    auto g = [&](double v) {
        const QString one = f(v);
        return std::fabs(one.toDouble() - v) < 1e-4 ? one : QString::number(v, 'f', 3);
    };
    return QStringLiteral("%1, %2, %3, %4")
        .arg(g(c.redF()), g(c.greenF()), g(c.blueF()), g(c.alphaF()));
}

} // namespace

VegasColorPicker::VegasColorPicker(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    m_row = new QWidget(this);
    auto *row = new QHBoxLayout(m_row);
    row->setContentsMargins(0, 0, 0, 0);
    m_swatch = new QToolButton(m_row);
    m_swatch->setFixedHeight(20);
    m_swatch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_swatch->setToolTip(tr("Show or hide the color picker"));
    connect(m_swatch, &QToolButton::clicked, this, [this] { setExpanded(!isExpanded()); });
    row->addWidget(m_swatch, 1);
    m_text = new QLineEdit(m_row);
    m_text->setMinimumWidth(130);
    connect(m_text, &QLineEdit::editingFinished, this, &VegasColorPicker::applyText);
    row->addWidget(m_text, 1);
    root->addWidget(m_row);

    m_panel = new QWidget(this);
    auto *panel = new QVBoxLayout(m_panel);
    panel->setContentsMargins(0, 0, 0, 0);
    auto *combos = new QHBoxLayout();
    m_model = new QComboBox(m_panel);
    m_model->addItems({QStringLiteral("RGB"), QStringLiteral("HSL")});
    m_plane = new QComboBox(m_panel);
    m_eyedropper = new QToolButton(m_panel);
    m_eyedropper->setText(QStringLiteral("⌖"));
    m_eyedropper->setToolTip(tr("Pick a color from the screen"));
    combos->addWidget(m_model);
    combos->addWidget(m_plane);
    combos->addWidget(m_eyedropper);
    combos->addStretch(1);
    panel->addLayout(combos);
    auto *pick = new QHBoxLayout();
    m_planeWidget = new VegasColorPlane(m_panel);
    m_channelStrip = new VegasColorStrip(false, m_panel);
    m_alphaStrip = new VegasColorStrip(true, m_panel);
    pick->addWidget(m_planeWidget);
    pick->addSpacing(12);
    pick->addWidget(m_channelStrip);
    pick->addSpacing(8);
    pick->addWidget(m_alphaStrip);
    pick->addStretch(1);
    panel->addLayout(pick);
    root->addWidget(m_panel);
    m_panel->setVisible(false);

    auto fillPlanes = [this] {
        const QSignalBlocker block(m_plane);
        const int keep = std::max(0, m_plane->currentIndex());
        m_plane->clear();
        if (m_model->currentIndex() == 1) {
            m_plane->addItems({QStringLiteral("(SxL) H"), QStringLiteral("(HxL) S"),
                               QStringLiteral("(HxS) L")});
        } else {
            m_plane->addItems({QStringLiteral("(GxB) R"), QStringLiteral("(RxB) G"),
                               QStringLiteral("(RxG) B")});
        }
        m_plane->setCurrentIndex(keep);
    };
    fillPlanes();
    connect(m_model, &QComboBox::currentIndexChanged, this, [this, fillPlanes] {
        fillPlanes();
        syncFromColor();
    });
    connect(m_plane, &QComboBox::currentIndexChanged, this, [this] { syncFromColor(); });
    connect(m_eyedropper, &QToolButton::clicked, this, &VegasColorPicker::startEyedropper);

    m_planeWidget->onPick = [this](const Channels &ch) {
        setFromPicker(colourOf(ch, m_model->currentIndex() == 1, m_color.alphaF()));
    };
    m_channelStrip->onPick = [this](double) {
        setFromPicker(colourOf(m_channelStrip->channels, m_model->currentIndex() == 1, m_color.alphaF()));
    };
    m_alphaStrip->onPick = [this](double a) {
        QColor c = m_color;
        c.setAlphaF(float(a));
        setFromPicker(c);
    };
    syncFromColor();
}

void VegasColorPicker::setColor(const QColor &c)
{
    if (!c.isValid() || c == m_color) {
        return;
    }
    m_color = c;
    syncFromColor();
}

bool VegasColorPicker::isExpanded() const
{
    return !m_panel->isHidden();
}

void VegasColorPicker::setExpanded(bool expanded)
{
    if (expanded == !m_panel->isHidden()) {
        return;
    }
    m_panel->setVisible(expanded);
    emit expandedChanged(expanded);
}

void VegasColorPicker::syncFromColor()
{
    m_block = true;
    // The swatch shows the colour over a checkerboard, so a transparent one reads as such.
    QPixmap pm(240, 16);
    {
        QPainter p(&pm);
        paintChecker(p, pm.rect(), 4);
        p.fillRect(pm.rect(), m_color);
    }
    m_swatch->setIcon(QIcon(pm));
    m_swatch->setIconSize(QSize(std::max(40, m_swatch->width() - 8), 12));
    m_swatch->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_text->setText(channelText(m_color));

    const bool hsl = m_model->currentIndex() == 1;
    const Channels ch = channelsOf(m_color, hsl);
    const PlaneAxes axes = axesFor(m_plane->currentIndex());
    for (VegasColorStrip *s : {m_channelStrip, m_alphaStrip}) {
        s->channels = ch;
        s->axes = axes;
        s->hsl = hsl;
        s->alphaValue = m_color.alphaF();
        s->update();
    }
    m_planeWidget->channels = ch;
    m_planeWidget->axes = axes;
    m_planeWidget->hsl = hsl;
    m_planeWidget->update();
    m_block = false;
}

void VegasColorPicker::applyText()
{
    const QStringList parts = m_text->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        syncFromColor();
        return;
    }
    double v[4] = {0, 0, 0, 1};
    for (int i = 0; i < std::min<qsizetype>(4, parts.size()); ++i) {
        bool ok = false;
        const double d = parts[i].trimmed().toDouble(&ok);
        if (!ok) {
            syncFromColor();
            return;
        }
        v[i] = std::clamp(d, 0.0, 1.0);
    }
    setFromPicker(QColor::fromRgbF(float(v[0]), float(v[1]), float(v[2]), float(v[3])));
}

void VegasColorPicker::setFromPicker(const QColor &c)
{
    if (m_block || !c.isValid()) {
        return;
    }
    m_color = c;
    syncFromColor();
    emit colorChanged(m_color);
}

void pickScreenColor(std::function<void(const QColor &)> done)
{
    new EyedropperFilter(std::move(done));
}

void VegasColorPicker::startEyedropper()
{
    pickScreenColor([this](const QColor &c) {
        QColor picked = c;
        picked.setAlphaF(m_color.alphaF());
        setFromPicker(picked);
    });
}

} // namespace openvegas
