# VEGAS Pro 22 — Видеоплагины: полный обзор

**Дата:** 2026-08-27
**Источник:** `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/`

---

## 1. Legacy COM/DXT плагины (`Video Plug-Ins/`)

Все 5 DLL в `Video Plug-Ins/` — **legacy-архитектура COM/ATL DirectShow Transform** от Sonic Foundry.
**Ни один не использует OFX.**

| DLL | Размер | Эффекты | Архитектура | Описание |
|-----|--------|---------|-------------|----------|
| `PluginWrapper.dll` | 183 KB | 0 | COM wrapper | Generic COM host/wrapper (`CSFVideoPlugin<T>`) |
| `SfPagePeel.dll` | 528 KB | 9 | COM DXT | Page Peel, Displacement Map, Height Map, Noise Texture, Portal, Checkerboard, Channel Blend |
| `sftrans1.dll` | 2.5 MB | 20+ | COM DXT + SkynUI + TextFX | Credit Roll, Text, Cross Effect, Swirl, Pinch, Spiral, Waves, Slide, Split, Venetian Blinds, Light Rays, etc. |
| `vfx1.dll` | 1.8 MB | 10 | COM DXT + 3D/spline | 3D Blinds, 3D Cascade, 3D Fly In/Out, Film Grain, Flash, Gradient Map, News Print, Saturation Adjust, Shuffle, TV Simulation |
| `vidpcore.dll` | 6.0 MB | 32 | COM DXT + bitmaps | Dissolve, Blur, Brightness/Contrast, HSL, Levels, Invert, B&W, Sepia, Sharpen, Chroma Keyer, Wipes, Text, Test Pattern |

### Общие черты

- Все импортируют **`sharedk.dll`** (Sonic Foundry shared kernel)
- COM-регистрация через `DllGetClassObject`/`DllRegisterServer`
- Шаблон `CSFVideoPlugin<TPLUGINPROPS>` — основа всех эффектов
- ProgID с префиксом `DXT` (например, `DXTAdditiveDissolve`)
- Интерфейсы: `ISfVideoPluginExtension`, `ISfSetHostApp`, `IDXEffect`, `IDXTransform`, `IDXSurface`
- `PluginWrapper.dll` — тонкая оболочка,/host для сторонних COM-плагинов

### sftrans1.dll — самый богатый

- 316 уникальных внутренних классов
- Полная система UI-скининга `CSfWSkyn*` (~80 классов)
- Рендеринг текста `CSfTxtFx`/`CSfTxtRender`
- XML-парсер `CSfXML_Document`/`CSfXMLParser`
- Менеджер меню `CSfMenuManager`
- Глифы `CSfGlyphSet`

---

## 2. OFX Video Plug-Ins (`OFX Video Plug-Ins/`)

**11 бандлов**, ~72 плагина. Все `.ofx` — PE x64 DLLs с расширением `.ofx`.

> **Подтверждено 2026-08-27** (ghidra headless + dumpbin): полный перечень экспортов
> и plugin IDs всех 11 бандлов — см. §2A ниже.

### Сводная таблица

| Бандл | Бинарник | Размер | Плагины | Особенности |
|-------|---------|--------|---------|-------------|
| **Vfx1** | Vfx1.ofx | **10.2 MB** | **78** | Монолитный, все базовые эффекты, 43 LUT-файла |
| **MagixCVFx** | MagixCVFx.ofx | 5.6 MB | 11 | Denoise, Mesh Warp, Motion Blur, GL Transition, Warp Flow |
| **spica_cutout** | spica_cutout.ofx | 3.6 MB | 1 | Smart Zoom (Sony-era), OpenCL |
| **spica_resizer** | spica_resizer.ofx | 3.6 MB | 1 | Smart Upscale (Sony-era), OpenCL |
| **MagixAiFx** | MagixAiFx.ofx | 1.9 MB | 10 | AI: Auto Reframe, Colorization, Z-Depth, Upscale, Style Transfer |
| **Stabilize** | Stabilize.ofx | 492 KB | 1 | Media-effect стабилизация |
| **Filters** | Filters.ofx | 524 KB | 1 | Stereoscopic 3D Adjust |
| **TitlesAndText** | TitlesAndText.ofx | 475 KB | 1 | Generator, RTF-текст |
| **ofx360Stabilizer** | ofx360Stabilizer.ofx | 60 KB | 1 | Thin proxy + cpu.exec/ocl.exec |
| **ofxRotation** | VegasOfxRotation.ofx | 60 KB | 1 | Thin proxy + cpu.exec/ocl.exec |
| **ofxStitch** | VegasOfxStitch.ofx | 221 KB | 1 | VR Dual-Fish-Eye + VrDualStitch.dll |

### Структура бандла

```
{Name}.ofx.bundle/
  Contents/
    Win64/
      {Name}.ofx          # PE x64 DLL (OFX плагин)
      [helper DLLs]       # Опционально: compute backends
    Resources/
      {Name}.xml          # OFX resource definitions (параметры, локализация)
      {Name}.{locale}.xml # 9 локалей: de-DE, es-ES, fr-FR, ja-JP, ko-KR, pl-PL, pt-BR, zh-CN, base
      *.png               # Thumbnail-иконки
    Presets/              # Опционально
      PresetPackage.xml
      PresetPackage.{locale}.xml
```

### Архитектурные паттерны

#### Монолитные (8 бандлов)
Весь код в `.ofx` файле. Самый большой — **Vfx1.ofx** (10.2 MB, 40+ плагинов).
Содержит: checkerboard, colorgradient, noisetexture, solidcolor, testpattern, addnoise,
autolooks, blackandwhite, blackrestore, border, brightnessandcontrast, broadcastcolors,
bumpmapfilter, channelblend, chromablur, chromakeyer, colorbalance, colorcorrector,
colorcorrectorsecondary, colorcurves, colormatch, convolutionkernel, cookiecutter, ...

#### Thin proxy + compute DLLs (3 бандла)
`ofx360Stabilizer`, `ofxRotation`, `ofxStitch` — тонкий `.ofx` (60-220 KB) делегирует
вычисления в отдельные DLL:

| Бандл | Main .ofx | CPU backend | GPU backend |
|-------|-----------|-------------|-------------|
| ofx360Stabilizer | 60 KB | ofx.cpu.executable (353 KB) | ofx.ocl.executable (555 KB) |
| ofxRotation | 60 KB | ofx.cpu.executable (226 KB) | ofx.ocl.executable (376 KB) |
| ofxStitch | 221 KB | VrDualStitchCpu.dll (97 KB) | VrDualStitch.dll (245 KB) |

#### Sony-era плагины (2 бандла)
`spica_cutout` и `spica_resizer` — namespace `com.sonycreativesoftware`,
требуют OpenCL, ошибки ссылаются на "OpenCL GPU memory exhausted".

### Детали по бандлам

#### MagixAiFx — AI-плагины (10)

| Plugin ID | Название |
|-----------|----------|
| `de.magix:autoframe` | VEGAS AI Auto Reframe |
| `de.magix:Colorization` | VEGAS AI Colorization |
| `de.magix:AiDehaze` | VEGAS AI Dehaze |
| `de.magix:zdepth` | VEGAS AI Z-Depth |
| `de.magix:AiSharpen` | VEGAS AI Sharpen |
| `de.magix:smartmask` | VEGAS AI Smart Mask |
| `de.magix:samasking` | VEGAS AI Smart Mask 2.0 |
| `de.magix:AiSmooth` | VEGAS AI Smoothen |
| `de.magix:StyleTransfer` | VEGAS AI Style Transfer |
| `de.magix:Upscale` | VEGAS AI Upscale |

Требуют Deep Learning Models компонент. Style Transfer включает 24 превью-картинки.

#### MagixCVFx — Computer Vision (11)

| Plugin ID | Название | Контекст |
|-----------|----------|----------|
| `de.magix:FlickerReducer` | VEGAS Flicker Control | Filter |
| `de.magix:DenoisingNLM` | VEGAS Denoise | Filter |
| `de.magix:glTransition` | VEGAS GL Transition | **Transition** |
| `de.magix:LensCorrection` | VEGAS Lens Correction | Filter |
| `de.magix:MeshWarp` | VEGAS Mesh Warp | Filter |
| `com.magix:MotionBlur` | VEGAS Motion Blur | Filter |
| `de.magix:MotionTracker` | VEGAS Motion Tracker | Filter |
| `de.magix:ShotDetector` | VEGAS Scene Detection | Filter |
| `de.magix:TimeWarp` | VEGAS Slow Motion | Filter |
| `de.magix:Stabilize` | VEGAS Video Stabilization | Filter |
| `de.magix:WarpFlowTransition` | VEGAS Warp Flow | **Transition** |

8 из 11 требуют GPU suites (OpenGL/OpenCL) — без них `DescribeInContext` возвращает
нуль клипов и нуль параметров. Описываются только motiontracker, shotdetector, stabilize.

#### Vfx1 — базовые эффекты (40+)

| Категория | Эффекты |
|-----------|---------|
| Generators | checkerboard, colorgradient, noisetexture, solidcolor, testpattern |
| Color | autolooks, blackandwhite, brightnessandcontrast, broadcastcolors, colorbalance, colorcorrector, colorcorrectorsecondary, colorcurves, colormatch |
| Keying | chromablur, chromakeyer |
| Blur | gaussianblur, linearblur, radialblur |
| Distort | deform, filmEffects, distortFilm, spherize |
| Stylize | addnoise, blackrestore, border, bumpmapfilter, convolutionkernel, cookiecutter, glow, lensFlare, median, minandmax, invert, sepia, sharpen, threshold, unsharpMask |
| Transition | clockwipe, iris, linearwipe, push, zoom |
| 3D | stereoscopicProjectionOCL |

43 LUT-файла (.cube) в `Resources/AutoLooks/`.

---

## 2A. OFX ABI и плагин-ID (подтверждено бинарно, 2026-08-27)

### Экспортная ABI — упрощённая, без `OfxSetHost`

Все 11 `.ofx` — PE x64 DLL. **Ни один не экспортирует `OfxSetHost`/`OfxGetPlugins`**
(legacy OFX 1.x). Только два метода:

| Экспорт | Сигнатура | Смысл |
|---------|-----------|-------|
| `OfxGetNumberOfPlugins` | `int(void)` | Количество слотов в таблице плагинов |
| `OfxGetPlugin` | `OfxPlugin* (int i)` | Плагин по индексу |

Хост должен: `GetProcAddress("OfxGetNumberOfPlugins")` → N, потом N раз
`GetProcAddress("OfxGetPlugin")` с индексом. **`OfxHost.cpp:2345-2346` уже делает
именно так** — ABI совпадает с реальным VEGAS 22.

`OfxGetPlugin` собирает `OfxPlugin` на лету (Stabilize.ofx @0x180020aa0,
TitlesAndText.ofx @0x1800267f0):
`{ "OfxImageEffectPluginAPI", apiVersion=1, pluginIdentifier, iPluginVersion,
  pluginDescriptor=LAB_setHost, mainEntry }` + контекстные данные в кэш по индексу.

### Общий каталог плагинов (plugin identifiers из .rdata)

| Бандл | Плагинов | Plugin IDs (`com.vegascreativesoftware:*` если не указано) |
|-------|----------|------------------------------------------------------------|
| **Vfx1** | **78** | см. список ниже, + `de.magix:stereographicProjectionOCL` |
| **MagixCVFx** | 11 | `de.magix:Stabilize,LensCorrection,MeshWarp,WarpFlowTransition,DenoisingNLM,TimeWarp,MotionTracker,ShotDetector,FlickerReducer,glTransition` + `com.magix:MotionBlur` |
| **MagixAiFx** | 10 | `de.magix:Colorization,StyleTransfer,AiDehaze,AiSharpen,AiSmooth,Upscale,zdepth,smartmask,samasking,autoframe` |
| **Stabilize** | 1 | `:Stabilize` |
| **Filters** | 1 | `:Filters.Stereoscopic3DAdjust` |
| **TitlesAndText** | 1 | `:titlesandtext` |
| **VegasOfxStitch** | 1 | `com.magix.ofx.vr.dual.stich` |
| **spica_cutout** | 1 | "Sony Smart Zoom" — класс `BPRL_SPICACutout_Plugin` (ID генерируется в OFX-фреймворке) |
| **spica_resizer** | 1 | "Sony Smart Resize" — класс `BPRL_SPICAResizer_Plugin` (ID генерируется) |
| **ofx360Stabilizer** | 1 | `uPlugin.ofx360Stabilizer` (proxy) |
| **VegasOfxRotation** | 1 | `uPlugin.MxOfxRotation` (proxy) |

Строки `com.vegascreativesoftware.vegas`, `.vegas.moviestudio.pe`, `.vegas.moviestudio.hd`,
`com.eyeonline.Fusion` в .rdata — **идентификаторы хостов**, не плагины.

### Vfx1.ofx — 78 плагинов (полный список из .rdata @0x18088e7c8-…)

```
checkerboard  addnoise      testpattern   solidcolor    noisetexture
hsladjust     glow          levels        lensflare      labadjust
invert        filllightvelvetmatter  blackbarfill  defocusvelvetmatter
gaussianblur  colorcorrectorsecondary colorcorrector colorbalance chromakeyer
cookiecutter  convolutionkernel  colormatch  colorcurves
brightnessandcontrast border  blackrestore  blackandwhite  chromablur
channelblend  bumpmapfilter  broadcastcolors  unsharpmask  timecode
radialpixelate whitebalance  sharpen  sepia  saturationadjust
starburstvelvetmatter spherize  newsprint  mirror  minandmax
radialblur    quickblur     pixelate      autolooks      lutfilter
linearblur    median        maskgenerator  bzmasking     colorgrading
vignette      layerdimensionality  offset  crop  pictureinpicture
-- transitions --
starwipe      squeeze       split         spiral         bumpmap
zoom          swap          pagepeel      pageloop       linearwipe
iris          slide         push          pageroll       clockwipe
barndoor      flash         dissolve      crosseffect
+ de.magix:stereographicProjectionOCL (3D)
```

### Thin-прокси — динамические загрузчики

`ofx360Stabilizer.ofx` / `VegasOfxRotation.ofx` (60 KB) — **не содержат логики**:
`OfxGetNumberOfPlugins`/`OfxGetPlugin` делегируют через функцию-инициализатор
(`FUN_180001900`, перезапись JMP-таблицы), которая:
- грузит `\ofx.cpu.executable` (CPU-бэкенд) или выбирает CPU/OpenCL по
  `OpenCL.dll` (`clGetPlatformIDs`, `clGetDeviceIDs`, `clGetDeviceInfo`),
- проверяет config в `%APPDATA%\..\Local\%PLUGIN_LOCAL_SUBDIR%\`,
- `entry`/`Plugin.ofx360Stabilizer.settings.xml`, `AlternativePath`, `Resources`,
- возвращает 0, если бэкенд не загружен.

### Out-of-process shared-memory (sfmemorytoken)

`Stabilize`, `Filters`, `MagixCVFx`, `Vfx1` экспортируют C++-классы
`CMappingOfSfMemoryToken`/`COutOfProcessMemoryToken` (интерфейс
`_sfmemorytoken`): host-сторона маппит shared memory чужого процесса для
рендера в отдельном процессе. `Filters.ofx` ещё и `CScanlineIntersect(Pool)`
(растеризация через сканирующие линии); `Vfx1` — полный приватный SDK
(`CSfDib*`, `SfScope_*`, `SfWnd_*`, `CSfSurface`, `CSfMenuManager`,
`Vector/Bounds/Path/Mask`).

### MagixCVFx — CV-ядра в экспортах

`opticalFlow`/`opticalFlow8/32`, `patchTracker`/`patchTrackerSingle`,
`calcHomography`, `calcTransformationParameter`,
`MotionCompensatedCrossFade8/32`, `MotionCompensatedY32` — вызываются из
CV-плагинов. `MagixAiFx.ofx` экспортирует `QueryDirectX`, `runAiTests(W)`.

---

## 3. OpenColorIO

### OpenColorIO_2_0.dll

| Свойство | Значение |
|----------|----------|
| Размер | 3.84 MB (4,029,448 bytes) |
| Тип | Native C++ DLL (не .NET) |
| Архитектура | x64 (PE32+) |
| Image base | `0x180000000` |
| Версия | Пустая (нет version info) |
| Экспорты | **943** named exports |

Полный OpenColorIO v2.0 C++ SDK. Ключевые классы:

- **Config** — загрузка/сохранение OCIO конфигов
- **Processor / CPUProcessor / GPUProcessor** — выполнение цветовых преобразований
- **GpuShaderCreator / GpuShaderDesc** — генерация GPU-шейдеров
- **ColorSpace / ColorSpaceSet** — управление цветовыми пространствами
- **Transform types**: AllocationTransform, CDLTransform, ColorSpaceTransform, DisplayViewTransform, ExponentTransform, FileTransform, FixedFunctionTransform, GradingPrimaryTransform, GradingRGBCurveTransform, GradingToneTransform, GroupTransform, LogAffineTransform, LogCameraTransform, LogTransform, LookTransform, Lut1DTransform, Lut3DTransform, MatrixTransform, NamedTransform, RangeTransform, ViewTransform, BuiltinTransform
- **Grading**: GradingBSplineCurve, GradingControlPoint, GradingPrimary, GradingTone, GradingRGBCurve, DynamicProperty*

### ACES конфиги

Директория `OpenColorIO/configs/` — **162 файла**, **460 MB** LUT-данных.

| Конфиг | Версия OCIO | Файлов | Описание |
|--------|-------------|--------|----------|
| `aces_0.7.1` | v1 | ~40 | Старый ACES |
| `aces_1.2` | v1 | ~120 | Полный ACES 1.2 с HDR |
| `aces__OLD` | v1 | ~2 | ACES 1.0.3 |

Типы LUT-файлов:

| Расширение | Количество | Суммарный размер | Назначение |
|------------|-----------|-------------------|------------|
| `.spi3d` | 77 | 439.4 MB | 3D LUT |
| `.spi1d` | 56 | 20.5 MB | 1D LUT |
| `.spimtx` | 20 | ~0 MB | Matrix transforms |
| `.py` | 4 | ~0 MB | Генераторы LUT |

### aces_1.2 — дисплеи

sRGB, DCDM, DCDM P3D60/D65 Limited, P3-D60, P3-D65 ST2084 (1000/2000/4000 nits),
P3-DCI D60/D65 sim, P3D65, P3D65 D60 sim, P3D65 Rec.709 Limited, P3D65 ST2084 108 nits,
Rec.2020, Rec.2020 P3D65/Rec.709 Limited, Rec.2020 HLG/ST2084 (1000/2000/4000 nits),
Rec.709, Rec.709 D60 sim, sRGB D60 sim, Raw, Log.

### ColorGradingWindow.dll

| Свойство | Значение |
|----------|----------|
| Размер | 3.17 MB |
| Тип | .NET assembly (x86) |
| OFX references | 34 (OFXParameter, OFXChoiceParameter, OFXDoubleParameter) |

OFX-плагин, оборачивающий OFX-систему параметров VEGAS для UI цветокоррекции.
Содержит `GetColorGradingPluginNode`, `VegasCOM` interop.
**Не импортирует** OpenColorIO_2_0.dll напрямую — OCIO-обработка идёт через другой путь.

### ColorGradingTools.dll

19 KB, .NET assembly — вспомогательный UI-модуль для цветокоррекции.

---

## 3.1 ImageProcessingPlugins (`ImageProcessingPlugins/`)

**Единственный файл: `MxVfxShrink.dll`** — «Magix Color Conversion Plugin» v1.0.0.0.
**Установлено 2026-08-27** (dumpbin + строки). Не OFX, не DXT — внутренняя COM-DLL
фреймворка **vfxComFX** (`VfxPlugins\vfxComFX\MxVfxShrink\Release\`).

| Свойство | Значение |
|----------|----------|
| Размер | 92 KB |
| Версия | 1.0.0.0 |
| Экспорты | `DllCanUnloadNow`, `DllGetClassObject`, `MXSetHost` (псевдо-OFX set-host) |
| Классы | `CShrinker`, `CGpuShrink`, `CFxDx11GpuImplementation`, `CShrinker` wrapper |
| Интерфейс | `UIMXShrinker` |
| IPC | `CSmpWrapper` / `CSmpWrapperAdapter` + `Factory_CreateSMPWrapperObject` (shared-memory) |

### Назначение

GPU-ускоряемый **конвертор пиксельных форматов / цветовых пространств** для захвата
и превью (crop+convert: «shrinker»). Работает через **D3D11 + HLSL-шейдеры**
(DXBC: `texLuma/texCroma`, `CB_DstParams`, `CB_SrcDimAndROI`, `virtDstDim`).

### Поддерживаемые форматы

- Планарные YUV 8/10/12/16-bit: `NV12`, `NV16`, `YV12`, `IYUV`, `YV16`, `YV24`,
  `Y210/Y216/Y410`, `AYUV`, `YUY2`, `YVYU`, `YUYV`, `UYVY`
- P-серия HDR 10/12-bit: `P010/P210/P410`, `P012/P212/P412`, `P016/P216/P416`
- Alpha: `A210/A410`, `RGB6`, `pRGBA`, плавающий `*F32I`
- `q*`-варианты (quazi-full-range) + `VideoFrameParam*` метаданные

### VideoFrameParam (контракт с ядром)

`AdapterID`, `ROI`, `Interlace`, `FullRange`, `Resolution`, `DebugText`,
`AspectRatio`, `ColorContext`, `AlphaChannel`, `MediaSubtype`

### Особенности

- Подпись: DigiCert / MAGIX Berlin, timestamp 2025-05-05
- PDB-путь: `...\ffshared\mxshared_components\VfxPlugins\vfxComFX\MxVfxShrink\Release\`
- Src path: `sonic3` (общая Moonwood/Nightly build-ветка с External Control Drivers,
  откуда `omps\ompplugs\*`)

---

## 4. Взаимосвязи

```
Vegas Pro 22 Video Plug-Ins
├── Video Plug-Ins/           ← Legacy COM/DXT (sharedk.dll)
│   ├── PluginWrapper.dll     ← Generic COM host
│   ├── SfPagePeel.dll        ← 9 effects
│   ├── sftrans1.dll          ← 20+ effects (richest)
│   ├── vfx1.dll              ← 10 effects (3D/spline)
│   └── vidpcore.dll          ← 32 effects (core)
│
├── OFX Video Plug-Ins/       ← OFX (.ofx = PE x64 DLLs)
│   ├── Vfx1.ofx              ← 40+ effects, 10 MB (monolithic)
│   ├── MagixCVFx.ofx         ← 11 CV effects, 5.6 MB
│   ├── MagixAiFx.ofx         ← 11 AI effects, 1.9 MB
│   ├── spica_cutout.ofx      ← Smart Zoom (Sony-era)
│   ├── spica_resizer.ofx     ← Smart Upscale (Sony-era)
│   ├── Stabilize.ofx         ← Media-effect stabilization
│   ├── Filters.ofx           ← Stereoscopic 3D
│   ├── TitlesAndText.ofx     ← Generator (RTF)
│   ├── ofx360Stabilizer.ofx  ← Thin proxy + compute DLLs
│   ├── ofxRotation.ofx       ← Thin proxy + compute DLLs
│   └── ofxStitch.ofx         ← VR stitching + helper DLLs
│
├── OpenColorIO_2_0.dll       ← 943 exports, OCIO v2.0 SDK
├── OpenColorIO/              ← 460 MB ACES LUTs (3 configs)
├── ColorGradingWindow.dll    ← .NET OFX UI wrapper
├── ColorGradingTools.dll     ← .NET UI helper
│
└── ImageProcessingPlugins/   ← НЕ OFX, COM-DLL vfxComFX
    └── MxVfxShrink.dll       ← D3D11 GPU color/pixel-format converter
```

---

## 5. Выводы для OpenVegas

1. **Legacy COM/DXT плагины не хостятся через OFX** — это замкнутая экосистема с
   `sharedk.dll`, `IDXEffect`, `CSFVideoPlugin<T>`. Поддержка в OpenVegas через
   `PluginWrapper.dll` или эмуляцию.

2. **OFX-плагины — основная цель**. 11 бандлов, **107 плагинов подтверждено**
   (Vfx1=78, MagixCVFx=11, MagixAiFx=10, остальные 8 бандлов по 1).
   Все — PE x64 DLLs. Кроссплатформенно только если есть нативные бинарники для целевой ОС.

3. **GPU suites критичны** для 8 из 11 плагинов MagixCVFx. Без них описываются
   только motiontracker, shotdetector, stabilize.

4. **OpenColorIO** — нативная C++ библиотека (943 экспорта), используется для
   цветокоррекции. ColorGradingWindow.dll — .NET OFX UI-обёртка, не импортирует
   OCIO напрямую.

5. **Thin proxy + compute DLLs** — паттерн для тяжёлых GPU-вычислений (360° стабилизация,
   ротация, ститчинг). Основной `.ofx` мал, ядро в отдельных DLL.

6. **`ImageProcessingPlugins/MxVfxShrink.dll`** — отдельная компонента (нет OFX):
   GPU-конвертер пиксельных форматов через D3D11/HLSL, интерфейс `UIMXShrinker`,
   shared-memory IPC (`CSmpWrapper`). Тот же shared-memory паттерн, что у OFX-бандлов
   и External Control Drivers. Кроссплатформенно при наличии D3D11.
