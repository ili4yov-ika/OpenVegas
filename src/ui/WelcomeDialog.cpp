#include "ui/WelcomeDialog.h"
#include "ui/IconFactory.h"
#include "io/SamplePaths.h"
#include "ui_WelcomeDialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSizePolicy>
#include <QDir>
#include <QFileInfo>
#include <QSvgRenderer>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QLabel>
#include <QVBoxLayout>
#include <QFont>

namespace openvegas {

WelcomeDialog::WelcomeDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WelcomeDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("Welcome — OpenVegas"));

    QSettings settings;
    ui->showOnStartupCheck->setChecked(settings.value(QStringLiteral("welcome/showOnStartup"), true).toBool());
    const QByteArray welcomeGeo = settings.value(QStringLiteral("welcome/geometry")).toByteArray();
    if (!welcomeGeo.isEmpty()) {
        restoreGeometry(welcomeGeo);
    }

    // Brand: app logo mark (blue tile + V)
    {
        QPixmap logo(28, 28);
        logo.fill(Qt::transparent);
        QPainter p(&logo);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x00, 0x78, 0xd7));
        p.drawRoundedRect(0, 0, 28, 28, 4, 4);
        QSvgRenderer svg(QStringLiteral(":/icons/logo.svg"));
        if (svg.isValid()) {
            svg.render(&p, QRectF(3, 3, 22, 22));
        } else {
            p.setPen(QPen(Qt::white, 2.2));
            p.drawText(logo.rect(), Qt::AlignCenter, QStringLiteral("V"));
        }
        p.end();
        ui->brandLogo->setPixmap(logo);
        ui->brandLogo->setText(QString());
        ui->brandLogo->setScaledContents(false);
    }

    paintHeroBanner(ui->welcomeHero, false);
    if (ui->welcomeHeroGetting) {
        paintHeroBanner(ui->welcomeHeroGetting, true);
    }

    // Close lives in window chrome; hide footer ✕ (Vegas title-bar close)
    ui->closeWelcomeButton->hide();

    setupNav();
    setupAspectCards();
    setupRecentList();
    setPane(0);

    // Restore last New Project form choices
    {
        auto setCombo = [](QComboBox *box, const QString &text) {
            if (!box || text.isEmpty()) {
                return;
            }
            const int i = box->findText(text);
            if (i >= 0) {
                box->setCurrentIndex(i);
            }
        };
        setCombo(ui->resolutionCombo, settings.value(QStringLiteral("welcome/resolution")).toString());
        setCombo(ui->framerateCombo, settings.value(QStringLiteral("welcome/framerate")).toString());
        setCombo(ui->dropFrameCombo, settings.value(QStringLiteral("welcome/dropFrame")).toString());

        const QString aspect = settings.value(QStringLiteral("welcome/aspect"), QStringLiteral("16:9")).toString();
        QToolButton *match = nullptr;
        if (aspect == QLatin1String("4:3")) {
            match = ui->aspect43;
        } else if (aspect == QLatin1String("9:16")) {
            match = ui->aspect916;
        } else if (aspect == QLatin1String("1:1")) {
            match = ui->aspect11;
        } else if (aspect == QLatin1String("2.4:1")) {
            match = ui->aspect24;
        } else {
            match = ui->aspect169;
        }
        if (match) {
            match->setChecked(true);
        }
    }

    connect(ui->showOnStartupCheck, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("welcome/showOnStartup"), on);
    });

    auto persistWelcomeUi = [this]() {
        QSettings s;
        s.setValue(QStringLiteral("welcome/geometry"), saveGeometry());
        s.setValue(QStringLiteral("welcome/showOnStartup"), ui->showOnStartupCheck->isChecked());
        s.setValue(QStringLiteral("welcome/resolution"), ui->resolutionCombo->currentText());
        s.setValue(QStringLiteral("welcome/framerate"), ui->framerateCombo->currentText());
        s.setValue(QStringLiteral("welcome/dropFrame"), ui->dropFrameCombo->currentText());
        QString aspect = QStringLiteral("16:9");
        if (ui->aspect43->isChecked()) {
            aspect = QStringLiteral("4:3");
        } else if (ui->aspect916->isChecked()) {
            aspect = QStringLiteral("9:16");
        } else if (ui->aspect11->isChecked()) {
            aspect = QStringLiteral("1:1");
        } else if (ui->aspect24->isChecked()) {
            aspect = QStringLiteral("2.4:1");
        }
        s.setValue(QStringLiteral("welcome/aspect"), aspect);
    };

    connect(ui->createProjectButton, &QPushButton::clicked, this, [this, persistWelcomeUi]() {
        persistWelcomeUi();
        emit newProjectRequested();
        accept();
    });
    connect(ui->advancedSettingsButton, &QPushButton::clicked, this, [this]() {
        emit advancedSettingsRequested();
    });
    connect(ui->browseButton, &QPushButton::clicked, this, [this, persistWelcomeUi]() {
        persistWelcomeUi();
        emit openProjectRequested();
        accept();
    });
    connect(ui->recentList, &QListWidget::itemDoubleClicked, this, [this, persistWelcomeUi](QListWidgetItem *item) {
        persistWelcomeUi();
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            emit openProjectPathRequested(path);
        } else {
            emit openProjectRequested();
        }
        accept();
    });
    connect(ui->closeWelcomeButton, &QToolButton::clicked, this, [this, persistWelcomeUi]() {
        persistWelcomeUi();
        reject();
    });
    connect(this, &QDialog::finished, this, [persistWelcomeUi](int) { persistWelcomeUi(); });
}

WelcomeDialog::~WelcomeDialog()
{
    delete ui;
}

bool WelcomeDialog::showOnStartup() const
{
    return ui->showOnStartupCheck->isChecked();
}

void WelcomeDialog::paintHeroBanner(QFrame *frame, bool gettingStarted)
{
    if (!frame) {
        return;
    }
    const int w = qMax(640, frame->width() > 0 ? frame->width() : 640);
    const int h = qMax(168, frame->height() > 0 ? frame->height() : 168);
    QPixmap pm(w, h);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    if (gettingStarted) {
        QLinearGradient g(0, 0, w, h);
        g.setColorAt(0, QColor(0x8a, 0x1a, 0x4a));
        g.setColorAt(0.45, QColor(0xc0, 0x30, 0x70));
        g.setColorAt(1, QColor(0x4a, 0x0a, 0x30));
        p.fillRect(pm.rect(), g);
    } else {
        QLinearGradient g(0, 0, w * 0.7, h);
        g.setColorAt(0, QColor(0x00, 0x6a, 0xc8));
        g.setColorAt(0.55, QColor(0x00, 0x78, 0xd7));
        g.setColorAt(1, QColor(0x00, 0x5a, 0xb0));
        p.fillRect(pm.rect(), g);

        QRadialGradient glow(w * 0.72, h * 0.42, h * 0.7);
        glow.setColorAt(0, QColor(255, 255, 255, 55));
        glow.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillRect(pm.rect(), glow);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x12, 0x18, 0x22));
        p.drawRoundedRect(QRectF(w * 0.52, h * 0.28, 170, 88), 6, 6);
        p.setBrush(QColor(0x1e, 0x28, 0x36));
        p.drawRoundedRect(QRectF(w * 0.55, h * 0.34, 70, 52), 4, 4);

        p.setBrush(QColor(0x0a, 0x0e, 0x14));
        p.drawEllipse(QPointF(w * 0.78, h * 0.48), 42, 42);
        p.setBrush(QColor(0x1a, 0x4a, 0x7a));
        p.drawEllipse(QPointF(w * 0.78, h * 0.48), 30, 30);
        p.setBrush(QColor(0x0d, 0x1a, 0x28));
        p.drawEllipse(QPointF(w * 0.78, h * 0.48), 18, 18);
        p.setBrush(QColor(0x40, 0xa0, 0xe8, 180));
        p.drawEllipse(QPointF(w * 0.76, h * 0.44), 6, 6);

        p.setBrush(QColor(0x00, 0x78, 0xd7));
        p.drawRoundedRect(QRectF(w * 0.57, h * 0.38, 18, 18), 3, 3);
        p.setPen(QPen(Qt::white, 1.6));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Bold));
        p.drawText(QRectF(w * 0.57, h * 0.38, 18, 18), Qt::AlignCenter, QStringLiteral("V"));
    }
    p.end();

    QLabel *banner = frame->findChild<QLabel *>(QStringLiteral("welcomeHeroBanner"));
    if (!banner) {
        if (!frame->layout()) {
            auto *lay = new QVBoxLayout(frame);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(0);
        }
        banner = new QLabel(frame);
        banner->setObjectName(QStringLiteral("welcomeHeroBanner"));
        banner->setScaledContents(true);
        banner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        frame->layout()->addWidget(banner);
    }
    banner->setPixmap(pm);
}

void WelcomeDialog::setupNav()
{
    const QColor iconColor(0xe0, 0xe0, 0xe0);
    ui->navNew->setIcon(IconFactory::iconFromSvgBody(IconFactory::svgNew(), 28, iconColor));
    ui->navOpen->setIcon(IconFactory::iconFromSvgBody(IconFactory::svgOpen(), 28, iconColor));
    // Graduation cap (Getting Started)
    ui->navGetting->setIcon(IconFactory::iconFromSvgBody(
        QStringLiteral("<path d='M1.5 7.2L8 3.5 14.5 7.2 8 10.9 1.5 7.2z' fill='none' stroke='currentColor' "
                       "stroke-width='1.2'/>"
                       "<path d='M4 8.8v3.2c0 .9 1.8 1.7 4 1.7s4-.8 4-1.7V8.8' fill='none' "
                       "stroke='currentColor' stroke-width='1.2'/>"
                       "<path d='M13.2 7.4v4.2' stroke='currentColor' stroke-width='1.2'/>"),
        28, iconColor));

    for (QToolButton *btn : {ui->navNew, ui->navOpen, ui->navGetting}) {
        btn->setIconSize(QSize(28, 28));
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setMinimumHeight(78);
        btn->setObjectName(QStringLiteral("welcomeNavBtn"));
    }

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    m_navGroup->addButton(ui->navNew, 0);
    m_navGroup->addButton(ui->navOpen, 1);
    m_navGroup->addButton(ui->navGetting, 2);
    connect(m_navGroup, &QButtonGroup::idClicked, this, &WelcomeDialog::setPane);

    m_gettingGroup = new QButtonGroup(this);
    m_gettingGroup->setExclusive(true);
    m_gettingGroup->addButton(ui->btnVideoTutorials);
    m_gettingGroup->addButton(ui->btnOnlineHelp);
    m_gettingGroup->addButton(ui->btnCommunity);
    m_gettingGroup->addButton(ui->btnSocial);
    for (QPushButton *btn : {ui->btnVideoTutorials, ui->btnOnlineHelp, ui->btnCommunity, ui->btnSocial}) {
        btn->setObjectName(QStringLiteral("welcomeGettingBtn"));
    }
}

void WelcomeDialog::setupAspectCards()
{
    struct Aspect {
        QToolButton *btn;
        AspectKind kind;
    };
    const Aspect cards[] = {
        {ui->aspect169, AspectKind::Wide},
        {ui->aspect43, AspectKind::Full},
        {ui->aspect916, AspectKind::Portrait},
        {ui->aspect11, AspectKind::Square},
        {ui->aspect24, AspectKind::Scope},
    };

    m_aspectGroup = new QButtonGroup(this);
    m_aspectGroup->setExclusive(true);

    for (const Aspect &a : cards) {
        a.btn->setProperty("aspectKind", static_cast<int>(a.kind));
        a.btn->setIconSize(QSize(56, 40));
        a.btn->setFixedSize(92, 78);
        a.btn->setObjectName(QStringLiteral("welcomeAspectCard"));
        m_aspectGroup->addButton(a.btn);
        connect(a.btn, &QToolButton::toggled, this, [this, btn = a.btn](bool) {
            const auto kind = static_cast<AspectKind>(btn->property("aspectKind").toInt());
            btn->setIcon(aspectIcon(kind, btn->isChecked()));
        });
        a.btn->setIcon(aspectIcon(a.kind, a.btn->isChecked()));
    }
}

QIcon WelcomeDialog::aspectIcon(AspectKind kind, bool selected) const
{
    QPixmap pm(56, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = selected ? QColor(0xff, 0xff, 0xff) : QColor(0x90, 0x90, 0x90);
    p.setPen(QPen(ink, 1.6));
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case AspectKind::Wide: {
        // Monitor 16:9
        p.drawRoundedRect(QRectF(6, 6, 44, 24), 2, 2);
        p.drawLine(QPointF(22, 30), QPointF(34, 30));
        p.drawLine(QPointF(18, 34), QPointF(38, 34));
        break;
    }
    case AspectKind::Full: {
        // 4:3 CRT-ish
        p.drawRoundedRect(QRectF(10, 5, 36, 26), 2, 2);
        p.drawLine(QPointF(22, 31), QPointF(34, 31));
        p.drawLine(QPointF(18, 35), QPointF(38, 35));
        break;
    }
    case AspectKind::Portrait: {
        // Phone
        p.drawRoundedRect(QRectF(18, 2, 20, 36), 3, 3);
        p.drawLine(QPointF(24, 5), QPointF(32, 5));
        break;
    }
    case AspectKind::Square: {
        p.drawRoundedRect(QRectF(10, 6, 28, 28), 3, 3);
        p.drawRoundedRect(QRectF(14, 10, 20, 16), 2, 2);
        break;
    }
    case AspectKind::Scope: {
        // Ultrawide
        p.drawRoundedRect(QRectF(2, 10, 52, 16), 2, 2);
        p.drawLine(QPointF(22, 26), QPointF(34, 26));
        p.drawLine(QPointF(16, 30), QPointF(40, 30));
        break;
    }
    }
    p.end();
    return QIcon(pm);
}

void WelcomeDialog::setupRecentList()
{
    ui->recentList->clear();

    // Prefer QSettings recent files
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QStringList recent = settings.value(QStringLiteral("recent/files")).toStringList();
    for (const QString &path : recent) {
        if (!QFileInfo::exists(path)) {
            continue;
        }
        const QFileInfo fi(path);
        auto *item = new QListWidgetItem(fi.fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        ui->recentList->addItem(item);
    }

    QStringList dirs;
    const QString vp = SamplePaths::vegProjectDir();
    if (!vp.isEmpty()) {
        dirs << vp;
    }
    const QString samples = SamplePaths::samplesDir();
    if (!samples.isEmpty()) {
        dirs << samples;
        dirs << QDir(samples).filePath(QStringLiteral("veg_project"));
    }
    dirs << QDir::current().absoluteFilePath(QStringLiteral("SAMPLES/veg_project"));
    dirs << QDir::current().absoluteFilePath(QStringLiteral("../SAMPLES/veg_project"));
    dirs << QDir::current().absoluteFilePath(QStringLiteral("SAMPLES"));
    dirs << QDir::current().absoluteFilePath(QStringLiteral("../SAMPLES"));
    dirs.removeDuplicates();

    QStringList vegFiles;
    const QStringList patterns = {QStringLiteral("project_*.veg"),
                                  QStringLiteral("sample_for_project*.veg"),
                                  QStringLiteral("example_project*.veg")};
    for (const QString &d : dirs) {
        const QDir dir(d);
        if (!dir.exists()) {
            continue;
        }
        for (const QString &pattern : patterns) {
            const auto entries = dir.entryList({pattern}, QDir::Files, QDir::Name);
            for (const QString &name : entries) {
                vegFiles << dir.absoluteFilePath(name);
            }
        }
        if (!vegFiles.isEmpty()) {
            break;
        }
    }

    for (const QString &path : vegFiles) {
        bool already = false;
        for (int i = 0; i < ui->recentList->count(); ++i) {
            if (ui->recentList->item(i)->data(Qt::UserRole).toString() == path) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        const QFileInfo fi(path);
        auto *item = new QListWidgetItem(QStringLiteral("%1    Sample").arg(fi.fileName()));
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        ui->recentList->addItem(item);
    }

    if (ui->recentList->count() == 0) {
        auto *item = new QListWidgetItem(tr("(No recent projects)"));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        ui->recentList->addItem(item);
    }
}

void WelcomeDialog::setPane(int index)
{
    ui->welcomeStack->setCurrentIndex(index);
    if (QAbstractButton *btn = m_navGroup->button(index)) {
        btn->setChecked(true);
    }
}

} // namespace openvegas
