# VEGAS Pro 22 — Audio Hardware Drivers

**Дата:** 2026-08-27
**Источник:** `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/Audio Hardware Drivers/`
**Метод:** dumpbin exports/imports, llvm-objdump disasm, строки ANSI/UTF-16.

---

## 1. Что это

Аудио-драйверы VEGAS (вход/выход звука), каждый DLL — один драйверный «класс»
(playback/record device class). Все 3: **v22.0 Build 250**, `MAGIX Computer Products Intl. Co.`,
подписаны DigiCert/MAGIX, timestamp 2025-05-05. Сборка: `...\kaudioa\audioio\...` (ветка «sonic3»).

| DLL | Размер | Драйвер | Orig |
|-----|--------|---------|------|
| `sfasio.dll` | 202 KB | **ASIO** (Steinberg) | ASIODRV.DLL |
| `sfdsound.dll` | 112 KB | **DirectSound / MME** | SFDSOUND.DLL |
| `extvid_drv.dll` | 76 KB | **Preview Device audio** (DV/HDV) | EXTVID_DRV.DLL |

---

## 2. Единый ABI (`SfAudioDriver_*`)

Все 3 экспортируют одинаковые C-символы + shared-memory токены:

```
SfAudioDriver_CreateDriverClass(ctx, outClass)   — создать объект класса драйвера
SfAudioDriver_TranslateDriver(int)               — прокидывает вызов локали (sink)

CMappingOfSfMemoryToken / COutOfProcessMemoryToken — 12 символов (тот же паттерн)
```

**Семантика `CreateDriverClass`** (из дизассемблера sfdsound, RVA 0x2980):
1. `*out = NULL`, выделяет объект класса размером **0x460** (sfdsound),
   vtable-init → объект ~`CDirectSoundClass`.
2. Инициализация полей, локали, флагов.
3. Возвращает новичок `IAudioClass`.

**Семантика `TranslateDriver(int)`** (RVA 0x2950): уведомление ядра о смене LCID
(вызов `SfLang_*` на sharedk) — драйверов общий номер LCID.

**Driver → ядро**: только `sharedk.dll` + Win32.

### Иерархии классов (RTTI из строк)

```
sfasio.dll:
  CASIOAudioClass / CASIOAudioDevice / CASIOAudioIn / CASIOAudioOut
  CEnumASIODev / CEnumASIOIn / CEnumASIOOut
  CASIODriverPPage / CASIOPPage (property pages)

sfdsound.dll:
  CDirectSoundClass / CDirectSoundDevice / CDirectSoundInput / CDirectSoundOutput

extvid_drv.dll:
  CExtVidAudioClass / CExtVidAudioDevice / CExtVidAudioIn / CExtVidAudioOut
  CExtVidEnumAudioDevs / CExtVidEnumAudioIn / CExtVidEnumAudioOut
```

Интерфейсы (COM-стиль, по RTTI):
`IAudioClass`, `IAudioDevice`, `IAudioDevice2`, `IAudioIn`, `IAudioOut`,
`IEnumAudioDevs`, `IEnumAudioIn`, `IEnumAudioOut`, `IAudioClass`/`IAudioDevice`.

---

## 3. sfasio.dll — ASIO (Steinberg)

Реализация драйверного класса **ASIO 2.0**. Ключевые строки и факты:

- **Enumerates ASIO devices**: `Initialize ASIO drivers...`, registry `software\asio`
  (стандартный ASIO key), через `VERSION.dll` (доп. инспекция версии драйвера).
- **Собственная конфигурация**: `Software\VEGAS Creative Software\SfDrivers\Audio\` —
  там хранятся настройки драйвера (`BufferSizeX`, выбор драйвера).
- **The «VEGAS ASIO Driver»** — сам называет себя «Sony ASIO / VEGAS ASIO Driver»,
  `About VEGAS ASIO Driver`.
- **UI диалог**: PropertyPage `CASIODriverPPage`:
  - «ASIO buffer setting (samples)»
  - «ASIO Clock Source», «Clock state»
  - «Enable ASIO 2.0 Direct Monitoring» / «Direct Monitoring aktivieren»
  - «Use Buffered Output» / «Set Output Thread Affinity» (настройка affinity выходного потока)
  - «Use ASIO Position Protocol» (сетевой position intf)
  - «Enable Output buffering»
- **Поддержка сэмплрейтов**: `ASIO 2.0`, formats 16/24/32-bit, Float.
  `%0.3f kHz` — отображение частоты (48,000 Hz), multi-language.
- **Каналы**: стерео лев/прав + surround-назначение.
- **Error handling**: «No ASIO Driver», «The ASIO device %s caused an internal
  exception (0x%x) and has been disabled.», `ASIOExceptionFilter`.
- **Из impl**: `Software\VEGAS Creative Software\SfDrivers\Audio\` — вся конфигурация.

### Как работает загрузка ASIO

```
[sfasio.dll]
   CEnumASIODev → читает registry "software\asio" (standard ASIO)
   → открывает внешние драйверы через ASIO COM-style interface (Clsid из reg)
   → CASIOAudioDevice = обёртка над внешним драйвером
   → буферы из CMappingOfSfMemoryToken (shared memory)
```

Примечание: registry указывает на `software\asio` (standard ASIO location), а не
`Software\...Magix`. Драйверы ASIO регистрируются как `software\asio\<drvname>`.

### Импорты sfasio

`sharedk.dll`, `VERSION.dll`, `WINMM.dll`, `ADVAPI32.dll`, `KERNEL32`, `USER32`, `ole32`.
(`WINMM`/`ADVAPI32` — вспомогательные для enums/version).

---

## 4. sfdsound.dll — DirectSound / MME

- Классы: `CDirectSoundClass/Device/Input/Output`.
- Делает: классический **DirectSound** playback + **MME**. Поддерживает **surround
  mapping**: строки «DirectSound Surround», «Sound Mapper», «Set DirectSound speaker
  geometry», «Use MME Surround Mode», `All speaker flag`.
- **Канальность**: Left/Right, Left of Center/Right of Center, Left/Right Rear,
  Center/LFE. **8-канальный surround** (каноничные имена каналов 5.1/7.1 в 10 языках).
- PropertyPage: настройка speaker geometry, выбор устройства (регистр-имени).
- Импорты: `sharedk.dll`, **`DSOUND.dll`**, `WINMM.dll`, `ole32`.

### Особенности DirectSound класса

- Property page перечисляет устройства выделения: `Enregistreur de sons`,
  `Sound Mapper`, `DS Surround Mapper`.
- «VEGAS DirectSound Driver» / «Direct Sound Surround Mapper» — 
  драйвер не выходит за DirectSound 7/8 пространство (без ASIO-латентности).

---

## 5. extvid_drv.dll — Preview Device Audio

**Отличие**: это НЕ playback-класс в обычном смысле. Его имя в строке —
«**Sony Preview Device Audio Driver**» / «Preview Device» / «VEGAS External Video
Audio Driver». Работает с **внешним видеоустройством предпросмотра (External Video
Device)**, через ядро `sharedk.dll`:

- Классы `CExtVid*` — не enum-класс, только `IAudioClass/IAudioDevice(2)/In/Out`.
- Ошибки показывают суть:
  - «The **Preview Device** does not exist.»
  - «The Preview Device could not be opened.»
  - «The Preview Device %s is in use by another application.»
  - «The Preview Device does not support audio.»
  - «The Preview Device %s does not support the current sample rate / bit depth
    / output format.»
  - «The Preview Device audio driver %s caused an internal exception (0x%x) and
    has been disabled.»
- Роль: мост, через который аудио из валидирующего **External Video Device**
  (DV/HDV) попадает в ядро как аудио-устройство.
- **Импорты**: только `sharedk.dll` + `KERNEL32`/`ole32` — сам на железе не сидит,
  слушает ядро через арену связи (см. `CExtVidAudioDevice::ExceptionFilter`).
- Размер 76 KB — самый тонкий.

---

## 6. Общие черты

1. **Все классы драйверов — sharedk-мосты**: работают через `sharedk.dll` (Sf* SDK),
   shared-memory token'ы для передачи звуковых буферов между процессами.
2. **Property page** у каждого (`*PPage`) — окно конфигурации в Preferences > Audio.
3. **Расположение конфига**: `Software\VEGAS Creative Software\SfDrivers\Audio\`
   (для ASIO в частности; возможно общий для всех).
4. **Единый ABI** — замена/добавление драйвера = новая DLL с `SfAudioDriver_CreateDriverClass`.
5. **Строки локализованы** - 5-6 языков (de/en/es/fr/pl).
6. **Copyright (c) 2024 MAGIX** — сборка свежая (Pro 22).

---

## 7. Связь с ядром VEGAS

```
vegas220.exe
   │  LoadLibrary("Audio Hardware Drivers\*.dll")  (enum папки)
   │  GetProcAddress("SfAudioDriver_CreateDriverClass")
   │  SfAudioDriver_TranslateDriver(lcid)  → SfLang_UpdateLCID
   ▼
   IAudioClass объект
   ├── IEnumAudioDevs → IAudioDevice(2) → IAudioIn/IAudioOut
   ├── буферы: CMappingOfSfMemoryToken (shared memory)
   └── конфиг: Software\VEGAS Creative Software\SfDrivers\Audio\
```

Ядро вызывает `SfAudioDriver_CreateDriverClass` один раз на драйвер DLL,
держит `IAudioClass` + page, а выбор устройства идёт через интерфейсы
`IAudioDevice`.

---

## 8. Выводы для OpenVegas

1. **Воспроизведение аудио в VEGAS** строится на:
   - **DirectSound (sfdsound)** — legacy, обычный выхлоп Windows
   - **ASIO (sfasio)** — интерфейс к драйверам Steinberg
   - **Preview Device (extvid)** — аудио внешнего видеоустройства
2. **Контракт «IAudioClass + IAudioDevice2 + IAudioIn/Out + shared-memory**
   — однозначная модель для OpenVegas аудио-подсистемы: driver-class регистрация
   через ABI `SfAudioDriver_*`.
3. **Кроссплатформенно**: на Windows достаточно реализовать DirectSound и option
   ASIO; extvid особой ценности нет (хватит stubs).
4. **Config key** `SfDrivers\Audio` — известное место для настроек.

---

## 9. Замечания

- `SfAudioDriver_TranslateDriver(int)` не принимает строку: это уведомление о новом
  LCID (вызовы `SfLang_*` внутри). Имя не отображает содержимое параметра.
- В sfasio есть `ASIOPage` = Property page для выбора ASIO-драйвера — тот самый
  UI в Preferences.
- `Software\VEGAS Creative Software\SfDrivers\Audio\` — VEGAS-специфичный ключ;
  сам ASIO-список читается из стандартного `software\asio`.