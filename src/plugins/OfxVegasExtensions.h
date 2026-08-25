#pragma once

/**
 * VEGAS Pro's private extensions to the OpenFX 1.x API.
 *
 * VEGAS ships its OFX bundles against a Sony/MAGIX fork of the OFX C++ support
 * library that is compiled with `OFX_EXTENSIONS_VEGAS`. That fork adds host
 * properties, in-args, actions and suites which are *not* part of the OpenFX
 * standard, and its `describeInContext` dispatcher reads some of them
 * unconditionally — a host that does not supply them cannot get a VEGAS bundle
 * past `kOfxImageEffectActionDescribeInContext`.
 *
 * The names below were recovered from the shipped binaries themselves
 * (the `.ofx` binaries under `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/OFX Video Plug-Ins`),
 * which keep the full literal strings in their data section. Note the quirk that the
 * *values* of `kOfxImageEffectHostPropNativeOrigin` carry a literal `k` prefix
 * while the property name itself does not — that is how the VEGAS SDK header
 * defines them, and matching it exactly matters because the plug-in string-compares.
 *
 * Only names are reproduced here (interface facts, not code), so this header
 * stays under the project's GPL like the rest of `src/`.
 */

// --- Host properties -------------------------------------------------------

/** Which corner of the image the host calls the origin. String, one of the values below. */
#define kOfxImageEffectHostPropNativeOrigin "OfxImageEffectHostPropNativeOrigin"
#define kOfxImageEffectHostPropNativeOriginBottomLeft "kOfxImageEffectHostPropNativeOriginBottomLeft"
#define kOfxImageEffectHostPropNativeOriginTopLeft "kOfxImageEffectHostPropNativeOriginTopLeft"
#define kOfxImageEffectHostPropNativeOriginCenter "kOfxImageEffectHostPropNativeOriginCenter"

/** Top-level host window handle (HWND on Windows), pointer. */
#define kOfxPropVegasHostHWnd "OfxPropVegasHostHWnd"
/** Per-user application data directory the plug-in may write to, string. */
#define kOfxPropVegasHostAppDataDirectory "OfxPropVegasHostAppDataDirectory"
/** Host COM IUnknown; VEGAS-only, pointer. Absent here — we are not a COM host. */
#define kOfxPropHostIUnknown "OfxPropHostIUnknown"

/**
 * Host field read by VEGAS Pro 18's fork of the support library, dropped by VEGAS Pro 22.
 *
 * It is read while the host description is built, in the run of ordinary capability fields
 * between `OfxPropName` and `OfxPropVersion`, and reading it throws when it is absent — so
 * an 18-era bundle answers `kOfxActionLoad` with kOfxStatErrMissingHostFeature in a host
 * that does not carry it. Deliberately not supplied: what the value means is not known, the
 * name reads like an authorisation token, and VEGAS Pro 22 — the version this project
 * targets — asks for nothing of the sort. Named here so the trace log can say what an old
 * bundle asked for, and so the refusal is recognisable rather than mysterious.
 */
#define kOfxPropVegasSpikeKey "OfxPropVegasSpikeKey"

/**
 * Pixel order of a clip's images. String, one of the two values below.
 *
 * Asked for on every clip before the images are fetched. VEGAS Pro 22's bundles carry on
 * without it — this host answers RGBA, which is what its buffers are.
 */
#define kOfxImageEffectPropPixelOrder "OfxImageEffectPropPixelOrder"
#define kOfxImagePixelOrderRGBA "OfxImagePixelOrderRGBA"
#define kOfxImagePixelOrderBGRA "OfxImagePixelOrderBGRA"

// --- Effect / instance properties -----------------------------------------

/** Where in the VEGAS object model the effect sits. String, one of the values below. */
#define kOfxImageEffectPropVegasContext "OfxImageEffectPropVegasContext"
#define kOfxImageEffectPropVegasContextUnknown "OfxImageEffectPropVegasContextUnknown"
#define kOfxImageEffectPropVegasContextMedia "OfxImageEffectPropVegasContextMedia"
#define kOfxImageEffectPropVegasContextTrack "OfxImageEffectPropVegasContextTrack"
#define kOfxImageEffectPropVegasContextEvent "OfxImageEffectPropVegasContextEvent"
#define kOfxImageEffectPropVegasContextProject "OfxImageEffectPropVegasContextProject"
#define kOfxImageEffectPropVegasContextGenerator "OfxImageEffectPropVegasContextGenerator"
#define kOfxImageEffectPropVegasContextEventFadeIn "OfxImageEffectPropVegasContextEventFadeIn"
#define kOfxImageEffectPropVegasContextEventFadeOut "OfxImageEffectPropVegasContextEventFadeOut"

/** Effect runs before Pan/Crop rather than after it. Int. */
#define kOfxImageEffectPropVegasPrePanCrop "OfxImageEffectPropVegasPrePanCrop"
/** The plug-in's UI is currently open. Int. */
#define kOfxImageEffectPropVegasUiOpened "OfxImageEffectPropVegasUiOpened"
/** GUID of the legacy (pre-OFX) VEGAS plug-in this one replaces. String. */
#define kOfxImageEffectPropVegasUpliftGUID "OfxImageEffectPropVegasUpliftGUID"
#define kOfxImageEffectPropVegasUseLocalOpenGLOnly "OfxImageEffectPropVegasUseLocalOpenGLOnly"

/** Render in-args: how much quality the host is asking for. String, values below. */
#define kOfxImageEffectPropRenderQuality "OfxImageEffectPropRenderQuality"
#define kOfxImageEffectPropRenderQualityBest "OfxImageEffectPropRenderQualityBest"
#define kOfxImageEffectPropRenderQualityGood "OfxImageEffectPropRenderQualityGood"
#define kOfxImageEffectPropRenderQualityPreview "OfxImageEffectPropRenderQualityPreview"

/** Stereoscopic render in-args: which view(s) this render call covers. Ints. */
#define kOfxImageEffectPropRenderView "OfxImageEffectPropRenderView"
#define kOfxImageEffectPropViewsToRender "OfxImageEffectPropViewsToRender"

#define kOfxPropVegasDoNotCache "OfxPropVegasDoNotCache"
#define kOfxPropVegasTimeCode "OfxPropVegasTimeCode"
#define kOfxPropVegasIconThumbnail "OfxPropVegasIconThumbnail"

// --- Legacy-preset ("uplift") migration ------------------------------------

#define kOfxPropVegasUpliftData "OfxPropVegasUpliftData"
#define kOfxPropVegasUpliftDataLength "OfxPropVegasUpliftDataLength"
#define kOfxPropVegasUpliftKeyframeData "OfxPropVegasUpliftKeyframeData"
#define kOfxPropVegasUpliftKeyframeDataLength "OfxPropVegasUpliftKeyframeDataLength"
#define kOfxPropVegasUpliftKeyframeTime "OfxPropVegasUpliftKeyframeTime"
#define kOfxPropVegasUpliftKeyframeInterpolation "OfxPropVegasUpliftKeyframeInterpolation"

// --- Actions ---------------------------------------------------------------

#define kOfxImageEffectActionVegasKeyframeUplift "OfxImageEffectActionVegasKeyframeUplift"
#define kOfxVegasImageEffectActionReload "OfxVegasImageEffectActionReload"
#define kOfxVegasActionCheckInstallation "OfxVegasActionCheckInstallation"
#define kOfxVegasActionShowEffectAbout "OfxVegasActionShowEffectAbout"
#define kOfxVegasActionShowEffectHelp "OfxVegasActionShowEffectHelp"
#define kOfxVegasActionRunScript "OfxVegasActionRunScript"

// --- Suites ----------------------------------------------------------------
//
// Struct layouts for these are not published and were not recovered, so the host
// answers fetchSuite() for them with null (the honest answer: "not supported").
// The names are kept so the trace log can name what a plug-in asked for.

#define kOfxVegasEffectSuite "OfxVegasEffectSuite"
#define kOfxVegasKeyframeSuite "OfxVegasKeyframeSuite"
#define kOfxVegasProgressSuite "OfxVegasProgressSuite"
#define kOfxVegasStereoscopicImageEffectSuite "OfxVegasStereoscopicImageEffectSuite"
#define kOfxHWndInteractSuite "OfxHWndInteractSuite"
#define kOfxHWndOverlayInteractSuite "OfxHWndOverlayInteractSuite"
