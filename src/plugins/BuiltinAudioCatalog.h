#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QVector>

namespace openvegas {

/** Static Vegas/MAGIX-like audio FX names (no DLL load). */
class BuiltinAudioCatalog {
public:
    static QVector<AudioPluginDesc> all();
    /** Default Track FX chain applied to new audio tracks. */
    static QVector<FxSlot> defaultTrackFxChain();
};

} // namespace openvegas
