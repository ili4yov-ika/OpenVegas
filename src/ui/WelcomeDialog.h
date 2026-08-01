#pragma once

#include <QDialog>

class QButtonGroup;
class QFrame;

namespace Ui {
class WelcomeDialog;
}

namespace openvegas {

class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(QWidget *parent = nullptr);
    ~WelcomeDialog() override;

    bool showOnStartup() const;

signals:
    void newProjectRequested();
    void openProjectRequested();
    void openProjectPathRequested(const QString &path);
    void advancedSettingsRequested();

private:
    enum class AspectKind { Wide, Full, Portrait, Square, Scope };

    void setupNav();
    void setupAspectCards();
    void setupRecentList();
    void setPane(int index);
    void paintHeroBanner(QFrame *frame, bool gettingStarted);
    QIcon aspectIcon(AspectKind kind, bool selected) const;

    Ui::WelcomeDialog *ui = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QButtonGroup *m_aspectGroup = nullptr;
    QButtonGroup *m_gettingGroup = nullptr;
};

} // namespace openvegas
