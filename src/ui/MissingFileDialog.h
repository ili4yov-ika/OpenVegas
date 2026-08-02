#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QRadioButton;

namespace openvegas {

enum class MissingFileAction {
    Search,
    Specify,
    Ignore,
    IgnoreAll,
    Cancel
};

/** Vegas-style “file could not be found” chooser. */
class MissingFileDialog : public QDialog {
    Q_OBJECT
public:
    explicit MissingFileDialog(const QString &missingPath, QWidget *parent = nullptr);

    MissingFileAction action() const { return m_action; }

private:
    void acceptChoice();

    MissingFileAction m_action = MissingFileAction::Cancel;
    QRadioButton *m_search = nullptr;
    QRadioButton *m_specify = nullptr;
    QRadioButton *m_ignore = nullptr;
    QRadioButton *m_ignoreAll = nullptr;
};

} // namespace openvegas
