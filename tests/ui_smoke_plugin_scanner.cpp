// An empty OFX catalog keeps UI integration independent of installed vendor DLLs.
// The application itself still uses src/plugins/PluginScanner.cpp.
#include "plugins/PluginScanner.h"
namespace openvegas {
QString PluginScanner::sampleVegasProPath() { return {}; }
QStringList PluginScanner::candidateRoots() const { return {}; }
QVector<PluginInfo> PluginScanner::scanOfx() const { return {}; }
}
