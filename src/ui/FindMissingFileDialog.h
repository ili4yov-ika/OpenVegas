#pragma once

#include "io/MediaProbe.h"

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QListView;
class QFileSystemModel;
class QModelIndex;
class QLineEdit;
class QCheckBox;

namespace openvegas {

/** Vegas-style “Find Missing File” browser with media probe details. */
class FindMissingFileDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindMissingFileDialog(const QString &missingPath, QWidget *parent = nullptr);

    QString selectedPath() const { return m_selectedPath; }

private:
    void buildUi();
    void navigateTo(const QString &dir);
    void onSelectionChanged(const QModelIndex &index);
    void refreshDetails(const QString &path);
    void clearDetails();
    void acceptSelection();

    QString m_expectedName;
    QString m_selectedPath;
    QFileSystemModel *m_model = nullptr;
    QListView *m_view = nullptr;
    QComboBox *m_pathCombo = nullptr;
    QComboBox *m_fileName = nullptr;
    QComboBox *m_filter = nullptr;

    QLabel *m_fileType = nullptr;
    QLabel *m_streams = nullptr;
    QLabel *m_video = nullptr;
    QLabel *m_audio = nullptr;
};

} // namespace openvegas
