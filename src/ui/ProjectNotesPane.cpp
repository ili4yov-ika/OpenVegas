#include "ui/ProjectNotesPane.h"
#include "ui/IconFactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <cmath>

namespace openvegas {

namespace {

const QVector<QColor> kNoteColors = {
    QColor(0x6a, 0x3a, 0x8a), // purple
    QColor(0xc0, 0x3a, 0x2a), // red
    QColor(0x2a, 0x8a, 0x4a), // green
    QColor(0x2a, 0x5a, 0x9a), // blue
    QColor(0xb0, 0x7a, 0x20), // amber
    QColor(0x3a, 0x7a, 0x8a), // teal
};

QColor darker(const QColor &c)
{
    return c.darker(140);
}

} // namespace

class ProjectNotesPane::NoteCard : public QFrame {
public:
    NoteCard(ProjectNotesPane *pane, const Note &note, QWidget *parent = nullptr)
        : QFrame(parent)
        , m_pane(pane)
        , m_id(note.id)
    {
        setObjectName(QStringLiteral("noteCard"));
        setFrameShape(QFrame::NoFrame);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_head = new QWidget(this);
        m_head->setObjectName(QStringLiteral("noteCardHead"));
        m_head->setMinimumHeight(36);
        auto *headLay = new QHBoxLayout(m_head);
        headLay->setContentsMargins(8, 6, 6, 6);
        headLay->setSpacing(8);

        auto *titleCol = new QVBoxLayout;
        titleCol->setSpacing(2);
        titleCol->setContentsMargins(0, 0, 0, 0);
        m_titleEdit = new QLineEdit(m_head);
        m_titleEdit->setObjectName(QStringLiteral("noteCardTitle"));
        m_titleEdit->setText(note.title);
        m_titleEdit->setFrame(false);
        m_dateLabel = new QLabel(m_head);
        m_dateLabel->setObjectName(QStringLiteral("noteCardDate"));
        m_dateLabel->setText(pane->formatCreated(note.created));
        titleCol->addWidget(m_titleEdit);
        titleCol->addWidget(m_dateLabel);
        headLay->addLayout(titleCol, 1);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(2);
        actions->setContentsMargins(0, 0, 0, 0);

        auto mk = [this](const QString &tip, const QString &svg) {
            auto *b = IconFactory::toolButton(m_head, tip, svg);
            b->setObjectName(QStringLiteral("noteCardBtn"));
            b->setFixedSize(22, 20);
            return b;
        };

        auto *pinAdd = mk(ProjectNotesPane::tr("Add marker at playhead"),
                          QStringLiteral("<path d='M8 2v9M5 8l3 3 3-3' fill='none' stroke='currentColor' "
                                         "stroke-width='1.3'/><circle cx='8' cy='13.5' r='1.2'/>"));
        auto *pinGo = mk(ProjectNotesPane::tr("Go to note timecode"),
                         QStringLiteral("<path d='M5 3v10l7-5z' fill='currentColor'/>"));
        m_tcLabel = new QLabel(m_head);
        m_tcLabel->setObjectName(QStringLiteral("noteCardTc"));
        m_tcLabel->setText(pane->formatTimecode(note.timecodeSec));
        m_tcLabel->setToolTip(ProjectNotesPane::tr("Note timecode"));

        m_warnBtn = mk(ProjectNotesPane::tr("Toggle warning"),
                       QStringLiteral("<path d='M8 2l6.5 12H1.5L8 2zm0 4v4M8 12.2h.01' fill='none' "
                                      "stroke='#e0b040' stroke-width='1.3'/>"));
        m_warnBtn->setCheckable(true);
        m_warnBtn->setChecked(note.warning);
        auto *delBtn = mk(ProjectNotesPane::tr("Delete note"),
                          QStringLiteral("<path d='M4 4l8 8M12 4L4 12' stroke='#d06060' stroke-width='1.5'/>"));
        auto *moreBtn = mk(ProjectNotesPane::tr("More"),
                           QStringLiteral("<circle cx='3' cy='8' r='1.2'/><circle cx='8' cy='8' r='1.2'/>"
                                          "<circle cx='13' cy='8' r='1.2'/>"));

        actions->addWidget(pinAdd);
        actions->addWidget(pinGo);
        actions->addWidget(m_tcLabel);
        actions->addWidget(m_warnBtn);
        actions->addWidget(delBtn);
        actions->addWidget(moreBtn);
        headLay->addLayout(actions);
        root->addWidget(m_head);

        m_body = new QPlainTextEdit(this);
        m_body->setObjectName(QStringLiteral("noteCardBody"));
        m_body->setPlaceholderText(ProjectNotesPane::tr("Enter note..."));
        m_body->setPlainText(note.body);
        m_body->setMaximumHeight(72);
        m_body->setMinimumHeight(40);
        root->addWidget(m_body);

        applyColor(note.color);

        connect(m_titleEdit, &QLineEdit::editingFinished, this, [this]() { pushUpdate(); });
        connect(m_body, &QPlainTextEdit::textChanged, this, [this]() { pushUpdate(); });
        connect(m_warnBtn, &QToolButton::toggled, this, [this](bool) { pushUpdate(); });
        connect(delBtn, &QToolButton::clicked, this, [this]() { m_pane->deleteNote(m_id); });
        connect(pinGo, &QToolButton::clicked, this, [this]() {
            if (Note *n = m_pane->findNote(m_id)) {
                m_pane->requestSeek(n->timecodeSec);
            }
        });
        connect(pinAdd, &QToolButton::clicked, this, [this]() {
            if (Note *n = m_pane->findNote(m_id)) {
                if (m_pane->m_playheadFn) {
                    n->timecodeSec = m_pane->m_playheadFn();
                    m_tcLabel->setText(m_pane->formatTimecode(n->timecodeSec));
                    m_pane->persistSoon();
                }
            }
        });
        connect(moreBtn, &QToolButton::clicked, this, [this, moreBtn]() {
            QMenu menu(this);
            auto *res = menu.addAction(ProjectNotesPane::tr("Mark as resolved"));
            res->setCheckable(true);
            if (Note *n = m_pane->findNote(m_id)) {
                res->setChecked(n->resolved);
            }
            auto *dup = menu.addAction(ProjectNotesPane::tr("Duplicate"));
            menu.addSeparator();
            auto *del = menu.addAction(ProjectNotesPane::tr("Delete"));
            QAction *picked = menu.exec(moreBtn->mapToGlobal(QPoint(0, moreBtn->height())));
            if (!picked) {
                return;
            }
            if (picked == res) {
                if (Note *n = m_pane->findNote(m_id)) {
                    n->resolved = res->isChecked();
                    m_pane->persistSoon();
                    m_pane->rebuildList();
                }
            } else if (picked == dup) {
                if (Note *n = m_pane->findNote(m_id)) {
                    Note copy = *n;
                    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    copy.created = QDateTime::currentDateTime();
                    copy.title = n->title + QStringLiteral(" (copy)");
                    m_pane->m_notes.push_back(copy);
                    m_pane->persistSoon();
                    m_pane->rebuildList();
                }
            } else if (picked == del) {
                m_pane->deleteNote(m_id);
            }
        });
    }

    void applyColor(const QColor &color)
    {
        m_head->setStyleSheet(QStringLiteral(
                                  "QWidget#noteCardHead {"
                                  " background-color: %1;"
                                  " border-bottom: 1px solid %2;"
                                  "}")
                                  .arg(color.name(), darker(color).name()));
    }

    void pushUpdate()
    {
        if (Note *n = m_pane->findNote(m_id)) {
            n->title = m_titleEdit->text().trimmed();
            if (n->title.isEmpty()) {
                n->title = ProjectNotesPane::tr("Untitled note");
            }
            n->body = m_body->toPlainText();
            n->warning = m_warnBtn->isChecked();
            m_pane->persistSoon();
        }
    }

private:
    ProjectNotesPane *m_pane = nullptr;
    QString m_id;
    QWidget *m_head = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QLabel *m_dateLabel = nullptr;
    QLabel *m_tcLabel = nullptr;
    QToolButton *m_warnBtn = nullptr;
    QPlainTextEdit *m_body = nullptr;
};

ProjectNotesPane::ProjectNotesPane(QWidget *parent)
    : QWidget(parent)
    , m_draftColor(kNoteColors.first())
{
    setObjectName(QStringLiteral("projectNotesPane"));
    buildUi();
    restoreSettings();
    rebuildList();
}

void ProjectNotesPane::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("notesHeader"));
    header->setFixedHeight(28);
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(10, 4, 10, 4);
    headerLay->addStretch(1);
    m_hideResolvedCheck = new QCheckBox(tr("Hide resolved notes"), header);
    m_hideResolvedCheck->setObjectName(QStringLiteral("notesHideResolved"));
    headerLay->addWidget(m_hideResolvedCheck);
    root->addWidget(header);

    connect(m_hideResolvedCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_hideResolved = on;
        persistSoon();
        rebuildList();
    });

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("notesScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_listHost = new QWidget;
    m_listHost->setObjectName(QStringLiteral("notesListHost"));
    m_listLay = new QVBoxLayout(m_listHost);
    m_listLay->setContentsMargins(8, 8, 8, 8);
    m_listLay->setSpacing(8);
    m_listLay->addStretch(1);
    m_scroll->setWidget(m_listHost);
    root->addWidget(m_scroll, 1);

    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("notesFooter"));
    footer->setFixedHeight(34);
    auto *footLay = new QHBoxLayout(footer);
    footLay->setContentsMargins(8, 4, 8, 4);
    footLay->setSpacing(6);

    m_colorBtn = new QToolButton(footer);
    m_colorBtn->setObjectName(QStringLiteral("notesColorBtn"));
    m_colorBtn->setFixedSize(22, 22);
    m_colorBtn->setToolTip(tr("Note color"));
    m_colorBtn->setPopupMode(QToolButton::InstantPopup);
    auto *colorMenu = new QMenu(m_colorBtn);
    for (const QColor &col : kNoteColors) {
        auto *a = colorMenu->addAction(QString());
        QPixmap pm(14, 14);
        pm.fill(col);
        a->setIcon(QIcon(pm));
        a->setData(col);
        connect(a, &QAction::triggered, this, [this, col]() {
            m_draftColor = col;
            m_colorBtn->setStyleSheet(
                QStringLiteral("QToolButton#notesColorBtn { background:%1; border:1px solid #555; }")
                    .arg(col.name()));
        });
    }
    m_colorBtn->setMenu(colorMenu);
    m_colorBtn->setStyleSheet(
        QStringLiteral("QToolButton#notesColorBtn { background:%1; border:1px solid #555; }")
            .arg(m_draftColor.name()));
    footLay->addWidget(m_colorBtn);

    m_labelEdit = new QLineEdit(footer);
    m_labelEdit->setObjectName(QStringLiteral("notesLabelEdit"));
    m_labelEdit->setPlaceholderText(tr("Enter default label"));
    footLay->addWidget(m_labelEdit, 1);

    m_addBtn = new QToolButton(footer);
    m_addBtn->setObjectName(QStringLiteral("notesAddBtn"));
    m_addBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addBtn->setText(tr("Add new note"));
    m_addBtn->setIcon(IconFactory::iconFromSvgBody(
        QStringLiteral("<path d='M8 3v10M3 8h10' stroke='currentColor' stroke-width='1.5'/>"), 14,
        QColor(0xe0, 0xe0, 0xe0)));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    footLay->addWidget(m_addBtn);
    root->addWidget(footer);

    connect(m_addBtn, &QToolButton::clicked, this, &ProjectNotesPane::addNote);
    connect(m_labelEdit, &QLineEdit::returnPressed, this, &ProjectNotesPane::addNote);
}

QString ProjectNotesPane::formatTimecode(double sec) const
{
    const int measure = 1 + static_cast<int>(std::floor(std::max(0.0, sec)));
    const int ticks = static_cast<int>(std::round((sec - std::floor(sec)) * 1000.0)) % 1000;
    // Vegas-like measures display used elsewhere: 1.1.000 — notes screenshot uses 3:1:050
    const int bar = measure;
    const int beat = 1;
    return QStringLiteral("%1:%2:%3")
        .arg(bar)
        .arg(beat)
        .arg(ticks, 3, 10, QChar('0'));
}

QString ProjectNotesPane::formatCreated(const QDateTime &dt) const
{
    return QLocale::system().toString(dt, QStringLiteral("dddd, MMMM d, yyyy h:mm:ss AP"));
}

QColor ProjectNotesPane::nextColor() const
{
    return m_draftColor.isValid() ? m_draftColor : kNoteColors[m_colorIndex % kNoteColors.size()];
}

ProjectNotesPane::Note *ProjectNotesPane::findNote(const QString &id)
{
    for (Note &n : m_notes) {
        if (n.id == id) {
            return &n;
        }
    }
    return nullptr;
}

void ProjectNotesPane::addNote()
{
    Note n;
    n.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    n.title = m_labelEdit->text().trimmed();
    if (n.title.isEmpty()) {
        n.title = tr("Untitled note");
    }
    n.body.clear();
    n.color = nextColor();
    n.created = QDateTime::currentDateTime();
    n.timecodeSec = m_playheadFn ? m_playheadFn() : 0.0;
    m_notes.push_back(n);
    m_labelEdit->clear();
    m_colorIndex = (m_colorIndex + 1) % kNoteColors.size();
    persistSoon();
    rebuildList();
    // Scroll to bottom
    QTimer::singleShot(0, this, [this]() {
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
}

void ProjectNotesPane::deleteNote(const QString &id)
{
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].id == id) {
            m_notes.removeAt(i);
            break;
        }
    }
    persistSoon();
    rebuildList();
}

void ProjectNotesPane::updateNote(const Note &)
{
    persistSoon();
}

void ProjectNotesPane::rebuildList()
{
    while (QLayoutItem *it = m_listLay->takeAt(0)) {
        if (auto *w = it->widget()) {
            w->deleteLater();
        }
        delete it;
    }

    for (const Note &n : m_notes) {
        if (m_hideResolved && n.resolved) {
            continue;
        }
        m_listLay->addWidget(new NoteCard(this, n, m_listHost));
    }
    m_listLay->addStretch(1);
}

void ProjectNotesPane::persistSoon()
{
    // Immediate save is fine for small note lists
    saveSettings();
}

void ProjectNotesPane::saveSettings() const
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("notes/hideResolved"), m_hideResolved);
    s.setValue(QStringLiteral("notes/draftLabel"), m_labelEdit ? m_labelEdit->text() : QString());
    s.setValue(QStringLiteral("notes/draftColor"), m_draftColor.name());

    QJsonArray arr;
    for (const Note &n : m_notes) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), n.id);
        o.insert(QStringLiteral("title"), n.title);
        o.insert(QStringLiteral("body"), n.body);
        o.insert(QStringLiteral("color"), n.color.name());
        o.insert(QStringLiteral("created"), n.created.toString(Qt::ISODate));
        o.insert(QStringLiteral("timecode"), n.timecodeSec);
        o.insert(QStringLiteral("warning"), n.warning);
        o.insert(QStringLiteral("resolved"), n.resolved);
        arr.append(o);
    }
    s.setValue(QStringLiteral("notes/items"),
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void ProjectNotesPane::restoreSettings()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    m_hideResolved = s.value(QStringLiteral("notes/hideResolved"), false).toBool();
    if (m_hideResolvedCheck) {
        m_hideResolvedCheck->setChecked(m_hideResolved);
    }
    if (m_labelEdit) {
        m_labelEdit->setText(s.value(QStringLiteral("notes/draftLabel")).toString());
    }
    const QString col = s.value(QStringLiteral("notes/draftColor")).toString();
    if (!col.isEmpty()) {
        m_draftColor = QColor(col);
        if (m_colorBtn) {
            m_colorBtn->setStyleSheet(
                QStringLiteral("QToolButton#notesColorBtn { background:%1; border:1px solid #555; }")
                    .arg(m_draftColor.name()));
        }
    }

    m_notes.clear();
    const QByteArray json = s.value(QStringLiteral("notes/items")).toString().toUtf8();
    if (!json.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        if (doc.isArray()) {
            for (const QJsonValue &v : doc.array()) {
                const QJsonObject o = v.toObject();
                Note n;
                n.id = o.value(QStringLiteral("id")).toString();
                n.title = o.value(QStringLiteral("title")).toString();
                n.body = o.value(QStringLiteral("body")).toString();
                n.color = QColor(o.value(QStringLiteral("color")).toString());
                n.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
                n.timecodeSec = o.value(QStringLiteral("timecode")).toDouble();
                n.warning = o.value(QStringLiteral("warning")).toBool();
                n.resolved = o.value(QStringLiteral("resolved")).toBool();
                if (n.id.isEmpty()) {
                    n.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                }
                if (!n.color.isValid()) {
                    n.color = kNoteColors.first();
                }
                if (!n.created.isValid()) {
                    n.created = QDateTime::currentDateTime();
                }
                m_notes.push_back(n);
            }
        }
    }
}

} // namespace openvegas
