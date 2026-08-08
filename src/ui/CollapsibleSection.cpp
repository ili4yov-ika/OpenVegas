#include "ui/CollapsibleSection.h"

#include <QToolButton>
#include <QVBoxLayout>

namespace openvegas {

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);

    m_headerButton = new QToolButton(this);
    m_headerButton->setText(title);
    m_headerButton->setCheckable(true);
    m_headerButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_headerButton->setArrowType(Qt::RightArrow);
    m_headerButton->setAutoRaise(true);
    m_layout->addWidget(m_headerButton);

    connect(m_headerButton, &QToolButton::toggled, this, &CollapsibleSection::setExpanded);
}

void CollapsibleSection::setContentWidget(QWidget *content)
{
    if (m_content) {
        m_layout->removeWidget(m_content);
        m_content->deleteLater();
    }
    m_content = content;
    if (m_content) {
        m_content->setParent(this);
        m_layout->addWidget(m_content);
        m_content->setVisible(m_expanded);
    }
}

void CollapsibleSection::setExpanded(bool expanded)
{
    m_expanded = expanded;
    if (m_headerButton->isChecked() != expanded) {
        m_headerButton->setChecked(expanded);
    }
    m_headerButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    if (m_content) {
        m_content->setVisible(expanded);
    }
    emit expandedChanged(expanded);
}

} // namespace openvegas
