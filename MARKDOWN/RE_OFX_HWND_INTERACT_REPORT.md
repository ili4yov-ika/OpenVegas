# Реверс нативных форм OFX-плагинов: Ghidra + MCP — настройка и результаты

Отчёт о двух вещах: (1) как настроен инструментальный стек **Ghidra 12.1.2 + GhidraMCP +
opencode MCP** для реверса OFX-бандлов VEGAS Pro 22 и (2) какие факты о
`OfxHWndInteractSuite` добыты этим стеком к настоящему моменту.

Исходные материалы: `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/` (см.
[`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md`](../SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md)),
план работ: [`PLAN_OFX_HWND_INTERACT_RE.md`](PLAN_OFX_HWND_INTERACT_RE.md),
родительский разбор: [`PLAN_OFX_VIDEO_PLUGINS.md`](PLAN_OFX_VIDEO_PLUGINS.md).

**Статус:** стек настроен и проверен end-to-end (2026-08-21); реверс `Vfx1.ofx`
дал раскладку suite на уровне «слоты, вызываемые плагином», диспетчер действий и
vtable interact-объекта. Полная сигнатурная раскладка всех слотов suite — не завершена.

---

## 1. Что установлено и где

| Компонент | Версия | Расположение |
|---|---|---|
| Ghidra | 12.1.2 PUBLIC | `C:\Program Files\Ghidra` (Java: системный Temurin JDK 25) |
| GhidraMCP | 1.4 (LaurieWired, Apache-2.0) | `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\Extensions\GhidraMCP` |
| Мост opencode ↔ Ghidra | локальный скрипт | `tools/ghidra-mcp/bridge_mcp_ghidra.py` |
| Конфиг opencode | — | `~/.config/opencode/opencode.jsonc`, секция `mcp.ghidra` |
| Проект Ghidra | — | `%TEMP%\opencode\ghidramcp\proj\vegas_ofx.gpr` |

В проекте проанализированы два бинарника из комплекта VEGAS Pro 22 (Build 22.0.250):

- `OFX Video Plug-Ins\Vfx1.ofx.bundle\Contents\Win64\Vfx1.ofx` (~10 MB, 78 эффектов;
  содержит `colorcurves` — единственный эффект с HWnd-interact);
- `OFX Video Plug-Ins\TitlesAndText.ofx.bundle\Contents\Win64\TitlesAndText.ofx`
  (~0.5 MB; overlay-interact у Titles & Text).

## 2. Патч расширения под Ghidra 12.1.2

GhidraMCP 1.4 собран под Ghidra 11.3.2. Ghidra отвергает расширение с чужой версией,
но Java-код совместим — достаточно поправить манифест (приём из PR #164 форка
james-pre/GhidraMCP):

1. В `GhidraMCP-release-1-4.zip` → `GhidraMCP-1-4.zip` → `extension.properties`:
   `version=12.1.2`, `ghidraVersion=12.1.2`.
2. Распаковать в `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\Extensions\GhidraMCP`.
3. `Module.manifest` должен быть в формате лицензионных комментариев (`:: ###` …),
   иначе Ghidra сыплет warnings «Module manifest file error».
4. Подключение плагина к CodeBrowser — в `%APPDATA%\...\tools\_code_browser.tcd`
   внутри `<TOOL>`: `<INCLUDE CLASS="com.lauriewired.GhidraMCPPlugin" />`;
   регистрация расширения выглядит как `<EXTENSIONS><EXTENSION NAME="GhidraMCP" /></EXTENSIONS>`.

Плагин стартует HTTP-сервер на `127.0.0.1:8080` в момент загрузки CodeBrowser
(в логе: `GhidraMCP HTTP server started on port 8080`).

## 3. Как запускать сессию реверса (рабочий рецепт)

Проблема: `ghidraRun.bat` не принимает аргументов командной строки, а кликать по
диалогам каждый раз неудобно. Решение — автовосстановление через preferences:

1. **Один раз:** в `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\preferences` добавить

   ```properties
   LastOpenedProject=C:/Users/Admin/AppData/Local/Temp/opencode/ghidramcp/proj/vegas_ofx
   ```

   Формат значения: путь к каталогу проекта **без расширения** `.gpr`, прямые слэши
   (ключ читает `DefaultProjectManager.getLastOpenedProject()`, восстановление
   включено по умолчанию — `shouldRestorePreviousProject = true`).

2. **Каждый раз:** запустить `C:\Program Files\Ghidra\ghidraRun.bat`. Дальше само:
   проект открывается при старте → CodeBrowser восстанавливается вместе с последней
   открытой программой (`Vfx1.ofx`) → MCP-сервер поднимается на :8080.

3. **Проверка готовности** (программа грузится ~10–20 c после окна проекта):

   ```powershell
   Invoke-WebRequest "http://127.0.0.1:8080/methods?offset=0&limit=5"
   # до загрузки программы: "No program loaded"; после — список FUN_...

   # декомпиляция (именно POST, имя функции — в теле):
   Invoke-WebRequest -Method Post "http://127.0.0.1:8080/decompile" -Body "FUN_180487cc0"
   ```

4. Мост для opencode уже прописан в конфиге; вручную:

   ```bash
   python tools/ghidra-mcp/bridge_mcp_ghidra.py --ghidra-server http://127.0.0.1:8080/
   ```

### Грабли, собранные по пути

- **Пути с `+` запрещены.** Проект в `D:\Devs\C++\...` не создаётся:
  `IllegalArgumentException: Path element contains invalid character: '+'`
  (`NamingUtilities`). Поэтому проект живёт в `%TEMP%\opencode\ghidramcp`.
- **`ghidraGo.bat` бесполезен без открытого проекта.** Его слушатель
  (`GhidraGoPlugin`) имеет статус `UNSTABLE` и потому **не загружается** во Frontend
  автоматически (`installDefaultApplicationLevelPlugins` берёт только `RELEASE`);
  отправленный URL никто не подхватывает, клиент вечно ждёт («Waiting for GhidraGo to
  listen…») или запускает второй экземпляр Ghidra, который упирается в lock проекта.
  Не использовать; автовосстановление через preferences решает ту же задачу.
- **IPC GhidraGo — файловые локи**, не сокет: `%TEMP%\ghidra\ghidraGo\{listenerLock,
  listenerReadyLock,senderLock,urls\*}`. Отсутствие `listenerLock` = слушатель не активен.
- **Лок проекта.** Второй экземпляр Ghidra над тем же проектом падает с
  «Project is Locked». Перед правкой preferences закрывать все `javaw`-процессы,
  иначе файл перезапишется при выходе.
- **Диагностика** — `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\application.log`
  (единый лог всех экземпляров): там видны и старт сервера MCP, и «Project is Locked»,
  и причины тихих смертей. Штатное закрытие окна = `Stopping GhidraMCP HTTP server…`
  в логе; отсутствие hs_err-файлов при исчезновении процесса означает, что его
  закрыли извне, а не уронили.

## 4. Результаты реверса `OfxHWndInteractSuite` (Vfx1.ofx)

Методика: headless-анализ (`analyzeHeadless`), затем поиск строк-литералов SDK и
обратная трассировка ссылок; декомпиляция через MCP. Скрипты извлечения и дампы —
в `build/re-ghidra/extract/` (`ExtractInteract.java`, `ExtractSuiteCalls.java`,
`_strings_report.txt`, `_suite_refs_report.txt`, `FUN_*.c`).

### 4.1 Карта глобалов support-библиотеки плагина

| Адрес | Содержимое |
|---|---|
| `DAT_1809a5a08` | `OfxHost*` (setHost), отсюда `fetchSuite` по смещению +8 |
| `DAT_1809a5a00` | кэш `OfxPropertySuite` |
| `DAT_1809a59b8` | кэш `OfxInteractSuite` v2 (слот +0x10 используется при Describe) |
| `DAT_1809a59e8` | кэш **`OfxHWndInteractSuite`** — целевой suite |
| `DAT_1809a5970` | кэш `OfxHWndOverlayInteractSuite` — в `Vfx1.ofx` ни разу не вызывается |

### 4.2 Диспетчер действий (host → plugin)

- `FUN_180487cc0` — верхнеуровневый `hwndInteractMainEntry`; сигнатура стандартная
  `mainEntry(pluginHandle, argc, argv)`; логирует `"START hwndInteractMainEntry (%s)"`.
- `FUN_180487a10` — внутренний диспетчер: разбирает `kOfxPropActionType` и передаёт
  методы C++-объекта interact.

Обрабатываемые действия и их аргументы (property sets):

| Действие | inArgs | outArgs |
|---|---|---|
| `OfxHWndInteractActionCreateWindow` | `OfxHWndInteractPropParent` (pointer, HWND) | плагин пишет `OfxHWndInteractPropMinSize` (int×2) и `OfxHWndInteractPropPrefferedSize` (int×2) |
| `OfxHWndInteractActionMoveWindow` | `OfxHWndInteractPropLocation` (int×4) | — |
| `OfxHWndInteractActionShowWindow` | — | — |
| `OfxHWndInteractActionDisposeWindow` | — | — |

Плюс стандартные `OfxActionDescribe / CreateInstance / DestroyInstance`.

Property set самого interact-handle: хост пишет `OfxPropEffectInstance`, плагин —
`OfxPropInstanceData` (указатель на его C++ объект).

### 4.3 Vtable C++-объекта interact (сторона плагина)

Восстановлено по конструктору `FUN_180487410` и вызовам из диспетчера:

| Слот | Метод | Примечание |
|---|---|---|
| +0x00 | destroy | вызывается на `OfxActionDestroyInstance` (арг 1) |
| +0x08 | createWindow(parent) | реализация `FUN_1802bd7e0`: Win32-диалог из ресурса шаблона (`GetModuleHandleExA`), размеры из `GetWindowRect` → пишет MinSize/PrefferedSize в outArgs |
| +0x10 | moveWindow(int[4]) | |
| +0x18 | disposeWindow() | |
| +0x20 | showWindow() | |

RTTI подтверждает классы: `colorcurvesInteract`,
`colorcurvesInteractDescriptor`, `OFX::HWNDInteractMainEntry<colorcurvesInteractDescriptor>`,
`OFX::DefaultEffectHWNDInteractDescriptor<colorcurvesInteractDescriptor, colorcurvesInteract>`,
`colorcurvesPlugin`, `colorcurvesPluginFactory`.

### 4.4 Suite (plugin → host): что известно твёрдо

Плагин берёт suite из кэша `DAT_1809a59e8` и вызывает ровно два слота:

| Слот | Сигнатура (восстановленная) | Где вызывается |
|---|---|---|
| **+0x00** | `getPropertySet(interactHandle, OfxPropertySetHandle* out)` | получение property set своего handle перед чтением `Parent`/`Location` |
| **+0x08** | `f(handle) → OfxStatus`, один аргумент | legacy-диалоговый код (`FUN_1802bb570`, `FUN_1802bc3a0`, `FUN_1802c4c20`) после `InvalidateRect`; семантика точно не подтверждена — вероятно redraw/уведомление хоста |

Остальные слоты suite плагином не трогаются — значит, для хостинга Color Curves
достаточно корректно реализовать эти два (остальные можно оставить заглушками,
возвращающими `kOfxStatFailed`).

Публикация interact: `colorcurvesPluginFactory::describe` выставляет свойство
`OfxImageEffectPluginPropHWndInteractV1` (pointer) = указатель на mainEntry — читать
так же, как стандартный `kOfxImageEffectPluginPropOverlayInteractV1`.

### 4.5 Что осталось

1. Подтвердить семантику слота +0x08 (вероятнее всего — «перерисуй/обновись»).
2. Прогнать этап 1 плана (`PLAN_OFX_HWND_INTERACT_RE.md`) с этими знаниями:
   `CreateWindow` с `Parent` → прочитать MinSize/PrefferedSize → `ShowWindow`.
3. Overlay-ветка (`TitlesAndText.ofx`, `OfxHWndOverlayInteractSuite`) — отдельно,
   строки действий уже видны (`Draw`, `Mouse*`, `Key*`, `GainFocus/LoseFocus`,
   свойства `HDC`, `Rect`, `MouseLoc`, `KeyCode`).

## 4а. Независимая перепроверка и уточнения (2026-08-21, вторая сессия)

Разделы 4.1–4.4 выше перепроверены заново на том же `Vfx1.ofx` через MCP —
не чтением отчёта, а декомпиляцией. Все ключевые утверждения подтвердились;
ниже — что доказано жёстче и что уточнено.

### Доказано, а не выведено

**`DAT_1809a59e8` — это именно HWnd-suite.** Видно в месте записи:

```c
lVar1 = (*(code *)DAT_1809a5a08[1])(*DAT_1809a5a08, "OfxHWndInteractSuite", 1);
DAT_1809a59e8 = lVar1;
```

Там же подтверждается `DAT_1809a5a08` = `OfxHost*`, `fetchSuite` по +8, и
`DAT_1809a5a00` = `OfxPropertySuite`.

**Оба HWnd-suite запрашиваются как опциональные** — соседний лог-вызов:
`"Could not fetch the optional suite '%s' version %d."`. Это объясняет, почему хост,
возвращающий `NULL`, спокойно проходит `Load`, и означает, что вся ветка нативных
форм — необязательная надстройка, а не требование.

**Слот +0x00 = `getPropertySet`.** В `FUN_1804873b0` результат немедленно
оборачивается в `OFX::PropertySet` и у него запрашивается `"OfxPropEffectInstance"`:

```c
uVar1 = (*(code *)*DAT_1809a59e8)(param_1, local_res10);
local_18 = OFX::PropertySet::vftable;
local_10 = local_res10[0];
uVar2 = FUN_180485b80(&local_18, "OfxPropEffectInstance", 0, 1);
```

### Уточнение: семантика слота +0x08 закрыта

В разделе 4.4 она помечена как неподтверждённая. Подтверждается: `FUN_1802bc3a0`
делает `InvalidateRect(hwnd, &rect, 0)` (дважды), после чего вызывающий
`FUN_1802bb570` дёргает слот +0x08 с одним аргументом — хэндлом interact'а. То есть
это **`redraw`**: «содержимое моего окна изменилось». Совпадает по форме с
`OfxInteractSuiteV1::interactRedraw`, но порядок полей в HWnd-suite другой —
`getPropertySet` идёт первым:

```c
typedef struct OfxHWndInteractSuiteV1 {
  OfxStatus (*interactGetPropertySet)(OfxHWndInteractHandle, OfxPropertySetHandle *); // +0x00
  OfxStatus (*interactRedraw)(OfxHWndInteractHandle);                                 // +0x08
} OfxHWndInteractSuiteV1;
```

### Полный разбор диспетчера `FUN_180487a10`

Декомпилирован целиком. Каждое действие сначала берёт property set через слот +0x00,
достаёт из него `"OfxPropInstanceData"` (C++-объект плагина) и вызывает метод его
vtable. Все методы возвращают `char`; хост получает `0` (`kOfxStatOK`) при `true` и
`0xe` (**`kOfxStatReplyDefault`**, не «ошибка») при `false`. Нераспознанное действие —
тоже `0xe`.

| Действие | vtable | Аргументы, читаемые из inArgs |
|---|---|---|
| `OfxActionDestroyInstance` | +0x00 | `(obj, 1)` |
| `OfxHWndInteractActionCreateWindow` | +0x08 | `OfxHWndInteractPropParent` (pointer) + outArgs |
| `OfxHWndInteractActionMoveWindow` | +0x10 | `OfxHWndInteractPropLocation` — **4 int'а**, индексы 0…3 |
| `OfxHWndInteractActionDisposeWindow` | +0x18 | — |
| `OfxHWndInteractActionShowWindow` | +0x20 | — |

### Контракт `CreateWindow` со стороны хоста (`FUN_1802bd7e0`)

Что плагин делает и чего ждёт:

1. `GetModuleHandleExA(6, …)` — берёт свой модуль и создаёт окно из **собственного
   ресурса-шаблона**; `SetWindowLongPtrW(hwnd, GWLP_USERDATA, obj)`.
2. Инициализирует диалог из **своих же параметров** — в коде виден
   `__RTDynamicCast(..., OFX::ChoiceParam::RTTI_Type_Descriptor)` для параметра
   `"AutoMode"`. То есть окно не автономно: параметры к моменту `CreateWindow`
   должны быть уже объявлены и читаемы.
3. `GetWindowRect` → размеры, и **пишет их в outArgs нашим `propSetInt`**
   (`DAT_1809a5a00 + 0x18` — четвёртый слот `OfxPropertySuiteV1`):
   `OfxHWndInteractPropMinSize[0,1]` и `OfxHWndInteractPropPrefferedSize[0,1]`.
4. Если окно создать не удалось — те же свойства пишутся с запасными значениями
   **468 × 330** (`0x1d4` × `0x14a`). Полезный ориентир для размера панели.

Отсюда требование к хосту: outArgs для `CreateWindow` обязан быть **записываемым**
property set, а `propSetInt` — принимать эти имена.

### Реализовано в коде

`OfxHWndInteractSuiteV1` из двух восстановленных слотов реализован в
[`OfxHost.cpp`](../src/plugins/OfxHost.cpp) и отдаётся по `fetchSuite`, когда
включён `OPENVEGAS_OFX_INTERACT`. Пробная таблица из 32 заглушек сохранена под
`OPENVEGAS_OFX_INTERACT=probe` — тем же приёмом можно снять раскладку
overlay-suite с `TitlesAndText.ofx`.

Проверено: с включённым флагом `fetchSuite("OfxHWndInteractSuite", v1) -> ok`,
все тесты `[vegas-video]` зелёные; с выключенным (по умолчанию) поведение прежнее.

### Замечания к инструментам

- **`tools/search_references.py` методологически неверен**: он сравнивает 64-битные
  слова с **файловым смещением** строки, тогда как в PE хранятся виртуальные адреса —
  совпадений быть не может. Плюс `O(n·m)` по 10 МБ на Python. Результаты отчёта
  получены через Ghidra, так что выводы не пострадали, но скрипт вводит в заблуждение.
  Корректный поиск указателей с пересчётом VA↔offset — `tools/pe_find_ptr.py`.
- **Каталог `1809a5970/` в корне репозитория** — 12 файлов, побайтово идентичных
  тем, что уже лежат в `build/re-ghidra/extract/` (случайный вывод скрипта в каталог,
  названный по адресу глобала overlay-suite). Не в `.gitignore`, уйдёт в коммит.

---

## 5. Связанные файлы

- `tools/ghidra-mcp/bridge_mcp_ghidra.py`, `tools/ghidra-mcp/README.md` — мост и инструкция.
- `build/re-ghidra/extract/` — дампы декомпиляции и отчёты поиска.
- `%TEMP%\opencode\ghidramcp\proj\vegas_ofx.gpr` — готовый проект Ghidra
  (каталог `%TEMP%` может быть очищен; переимпорт: `analyzeHeadless.bat <каталог>
  vegas_ofx -import <файл>.ofx -processor x86:LE:64:default`).
