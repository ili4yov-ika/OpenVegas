# VEGAS Pro 22 — External Control Drivers (внешние control surface)

**Дата:** 2026-08-27
**Источник:** `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/External Control Drivers/`
**Метод:** dumpbin exports/imports, строки. Плагины для Ghidra не импортированы (лёгкая идентификация).

---

## 1. Что это

Внешние драйверы контрольных поверхностей микшерной консоли VEGAS: MIDI-контроллеры
(PreSonus FaderPort, Frontier Design TranzPort, Mackie Control), универсальный
XML-маппируемый MIDI-контроллер и TCP-remote (Network XML).

Все 6 DLL: **v22.0 Build 250**, компания `MAGIX Computer Products Intl. Co.`.
Подписаны DigiCert / MAGIX Software GmbH, timestamp 2025-05-05.

| DLL | Размер | Описание (FileDescription) |
|-----|--------|---------------------------|
| `faderport.dll` | 539 KB | PreSonus Faderport Plug |
| `tranzport.dll` | 607 KB | Frontier Design TranzPort Plug |
| `spmackiectrlopt.dll` | 836 KB | VEGAS OPT Mackie Control Plug |
| `spgenctrlopt.dll` | 1 008 KB | VEGAS OPT Generic Control Plug |
| `networkXML.dll` | 338 KB | Network XML Plug |
| `spconsoleopt.dll` | 2 339 KB | VEGAS Mixer Console Plug |

---

## 2. Общий ABI — фреймворк OMPS

Все 6 — плагины единого фреймворка **OMPS** (build-tree `...\omps\ompplugs\<Plugin>\Release\`).
Единый C-ABI из 4 экспортов + классы shared-memory токенов:

```
SfOPTPlug_CreateOPTObject      — создать объект плагина
SfOPTPlug_CreateOPTPropertyPage— создать property page
SfOPTPlug_GetOPTObjectInfo     — получить информацию
SfOPTPlug_UpdateKernelLCID     — update локали ядра

CMappingOfSfMemoryToken        — маппинг _sfmemorytoken
COutOfProcessMemoryToken       — out-of-process токен
```

**Shared-memory токены** — тот же out-of-process паттерн, что в OFX-бандлах
и `ImageProcessingPlugins/MxVfxShrink.dll`: данные рендера передаются между
процессами через `_sfmemorytoken`, не через COM-маршаллинг.

### Интерфейсы хоста (`IMP*`)

Общая связка, которую получают плагины от ядра VEGAS:

- `IMPControlSurfaceDriver` — базовый драйвер поверхности
- `IMPCtrlChannelSink` — приём изменения каналов (фейдеры/кнопки)
- `IMPNotificationSink` — уведомления
- `IMPTimeSink` — приём времени транспортного потока
- `IMPEventFilter` — фильтр событий
- `IMPInitialise` — инициализация

---

## 3. Плагины

### 3.1 faderport.dll — PreSonus FaderPort

- Классы: `CFaderportPlugin`, `CFaderportMap`, `CFaderportPage`
- Импорт: `WINMM.dll` (MIDI), `KERNEL32/USER32`
- USB-MIDI поверхность: 1 фейдер, транспорт, панорама, банки

### 3.2 tranzport.dll — Frontier Design TranZPort

- Классы: `CTranzportPlugin`, `CTranzportMap`, `CTranzportPage`,
  базовые `COPTControlPlugin` / `COPTPlugin` / `COPTPluginPersit`
- Импорт: `WINMM.dll` (MIDI)
- Беспроводная (2.4 GHz) транспорт-поверхность: scrub, банки, лист-навигация

### 3.3 spmackiectrlopt.dll — Mackie Control Universal

- Классы: `COPTMackieCtrlPlugin`, `COPTMackieCtrlPage`, `CMackieControlMap`,
  `COMPSPlugClassFactory` (ATL), `CIMPCtrlChannelSink`
- Импорт: `WINMM.dll` (MIDI)
- Протокол Mackie Control (8 фейдеров, V-Pot, LCD-дисплеи, транспорт)

### 3.4 spgenctrlopt.dll — Generic Control (универсальный MIDI)

- Классы: `COPTGenCtrlPlugin`, `COPTGenCtrlPage`, `CGenControlMap`,
  `CXMLFunctionMap`, `CControlMap`, `CBaseListPage`, `CSpecifyPropertyPages`
- Импорт: `sharedk.dll` (Sonic Foundry shared kernel), `GDI32`, `ole32/OLEAUT32`
- **Без прямого MIDI-импорта** — data-driven: функции маппятся на MIDI-сообщения
  через XML (функция ↔ CC/note).
- `CXMLFunctionMap` — ключ: XML определяет привязку CC к командам VEGAS.

> Info: Файл маппинга лежит вне DLL (XML в каталоге конфигурации пользователя),
> отсюда отсутствие импорта WINMM — MIDI-ввод идёт через `sharedk.dll`.

### 3.5 networkXML.dll — Network XML (TCP remote)

- Классы: `CNetworkXMLPlugin`, `CNetworkXMLPage`, `SoNetworkServerStatusDelegate`,
  `SoNetworkMessageReceivedDelegate`, `SoNetworkHostMessage` (+ `_Seek`, `_AddMarker`)
- Функции: `createNetworkHost(int, std::wstring)`, `destroyNetworkHost`
- Импорт: `WS2_32.dll` (TCP), `GetAdaptersAddresses` (выбор сетевого адаптера)
- Удалённое управление VEGAS по TCP: seek, маркеры, сообщения ядра
- `SoNetworkHost` — низкоуровневый хост-объект сетевого слоя «So» (Sonic)

### 3.6 spconsoleopt.dll — VEGAS Mixer Console (сама консоль)

Крупнейший плагин (2.34 MB). **Это полноценное окно микшерной консоли, вынесенное
из main exe.**

- Классы: `CConsoleControlPlugin`, `CConsoleControlMap`, `CConsoleMixerPage`,
  `CConsolePage`, `CConsoleCfgPage`, `CConsoleMenuManager`, `CChannelList`,
  `CMixerView`, `CMixConsoleUIForShowMeHow`
- Экспортирует **весь приватный SDK VEGAS** (статически слит, 76+ символов):
  - Дибы/изображения: `SfDib*`, `SfLoadDibFromDisk/Memory`, `SfLoadDiskJpegDib`
  - Скины UI: `SfSkin_*`, `SfDrawThemeBackground/Text`, `SfGetTheme*`, `XPTHEME`
  - Фейдеры: `FaderInst_*`, `CSfWSkynFaderCtl`, `CSfWSkynTrackbarCtl` (Skyn UI)
  - XML: `CSfXML_Document`, `CSfXMLParser`, `CSfXMLNode_*` (SAX)
  - Scope: `SfScope_*`, `ScopeInst_*` (осциллограф-channels)
  - Меню: `CSfMenuManager`, `MenuMan_*`, `ConsoleMixerPage_GetKeyNamesForCommand`
  - События: `SfWnd_ForwardMsg`, `SfWnd_DisregardFutureMsgs`, `CMonitored_CoInitializeEx`
- Импорт: `WINMM.dll` (MIDI) — консоль участвует в MIDI-подключении
- Export `PMBlend_DUALALPHACH` — интерфейс к блендингу наложений (парт. SfDib)

> Вывод: UI микшера полностью отделён от ядра. Он в отдельной DLL, общается с
> ядром через `IMP*`, а не имеет прямой связи с рендером.

---

## 4. Связи с ядром VEGAS

```
vegas220.exe (ядро)
  │  LoadLibrary("External Control Drivers\*.dll")
  │  GetProcAddress("SfOPTPlug_CreateOPTObject" ...)
  ▼
  OMPS-плагин ──(IMPControlSurfaceDriver/IMPCtrlChannelSink/...)──► ядро
  │
  ├── WINMM (MIDI): FaderPort, TranZPort, Mackie, Console
  ├── WS2_32 (TCP): Network XML remote
  └── sharedk.dll: Generic Control (XML-маппинг через shared kernel)
```

Общий паттерн: **плагин отдаёт данные обратно ядру через `IMPCtrlChannelSink`**,
а ядро присылает состояние (метры, положение) через `IMPNotificationSink`/`IMPTimeSink`.

---

## 5. Директория сборки

Все PDB-пути указывают на GitLab-runner сборку «sonic3»:

```
C:\gitlab-runner\builds\NightlyBuild\BUILD sonic3\build\omps\ompplugs\
  ├── Faderport\Release\faderport.pdb
  ├── Tranzport\Release\tranzport.pdb
  ├── MackieControl\Release\spmackiectrlopt.pdb
  ├── GenericControlSurface\Release\spgenctrlopt.pdb
  ├── Network\Release\networkXML.pdb
  └── MixConsole\Release\spconsoleopt.pdb
```

---

## 6. Выводы для OpenVegas

1. **Контракт `SfOPTPlug_*` + `IMP*` независим от OFX** — отдельная плаг-система
   внешнего управления, не связана с видеоэффектами.
2. **Console UI изолирован** — OpenVegas может реализовать микшер как отдельный
   модуль с тем же контрактом (fader/meter/solo/mute через `IMPCtrlChannelSink`).
3. **`spgenctrlopt` — самый интересный для ре-имплементации**: data-driven,
   XML-маппинг MIDI CC на функции ядра. Логика «CC → команда» открыта.
4. **`networkXML` задаёт протокол TCP-remote** (`SoNetworkHost`, `_Seek`, `_AddMarker`) —
   потенциальный источник для remote-функций OpenVegas.
5. Shared-memory токены — единый паттерн передачи данных во всех плаг-системах VEGAS
   (OFX, ImageProcessing, OMPS). Стоит унифицировать в OpenVegas.