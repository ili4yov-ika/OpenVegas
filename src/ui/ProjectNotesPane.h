#pragma once

#include <QWidget>
#include <QString>
#include <QVector>
#include <QColor>
#include <QDateTime>
#include <functional>

class QCheckBox;
class QScrollArea;
class QVBoxLayout;
class QLineEdit;
class QToolButton;
class QComboBox;
class QLabel;

namespace openvegas {

/** Vegas-style Project Notes dock. */
class ProjectNotesPane : public QWidget {
    Q_OBJECT
public:
    explicit ProjectNotesPane(QWidget *parent = nullptr);

    using PlayheadFn = std::function<double()>;
    void setPlayheadProvider(PlayheadFn fn) { m_playheadFn = std::move(fn); }

    void saveSettings() const;
    void restoreSettings();
    void requestSeek(double seconds) { emit seekRequested(seconds); }

signals:
    void seekRequested(double seconds);

private:
    struct Note {
        QString id;
        QString title;
        QString body;
        QColor color;
        QDateTime created;
        double timecodeSec = 0.0;
        bool warning = false;
        bool resolved = false;
    };

    class NoteCard;

    void buildUi();
    void rebuildList();
    void addNote();
    void deleteNote(const QString &id);
    void updateNote(const Note &n);
    Note *findNote(const QString &id);
    QString formatTimecode(double sec) const;
    QString formatCreated(const QDateTime &dt) const;
    QColor nextColor() const;
    void persistSoon();

    PlayheadFn m_playheadFn;
    QVector<Note> m_notes;
    int m_colorIndex = 0;
    bool m_hideResolved = false;

    QCheckBox *m_hideResolvedCheck = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLay = nullptr;
    QToolButton *m_colorBtn = nullptr;
    QLineEdit *m_labelEdit = nullptr;
    QToolButton *m_addBtn = nullptr;
    QColor m_draftColor;
};

} // namespace openvegas
