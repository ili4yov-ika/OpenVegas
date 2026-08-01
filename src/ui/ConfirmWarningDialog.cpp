#include "ui/ConfirmWarningDialog.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace openvegas {

ConfirmWarningDialog::ConfirmWarningDialog(const QString &message, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("confirmWarningDialog"));
    setWindowTitle(tr("Warning"));
    setWindowFlags((windowFlags() & ~Qt::WindowContextHelpButtonHint) | Qt::MSWindowsFixedSizeDialogHint);
    setModal(true);
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(14);

    auto *msg = new QLabel(message, this);
    msg->setObjectName(QStringLiteral("confirmWarningMessage"));
    msg->setWordWrap(true);
    msg->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    msg->setMinimumWidth(380);
    root->addWidget(msg);

    m_doNotShow = new QCheckBox(tr("Do not show again"), this);
    m_doNotShow->setObjectName(QStringLiteral("confirmWarningDontShow"));
    root->addWidget(m_doNotShow, 0, Qt::AlignLeft);

    root->addStretch(1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 4, 0, 0);
    btnRow->setSpacing(8);
    btnRow->addStretch(1);

    auto *yes = new QPushButton(tr("Yes"), this);
    yes->setObjectName(QStringLiteral("confirmWarningYes"));
    yes->setDefault(true);
    yes->setMinimumWidth(72);
    auto *no = new QPushButton(tr("No"), this);
    no->setObjectName(QStringLiteral("confirmWarningNo"));
    no->setMinimumWidth(72);
    no->setAutoDefault(false);

    btnRow->addWidget(yes);
    btnRow->addWidget(no);
    root->addLayout(btnRow);

    connect(yes, &QPushButton::clicked, this, &QDialog::accept);
    connect(no, &QPushButton::clicked, this, &QDialog::reject);

    adjustSize();
}

bool ConfirmWarningDialog::doNotShowAgain() const
{
    return m_doNotShow && m_doNotShow->isChecked();
}

bool ConfirmWarningDialog::confirm(QWidget *parent, const QString &message,
                                   const QString &settingsKey)
{
    if (!settingsKey.isEmpty()) {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        if (s.value(settingsKey, false).toBool()) {
            return true;
        }
    }

    ConfirmWarningDialog dlg(message, parent);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    if (!settingsKey.isEmpty() && dlg.doNotShowAgain()) {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        s.setValue(settingsKey, true);
    }
    return true;
}

} // namespace openvegas
