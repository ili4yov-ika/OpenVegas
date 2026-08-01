#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace openvegas {

/** Vegas-like keyboard context (Customize Keyboard tabs). */
enum class KeyboardContext {
    TrackView = 0,
    TrackList,
    Explorer,
    Trimmer,
    Mixer,
    EditDetails,
    TrackMotion,
    Global,
    MixingConsole,
    Count
};

QString keyboardContextId(KeyboardContext ctx);
QString keyboardContextLabel(KeyboardContext ctx);
KeyboardContext keyboardContextFromId(const QString &id);

struct KeyboardCommand {
    QString id;           // e.g. "Transport.PlayToggle"
    QString displayName;  // tree label leaf (last segment styled in UI)
    KeyboardContext context = KeyboardContext::Global;
    QVector<QKeySequence> shortcuts;
    bool assignable = true;
};

/**
 * Default + user keyboard map (QSettings).
 * Command IDs are stable; shortcuts may be edited in Customize Keyboard.
 */
class KeyboardMap : public QObject {
    Q_OBJECT
public:
    static KeyboardMap &instance();

    /** Built-in Vegas-ish defaults for commands OpenVegas can run. */
    static QVector<KeyboardCommand> defaultCatalog();

    void load();
    void save() const;
    void resetToDefaults();

    QVector<KeyboardCommand> commandsFor(KeyboardContext ctx) const;
    QVector<KeyboardCommand> allCommands() const { return m_commands; }

    KeyboardCommand *findMutable(const QString &id);
    KeyboardCommand *findMutable(KeyboardContext ctx, const QString &id);
    const KeyboardCommand *find(const QString &id) const;
    const KeyboardCommand *find(KeyboardContext ctx, const QString &id) const;

    QStringList commandIdsUsing(const QKeySequence &seq, const QString &exceptId = {}) const;

    bool setShortcuts(const QString &id, const QVector<QKeySequence> &seqs);
    bool addShortcut(const QString &id, const QKeySequence &seq);
    bool removeShortcut(const QString &id, const QKeySequence &seq);
    bool replaceShortcut(const QString &id, const QKeySequence &oldSeq, const QKeySequence &newSeq);

    /** Notify listeners after in-place command edits (dialog). */
    void notifyChanged();

signals:
    void mapChanged();

private:
    explicit KeyboardMap(QObject *parent = nullptr);
    void ensureCatalog();

    QVector<KeyboardCommand> m_commands;
    QHash<QString, int> m_index;
};

} // namespace openvegas
