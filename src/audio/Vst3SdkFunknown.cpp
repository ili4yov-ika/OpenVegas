// Steinberg's funknown.cpp, built through this file so its warnings stay out of the log.
// Under COM_COMPATIBLE it prints the GUID's Data1 with %X, and on Windows that field is
// an unsigned long: harmless where long is 32 bits. MSVC also flags its sprintf/sscanf/
// strcpy as deprecated (C4996). The SDK is not ours to edit. Listed by CMakeLists.txt and
// OpenVegas.pro in place of the SDK file itself, and only when the SDK is present.
#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat"
#endif
#include "pluginterfaces/base/funknown.cpp"
