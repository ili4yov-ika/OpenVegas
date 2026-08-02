#pragma once

#include "io/MediaProbe.h"

#include <QDialog>

class QLineEdit;
class QLabel;
class QCheckBox;
class QComboBox;
class QListView;
class QFileSystemModel;
class QModelIndex;

namespace openvegas {

/**
 * Vegas-style “Match Media Video Settings” picker:
 * file browser + media details strip → apply video properties.
 */
class MatchMediaVideoSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MatchMediaVideoSettingsDialog(QWidget *parent = nullptr);

    /** Last successfully chosen probe (valid after accept()). */
    const MediaProbeInfo &result() const { return m_result; }

    void setStartDirectory(const QString &dir);

private:
    void buildUi();
    void navigateTo(const QString &dir);
    void onSelectionChanged(const QModelIndex &index);
    void refreshDetails(const QString &path);
    void acceptSelection();
    void clearDetails();

    QFileSystemModel *m_model = nullptr;
    QListView *m_view = nullptr;
    QComboBox *m_pathCombo = nullptr;
    QComboBox *m_fileName = nullptr;
    QComboBox *m_filter = nullptr;

    QLabel *m_fileType = nullptr;
    QLabel *m_streams = nullptr;
    QLabel *m_video = nullptr;
    QLabel *m_videoTime = nullptr;
    QLabel *m_audio = nullptr;
    QLabel *m_audioTime = nullptr;
    QCheckBox *m_openSequence = nullptr;
    QLineEdit *m_firstImage = nullptr;
    QLineEdit *m_lastImage = nullptr;

    MediaProbeInfo m_result;
    QString m_currentPath;
};

} // namespace openvegas
