#pragma once

#include "io/CdAudioReader.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QTableWidget;
class QPushButton;
class QLabel;

namespace openvegas {

/** Vegas-style File → Extract Audio from CD… (real CDDA rip on Windows). */
class ExtractAudioFromCdDialog : public QDialog {
    Q_OBJECT
public:
    struct ExtractedFile {
        QString path;
        QString displayName;
        double lengthSec = 0.0;
        int trackNumber = 0;
    };

    explicit ExtractAudioFromCdDialog(QWidget *parent = nullptr);
    ~ExtractAudioFromCdDialog() override;

    const QVector<ExtractedFile> &extractedFiles() const { return m_extracted; }

public slots:
    void accept() override;
    void reject() override;

private slots:
    void refreshTracks();
    void updateSelectionUi();
    void onEject();
    void onConfigure();
    void onPlay();
    void onActionChanged(int index);

private:
    void buildUi();
    void populateDrives();
    void fillTrackTable(const QVector<CdTrackInfo> &tracks);
    QVector<CdTrackInfo> selectedCdTracks() const;
    int speedFactor() const;
    static QString formatCdTime(double sec);

    QComboBox *m_actionCombo = nullptr;
    QTableWidget *m_tracks = nullptr;
    QComboBox *m_driveCombo = nullptr;
    QComboBox *m_speedCombo = nullptr;
    QPushButton *m_okBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QLabel *m_selectedLength = nullptr;
    QVector<CdTrackInfo> m_toc;
    QVector<ExtractedFile> m_extracted;
    int m_extractOptimization = 2;
};

} // namespace openvegas
