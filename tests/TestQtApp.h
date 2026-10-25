#pragma once

#include <QApplication>

namespace openvegas::test {

/**
 * The one Qt application object a test binary gets, made on first use and kept to the end.
 *
 * Tests used to make their own: some a QCoreApplication on the stack, some a static one,
 * some a QGuiApplication — whichever ran first decided. Qt allows one application object at
 * a time, and fonts need a QGuiApplication, so a static QCoreApplication from an earlier test
 * left the Titles & Text tests without one, and a later test's own made a second. Qt's
 * release libraries (the MinGW kit) let all of that pass; its debug libraries (the MSVC kit)
 * stop on each with an assert. Qt also keeps a reference to argc and the argv pointer, so
 * they have to outlive the call as well.
 *
 * A QApplication, so a test that needs widgets has them too; off screen unless the
 * environment says otherwise.
 */
inline void ensureQtApp()
{
    if (QCoreApplication::instance()) {
        return;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static int argc = 1;
    static char arg0[] = "openvegas_tests";
    static char *argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
}

} // namespace openvegas::test
