#include "ui/MediaGeneratorDialog.h"

#include "io/VegLegacyGenerators.h"
#include "model/ProjectModel.h"
#include "ui/ColorGradientPage.h"
#include "ui/CreditRollPage.h"
#include "ui/IconFactory.h"
#include "ui/MediaPropertiesDialog.h"
#include "ui/VegasColorPicker.h"
#include "video/GeneratorParamSchema.h"
#include "video/GeneratorPresetData.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <QHash>

#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {

constexpr int kSliderSteps = 1000;

QString groupSettingsKey(const QString &pluginId)
{
    QString k = pluginId;
    k.remove(QLatin1Char('{')).remove(QLatin1Char('}')).replace(QLatin1Char(':'), QLatin1Char('_'));
    return QStringLiteral("mediaGen/userPresets/") + k;
}

QColor colourFrom(const QVariant &v, const QColor &fallback)
{
    const QVariantList l = v.toList();
    if (l.size() < 3) {
        return fallback;
    }
    auto c = [](const QVariant &x) { return float(std::clamp(x.toDouble(), 0.0, 1.0)); };
    return QColor::fromRgbF(c(l[0]), c(l[1]), c(l[2]), l.size() > 3 ? c(l[3]) : 1.0f);
}

QVariantList colourTo(const QColor &c)
{
    return {double(c.redF()), double(c.greenF()), double(c.blueF()), double(c.alphaF())};
}

/**
 * A page built from what an OFX generator declares, as VEGAS lays out any OFX effect: one
 * row per parameter in declaration order, groups as bold headers with a fold arrow and
 * their members indented under them; a number is a slider and its value to three places,
 * a switch a check box, a choice a list, a colour a swatch row that opens a picker.
 */
class OfxGeneratorPage : public GeneratorPage {
public:
    OfxGeneratorPage(const QVector<GeneratorParamDef> &schema, QWidget *parent)
        : GeneratorPage(parent)
        , m_schema(schema)
    {
        auto *grid = new QGridLayout(this);
        grid->setColumnStretch(1, 1);
        grid->setColumnMinimumWidth(3, 28);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(6);
        int row = 0;
        for (const GeneratorParamDef &d : m_schema) {
            const bool child = !d.parent.isEmpty();
            const QString indent = child ? QStringLiteral("    ") : QString();
            if (d.type == QLatin1String("Group")) {
                auto *head = new QToolButton(this);
                head->setText(QStringLiteral("▼ ") + d.label);
                head->setCheckable(true);
                head->setChecked(true);
                head->setAutoRaise(true);
                QFont f = head->font();
                f.setBold(true);
                head->setFont(f);
                connect(head, &QToolButton::toggled, this, [this, head, name = d.name](bool open) {
                    head->setText((open ? QStringLiteral("▼ ") : QStringLiteral("▶ "))
                                  + head->text().mid(2));
                    for (QWidget *w : m_members.value(name)) {
                        w->setVisible(open);
                    }
                });
                grid->addWidget(head, row++, 0, 1, 3, Qt::AlignLeft);
                continue;
            }
            auto *label = new QLabel(indent + d.label + QLatin1Char(':'), this);
            QList<QWidget *> rowWidgets{label};
            if (d.type == QLatin1String("Double") || d.type == QLatin1String("Integer")) {
                auto *slider = new QSlider(Qt::Horizontal, this);
                slider->setRange(0, kSliderSteps);
                auto *edit = new QLineEdit(this);
                edit->setFixedWidth(150);
                const double lo = d.min, hi = d.max > d.min ? d.max : d.min + 1.0;
                connect(slider, &QSlider::valueChanged, this, [=](int v) {
                    if (m_block) {
                        return;
                    }
                    set(d.name, lo + (hi - lo) * v / kSliderSteps);
                });
                connect(edit, &QLineEdit::editingFinished, this, [=] {
                    bool ok = false;
                    const double v = edit->text().toDouble(&ok);
                    if (ok) {
                        set(d.name, std::clamp(v, lo, hi));
                    } else {
                        refresh();
                    }
                });
                // VEGAS's clock button: animate this parameter, a key at the cursor.
                auto *key = new QToolButton(this);
                key->setCheckable(true);
                key->setAutoRaise(true);
                key->setToolTip(tr("Animate"));
                connect(key, &QToolButton::clicked, this, [=](bool on) {
                    if (m_block) {
                        return;
                    }
                    if (on) {
                        m_curves.insert(d.name, QVariantList{m_time, number(d)});
                    } else {
                        m_params.insert(d.name, number(d)); // keep the value it shows
                        m_curves.remove(d.name);
                        emit paramsEdited(m_params);
                    }
                    refresh();
                    emit curvesEdited(m_curves);
                });
                m_refreshers.push_back([=] {
                    const double v = number(d);
                    slider->setValue(int(std::lround((v - lo) / (hi - lo) * kSliderSteps)));
                    edit->setText(QString::number(v, 'f', 3));
                    const bool animated = m_curves.contains(d.name);
                    key->setChecked(animated);
                    key->setText(animated ? QStringLiteral("\u25C6") : QStringLiteral("\u25C7+"));
                });
                grid->addWidget(label, row, 0);
                grid->addWidget(slider, row, 1);
                grid->addWidget(edit, row, 2);
                grid->addWidget(key, row, 3);
                rowWidgets << slider << edit << key;
                m_enablers.insert(d.name, {label, slider, edit, key});
            } else if (d.type == QLatin1String("Boolean")) {
                auto *box = new QCheckBox(this);
                connect(box, &QCheckBox::toggled, this, [=](bool on) {
                    if (!m_block) {
                        set(d.name, on ? 1.0 : 0.0);
                    }
                });
                m_refreshers.push_back([=] { box->setChecked(number(d) >= 0.5); });
                grid->addWidget(label, row, 0);
                grid->addWidget(box, row, 1, 1, 2, Qt::AlignHCenter);
                rowWidgets << box;
            } else if (d.type == QLatin1String("Choice")) {
                auto *combo = new QComboBox(this);
                combo->addItems(d.choices);
                connect(combo, &QComboBox::currentIndexChanged, this, [=](int i) {
                    if (!m_block && i >= 0) {
                        set(d.name, double(i));
                    }
                });
                m_refreshers.push_back([=] {
                    combo->setCurrentIndex(std::clamp(int(std::lround(number(d))), 0,
                                                      std::max(0, int(d.choices.size()) - 1)));
                });
                grid->addWidget(label, row, 0);
                grid->addWidget(combo, row, 1, 1, 2, Qt::AlignLeft);
                rowWidgets << combo;
            } else if (d.type == QLatin1String("RGBA") || d.type == QLatin1String("RGB")) {
                auto *arrow = new QToolButton(this);
                arrow->setAutoRaise(true);
                arrow->setText(QStringLiteral("▶"));
                label->setText(indent + d.label + QLatin1Char(':'));
                auto *head = new QWidget(this);
                auto *hl = new QHBoxLayout(head);
                hl->setContentsMargins(0, 0, 0, 0);
                hl->addWidget(arrow);
                hl->addWidget(label);
                hl->addStretch(1);
                auto *picker = new VegasColorPicker(this);
                connect(arrow, &QToolButton::clicked, picker,
                        [picker] { picker->setExpanded(!picker->isExpanded()); });
                connect(picker, &VegasColorPicker::expandedChanged, arrow, [arrow](bool open) {
                    arrow->setText(open ? QStringLiteral("▼") : QStringLiteral("▶"));
                });
                connect(picker, &VegasColorPicker::colorChanged, this, [=](const QColor &c) {
                    if (!m_block) {
                        setValue(d.name, colourTo(c));
                    }
                });
                m_refreshers.push_back([=] {
                    picker->setColor(colourFrom(m_params.value(d.name, d.defaultValue),
                                                QColor(255, 255, 255)));
                });
                grid->addWidget(head, row, 0, Qt::AlignTop);
                grid->addWidget(picker, row, 1, 1, 2);
                rowWidgets << head << picker;
            } else {
                continue; // no control VEGAS would show for this kind
            }
            if (child) {
                m_members[d.parent] << rowWidgets;
            }
            ++row;
        }
        grid->setRowStretch(row, 1);
    }

    void setParams(const QVariantMap &params) override
    {
        m_params = params;
        refresh();
    }
    void setAnimation(const QVariantMap &curves, double timeSec) override
    {
        m_curves = curves;
        m_time = timeSec;
        refresh();
    }

private:
    /** A flat [t0, v0, t1, v1, …] curve at `t`: straight between keys, held at the ends. */
    static double curveAt(const QVariantList &flat, double t)
    {
        const int pairs = int(flat.size() / 2);
        if (pairs == 0) {
            return 0.0;
        }
        if (t <= flat[0].toDouble()) {
            return flat[1].toDouble();
        }
        for (int i = 1; i < pairs; ++i) {
            const double t0 = flat[(i - 1) * 2].toDouble(), t1 = flat[i * 2].toDouble();
            if (t <= t1) {
                const double v0 = flat[(i - 1) * 2 + 1].toDouble(), v1 = flat[i * 2 + 1].toDouble();
                return t1 > t0 ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v1;
            }
        }
        return flat[(pairs - 1) * 2 + 1].toDouble();
    }
    double number(const GeneratorParamDef &d) const
    {
        if (const auto c = m_curves.constFind(d.name); c != m_curves.constEnd()) {
            return curveAt(c.value().toList(), m_time);
        }
        const QVariant v = m_params.value(d.name);
        if (!v.isValid()) {
            return d.defaultValue.isEmpty() ? 0.0 : d.defaultValue.first().toDouble();
        }
        return v.typeId() == QMetaType::QVariantList ? v.toList().value(0).toDouble() : v.toDouble();
    }
    void set(const QString &name, double v)
    {
        if (!m_curves.contains(name)) {
            setValue(name, v);
            return;
        }
        // Animated: the edit is a key at the cursor — a new one, or the one already there.
        QVariantList flat = m_curves.value(name).toList();
        QVector<QPair<double, double>> keys;
        for (int i = 0; i + 1 < flat.size(); i += 2) {
            keys.push_back({flat[i].toDouble(), flat[i + 1].toDouble()});
        }
        bool replaced = false;
        for (auto &k : keys) {
            if (std::fabs(k.first - m_time) < 1e-3) {
                k.second = v;
                replaced = true;
            }
        }
        if (!replaced) {
            keys.push_back({m_time, v});
            std::sort(keys.begin(), keys.end());
        }
        flat.clear();
        for (const auto &k : keys) {
            flat << k.first << k.second;
        }
        m_curves.insert(name, flat);
        refresh();
        emit curvesEdited(m_curves);
    }
    void setValue(const QString &name, const QVariant &v)
    {
        m_params.insert(name, v);
        refresh();
        emit paramsEdited(m_params);
    }
    void refresh()
    {
        m_block = true;
        for (const auto &fn : m_refreshers) {
            fn();
        }
        // Checkerboard's own couplings, as its page greys them: a square tile has no height
        // of its own, proportional blending no vertical amount.
        auto enable = [this](const QString &name, bool on) {
            for (QWidget *w : m_enablers.value(name)) {
                w->setEnabled(on);
            }
        };
        if (m_enablers.contains(QStringLiteral("TileDimensionsHeight"))) {
            enable(QStringLiteral("TileDimensionsHeight"),
                   m_params.value(QStringLiteral("TileDimensionsSquare"), 1.0).toDouble() < 0.5);
        }
        if (m_enablers.contains(QStringLiteral("EdgeBlendingVertical"))) {
            enable(QStringLiteral("EdgeBlendingVertical"),
                   m_params.value(QStringLiteral("EdgeBlendingProportional"), 1.0).toDouble() < 0.5);
        }
        m_block = false;
    }

    QVector<GeneratorParamDef> m_schema;
    QVariantMap m_params;
    QVariantMap m_curves;
    double m_time = 0.0;
    QVector<std::function<void()>> m_refreshers;
    QHash<QString, QList<QWidget *>> m_members;
    QHash<QString, QList<QWidget *>> m_enablers;
    bool m_block = false;
};

} // namespace

MediaGeneratorDialog::MediaGeneratorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setWindowModality(Qt::NonModal);
    setWindowTitle(tr("Video Media Generators"));
    setMinimumSize(620, 520);
    resize(680, 820);
    buildUi();
}

void MediaGeneratorDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *nameBar = new QHBoxLayout();
    m_nameLabel = new QLabel(this);
    nameBar->addWidget(m_nameLabel, 1);
    auto *help = new QToolButton(this);
    help->setText(QStringLiteral("?"));
    help->setAutoRaise(true);
    help->setToolTip(tr("About this media generator"));
    connect(help, &QToolButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Video Media Generator"),
                                 tr("Generated media is rendered at the project frame size."));
    });
    nameBar->addWidget(help);
    root->addLayout(nameBar);

    auto *frame = new QHBoxLayout();
    frame->addWidget(new QLabel(tr("Frame Size:"), this));
    m_frameW = new QLineEdit(this);
    m_frameH = new QLineEdit(this);
    for (QLineEdit *e : {m_frameW, m_frameH}) {
        e->setReadOnly(true);
        e->setFixedWidth(64);
        e->setToolTip(tr("Generated at the project's frame size"));
    }
    frame->addWidget(m_frameW);
    frame->addWidget(new QLabel(QStringLiteral("x"), this));
    frame->addWidget(m_frameH);
    frame->addSpacing(18);
    frame->addWidget(new QLabel(tr("Duration:"), this));
    m_duration = new QLineEdit(this);
    m_duration->setFixedWidth(96);
    connect(m_duration, &QLineEdit::editingFinished, this, &MediaGeneratorDialog::applyDuration);
    frame->addWidget(m_duration);
    frame->addStretch(1);
    m_mediaProps = IconFactory::toolButton(this, tr("Media Properties…"),
                                           IconFactory::svgMediaProps());
    connect(m_mediaProps, &QToolButton::clicked, this, &MediaGeneratorDialog::openMediaProperties);
    frame->addWidget(m_mediaProps);
    root->addLayout(frame);

    auto *presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("Preset:"), this));
    m_preset = new QComboBox(this);
    m_preset->setEditable(true);
    m_preset->setInsertPolicy(QComboBox::NoInsert);
    m_preset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_preset, &QComboBox::activated, this, [this](int i) {
        if (!m_block && i >= 0) {
            applyPreset(m_preset->itemText(i));
        }
    });
    presetRow->addWidget(m_preset, 1);
    m_savePreset = new QToolButton(this);
    m_savePreset->setText(QStringLiteral("\U0001F4BE"));
    m_savePreset->setToolTip(tr("Save Preset"));
    connect(m_savePreset, &QToolButton::clicked, this, &MediaGeneratorDialog::saveUserPreset);
    m_deletePreset = new QToolButton(this);
    m_deletePreset->setText(QStringLiteral("✕"));
    m_deletePreset->setToolTip(tr("Delete Preset"));
    connect(m_deletePreset, &QToolButton::clicked, this, &MediaGeneratorDialog::deleteUserPreset);
    presetRow->addWidget(m_savePreset);
    presetRow->addWidget(m_deletePreset);
    root->addLayout(presetRow);

    auto *titleBar = new QHBoxLayout();
    m_pageTitle = new QLabel(this);
    QFont tf = m_pageTitle->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.3);
    m_pageTitle->setFont(tf);
    titleBar->addWidget(m_pageTitle, 1);
    auto *about = new QToolButton(this);
    about->setText(tr("About"));
    about->setAutoRaise(true);
    connect(about, &QToolButton::clicked, this, [this] {
        QMessageBox::about(this, tr("About %1").arg(m_pageTitle->text()),
                           tr("%1\n\nDrawn by OpenVegas from the parameters VEGAS stores for it; "
                              "when VEGAS Pro is installed, its own plug-in draws the OFX "
                              "generators.")
                               .arg(m_pageTitle->text()));
    });
    titleBar->addWidget(about);
    root->addLayout(titleBar);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(m_scroll, 1);
}

void MediaGeneratorDialog::setEvent(TrackEvent *ev, int frameWidth, int frameHeight,
                                    double frameRateFps)
{
    m_event = ev;
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    m_frameRateFps = frameRateFps > 0 ? frameRateFps : 30.0;
    if (!ev || ev->fxChain.isEmpty() || !isMediaGeneratorPluginId(ev->fxChain.first().pluginId)) {
        return;
    }
    m_gen = mediaGeneratorFromSlot(ev->fxChain.first());
    m_id = mediaGeneratorVegasId(m_gen);
    if (m_gen.vegasParams.isEmpty()) {
        // A slot placed before VEGAS's values were kept: start from the generator's default.
        if (const StockGeneratorGroup *g = stockGeneratorGroup(m_id); g && !g->presets.isEmpty()) {
            m_gen.vegasPluginId = m_id;
            m_gen.vegasPreset = g->presets.first().name;
            m_gen.vegasParams = g->presets.first().params;
        }
    }
    const StockGeneratorGroup *group = stockGeneratorGroup(m_id);
    const QString name = group ? group->name : m_gen.pluginName;
    m_nameLabel->setText(tr("<b>Video Media Generator:</b> %1").arg(name.toHtmlEscaped()));
    m_pageTitle->setText(name);
    m_frameW->setText(QString::number(frameWidth));
    m_frameH->setText(QString::number(frameHeight));
    m_duration->setText(durationText());
    rebuildPage();
    fillPresets();
}

void MediaGeneratorDialog::openMediaProperties()
{
    if (!m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    MediaPropertiesDialog dlg(this);
    dlg.setMedia(m_event->name, m_gen.media, m_event->lengthSec, m_frameRateFps, m_frameWidth,
                 m_frameHeight);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    m_gen.media = dlg.mediaProps();
    mediaGeneratorSaveToSlot(&m_event->fxChain.first(), m_gen);
    const double newLength = dlg.lengthSec();
    if (newLength > 0.05 && std::fabs(newLength - m_event->lengthSec) > 1e-6) {
        m_event->lengthSec = newLength;
        m_duration->setText(durationText());
        emit durationChanged();
    }
    emit previewInvalidated();
}

void MediaGeneratorDialog::rebuildPage()
{
    if (m_page) {
        m_page->deleteLater();
        m_page = nullptr;
    }
    if (m_id.compare(vegColorGradientClsid(), Qt::CaseInsensitive) == 0) {
        m_page = new ColorGradientPage(m_scroll);
    } else if (m_id.compare(vegCreditRollClsid(), Qt::CaseInsensitive) == 0) {
        m_page = new CreditRollPage(m_scroll);
    } else if (const QVector<GeneratorParamDef> *schema = generatorParamSchema(m_id)) {
        m_page = new OfxGeneratorPage(*schema, m_scroll);
    }
    if (!m_page) {
        m_scroll->setWidget(new QLabel(tr("This generator has no parameters to show."), m_scroll));
        return;
    }
    m_page->setParams(m_gen.vegasParams);
    m_page->setAnimation(m_gen.vegasCurves, eventTime());
    connect(m_page, &GeneratorPage::paramsEdited, this, &MediaGeneratorDialog::storeParams);
    connect(m_page, &GeneratorPage::curvesEdited, this, &MediaGeneratorDialog::storeCurves);
    m_scroll->setWidget(m_page);
}

void MediaGeneratorDialog::fillPresets()
{
    m_block = true;
    m_preset->clear();
    if (const StockGeneratorGroup *g = stockGeneratorGroup(m_id)) {
        for (const StockGeneratorPreset &p : g->presets) {
            m_preset->addItem(p.name);
        }
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QVariantMap user = s.value(groupSettingsKey(m_id)).toMap();
    for (auto it = user.constBegin(); it != user.constEnd(); ++it) {
        if (m_preset->findText(it.key()) < 0) {
            m_preset->addItem(it.key());
        }
    }
    m_preset->setEditText(m_gen.vegasPreset);
    m_block = false;
}

void MediaGeneratorDialog::applyPreset(const QString &name)
{
    QVariantMap params;
    if (const StockGeneratorPreset *stock = stockGeneratorPreset(m_id, name)) {
        params = stock->params;
    } else {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        params = s.value(groupSettingsKey(m_id)).toMap().value(name).toMap();
    }
    if (params.isEmpty()) {
        return;
    }
    m_gen.vegasPreset = name;
    m_gen.vegasCurves.clear(); // a preset is a set of still values
    if (m_page) {
        m_page->setParams(params);
        m_page->setAnimation({}, eventTime());
    }
    storeParams(params);
}

void MediaGeneratorDialog::saveUserPreset()
{
    QString name = m_preset->currentText().trimmed();
    if (name.isEmpty() || stockGeneratorPreset(m_id, name)) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("Save Preset"), tr("Preset name:"), QLineEdit::Normal,
                                     name.isEmpty() ? QString() : name + tr(" (custom)"), &ok)
                   .trimmed();
        if (!ok || name.isEmpty() || stockGeneratorPreset(m_id, name)) {
            return;
        }
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QVariantMap user = s.value(groupSettingsKey(m_id)).toMap();
    user.insert(name, m_gen.vegasParams);
    s.setValue(groupSettingsKey(m_id), user);
    m_gen.vegasPreset = name;
    storeParams(m_gen.vegasParams);
    fillPresets();
}

void MediaGeneratorDialog::deleteUserPreset()
{
    const QString name = m_preset->currentText().trimmed();
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QVariantMap user = s.value(groupSettingsKey(m_id)).toMap();
    if (!user.contains(name)) {
        return; // VEGAS's own presets stay
    }
    user.remove(name);
    s.setValue(groupSettingsKey(m_id), user);
    fillPresets();
}

void MediaGeneratorDialog::storeParams(const QVariantMap &params)
{
    if (!m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    const GeneratorMediaProps media = m_gen.media;
    m_gen = mediaGeneratorFromVegas(m_id, m_gen.vegasPreset, params, m_gen.vegasCurves,
                                    m_gen.pluginName);
    m_gen.media = media;
    mediaGeneratorSaveToSlot(&m_event->fxChain.first(), m_gen);
    emit previewInvalidated();
}

void MediaGeneratorDialog::storeCurves(const QVariantMap &curves)
{
    if (!m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    m_gen.vegasCurves = curves;
    mediaGeneratorSaveToSlot(&m_event->fxChain.first(), m_gen);
    emit previewInvalidated();
}

double MediaGeneratorDialog::eventTime() const
{
    if (!m_event || !m_cursorSec) {
        return 0.0;
    }
    return std::clamp(m_cursorSec() - m_event->startSec, 0.0, m_event->lengthSec);
}

void MediaGeneratorDialog::cursorMoved()
{
    if (m_page && m_event && isVisible()) {
        m_page->setAnimation(m_gen.vegasCurves, eventTime());
    }
}

QString MediaGeneratorDialog::durationText() const
{
    if (!m_event) {
        return {};
    }
    // VEGAS's timecode: hours, minutes, seconds and frames, the frames after a comma.
    const qint64 frames = qint64(std::llround(m_event->lengthSec * m_frameRateFps));
    const qint64 fps = std::max<qint64>(1, qint64(std::llround(m_frameRateFps)));
    const qint64 secs = frames / fps;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(secs / 3600, 2, 10, QLatin1Char('0'))
        .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0'))
        .arg(frames % fps, 2, 10, QLatin1Char('0'));
}

void MediaGeneratorDialog::applyDuration()
{
    if (!m_event) {
        return;
    }
    const QString t = m_duration->text().trimmed();
    const QStringList hms = t.section(QLatin1Char(','), 0, 0).split(QLatin1Char(':'));
    const int frames = t.contains(QLatin1Char(',')) ? t.section(QLatin1Char(','), 1).toInt() : 0;
    double secs = 0.0;
    for (const QString &part : hms) {
        secs = secs * 60.0 + part.toDouble();
    }
    secs += frames / m_frameRateFps;
    if (secs > 0.01 && std::fabs(secs - m_event->lengthSec) > 1e-6) {
        m_event->lengthSec = secs;
        emit durationChanged();
        emit previewInvalidated();
    }
    m_duration->setText(durationText());
}

} // namespace openvegas
