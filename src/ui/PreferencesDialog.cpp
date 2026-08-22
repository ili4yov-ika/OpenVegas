#include "ui/PreferencesDialog.h"
#include "plugins/VegasSharedAudioCatalog.h"
#include "ui_PreferencesDialog.h"
#include "io/FFmpegStreamDecoder.h"
#include "io/MediaFilmstripCache.h"
#include "plugins/AudioPluginScanner.h"
#include "plugins/AudioPluginRegistry.h"
#include "plugins/PluginScanner.h"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QPlainTextEdit>
#include <QSettings>

namespace openvegas {
namespace {

QStringList linesFromEdit(QPlainTextEdit *edit)
{
    QStringList out;
    if (!edit) {
        return out;
    }
    const QStringList raw = edit->toPlainText().split(QLatin1Char('\n'));
    for (QString s : raw) {
        s = s.trimmed();
        if (!s.isEmpty()) {
            out << s;
        }
    }
    return out;
}

void setEditLines(QPlainTextEdit *edit, const QStringList &lines)
{
    if (edit) {
        edit->setPlainText(lines.join(QLatin1Char('\n')));
    }
}

/** Settings token ("auto"/"nvenc"/…) ↔ combo row. */
const char *const kHwEncoderKeys[] = {"auto", "nvenc", "qsv", "amf", "cpu"};

} // namespace

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PreferencesDialog)
{
    ui->setupUi(this);
    loadSettings();
    connect(ui->browseVegasButton, &QPushButton::clicked, this, [this]() {
        const QString path =
            QFileDialog::getExistingDirectory(this, tr("Vegas Pro Program Files"),
                                              ui->vegasPathEdit->text());
        if (!path.isEmpty()) {
            ui->vegasPathEdit->setText(path);
        }
    });
    connect(ui->browseOfxButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, tr("OFX folder"),
                                                               ui->ofxPathEdit->text());
        if (!path.isEmpty()) {
            ui->ofxPathEdit->setText(path);
        }
    });
    connect(ui->vstDefaultsButton, &QPushButton::clicked, this, [this]() {
        setEditLines(ui->vst1PathsEdit, AudioPluginScanner::defaultVst1Roots());
        setEditLines(ui->vst2PathsEdit, AudioPluginScanner::defaultVst2Roots());
        setEditLines(ui->vst3PathsEdit, AudioPluginScanner::defaultVst3Roots());
    });
    connect(ui->checkHwDecode, &QCheckBox::toggled, ui->hwDecodeCombo, &QWidget::setEnabled);
    ui->hwDecodeCombo->setEnabled(ui->checkHwDecode->isChecked());
    connect(ui->hwDetectButton, &QPushButton::clicked, this, [this]() {
        // Re-run the trial encodes from scratch: the point of the button is to pick up
        // a GPU/driver change, and a cached verdict would hide exactly that.
        FFmpegStreamDecoder::clearEncoderProbeCache();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        refreshHwStatus();
        QApplication::restoreOverrideCursor();
    });
    connect(ui->vstRescanButton, &QPushButton::clicked, this, [this]() {
        AudioPluginScanner::savePathsToSettings(linesFromEdit(ui->vst1PathsEdit),
                                                linesFromEdit(ui->vst2PathsEdit),
                                                linesFromEdit(ui->vst3PathsEdit));
        AudioPluginRegistry::instance().refresh();
        ui->vstScanStatus->setText(
            tr("Scan: %1 plugins — %2")
                .arg(AudioPluginRegistry::instance().all().size())
                .arg(AudioPluginRegistry::instance().sourceSummary()));
    });
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}

void PreferencesDialog::showAudioPluginPaths()
{
    ui->tabs->setCurrentWidget(ui->tabVst);
}

void PreferencesDialog::showVideoPluginPaths()
{
    ui->tabs->setCurrentWidget(ui->tabPlugins);
}

QString PreferencesDialog::ofxPath() const
{
    return ui->ofxPathEdit->text().trimmed();
}

QString PreferencesDialog::vegasProPath() const
{
    return ui->vegasPathEdit->text().trimmed();
}

void PreferencesDialog::loadSettings()
{
    ui->sharedFxModeCombo->setCurrentIndex(
        VegasSharedAudioCatalog::substitutionPolicy() == VegasSharedSubstitution::UseOriginal ? 1
                                                                                              : 0);
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QString vegas = settings.value(QStringLiteral("plugins/vegasProPath")).toString();
    if (vegas.isEmpty()) {
        vegas = PluginScanner::sampleVegasProPath();
    }
    ui->vegasPathEdit->setText(vegas);
    ui->ofxPathEdit->setText(settings.value(QStringLiteral("plugins/ofxPath")).toString());
    ui->checkUseVegasOfx->setChecked(
        settings.value(QStringLiteral("plugins/useVegasOfx"), true).toBool());
    ui->checkAutoSave->setChecked(settings.value(QStringLiteral("general/autosave"), true).toBool());

    QStringList v1;
    QStringList v2;
    QStringList v3;
    AudioPluginScanner::loadPathsFromSettings(&v1, &v2, &v3);
    setEditLines(ui->vst1PathsEdit, v1);
    setEditLines(ui->vst2PathsEdit, v2);
    setEditLines(ui->vst3PathsEdit, v3);
    ui->vstScanStatus->setText(
        tr("Registry: %1 plugins").arg(AudioPluginRegistry::instance().all().size()));

    ui->checkHwDecode->setChecked(settings.value(QStringLiteral("media/hwAccel"), true).toBool());
    ui->hwDecodeCombo->clear();
    ui->hwDecodeCombo->addItem(tr("Automatic"), QStringLiteral("auto"));
    for (const QString &m : FFmpegStreamDecoder::availableHwDecodeMethods()) {
        ui->hwDecodeCombo->addItem(m, m);
    }
    const QString decoder =
        settings.value(QStringLiteral("media/hwDecoder"), QStringLiteral("auto")).toString();
    const int di = ui->hwDecodeCombo->findData(decoder);
    ui->hwDecodeCombo->setCurrentIndex(di >= 0 ? di : 0);

    ui->hwEncodeCombo->clear();
    ui->hwEncodeCombo->addItem(tr("Automatic (fastest that works)"), QStringLiteral("auto"));
    ui->hwEncodeCombo->addItem(tr("NVIDIA NVENC"), QStringLiteral("nvenc"));
    ui->hwEncodeCombo->addItem(tr("Intel Quick Sync (QSV)"), QStringLiteral("qsv"));
    ui->hwEncodeCombo->addItem(tr("AMD AMF"), QStringLiteral("amf"));
    ui->hwEncodeCombo->addItem(tr("CPU only (libx264)"), QStringLiteral("cpu"));
    const QString encoder =
        settings.value(QStringLiteral("media/hwEncoder"), QStringLiteral("auto")).toString();
    const int ei = ui->hwEncodeCombo->findData(encoder);
    ui->hwEncodeCombo->setCurrentIndex(ei >= 0 ? ei : 0);

    refreshHwStatus();
}

void PreferencesDialog::refreshHwStatus()
{
    if (MediaFilmstripCache::findFfmpeg().isEmpty()) {
        ui->hwStatusLabel->setText(
            tr("FFmpeg was not found — hardware acceleration is unavailable."));
        return;
    }
    QStringList works;
    QStringList absent;
    for (const QString &name : FFmpegStreamDecoder::knownHwEncoders()) {
        if (FFmpegStreamDecoder::encoderUsable(name)) {
            works << name;
        } else {
            absent << name;
        }
    }
    // Say plainly which ones ffmpeg advertises but this machine cannot run — that
    // difference is the whole reason the trial encode exists.
    QString text = works.isEmpty()
                       ? tr("No hardware encoder works here — rendering uses libx264 (CPU).")
                       : tr("Works here: %1.").arg(works.join(QStringLiteral(", ")));
    if (!absent.isEmpty()) {
        text += QLatin1Char(' ')
                + tr("Not usable on this machine: %1.").arg(absent.join(QStringLiteral(", ")));
    }
    ui->hwStatusLabel->setText(text);
}

void PreferencesDialog::saveSettings()
{
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    settings.setValue(QStringLiteral("plugins/vegasProPath"), ui->vegasPathEdit->text().trimmed());
    settings.setValue(QStringLiteral("plugins/ofxPath"), ui->ofxPathEdit->text().trimmed());
    settings.setValue(QStringLiteral("plugins/useVegasOfx"), ui->checkUseVegasOfx->isChecked());
    // Index 0 = replace with OpenVegas's own, 1 = always use the VEGAS plug-in. Written
    // through the catalog so the key and its default live in one place.
    VegasSharedAudioCatalog::setSubstitutionPolicy(
        ui->sharedFxModeCombo->currentIndex() == 1 ? VegasSharedSubstitution::UseOriginal
                                                   : VegasSharedSubstitution::ReplaceWithBuiltin);
    settings.setValue(QStringLiteral("general/autosave"), ui->checkAutoSave->isChecked());
    settings.setValue(QStringLiteral("media/hwAccel"), ui->checkHwDecode->isChecked());
    settings.setValue(QStringLiteral("media/hwDecoder"),
                      ui->hwDecodeCombo->currentData().toString());
    settings.setValue(QStringLiteral("media/hwEncoder"),
                      ui->hwEncodeCombo->currentData().toString());
    AudioPluginScanner::savePathsToSettings(linesFromEdit(ui->vst1PathsEdit),
                                            linesFromEdit(ui->vst2PathsEdit),
                                            linesFromEdit(ui->vst3PathsEdit));
}

void PreferencesDialog::accept()
{
    saveSettings();
    AudioPluginRegistry::instance().refresh();
    QDialog::accept();
}

} // namespace openvegas
