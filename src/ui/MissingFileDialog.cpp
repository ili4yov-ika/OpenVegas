#include "ui/MissingFileDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace openvegas {

MissingFileDialog::MissingFileDialog(const QString &missingPath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("OpenVegas"));
    setModal(true);
    setMinimumWidth(440);
    setObjectName(QStringLiteral("missingFileDialog"));
    setStyleSheet(QStringLiteral(
        "#missingFileDialog { background:#2a2a2a; color:#e0e0e0; }"
        "QLabel { color:#ddd; }"
        "QLabel#mfPath {"
        "  background:#1e1e1e; color:#eee; border:1px solid #555; padding:6px 8px; }"
        "QRadioButton { color:#ddd; spacing:8px; }"
        "QPushButton { background:#3a3a3a; color:#eee; border:1px solid #555;"
        "  padding:4px 16px; min-width:72px; }"
        "QPushButton:hover { background:#4a4a4a; }"
        "QPushButton:default { background:#0a6ebd; border-color:#085a9c; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(10);

    root->addWidget(new QLabel(tr("The following file could not be found in the specified location:"),
                               this));

    auto *pathLab = new QLabel(this);
    pathLab->setObjectName(QStringLiteral("mfPath"));
    pathLab->setWordWrap(true);
    pathLab->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const QFontMetrics fm(pathLab->font());
    pathLab->setText(fm.elidedText(QDir::toNativeSeparators(missingPath), Qt::ElideMiddle, 400));
    pathLab->setToolTip(QDir::toNativeSeparators(missingPath));
    root->addWidget(pathLab);

    root->addWidget(new QLabel(tr("What do you want to do?"), this));

    m_search = new QRadioButton(tr("Search for missing file"), this);
    m_specify = new QRadioButton(tr("Specify a new location or replacement file"), this);
    m_ignore = new QRadioButton(tr("Ignore missing file and leave it offline"), this);
    m_ignoreAll = new QRadioButton(tr("Ignore all missing files and leave them offline"), this);
    m_search->setChecked(true);
    root->addWidget(m_search);
    root->addWidget(m_specify);
    root->addWidget(m_ignore);
    root->addWidget(m_ignoreAll);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &MissingFileDialog::acceptChoice);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);
}

void MissingFileDialog::acceptChoice()
{
    if (m_search->isChecked()) {
        m_action = MissingFileAction::Search;
    } else if (m_specify->isChecked()) {
        m_action = MissingFileAction::Specify;
    } else if (m_ignore->isChecked()) {
        m_action = MissingFileAction::Ignore;
    } else if (m_ignoreAll->isChecked()) {
        m_action = MissingFileAction::IgnoreAll;
    }
    accept();
}

} // namespace openvegas
