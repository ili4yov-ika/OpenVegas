/* Minimal OFX filter fixture: multiplies RGB by "gain" (default 1.5). */
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

OfxHost *g_host = nullptr;
OfxPropertySuiteV1 *g_prop = nullptr;
OfxImageEffectSuiteV1 *g_effect = nullptr;
OfxParameterSuiteV1 *g_param = nullptr;
OfxMemorySuiteV1 *g_memory = nullptr;

bool fetchSuites()
{
    if (!g_host || !g_host->fetchSuite) {
        return false;
    }
    g_prop = (OfxPropertySuiteV1 *)g_host->fetchSuite(g_host->host, kOfxPropertySuite, 1);
    g_effect = (OfxImageEffectSuiteV1 *)g_host->fetchSuite(g_host->host, kOfxImageEffectSuite, 1);
    g_param = (OfxParameterSuiteV1 *)g_host->fetchSuite(g_host->host, kOfxParameterSuite, 1);
    g_memory = (OfxMemorySuiteV1 *)g_host->fetchSuite(g_host->host, kOfxMemorySuite, 1);
    (void)g_memory;
    return g_prop && g_effect && g_param;
}

OfxStatus actionLoad(void)
{
    if (!fetchSuites()) {
        return kOfxStatFailed;
    }
    return kOfxStatOK;
}

OfxStatus actionDescribe(OfxImageEffectHandle effect)
{
    OfxPropertySetHandle props = nullptr;
    if (g_effect->getPropertySet(effect, &props) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    g_prop->propSetString(props, kOfxPropLabel, 0, "Gain");
    g_prop->propSetString(props, kOfxImageEffectPropSupportedContexts, 0,
                          kOfxImageEffectContextFilter);
    g_prop->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
    g_prop->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    g_prop->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);

    OfxParamSetHandle paramSet = nullptr;
    if (g_effect->getParamSet(effect, &paramSet) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    OfxPropertySetHandle gainProps = nullptr;
    if (g_param->paramDefine(paramSet, kOfxParamTypeDouble, "gain", &gainProps) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    g_prop->propSetString(gainProps, kOfxPropLabel, 0, "Gain");
    g_prop->propSetDouble(gainProps, kOfxParamPropDefault, 0, 1.5);
    g_prop->propSetDouble(gainProps, kOfxParamPropMin, 0, 0.0);
    g_prop->propSetDouble(gainProps, kOfxParamPropMax, 0, 4.0);
    return kOfxStatOK;
}

OfxStatus actionDescribeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs)
{
    (void)inArgs;
    OfxPropertySetHandle clipProps = nullptr;
    if (g_effect->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &clipProps) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    g_prop->propSetString(clipProps, kOfxImageEffectPropSupportedComponents, 0,
                          kOfxImageComponentRGBA);
    g_prop->propSetString(clipProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);

    if (g_effect->clipDefine(effect, kOfxImageEffectOutputClipName, &clipProps) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    g_prop->propSetString(clipProps, kOfxImageEffectPropSupportedComponents, 0,
                          kOfxImageComponentRGBA);
    g_prop->propSetString(clipProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
    return kOfxStatOK;
}

OfxStatus actionRender(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs)
{
    OfxTime time = 0.0;
    g_prop->propGetDouble(inArgs, kOfxPropTime, 0, &time);

    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    g_prop->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 0, &x1);
    g_prop->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 1, &y1);
    g_prop->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 2, &x2);
    g_prop->propGetInt(inArgs, kOfxImageEffectPropRenderWindow, 3, &y2);

    OfxParamSetHandle paramSet = nullptr;
    g_effect->getParamSet(effect, &paramSet);
    OfxParamHandle gainParam = nullptr;
    g_param->paramGetHandle(paramSet, "gain", &gainParam, nullptr);
    double gain = 1.5;
    if (gainParam) {
        g_param->paramGetValueAtTime(gainParam, time, &gain);
    }

    OfxImageClipHandle srcClip = nullptr;
    OfxImageClipHandle dstClip = nullptr;
    g_effect->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &srcClip, nullptr);
    g_effect->clipGetHandle(effect, kOfxImageEffectOutputClipName, &dstClip, nullptr);

    OfxPropertySetHandle srcImg = nullptr;
    OfxPropertySetHandle dstImg = nullptr;
    if (g_effect->clipGetImage(srcClip, time, nullptr, &srcImg) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    if (g_effect->clipGetImage(dstClip, time, nullptr, &dstImg) != kOfxStatOK) {
        g_effect->clipReleaseImage(srcImg);
        return kOfxStatFailed;
    }

    void *srcData = nullptr;
    void *dstData = nullptr;
    int srcRow = 0, dstRow = 0;
    g_prop->propGetPointer(srcImg, kOfxImagePropData, 0, &srcData);
    g_prop->propGetPointer(dstImg, kOfxImagePropData, 0, &dstData);
    g_prop->propGetInt(srcImg, kOfxImagePropRowBytes, 0, &srcRow);
    g_prop->propGetInt(dstImg, kOfxImagePropRowBytes, 0, &dstRow);

    if (!srcData || !dstData) {
        g_effect->clipReleaseImage(srcImg);
        g_effect->clipReleaseImage(dstImg);
        return kOfxStatFailed;
    }

    for (int y = y1; y < y2; ++y) {
        auto *src = reinterpret_cast<unsigned char *>(static_cast<char *>(srcData) + y * srcRow);
        auto *dst = reinterpret_cast<unsigned char *>(static_cast<char *>(dstData) + y * dstRow);
        for (int x = x1; x < x2; ++x) {
            const int i = x * 4;
            dst[i + 0] = (unsigned char)std::clamp(int(std::lround(src[i + 0] * gain)), 0, 255);
            dst[i + 1] = (unsigned char)std::clamp(int(std::lround(src[i + 1] * gain)), 0, 255);
            dst[i + 2] = (unsigned char)std::clamp(int(std::lround(src[i + 2] * gain)), 0, 255);
            dst[i + 3] = src[i + 3];
        }
    }

    g_effect->clipReleaseImage(srcImg);
    g_effect->clipReleaseImage(dstImg);
    return kOfxStatOK;
}

OfxStatus mainEntry(const char *action, const void *handle, OfxPropertySetHandle inArgs,
                    OfxPropertySetHandle outArgs)
{
    (void)outArgs;
    if (!action) {
        return kOfxStatFailed;
    }
    if (std::strcmp(action, kOfxActionLoad) == 0) {
        return actionLoad();
    }
    if (std::strcmp(action, kOfxActionUnload) == 0) {
        return kOfxStatOK;
    }
    if (std::strcmp(action, kOfxActionDescribe) == 0) {
        return actionDescribe((OfxImageEffectHandle)handle);
    }
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
        return actionDescribeInContext((OfxImageEffectHandle)handle, inArgs);
    }
    if (std::strcmp(action, kOfxActionCreateInstance) == 0
        || std::strcmp(action, kOfxActionDestroyInstance) == 0) {
        return kOfxStatOK;
    }
    if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
        return actionRender((OfxImageEffectHandle)handle, inArgs);
    }
    return kOfxStatReplyDefault;
}

void setHost(OfxHost *host)
{
    g_host = host;
}

OfxPlugin g_plugin = {kOfxImageEffectPluginApi,
                      kOfxImageEffectPluginApiVersion,
                      "com.openvegas.gain",
                      1,
                      0,
                      setHost,
                      mainEntry};

} // namespace

OfxExport int OfxGetNumberOfPlugins(void)
{
    return 1;
}

OfxExport OfxPlugin *OfxGetPlugin(int nth)
{
    if (nth != 0) {
        return nullptr;
    }
    return &g_plugin;
}
