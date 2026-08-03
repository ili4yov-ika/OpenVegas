#include "ui/PreferencesDialog.h"
#include "ui_PreferencesDialog.h"
#include "plugins/AudioPluginScanner.h"
#include "plugins/AudioPluginRegistry.h"
#include "plugins/PluginScanner.h"

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
}

void PreferencesDialog::saveSettings()
{
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    settings.setValue(QStringLiteral("plugins/vegasProPath"), ui->vegasPathEdit->text().trimmed());
    settings.setValue(QStringLiteral("plugins/ofxPath"), ui->ofxPathEdit->text().trimmed());
    settings.setValue(QStringLiteral("plugins/useVegasOfx"), ui->checkUseVegasOfx->isChecked());
    settings.setValue(QStringLiteral("general/autosave"), ui->checkAutoSave->isChecked());
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
