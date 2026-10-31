// Standalone integration check, linked against the built application's objects.
// Run through tools/run_ui_stubs_smoke.py with a MinGW/Ninja build.
#include "app/MainWindow.h"
#include "ui/ProjectPropertiesDialog.h"
#include "ui/MediaGeneratorDialog.h"
#include "ui/MediaGeneratorPane.h"
#include "ui/VegasColorPicker.h"
#include "ui/MixingConsoleWindow.h"
#include "model/ProjectSnapshot.h"
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QThreadPool>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace openvegas;
static int checks = 0;
static void require(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static QAction *action(QWidget &widget, const QString &label) {
    for (QAction *a : widget.findChildren<QAction *>()) if (a->text() == label) return a;
    throw std::runtime_error(("Action not found: " + label).toStdString());
}
static void answerInput(const QString &value) {
    QTimer::singleShot(0, [value]() {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QInputDialog *>(widget)) {
                dialog->setTextValue(value); dialog->accept();
            }
    });
}
int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    QApplication app(argc, argv);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTemporaryDir settings;
    if (!settings.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    try {
        ProjectModel model;
        model.setFrameRate(120.0); model.setSampleRate(88200);
        model.setSummary({QStringLiteral("Original"), {}, {}, {}, QStringLiteral("Comment")});
        model.setRulerStartSec(2.25); model.setBeatsPerMeasure(3); model.setBeatNote(3);
        ProjectPropertiesDialog properties(&model);
        auto *title = properties.findChild<QLineEdit *>(QStringLiteral("projectSummaryTitle"));
        auto *offset = properties.findChild<QLineEdit *>(QStringLiteral("projectRulerStart"));
        require(title && offset, "Property fields must exist");
        title->setText(QStringLiteral("Changed")); offset->setText(QStringLiteral("4.5"));
        auto *box = properties.findChild<QDialogButtonBox *>();
        box->button(QDialogButtonBox::Apply)->click();
        require(model.summary().title == QStringLiteral("Changed"), "Summary Apply");
        require(model.rulerStartSec() == 4.5, "Ruler Apply");
        require(model.frameRate() == 120.0 && model.sampleRate() == 88200, "Apply preserves custom rates");
        const ProjectSnapshot before = ProjectSnapshot::capture(model);
        model.setSummary({}); model.setRulerStartSec(0.0); before.apply(model);
        require(ProjectSnapshot::capture(model) == before, "Snapshot restores summary and ruler");

        std::cout << "Properties and snapshot passed; constructing MainWindow\n";
        MainWindow window;
        std::cout << "MainWindow constructed\n";
        window.show(); app.processEvents();
        window.setSnappingEnabled(false);
        window.setIgnoreEventGrouping(true);
        for (QAction *a : window.findChildren<QAction *>()) {
            const QString option = a->property("editingOption").toString();
            if (option == QStringLiteral("snap")) require(!a->isChecked(), "Snapping menu follows model");
            if (option == QStringLiteral("group")) require(a->isChecked(), "Grouping menu follows model");
        }
        action(window, QStringLiteral("Enable Snapping"))->trigger();
        require(window.projectModel().snappingEnabled(), "Snapping action updates model");
        window.setLoopPlaybackEnabled(true);
        require(action(window, QStringLiteral("Loop Playback"))->isChecked(), "Loop menu synchronized");
        window.projectModel().setSummary({QStringLiteral("Before cancel"), {}, {}, {}, {}});
        const ProjectSnapshot cancelBefore = ProjectSnapshot::capture(window.projectModel());
        QTimer::singleShot(0, []() {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<ProjectPropertiesDialog *>(widget)) {
                    dialog->findChild<QLineEdit *>(QStringLiteral("projectSummaryTitle"))->setText(QStringLiteral("Cancel me"));
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
                    dialog->reject();
                }
        });
        std::cout << "Checking Apply/Cancel\n";
        window.onProjectProperties();
        require(ProjectSnapshot::capture(window.projectModel()) == cancelBefore, "Cancel rolls back applied metadata");

        window.setMainToolbarVisible(false);
        auto *preview = window.findChild<QWidget *>(QStringLiteral("previewPanel"));
        preview->hide();
        std::cout << "Saving layout\n";
        answerInput(QStringLiteral("Smoke layout")); window.onSaveLayout();
        QSettings savedLayout(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        savedLayout.sync();
        require(savedLayout.contains(QStringLiteral("layouts/Smoke layout/workspace")), "Layout was persisted");
        window.onDefaultLayout();
        require(window.mainToolbarVisible() && !preview->isHidden(), "Default layout restores panels");
        std::cout << "Loading layout\n";
        answerInput(QStringLiteral("Smoke layout")); window.onLoadLayout();
        require(!window.mainToolbarVisible() && preview->isHidden(), "Saved layout restores visibility");
        window.onDefaultLayout();

        const int trackIndex = window.projectModel().addTrack(TrackKind::Audio);
        TrackEvent ev; ev.id = 900; ev.selected = true; ev.startSec = 1; ev.lengthSec = 8;
        ev.mediaKind = EventMediaKind::Audio;
        window.projectModel().tracks()[trackIndex].events.push_back(ev);
        window.projectModel().setLoopRegion(3, 6);
        window.onEditTrim();
        const TrackEvent *trimmed = window.projectModel().findEvent(900);
        require(trimmed && trimmed->startSec == 3 && trimmed->lengthSec == 3 && trimmed->mediaStartSec == 2,
                "Trim clips to the loop region and advances source");
        window.undoStack()->undo();
        require(window.projectModel().findEvent(900)->startSec == 1, "Trim is undoable");
        window.onNewProject();
        window.projectModel().setFrameSize(64, 36);
        window.onCopySnapshot();
        require(QApplication::clipboard()->image().size() == QSize(64, 36), "Snapshot uses project resolution");

        std::cout << "Snapshot and Trim passed; checking mixer\n";
        ProjectModel audio;
        audio.loadEmptyProject();
        const int at = audio.addTrack(TrackKind::Audio);
        audio.tracks()[at].volumeDb = 0.0;
        MixingConsoleWindow mixer; mixer.setProject(&audio); mixer.show(); app.processEvents();
        ProjectSnapshot mixerBefore; bool began = false, committed = false;
        QObject::connect(&mixer, &MixingConsoleWindow::documentEditBegan, [&]() {
            began = true; mixerBefore = ProjectSnapshot::capture(audio);
        });
        QObject::connect(&mixer, &MixingConsoleWindow::documentEditCommitted, [&]() { committed = true; });
        QWidget *strip = mixer.findChild<QWidget *>(QStringLiteral("mixerStrip"));
        require(strip != nullptr, "Audio strip exists");
        auto *fader = strip->findChild<QSlider *>(QStringLiteral("mixerFader"));
        fader->setSliderDown(true); fader->setValue(fader->value() - 10); fader->setSliderDown(false);
        require(began && committed && mixerBefore.tracks[at].volumeDb == 0.0 && audio.tracks()[at].volumeDb < 0,
                "Mixer captures state before live fader edit");
        action(mixer, QStringLiteral("VU Meters"))->trigger();
        for (auto *meter : strip->findChildren<QProgressBar *>()) require(meter->isHidden(), "Meter visibility action");
        mixer.setMasterMeter(1.1f, 0.5f);
        action(mixer, QStringLiteral("Reset Meter Clip"))->trigger();
        bool propertiesRequested = false;
        QObject::connect(&mixer, &MixingConsoleWindow::audioPropertiesRequested, [&]() { propertiesRequested = true; });
        action(mixer, QStringLiteral("Audio Properties..."))->trigger();
        require(propertiesRequested, "Mixer properties action");
        require(!action(mixer, QStringLiteral("Preview Bus"))->isEnabled(), "Unavailable preview bus is disabled");
        // The generator browser offers every preset VEGAS ships, not one invented each.
        {
            MediaGeneratorPane generators;
            auto *plugins = generators.findChild<QListWidget *>(QStringLiteral("fxPluginList"));
            auto *presets = generators.findChild<QListWidget *>(QStringLiteral("fxPresetGrid"));
            require(plugins && presets, "Generator browser lists exist");
            const QHash<QString, int> expected = {
                {QStringLiteral("Checkerboard"), 13}, {QStringLiteral("Color Gradient"), 18},
                {QStringLiteral("Credit Roll"), 8},   {QStringLiteral("Noise Texture"), 20},
                {QStringLiteral("Solid Color"), 10},  {QStringLiteral("Test Pattern"), 10},
                {QStringLiteral("Titles & Text"), 53},
            };
            int seen = 0;
            for (int row = 0; row < plugins->count(); ++row) {
                // Rows read "☆  Checkerboard": the favourite star, then the name.
                const QString name = plugins->item(row)->text().mid(1).trimmed();
                if (!expected.contains(name)) continue;
                plugins->setCurrentRow(row);
                require(presets->count() == expected.value(name),
                        qPrintable(QStringLiteral("%1 preset count").arg(name)));
                if (name == QLatin1String("Titles & Text")) {
                    require(presets->item(0)->text() == QStringLiteral("(Default)"),
                            "Titles & Text lists \"(Default)\" first, as VEGAS does");
                }
                ++seen;
                if (const QByteArray dump = qgetenv("OPENVGAS_DUMP_GENERATORS"); !dump.isEmpty()) {
                    generators.resize(1200, 700);
                    generators.grab().save(QStringLiteral("%1/generators_%2.png")
                                               .arg(QString::fromLocal8Bit(dump), name));
                }
            }
            require(seen == expected.size(), "Every generator is listed");
        }
        {
            // The Video Media Generators window, once per non-text generator: it opens on an
            // event placed from a preset, shows that preset, and an edit on its page lands
            // in the event's slot.
            std::cout << "Checking the Video Media Generators window\n";
            const QStringList names = {QStringLiteral("Checkerboard"), QStringLiteral("Color Gradient"),
                                       QStringLiteral("Credit Roll"), QStringLiteral("Noise Texture"),
                                       QStringLiteral("Solid Color"), QStringLiteral("Test Pattern")};
            ProjectModel genModel;
            for (const QString &name : names) {
                const int id = genModel.addMediaGeneratorEvent(name, QString());
                require(id > 0, qPrintable(name + QStringLiteral(" event")));
                TrackEvent *ev = genModel.findEvent(id);
                MediaGeneratorDialog dlg;
                dlg.setEvent(ev, 1920, 1080, 29.97);
                dlg.show();
                app.processEvents();
                auto *preset = dlg.findChild<QComboBox *>();
                require(preset && !preset->currentText().isEmpty(), "Generator window shows a preset");
                QLabel *nameLabel = nullptr;
                for (QLabel *label : dlg.findChildren<QLabel *>()) {
                    if (label->text().startsWith(QStringLiteral("<b>Video Media Generator:"))) {
                        nameLabel = label;
                        break;
                    }
                }
                require(nameLabel && !nameLabel->text().contains(QStringLiteral("VEGAS ")),
                        qPrintable(name + QStringLiteral(": visible generator name has no VEGAS prefix")));
                require(dlg.findChild<QToolButton *>(QString(), Qt::FindDirectChildrenOnly) != nullptr,
                        "Generator window has toolbar controls");
                require(dlg.findChildren<QToolButton *>().size() >= 3,
                        "Generator window exposes properties and preset actions");
                auto *page = dlg.findChild<GeneratorPage *>();
                require(page, qPrintable(name + QStringLiteral(" page")));
                const QByteArray before = ev->fxChain.first().state;
                // Any number on the page will do: type a new one and commit it.
                bool edited = false;
                for (QLineEdit *e : page->findChildren<QLineEdit *>()) {
                    if (!e->isEnabled() || e->isReadOnly()) {
                        continue;
                    }
                    bool ok = false;
                    const double v = e->text().toDouble(&ok);
                    if (!ok) {
                        continue;
                    }
                    e->setText(QString::number(v > 0.5 ? v * 0.5 : v + 0.25, 'f', 3));
                    emit e->editingFinished();
                    edited = ev->fxChain.first().state != before;
                    if (edited) {
                        break;
                    }
                }
                if (!edited) {
                    // Solid Color and Test Pattern have no number: a colour or a choice instead.
                    if (auto *picker = page->findChild<VegasColorPicker *>()) {
                        picker->findChild<QLineEdit *>()->setText(QStringLiteral("0.2, 0.4, 0.6, 1.0"));
                        emit picker->findChild<QLineEdit *>()->editingFinished();
                    } else if (auto *combo = page->findChild<QComboBox *>()) {
                        combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
                    }
                    edited = ev->fxChain.first().state != before;
                }
                require(edited, qPrintable(name + QStringLiteral(": an edit reaches the event")));
                if (const QByteArray dump = qgetenv("OPENVGAS_DUMP_GENERATORS"); !dump.isEmpty()) {
                    for (VegasColorPicker *p : dlg.findChildren<VegasColorPicker *>()) {
                        p->setExpanded(true);
                    }
                    dlg.resize(560, 900);
                    app.processEvents();
                    dlg.grab().save(QStringLiteral("%1/window_%2.png").arg(QString::fromLocal8Bit(dump), name));
                }
            }
        }
        require(QThreadPool::globalInstance()->waitForDone(10000), "Preview jobs finish before test teardown");
        std::cout << "UI smoke passed: " << checks << " checks\n";
    } catch (const std::exception &e) {
        std::cerr << "UI smoke FAILED: " << e.what() << "\n"; return 1;
    }
    return 0;
}
