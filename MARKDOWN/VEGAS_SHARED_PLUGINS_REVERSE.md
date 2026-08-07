# VEGAS Shared Plug-Ins — разбор бинарников для будущего реверс-инжиниринга

**Дата:** 2026-08-07
**Источник:** реальная установка на этой машине — `C:\Program Files (x86)\VEGAS\Shared Plug-Ins\` и `C:\Program Files (x86)\Sony\Shared Plug-Ins\` (идентичный набор DLL, второй — судя по `Help Files\vegas180.chm`, наследие ещё Sony Vegas 18).
**Метод:** `PowerShell Get-ChildItem ... VersionInfo` (ProductName/FileDescription) + `objdump -x` (MinGW binutils, `C:\Qt\Tools\mingw1310_64\bin\objdump.exe`) на PE export-таблицы. Дизассемблер (IDA/Ghidra) не использовался — это разведка «снаружи», не полный реверс.
**Правообладатель:** MAGIX Computer Products Intl. Co. (наследие Sonic Foundry / Sony Creative Software). Материал — для сопоставления имён/архитектуры с OpenVegas (`VegasSharedAudioCatalog`), **не для копирования кода или встраивания бинарников**.

Связано с: [`UI_STUBS_AUDIT.md`](UI_STUBS_AUDIT.md) §11 (VEGAS Shared `CatalogOnly` эффекты), `src/plugins/VegasSharedAudioCatalog.cpp`.

---

## 1. Инвентарь на диске

```
C:\Program Files (x86)\VEGAS\Shared Plug-Ins\
└── Audio_x64\
    ├── apluginsk.dll         10 730 528 байт   — общее ядро (см. §3)
    ├── Errorreport.dll          130 128 байт   — не FX, crash-reporter
    ├── mchammer_x64.dll         359 984 байт   — Wave Hammer 5.1
    ├── sffrgpnv_x64.dll         267 344 байт   — Sound Forge Pro Pan and Volume 1
    ├── sfppack1_x64.dll         791 088 байт   — XFX 1 Plug-In Pack
    ├── sfppack2_x64.dll         907 312 байт   — XFX 2 Plug-In Pack
    ├── sfppack3_x64.dll         589 360 байт   — XFX 3 Plug-In Pack
    ├── sfresfilter_x64.dll      301 104 байт   — Resonant Filter
    ├── sftrkfx1_x64.dll         589 856 байт   — TrackFX 1
    ├── sfxpfx1_x64.dll          362 024 байт   — ExpressFX 1
    ├── sfxpfx2_x64.dll          366 120 байт   — ExpressFX 2
    ├── sfxpfx3_x64.dll          425 512 байт   — ExpressFX 3
    └── xpvinyl_x64.dll          272 456 байт   — ExpressFX Audio Restoration
```

`C:\Program Files (x86)\Sony\Shared Plug-Ins\Audio_x64\` — тот же список **без** `apluginsk.dll` (комментарий в коде проекта — «Shared kernel present on VEGAS install only» — подтверждён на реальной установке).

### Сверка с `VegasSharedAudioCatalog::catalog()` — 100% совпадение

`ProductName`/`FileDescription` из VersionInfo каждой DLL совпадают буква-в-букву с `packProductName`, уже захардкоженным в `src/plugins/VegasSharedAudioCatalog.cpp`:

| DLL | ProductName (реальный, из версии) | CompanyName |
|---|---|---|
| `apluginsk.dll` | VEGAS Pro | MAGIX Computer Products Intl. Co. |
| `Errorreport.dll` | VEGAS Error Reporting Component | MAGIX Computer Products Intl. Co. |
| `mchammer_x64.dll` | Wave Hammer 5.1 | MAGIX Computer Products Intl. Co. |
| `sffrgpnv_x64.dll` | Sound Forge Pro Pan and Volume 1 | MAGIX Computer Products Intl. Co. |
| `sfppack1_x64.dll` | XFX 1 Plug-In Pack | MAGIX Computer Products Intl. Co. |
| `sfppack2_x64.dll` | XFX 2 Plug-In Pack | MAGIX Computer Products Intl. Co. |
| `sfppack3_x64.dll` | XFX 3 Plug-In Pack | MAGIX Computer Products Intl. Co. |
| `sfresfilter_x64.dll` | Resonant Filter | MAGIX Computer Products Intl. Co. |
| `sftrkfx1_x64.dll` | TrackFX 1 | MAGIX Computer Products Intl. Co. |
| `sfxpfx1_x64.dll` | ExpressFX 1 | MAGIX Computer Products Intl. Co. |
| `sfxpfx2_x64.dll` | ExpressFX 2 | MAGIX Computer Products Intl. Co. |
| `sfxpfx3_x64.dll` | ExpressFX 3 | MAGIX Computer Products Intl. Co. |
| `xpvinyl_x64.dll` | ExpressFX Audio Restoration | MAGIX Computer Products Intl. Co. |

**Вывод:** каталог в коде OpenVegas не нуждается в правках — он точно описывает реально установленный пакет.

Команда для воспроизведения на другой машине:
```powershell
Get-ChildItem 'C:\Program Files (x86)\VEGAS\Shared Plug-Ins\Audio_x64\*.dll' | ForEach-Object {
  $vi = $_.VersionInfo
  [PSCustomObject]@{ Name=$_.Name; ProductName=$vi.ProductName; CompanyName=$vi.CompanyName }
} | Format-Table -AutoSize
```

---

## 2. Формат бинарников — почему их нельзя просто `LoadLibrary`

Экспорты каждой из **12 «эффектных» DLL** (все, кроме `apluginsk.dll` и `Errorreport.dll`) проверены через:
```bash
objdump.exe -x "<dll>" | awk '/\[Ordinal\/Name Pointer\] Table/,/^$/'
```

Результат **идентичен** во всех 12 файлах (кроме `sftrkfx1_x64.dll`, у которого на 2 экспорта больше):

```
DllCanUnloadNow
DllGetClassObject
DllMain
DllRegisterServer
DllUnregisterServer
??0CMappingOfSfMemoryToken@@QEAA@AEBU_sfmemorytoken@@K@Z         ; ctor(const _sfmemorytoken&, unsigned long)
??0COutOfProcessMemoryToken@@QEAA@AEAU_sfmemorytoken@@KH@Z       ; ctor(_sfmemorytoken&, unsigned long, int)
??1CMappingOfSfMemoryToken@@QEAA@XZ                              ; dtor
??1COutOfProcessMemoryToken@@QEAA@XZ                             ; dtor
??4CMappingOfSfMemoryToken@@QEAAAEAV0@AEBV0@@Z                   ; operator=
??4COutOfProcessMemoryToken@@QEAAAEAV0@AEBV0@@Z                  ; operator=
?Close@CMappingOfSfMemoryToken@@QEAAXXZ                          ; void Close()
?DataSize@CMappingOfSfMemoryToken@@QEAAJXZ                       ; long DataSize()
?Dispose@CMappingOfSfMemoryToken@@QEAAXXZ                        ; void Dispose()
?GetMemoryToken@COutOfProcessMemoryToken@@QEAAJAEAU_sfmemorytoken@@@Z
?GetPointer@CMappingOfSfMemoryToken@@QEAAJPEAPEAX@Z
?Pointer@CMappingOfSfMemoryToken@@QEAAPEAXXZ
```

`sftrkfx1_x64.dll` (TrackFX 1) дополнительно экспортирует:
```
??4CSfDither@@QEAAAEAV0@AEBV0@@Z                                 ; CSfDither::operator=
?BuildPositionStringEx@@YAXPEA_WPEAU_posfmt@@_J0I@Z              ; free fn — форматирование позиции/таймкода
?GetWingdingsFont@@YAPEAUHFONT__@@XZ                             ; free fn — возвращает HFONT (Wingdings, вероятно для VU-метра/иконок)
```

### Что это значит

1. **Это COM in-process сервер**, не VST (`VSTPluginMain`) и не OFX (`OfxGetNumberOfPlugins`). Реальный вход — `DllGetClassObject(CLSID, IID, ppv)`, классический COM-фабричный паттерн.
2. **Ни один из 12 файлов не экспортирует ничего специфичного для конкретного эффекта.** DSP-код (собственно Compressor/Chorus/Reverb/Distortion/…) находится **внутри**, не экспортирован — доступен только через `CoCreateInstance`/`DllGetClassObject` с конкретным (нам неизвестным) CLSID на каждый эффект внутри пака (например, `sfppack2_x64.dll` = «XFX 2» = по комментарию в `VegasSharedAudioCatalog.cpp` содержит Graphic EQ + Parametric EQ + Paragraphic EQ — то есть **минимум 3 разных CLSID в одной DLL**, различить их по экспортам невозможно).
3. `CMappingOfSfMemoryToken` / `COutOfProcessMemoryToken` — общая инфраструктура передачи звуковых буферов через **shared memory токены**, причём именно «OutOfProcess» — сильный намёк, что обработка звука у Sonic Foundry/MAGIX исторически может идти в отдельном процессе (изоляция/стабильность хоста), а не in-process как VST.
4. **Следствие:** чтобы реально дёрнуть эти эффекты (не просто показать имя в списке), нужно:
   - Найти CLSID каждого эффекта — либо через `regsvr32 <dll>` и чтение `HKEY_CLASSES_ROOT\CLSID\{...}` (какие ключи появились), либо дизассемблировать `DllGetClassObject`/`DllRegisterServer` (там сравнение с константами GUID).
   - Найти/угадать proprietary COM-интерфейс (IID) для передачи аудио через `_sfmemorytoken` — недокументирован, у OpenVegas такого хоста нет и не планируется (см. Non-goals в `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`).
   - Это уровень полноценного реверса (Ghidra/IDA), а не просто вызова функции — поэтому текущий подход проекта (каталог для UI + маппинг на builtin-эквиваленты, без `LoadLibrary`/`CoCreateInstance`) остаётся правильным решением, не временной затычкой.

---

## 3. `apluginsk.dll` — общее ядро (10.7 МБ, ~1300 экспортов)

В отличие от 12 «эффектных» пустышек, `apluginsk.dll` экспортирует **огромный C++/C API** — это разделяемый рантайм-кит, на котором построены и VEGAS, и Sound Forge, и, вероятно, сами FX-паки (вызывают его внутри, не экспортируя наружу). Полный список получен командой:

```bash
objdump.exe -x "C:/Program Files (x86)/VEGAS/Shared Plug-Ins/Audio_x64/apluginsk.dll" \
  | awk '/\[Ordinal\/Name Pointer\] Table/,/^$/'
```//→ 1300 строк; ниже — категоризированная выжимка (полный дамп воспроизводим командой выше на любой машине с этой установкой).

### 3.1 Аудио-DSP (самое интересное для OpenVegas)

| Класс/группа | Экспорты (примеры) | Комментарий |
|---|---|---|
| Reverb | `SfReverbInit`, `SfReverbProcess` (float/double), `SfReverbClose` | Готовый DSP-движок реверба — потенциальный референс для golden-compare с `BuiltinDsp` реверба. |
| IIR/FIR фильтры | `CSfIIR::Design/Process*/getBiquad/getGain`, `CSfFIR::Design/Process*`, `SfMakeBiquad`, `SfEq_Init/Process*/Close/Reset` | Это и есть движок EQ, на который, по всей видимости, опираются `sfppack2` (Graphic/Parametric/Paragraphic EQ) и `sftrkfx1` (Track EQ). |
| Dither | `CSfDither` (ctor с `eSFDITHER_DITHERTYPE`/`eSFDITHER_NOISESHAPE`), `ProcessDouble/ProcessFloat` | Полноценный noise-shaping dither — актуально при будущей реализации честного bit-depth reduction. |
| Ресэмплинг | `SfAudio_Resample`, `SfAudio_Resample2*` (Open/Process/Close/GetSrcPos/GetDstPos — отдельный «v2» API), `SfAudio_Resample_Sinc` | Минимум 2 разных алгоритма ресэмплинга (в т.ч. sinc). |
| Микширование/копирование каналов | `SfAudio_Mix`, `SfAudio_MixEx`, `SfAudio_CopyChannels`, `SfAudio_InterleaveStereo(Ex)`, `SfAudio_SwapStereoChannels`, `SfAudio_ExtractChannel(Pair)` | Общие билд-блоки для audio engine. |
| FFT / спектральный анализ | `SfSig_FFT*`, `SfSig_InverseFFT*`, `SfSig_PrepareFFT/Reprepare/Unprepare`, `SfSig_FindBandPassCoeffs`, `SfSig_Find{Hi,Low}ShelfBoost/CutCoeffs`, плюс отдельно `SfPerfsFFT*_32fc/_64f` (видимо Intel IPP-подобная быстрая реализация) | Полезно для будущего spectral EQ / анализатора. |
| Peak/waveform | `SfAudioPeak_CreateInstance`, `SfAudioPeak_Resample`, `SfAudioPeak_CalcMinMax`, `SfAudioStream_*` (полноценный потоковый I/O слой поверх WAV) | Похоже на движок построения waveform-превью — сопоставимо с `MediaWaveformCache` в OpenVegas. |

### 3.2 Видео / изображения

| Группа | Экспорты (примеры) | Комментарий |
|---|---|---|
| YUV↔RGB, chroma subsampling | `SfYUV420ToRGBA(_Half/_Quarter/_BlendFields варианты)`, `SfYUV_I420_To_{NV12,YV12}`, `SfYUV_NV12_To_I420`, `SfYUV_P010_To_I420`, `SfYUV_P210_To_{I420,v210}`, `SfYUV_YUVToRGB(_OCL)`, `SfYUV_RGBToYUV(_OCL)`, `SfRGB2UYVY`/`SfUYVY2RGBA` | Профессиональный набор конверсий пиксельных форматов, включая 10-бит (`P010`/`P210`/`v210`) — шире того, что сейчас есть в `src/video/*`. |
| GPU/OpenCL | `DibCopy_OpenCL_Initialize/Shutdown`, `DibInterlace_OpenCL_Initialize/Shutdown`, `SfDibCopyOCL`, `SfDibScale_Bilinear/BiCubic(SubPixel)`, `SfDibFadeToColorOCL`, `SfDibCopyInterlace*OCL`, `YUV_OpenCL_Initialize/Shutdown`, `SfYUV_RGBToYUV_OCL` | **Прямое пересечение с бэклогом OpenVegas** («Полный GPU realtime compositor... позже GPU/OpenCL опционально» в `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`) — у Vegas это уже готовый, скорее всего проверенный временем набор OpenCL-кернелов для масштабирования/деинтерлейса/fade. |
| Прочее | `SfDibQuantizePalette`, `SfDibCreateColorQuantizer`, `SfRGBtoHSL`/`SfHSLtoRGB`, `SFDIBPIXEL_To_SFHSL(_Float)` | Цветовые преобразования (может пригодиться для Color Grading HSL-вкладок — см. `UI_STUBS_AUDIT.md` §3, они сейчас заглушки). |

### 3.3 Файловый I/O (WAV/BWF/AIFF/RIFF)

`riff_*` / `riff64_*` (64-битные RIFF-размеры для >4ГБ WAV), `iff_*`, `aiff_*`, `SfBWFField_*`/`SfBWFList_*` (Broadcast Wave Format метаданные), `SfInitializeWav(64)(WithJunk)`, `SfFixTrashedWav(64)` — это полноценная библиотека чтения/записи WAV/BWF/AIFF с поддержкой junk-chunks и повреждённых файлов. Существенно шире, чем то, что сейчас есть в `src/io/` OpenVegas.

### 3.4 UI-тулкит (классические Vegas/Sound Forge виджеты)

`Knob*`, `Fader*`, `MeterInst_*`/`MeterExInst_*`, `Scope*`, `Spinner*`, `Trackbar*`, `TabControl*`, `DoneBar*`, `Logo*`, `Plexiglas*`, `SfDlg*` (диалоговые хелперы) — это набор custom-controls (фейдеры, ручки, VU-метры, скоупы), которыми отрисован весь классический интерфейс Vegas. Не актуально для OpenVegas (Qt-виджеты), но полезно знать при сравнении внешнего вида/поведения контролов 1:1.

### 3.5 Служебное

- `SFSMPTE_*` — SMPTE-таймкод (frame↔nanos, add/subtract, text↔SMPTE) — сравнимо с уже реализованным в OpenVegas таймкодом, можно свести для проверки edge-cases (drop-frame и т.п.).
- `SfMetric_*` / `SfLang_*` — их аналог QSettings + локализации (реестровое хранилище настроек с версионированием по hive).
- `SfErrorHandler_*` — их crash-handler/error-reporting инфраструктура (не то же самое, что отдельный `Errorreport.dll`, скорее общая база для него).
- `SfHelp_*` — CHM-хелп система (см. `Help Files\*.chm` рядом — `vfx1.ofx.chm`, `vegas180.chm` и т.д. — реальные файлы справки от старой версии).

---

## 4. Практические следующие шаги (если понадобится реальный реверс)

1. **Найти CLSID-ы**: `regsvr32 /s "<dll>"` в тестовой VM (не на рабочей машине — модифицирует реестр) → сравнить `HKEY_CLASSES_ROOT\CLSID` до/после, вытащить GUID-и и человекочитаемые имена (ProgID) для каждого эффекта внутри пака (особенно `sfppack1/2/3` — по 3 эффекта на файл).
2. **Дизасм `DllGetClassObject`** (Ghidra/IDA) — самый надёжный способ перечислить все CLSID без явной регистрации, там обычно линейное сравнение с константами.
3. **`SfEq_*`/`CSfIIR` в `apluginsk.dll`** — если понадобится golden-compare для Track EQ/Graphic EQ, это первое место, куда смотреть на предмет точных коэффициентов биквадов (`SfMakeBiquad`, `getBiquad`) — potentially можно сверить с `BuiltinDsp` EQ без необходимости трогать сами FX-паки.
4. **OpenCL-кернелы** (`SfDib*OCL`, `YUV_OpenCL_*`) — если/когда в OpenVegas реально дойдёт очередь до GPU-compositor (см. `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`, Non-goals → возможно снимется), это готовая референсная реализация масштабирования/деинтерлейса/fade для сверки корректности, а не для копирования (проприетарный код).
5. Всё это — **только для сверки архитектуры/поведения**, не для встраивания бинарного кода MAGIX в OpenVegas (лицензионно закрыто).

---

## Приложение — полные export-таблицы 12 «эффектных» DLL

Для справки — то, что реально видно снаружи (без дизасма) на каждом из 12 FX-контейнеров. Все идентичны базовому набору COM + `SfMemoryToken`; отличия отмечены.

<details>
<summary>Развернуть (12 × ~13-19 строк)</summary>

```
=== mchammer_x64.dll (Wave Hammer 5.1) ===
??0CMappingOfSfMemoryToken@@QEAA@AEBU_sfmemorytoken@@K@Z
??0COutOfProcessMemoryToken@@QEAA@AEAU_sfmemorytoken@@KH@Z
??1CMappingOfSfMemoryToken@@QEAA@XZ
??1COutOfProcessMemoryToken@@QEAA@XZ
??4CMappingOfSfMemoryToken@@QEAAAEAV0@AEBV0@@Z
??4COutOfProcessMemoryToken@@QEAAAEAV0@AEBV0@@Z
?Close@CMappingOfSfMemoryToken@@QEAAXXZ
?DataSize@CMappingOfSfMemoryToken@@QEAAJXZ
?Dispose@CMappingOfSfMemoryToken@@QEAAXXZ
?GetMemoryToken@COutOfProcessMemoryToken@@QEAAJAEAU_sfmemorytoken@@@Z
?GetPointer@CMappingOfSfMemoryToken@@QEAAJPEAPEAX@Z
?Pointer@CMappingOfSfMemoryToken@@QEAAPEAXXZ
DllCanUnloadNow / DllGetClassObject / DllMain / DllRegisterServer / DllUnregisterServer

=== sffrgpnv_x64.dll (Sound Forge Pro Pan and Volume 1) ===   — идентично mchammer_x64.dll
=== sfppack1_x64.dll (XFX 1: Chorus/Reverb/Pitch Shift) ===   — идентично
=== sfppack2_x64.dll (XFX 2: Graphic/Parametric/Paragraphic EQ) === — идентично
=== sfppack3_x64.dll (XFX 3: Flange/Distortion) ===           — идентично
=== sfxpfx1_x64.dll (ExpressFX 1: Chorus/Distortion/Flange/Reverb) === — идентично
=== sfxpfx2_x64.dll (ExpressFX 2: Delay/Simple Delay/EQ) ===  — идентично
=== sfxpfx3_x64.dll (ExpressFX 3: AM/Smooth-Enhance/Vibrato) === — идентично
=== sfresfilter_x64.dll (Resonant Filter) ===                 — идентично
=== xpvinyl_x64.dll (ExpressFX Audio Restoration) ===         — идентично

=== sftrkfx1_x64.dll (TrackFX 1: Track EQ/Compressor/Noise Gate) ===
… (тот же базовый набор) плюс:
??4CSfDither@@QEAAAEAV0@AEBV0@@Z
?BuildPositionStringEx@@YAXPEA_WPEAU_posfmt@@_J0I@Z
?GetWingdingsFont@@YAPEAUHFONT__@@XZ

=== Errorreport.dll (не FX) ===
ErrorReport_AddFile / AddText / CommonExceptionFilter / Deadlock / EndTrapBypass /
Initialize / InitializeNoCrash / InitializeWithOptions / InstallFail / IsUILaunched /
ManagedException / RemoveFile / Shutdown / StartMonitoringProcess / StartTrapBypass /
StopMonitoringProcess / UnmanagedException / UserAbort
```

</details>
