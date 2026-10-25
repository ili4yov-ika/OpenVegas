#include "ui/CreditRollPage.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {

enum LineKind { Header = 0, Single = 1, Dual = 2 };

/** One line of the credits as the table shows it. */
struct Line {
    LineKind kind = Single;
    QString left, right;
};

QVector<Line> linesOf(const QString &text)
{
    QVector<Line> out;
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    for (const QString &raw : t.split(QLatin1Char('\n'))) {
        if (raw.isEmpty()) {
            continue;
        }
        Line l;
        if (raw.startsWith(QLatin1Char(' '))) {
            l.kind = Header;
            l.left = raw.mid(1);
        } else if (raw.contains(QLatin1Char('\t'))) {
            l.kind = Dual;
            l.left = raw.section(QLatin1Char('\t'), 0, 0);
            l.right = raw.section(QLatin1Char('\t'), 1);
        } else {
            l.left = raw;
        }
        out.push_back(l);
    }
    return out;
}

QString textOf(const QVector<Line> &lines)
{
    QString out;
    for (const Line &l : lines) {
        if (l.kind == Header) {
            out += QLatin1Char(' ') + l.left;
        } else if (l.kind == Dual) {
            out += l.left + QLatin1Char('\t') + l.right;
        } else {
            out += l.left;
        }
        out += QStringLiteral("\r\n");
    }
    return out;
}

/** The icon VEGAS marks a line with: a "Hdr" card, lines of text, or two columns of them. */
QIcon kindIcon(LineKind kind)
{
    QPixmap pm(20, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.fillRect(1, 1, 18, 14, QColor(0xf4, 0xf4, 0xf4));
    p.setPen(QColor(0x40, 0x40, 0x80));
    p.drawRect(1, 1, 17, 13);
    if (kind == Header) {
        QFont f;
        f.setPixelSize(7);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(1, 1, 18, 14), Qt::AlignCenter, QStringLiteral("Hdr"));
    } else {
        p.setPen(kind == Single ? QColor(0x30, 0x40, 0xd0) : QColor(0x20, 0x20, 0x20));
        for (int y = 4; y < 14; y += 3) {
            if (kind == Single) {
                p.drawLine(4, y, 15, y);
            } else {
                p.drawLine(3, y, 8, y);
                p.drawLine(11, y, 16, y);
            }
        }
    }
    return QIcon(pm);
}

QColor colourOf(const QVariant &v, const QColor &fallback)
{
    const QVariantList l = v.toList();
    if (l.size() < 3) {
        return fallback;
    }
    auto c = [](const QVariant &x) { return float(std::clamp(x.toDouble(), 0.0, 1.0)); };
    return QColor::fromRgbF(c(l[0]), c(l[1]), c(l[2]), l.size() > 3 ? c(l[3]) : 1.0f);
}

QVariantList listOf(const QColor &c)
{
    return {double(c.redF()), double(c.greenF()), double(c.blueF()), double(c.alphaF())};
}

void paintSwatch(QToolButton *b, const QColor &c)
{
    QPixmap pm(34, 14);
    QPainter p(&pm);
    for (int y = 0; y < pm.height(); y += 4) {
        for (int x = 0; x < pm.width(); x += 4) {
            p.fillRect(x, y, 4, 4, ((x + y) / 4) % 2 ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x99, 0x99, 0x99));
        }
    }
    p.fillRect(pm.rect(), c);
    p.setPen(Qt::black);
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    b->setIcon(QIcon(pm));
    b->setIconSize(pm.size());
}

/** "Connect sides with" (strings 1877–1886) and the fill each repeats. */
const QVector<QPair<QString, QString>> &connectors()
{
    static const QVector<QPair<QString, QString>> list = {
        {QStringLiteral("Spaces"), QString()},
        {QStringLiteral("Dots (.....)"), QStringLiteral(".")},
        {QStringLiteral("Underline (____)"), QStringLiteral("_")},
        {QStringLiteral("Dashes (----)"), QStringLiteral("-")},
        {QStringLiteral("Centered Circles"), QStringLiteral("◦")},
        {QStringLiteral("Centered Dots"), QStringLiteral("·")},
        {QStringLiteral("Solid Squares"), QStringLiteral("▪")},
        {QStringLiteral("Hollow Squares"), QStringLiteral("▫")},
        {QStringLiteral("Sparse Dots (. . . .)"), QStringLiteral(". ")},
        {QStringLiteral("Very Sparse Dots (.  .  .)"), QStringLiteral(".  ")},
    };
    return list;
}

} // namespace

/** The Position picture: the frame in black with the text area in blue between Left and Right. */
class CreditPlacement : public QWidget {
public:
    explicit CreditPlacement(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(140, 112);
    }
    double left = 0.1, right = 0.9, top = 0.3;
    bool timed = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect frame = rect().adjusted(0, 0, -1, -1);
        p.fillRect(frame, QColor(0x80, 0x80, 0x80));
        const int x0 = int(frame.left() + left * frame.width());
        const int x1 = int(frame.left() + right * frame.width());
        const int y0 = timed ? int(frame.top() + top * frame.height()) : frame.top();
        const QRect area(x0, y0, std::max(4, x1 - x0), frame.bottom() - y0);
        p.fillRect(area, QColor(0x00, 0x00, 0x8c));
        p.setPen(Qt::white);
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(area.adjusted(2, 4, -2, 0), Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("Credit Text"));
        p.setPen(QColor(0x30, 0x30, 0x30));
        p.setBrush(QColor(0xd8, 0xd8, 0xd8));
        p.drawRect(QRect(x0 - 4, frame.center().y() - 5, 8, 10));
        p.drawRect(QRect(x1 - 4, frame.center().y() - 5, 8, 10));
    }
};

CreditRollPage::CreditRollPage(QWidget *parent)
    : GeneratorPage(parent)
{
    auto *root = new QHBoxLayout(this);

    // Left: the toolbar and the credits.
    auto *left = new QVBoxLayout();
    auto *tools = new QHBoxLayout();
    auto tool = [this, tools](const QString &text, const QString &tip, auto fn) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        tools->addWidget(b);
    };
    tool(QStringLiteral("→≡"), tr("Insert a row (Insert)"), [this] { insertRow(); });
    tool(QStringLiteral("←≡"), tr("Delete current row or selected rows (Delete)"),
         [this] { deleteRows(); });
    tool(QStringLiteral("⇩A"), tr("Import text from a file..."), [this] { importText(); });
    tool(QStringLiteral("⇧A"), tr("Export all or selected text to a file..."),
         [this] { exportText(); });
    tools->addStretch(1);
    left->addLayout(tools);
    left->addWidget(new QLabel(tr("Credits &text:"), this));
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({QString(), tr("Item Text"), QString()});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 26);
    m_table->setMinimumWidth(300);
    connect(m_table, &QTableWidget::itemChanged, this, [this] {
        if (!m_block) {
            tableEdited();
        }
    });
    // Clicking a line's icon steps it through header, single item and two columns.
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column != 0 || !m_table->item(row, 0) || m_table->item(row, 0)->data(Qt::UserRole).isNull()) {
            return;
        }
        QTableWidgetItem *icon = m_table->item(row, 0);
        icon->setData(Qt::UserRole, (icon->data(Qt::UserRole).toInt() + 1) % 3);
        tableEdited();
    });
    left->addWidget(m_table, 1);
    root->addLayout(left, 1);

    // Right: the two tabs.
    auto *tabs = new QTabWidget(this);
    tabs->setFixedWidth(280);

    auto *props = new QWidget(tabs);
    auto *pl = new QVBoxLayout(props);
    auto *effectRow = new QHBoxLayout();
    effectRow->addWidget(new QLabel(tr("&Effect:"), props));
    m_effect = new QComboBox(props);
    m_effect->addItems({tr("Scrolling Credits"), tr("Timed Sequence")});
    effectRow->addWidget(m_effect, 1);
    pl->addLayout(effectRow);
    auto *pos = new QGroupBox(tr("&Position"), props);
    auto *posl = new QHBoxLayout(pos);
    m_placement = new CreditPlacement(pos);
    posl->addWidget(m_placement);
    auto *edges = new QGridLayout();
    m_left = new QLineEdit(pos);
    m_right = new QLineEdit(pos);
    m_top = new QLineEdit(pos);
    int r = 0;
    for (const auto &[label, edit] : {std::pair<QString, QLineEdit *>{tr("&Left:"), m_left},
                                      {tr("&Right:"), m_right}, {tr("&Top:"), m_top}}) {
        edit->setFixedWidth(56);
        edges->addWidget(new QLabel(label, pos), r, 0);
        edges->addWidget(edit, r++, 1);
    }
    edges->setRowStretch(r, 1);
    posl->addLayout(edges);
    pl->addWidget(pos);
    auto *ep = new QGroupBox(tr("Effect parameters"), props);
    auto *epl = new QVBoxLayout(ep);
    m_effectParams = new QStackedWidget(ep);
    auto *scroll = new QWidget(m_effectParams);
    auto *sl = new QHBoxLayout(scroll);
    sl->addWidget(new QLabel(tr("Scroll &direction:"), scroll));
    m_direction = new QComboBox(scroll);
    m_direction->addItems({tr("Up (Forward)"), tr("Down (Backward)")});
    sl->addWidget(m_direction, 1);
    m_effectParams->addWidget(scroll);
    auto *timed = new QWidget(m_effectParams);
    auto *tl = new QGridLayout(timed);
    m_in = new QComboBox(timed);
    m_in->addItems({tr("None"), tr("Fade In"), tr("Fast Fade In"), tr("Enter from Left"),
                    tr("Enter from Right"), tr("Enter from Top"), tr("Enter from Bottom"),
                    tr("Slow Enter from Left"), tr("Slow Enter from Right"), tr("Slow Enter from Top"),
                    tr("Slow Enter from Bottom"), tr("Zoom In"), tr("Wipe In Left"), tr("Wipe In Right"),
                    tr("Wipe In Up"), tr("Wipe In Down")});
    m_out = new QComboBox(timed);
    m_out->addItems({tr("None"), tr("Fade Out"), tr("Fast Fade Out"), tr("Exit to Left"),
                     tr("Exit to Right"), tr("Exit to Top"), tr("Exit to Bottom"), tr("Slow Exit to Left"),
                     tr("Slow Exit to Right"), tr("Slow Exit to Top"), tr("Slow Exit to Bottom"),
                     tr("Zoom Out"), tr("Wipe Out Left"), tr("Wipe Out Right"), tr("Wipe Out Up"),
                     tr("Wipe Out Down")});
    m_display = new QComboBox(timed);
    m_display->addItems({tr("Up to Next Header"), tr("One at a Time"), tr("Header and Single Subitem"),
                         tr("Up to Next Header or Single Item")});
    tl->addWidget(new QLabel(tr("&In:"), timed), 0, 0);
    tl->addWidget(m_in, 0, 1);
    tl->addWidget(new QLabel(tr("&Out:"), timed), 1, 0);
    tl->addWidget(m_out, 1, 1);
    tl->addWidget(new QLabel(tr("&Display:"), timed), 2, 0);
    tl->addWidget(m_display, 2, 1);
    m_effectParams->addWidget(timed);
    epl->addWidget(m_effectParams);
    pl->addWidget(ep);
    pl->addStretch(1);
    tabs->addTab(props, tr("Properties"));

    auto *styles = new QWidget(tabs);
    auto *stl = new QGridLayout(styles);
    stl->addWidget(new QLabel(tr("&Name:"), styles), 0, 0);
    m_styleName = new QComboBox(styles);
    m_styleName->addItems({tr("Header"), tr("Single Item"), tr("Dual Item")});
    stl->addWidget(m_styleName, 0, 1, 1, 2);
    m_font = new QFontComboBox(styles);
    stl->addWidget(m_font, 1, 0, 1, 2);
    m_size = new QComboBox(styles);
    m_size->setEditable(true);
    for (int s : {8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72}) {
        m_size->addItem(QString::number(s));
    }
    stl->addWidget(m_size, 1, 2);
    m_color = new QToolButton(styles);
    stl->addWidget(m_color, 2, 2);
    m_bold = new QCheckBox(tr("Bold"), styles);
    stl->addWidget(m_bold, 2, 0, 1, 2);
    auto sliderRow = [styles, stl](int row, const QString &label, QSlider *&slider, QLineEdit *&edit,
                                   int lo, int hi) {
        stl->addWidget(new QLabel(label, styles), row, 0);
        slider = new QSlider(Qt::Horizontal, styles);
        slider->setRange(lo, hi);
        stl->addWidget(slider, row, 1);
        edit = new QLineEdit(styles);
        edit->setFixedWidth(52);
        stl->addWidget(edit, row, 2);
    };
    sliderRow(3, tr("T&racking:"), m_trackingSlider, m_tracking, -100, 100);
    sliderRow(4, tr("Space &below:"), m_spacingSlider, m_spacing, 0, 100);
    stl->addWidget(new QLabel(tr("Connect &sides with:"), styles), 5, 0);
    m_connector = new QComboBox(styles);
    for (const auto &c : connectors()) {
        m_connector->addItem(c.first);
    }
    stl->addWidget(m_connector, 5, 1, 1, 2);
    stl->setRowStretch(6, 1);
    stl->addWidget(new QLabel(tr("&Background color:"), styles), 7, 0, 1, 2);
    m_background = new QToolButton(styles);
    stl->addWidget(m_background, 7, 2);
    tabs->addTab(styles, tr("Styles"));
    root->addWidget(tabs);

    // Wiring.
    connect(m_effect, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_block) {
            m_params.insert(QStringLiteral("Effect"), i);
            edited();
        }
    });
    auto edge = [this](QLineEdit *e, const char *key) {
        connect(e, &QLineEdit::editingFinished, this, [this, e, key] {
            bool ok = false;
            const double v = e->text().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
            if (ok) {
                m_params.insert(QLatin1String(key), std::clamp(v, 0.0, 1.0));
                edited();
            } else {
                refresh();
            }
        });
    };
    edge(m_left, "Left");
    edge(m_right, "Right");
    edge(m_top, "Top");
    auto combo = [this](QComboBox *c, const char *key) {
        connect(c, &QComboBox::currentIndexChanged, this, [this, key](int i) {
            if (!m_block && i >= 0) {
                m_params.insert(QLatin1String(key), i);
                edited();
            }
        });
    };
    combo(m_direction, "ScrollDirection");
    combo(m_in, "InEffect");
    combo(m_out, "OutEffect");
    combo(m_display, "Display");
    connect(m_styleName, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        if (!m_block) {
            setStyle(currentStyleIndex(), QStringLiteral("Font"), f.family());
        }
    });
    connect(m_size, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        bool ok = false;
        const int v = t.toInt(&ok);
        if (!m_block && ok && v > 0) {
            setStyle(currentStyleIndex(), QStringLiteral("Size"), v);
        }
    });
    connect(m_bold, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_block) {
            setStyle(currentStyleIndex(), QStringLiteral("Bold"), on ? 1 : 0);
        }
    });
    connect(m_color, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(colourOf(style(currentStyleIndex()).value(QStringLiteral("Color")),
                                                         Qt::white),
                                                this, tr("Text Color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            setStyle(currentStyleIndex(), QStringLiteral("Color"), listOf(c));
        }
    });
    connect(m_background, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(colourOf(m_params.value(QStringLiteral("Background")), Qt::black),
                                                this, tr("Background Color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_params.insert(QStringLiteral("Background"), listOf(c));
            edited();
        }
    });
    auto slidAndTyped = [this](QSlider *slider, QLineEdit *edit, const char *key, double scale) {
        connect(slider, &QSlider::valueChanged, this, [this, key, scale](int v) {
            if (!m_block) {
                setStyle(currentStyleIndex(), QLatin1String(key), v / scale);
            }
        });
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key] {
            bool ok = false;
            const double v = edit->text().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
            if (ok) {
                setStyle(currentStyleIndex(), QLatin1String(key), v);
            } else {
                refresh();
            }
        });
    };
    slidAndTyped(m_trackingSlider, m_tracking, "Tracking", 100.0);
    slidAndTyped(m_spacingSlider, m_spacing, "SpaceBelow", 100.0);
    connect(m_connector, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_block && i >= 0) {
            m_params.insert(QStringLiteral("Connector"), connectors().value(i).second);
            edited();
        }
    });
}

void CreditRollPage::setParams(const QVariantMap &params)
{
    m_params = params;
    refresh();
}

int CreditRollPage::currentStyleIndex() const
{
    return std::max(0, m_styleName->currentIndex());
}

QVariantMap CreditRollPage::style(int index) const
{
    return m_params.value(QStringLiteral("Styles")).toList().value(index).toMap();
}

void CreditRollPage::setStyle(int index, const QString &key, const QVariant &value)
{
    QVariantList styles = m_params.value(QStringLiteral("Styles")).toList();
    while (styles.size() < 5) {
        styles.append(QVariantMap());
    }
    // A two-column item has a style per side; the page edits them together.
    const QVector<int> targets = index == Dual ? QVector<int>{2, 3} : QVector<int>{index};
    for (int t : targets) {
        QVariantMap s = styles[t].toMap();
        s.insert(key, value);
        styles[t] = s;
    }
    m_params.insert(QStringLiteral("Styles"), styles);
    edited();
}

void CreditRollPage::edited()
{
    refresh();
    emit paramsEdited(m_params);
}

void CreditRollPage::tableEdited()
{
    QVector<Line> lines;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *icon = m_table->item(row, 0);
        if (!icon || icon->data(Qt::UserRole).isNull()) {
            // The placeholder row: typing into it adds a line.
            const QString typed = m_table->item(row, 1) ? m_table->item(row, 1)->text() : QString();
            if (!typed.isEmpty() && typed != tr("<Insert Text Here>")) {
                lines.push_back({Single, typed, {}});
            }
            continue;
        }
        Line l;
        l.kind = LineKind(icon->data(Qt::UserRole).toInt());
        l.left = m_table->item(row, 1) ? m_table->item(row, 1)->text() : QString();
        l.right = m_table->item(row, 2) ? m_table->item(row, 2)->text() : QString();
        lines.push_back(l);
    }
    m_params.insert(QStringLiteral("Text"), textOf(lines));
    edited();
}

void CreditRollPage::insertRow()
{
    QVector<Line> lines = linesOf(m_params.value(QStringLiteral("Text")).toString());
    const int at = std::clamp(m_table->currentRow(), 0, int(lines.size()));
    lines.insert(at, Line{Single, tr("New Item"), {}});
    m_params.insert(QStringLiteral("Text"), textOf(lines));
    edited();
}

void CreditRollPage::deleteRows()
{
    QVector<Line> lines = linesOf(m_params.value(QStringLiteral("Text")).toString());
    QList<int> rows;
    for (const QTableWidgetSelectionRange &r : m_table->selectedRanges()) {
        for (int i = r.topRow(); i <= r.bottomRow(); ++i) {
            rows << i;
        }
    }
    if (rows.isEmpty() && m_table->currentRow() >= 0) {
        rows << m_table->currentRow();
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) {
        if (row < lines.size()) {
            lines.removeAt(row);
        }
    }
    m_params.insert(QStringLiteral("Text"), textOf(lines));
    edited();
}

void CreditRollPage::importText()
{
    // VEGAS's credit text file (strings 6047–6057): a line per credit, a leading space for
    // a header, a tab between two columns, two slashes for a comment.
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Text"), {}, tr("Text File (*.txt)"));
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QStringList kept;
    for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        QString l = line;
        l.remove(QLatin1Char('\r'));
        if (!l.startsWith(QLatin1String("//"))) {
            kept << l;
        }
    }
    m_params.insert(QStringLiteral("Text"), textOf(linesOf(kept.join(QLatin1Char('\n')))));
    edited();
}

void CreditRollPage::exportText()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Text"), {}, tr("Text File (*.txt)"));
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    f.write(m_params.value(QStringLiteral("Text")).toString().replace(QStringLiteral("\r\n"), QStringLiteral("\n")).toUtf8());
}

void CreditRollPage::refresh()
{
    m_block = true;
    const QVector<Line> lines = linesOf(m_params.value(QStringLiteral("Text")).toString());
    const QVariantList styles = m_params.value(QStringLiteral("Styles")).toList();
    m_table->setRowCount(int(lines.size()) + 1);
    for (int row = 0; row < lines.size(); ++row) {
        const Line &l = lines[row];
        auto *icon = new QTableWidgetItem(kindIcon(l.kind), QString());
        icon->setData(Qt::UserRole, int(l.kind));
        icon->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        icon->setToolTip(l.kind == Header ? tr("Header") : l.kind == Single ? tr("Single Item") : tr("Dual Item"));
        m_table->setItem(row, 0, icon);
        // Each line in its own style's face and weight, as the list shows it in VEGAS.
        const QVariantMap st = styles.value(l.kind == Header ? 0 : l.kind == Single ? 1 : 2).toMap();
        QFont f(st.value(QStringLiteral("Font"), QStringLiteral("Arial")).toString());
        f.setBold(st.value(QStringLiteral("Bold")).toInt() != 0);
        f.setPointSizeF(l.kind == Header ? 12.0 : 10.0);
        auto *a = new QTableWidgetItem(l.left);
        a->setFont(f);
        m_table->setItem(row, 1, a);
        auto *b = new QTableWidgetItem(l.right);
        b->setFont(f);
        if (l.kind != Dual) {
            b->setFlags(Qt::ItemIsEnabled);
        }
        m_table->setItem(row, 2, b);
    }
    const int last = int(lines.size());
    m_table->setItem(last, 0, new QTableWidgetItem());
    auto *hint = new QTableWidgetItem(tr("<Insert Text Here>"));
    QFont hf = hint->font();
    hf.setItalic(true);
    hf.setBold(true);
    hint->setFont(hf);
    m_table->setItem(last, 1, hint);
    m_table->setItem(last, 2, new QTableWidgetItem());

    const bool timed = m_params.value(QStringLiteral("Effect")).toInt() == 1;
    m_effect->setCurrentIndex(timed ? 1 : 0);
    m_left->setText(QString::number(m_params.value(QStringLiteral("Left"), 0.1).toDouble(), 'f', 3));
    m_right->setText(QString::number(m_params.value(QStringLiteral("Right"), 0.9).toDouble(), 'f', 3));
    m_top->setText(QString::number(m_params.value(QStringLiteral("Top"), 0.3).toDouble(), 'f', 3));
    m_top->setEnabled(timed); // a scroll starts below the frame, so it has no top
    m_placement->left = m_params.value(QStringLiteral("Left"), 0.1).toDouble();
    m_placement->right = m_params.value(QStringLiteral("Right"), 0.9).toDouble();
    m_placement->top = m_params.value(QStringLiteral("Top"), 0.3).toDouble();
    m_placement->timed = timed;
    m_placement->update();
    m_effectParams->setCurrentIndex(timed ? 1 : 0);
    m_direction->setCurrentIndex(std::clamp(m_params.value(QStringLiteral("ScrollDirection")).toInt(), 0, 1));
    m_in->setCurrentIndex(std::clamp(m_params.value(QStringLiteral("InEffect")).toInt(), 0, m_in->count() - 1));
    m_out->setCurrentIndex(std::clamp(m_params.value(QStringLiteral("OutEffect")).toInt(), 0, m_out->count() - 1));
    m_display->setCurrentIndex(
        std::clamp(m_params.value(QStringLiteral("Display")).toInt(), 0, m_display->count() - 1));

    const QVariantMap st = style(currentStyleIndex() == Dual ? 2 : currentStyleIndex());
    m_font->setCurrentFont(QFont(st.value(QStringLiteral("Font"), QStringLiteral("Arial")).toString()));
    m_size->setCurrentText(QString::number(st.value(QStringLiteral("Size"), 20).toInt()));
    paintSwatch(m_color, colourOf(st.value(QStringLiteral("Color")), Qt::white));
    m_bold->setChecked(st.value(QStringLiteral("Bold")).toInt() != 0);
    const double tracking = st.value(QStringLiteral("Tracking")).toDouble();
    const double spacing = st.value(QStringLiteral("SpaceBelow")).toDouble();
    m_trackingSlider->setValue(int(std::lround(tracking * 100.0)));
    m_tracking->setText(QString::number(tracking, 'f', 2));
    m_spacingSlider->setValue(int(std::lround(spacing * 100.0)));
    m_spacing->setText(QString::number(spacing, 'f', 2));
    const QString conn = m_params.value(QStringLiteral("Connector")).toString();
    int ci = 0;
    for (int i = 0; i < connectors().size(); ++i) {
        if (connectors()[i].second == conn) {
            ci = i;
        }
    }
    m_connector->setCurrentIndex(ci);
    m_connector->setEnabled(currentStyleIndex() == Dual);
    paintSwatch(m_background, colourOf(m_params.value(QStringLiteral("Background")), Qt::black));
    m_block = false;
}

} // namespace openvegas
