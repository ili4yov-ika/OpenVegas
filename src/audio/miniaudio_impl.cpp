#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // ma_uint64 → ma_uint32 in vendored miniaudio.h
#endif
#include "miniaudio.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
