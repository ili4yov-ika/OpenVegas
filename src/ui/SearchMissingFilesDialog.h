#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QThread;

namespace openvegas {

/** Vegas-style “Search for Missing Files” — scan drives for a filename. */
class SearchMissingFilesDialog : public QDialog {
    Q_OBJECT
public:
    explicit SearchMissingFilesDialog(const QString &fileName, QWidget *parent = nullptr);
    ~SearchMissingFilesDialog() override;

    /** Absolute path chosen by the user (after accept). */
    QString selectedPath() const { return m_selectedPath; }

private slots:
    void startSearch();
    void stopSearch();
    void onHit(const QString &path, qint64 sizeBytes, const QString &modified);
    void onStatus(const QString &text);
    void onFinished();
    void acceptSelection();

private:
    void rebuildLookIn();

    QString m_fileName;
    QString m_selectedPath;
    QComboBox *m_lookIn = nullptr;
    QTreeWidget *m_results = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_searchBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_okBtn = nullptr;
    QThread *m_workerThread = nullptr;
    class SearchWorker *m_worker = nullptr; // defined in .cpp
};

} // namespace openvegas
