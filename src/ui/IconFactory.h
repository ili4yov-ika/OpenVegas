#pragma once

#include <QIcon>
#include <QString>
#include <QToolButton>
#include <QWidget>
#include <QColor>

namespace openvegas {

class IconFactory {
public:
    /** Render inline SVG (viewBox 0 0 16 16 body) to a tinted icon. */
    static QIcon iconFromSvgBody(const QString &svgInner, int size = 16, const QColor &color = QColor(0xa0, 0xa0, 0xa0));

    static QToolButton *toolButton(QWidget *parent, const QString &title, const QString &svgInner,
                                   bool checkable = false, bool checked = false);

    // Paths from SAMPLES/js/chrome.js (16×16)
    static QString svgNew();
    static QString svgOpen();
    static QString svgSave();
    static QString svgRender();
    static QString svgGear();
    static QString svgCut();
    static QString svgCopy();
    static QString svgPaste();
    static QString svgUndo();
    static QString svgRedo();
    static QString svgPlay();
    static QString svgPause();
    static QString svgStop();
    static QString svgLoop();
    static QString svgPlayFromStart();
    static QString svgGoStart();
    static QString svgGoEnd();
    static QString svgPrevFrame();
    static QString svgNextFrame();
    static QString svgEditNormal();
    static QString svgEnvelope();
    static QString svgSelection();
    static QString svgZoom();
    static QString svgSnap();
    static QString svgAutoCf();
    static QString svgImport();
    static QString svgSearch();
    static QString svgFx();
    static QString svgMute();
    static QString svgSolo();
    static QString svgRecord();
    static QString svgAutoPreview();
    static QString svgCapture();
    /** Framed picture — Vegas's "Media Properties…" toolbar button. */
    static QString svgMediaProps();
    static QString svgCdExtract();
    static QString svgWeb();
    static QString svgFilter();
    static QString svgViews();
    static QString svgWaveform();
    static QString svgRemove();
    static QString svgAudioDevice();
    /** Downmix Output: 5.1 surround / stereo / mono (button cycles these). */
    static QString svgDownmixSurround();
    static QString svgDownmix();
    static QString svgDownmixMono();
    static QString svgDimOutput();
    static QString svgMixingConsole();
    static QString svgExternalMonitor();
    static QString svgSplitScreen();
    static QString svgOverlays();
    static QString svgMore();
    static QString svgDelete();
    static QString svgTrim();
    static QString svgTrimStart();
    static QString svgTrimEnd();
    static QString svgSplit();
    static QString svgHeal();
    static QString svgLock();
    static QString svgMarker();
    static QString svgRegion();
    static QString svgAutoRipple();
    static QString svgLockEnvelopes();
    static QString svgIgnoreGrouping();
    static QString svgColorGrade();
    static QString svgPasteAttr();
    static QString svgCopyAttr();
    static QString svgGroup();
    static QString svgAutomation();
    static QString svgLockFader();
    static QString svgMasterTitle();
};

} // namespace openvegas
