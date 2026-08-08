#pragma once

#include <QWidget>

class QToolButton;
class QVBoxLayout;

namespace openvegas {

/**
 * Small reusable "expand/collapse" section: a clickable header (arrow + title)
 * that shows/hides an arbitrary content widget. Used for Text color / Advanced /
 * Outline / Shadow in TitlesTextEditorDialog, bound to Vegas's persisted
 * per-section expand state (the *Group flags in TitlesTextParams).
 */
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString &title, QWidget *parent = nullptr);

    /** Takes ownership; replaces any previous content widget. */
    void setContentWidget(QWidget *content);
    bool isExpanded() const { return m_expanded; }

public slots:
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);

private:
    QToolButton *m_headerButton = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QWidget *m_content = nullptr;
    bool m_expanded = false;
};

} // namespace openvegas
