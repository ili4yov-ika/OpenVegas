#pragma once

class QMenuBar;
class QObject;

namespace openvegas {

class MainWindow;

class MenuBuilder {
public:
    static void build(MainWindow *window, QMenuBar *menuBar);
};

} // namespace openvegas
