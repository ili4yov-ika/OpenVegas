# Ghidra MCP bridge

Python-мост между opencode (MCP stdio) и плагином GhidraMCP, который запускает
внутри Ghidra HTTP-сервер на `127.0.0.1:8080`.

Источник: [LaurieWired/GhidraMCP](https://github.com/LaurieWired/GhidraMCP) (Apache-2.0),
взят из форка `james-pre/GhidraMCP` с обновлённым MCP SDK 2.x.

## Установка плагина в Ghidra

1. Взять `GhidraMCP-release-1-4.zip`, внутри — `GhidraMCP-1-4.zip`.
2. В `extension.properties` выставить `version=12.1.2` и `ghidraVersion=12.1.2`
   (иначе Ghidra 12.1.2 откажется ставить расширение, собранное под 11.3.2).
3. Распаковать `GhidraMCP/` в `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\Extensions\GhidraMCP`
   (эквивалент File → Install Extensions).
4. Включить плагин в CodeBrowser: в `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\tools\_code_browser.tcd`
   добавить внутрь `<TOOL ...>` строку
   `<INCLUDE CLASS="com.lauriewired.GhidraMCPPlugin" />`.

## Использование

```bash
python tools/ghidra-mcp/bridge_mcp_ghidra.py --ghidra-server http://127.0.0.1:8080/
```

opencode уже настроен (`~/.config/opencode/opencode.jsonc`, секция `mcp.ghidra`).

Порядок запуска сессии реверса:

1. Один раз: в `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\preferences` прописать

   ```properties
   LastOpenedProject=C:/Users/Admin/AppData/Local/Temp/opencode/ghidramcp/proj/vegas_ofx
   ```

   (путь к проекту без `.gpr`, прямые слэши) — тогда проект и программа
   открываются сами при старте.
2. `C:\Program Files\Ghidra\ghidraRun.bat` — проект восстанавливается, CodeBrowser
   открывается с последней программой, HTTP-сервер стартует на порту 8080.
3. Проверка (программа грузится ~10–20 c): `http://127.0.0.1:8080/methods?offset=0&limit=5`
   — до загрузки отвечает «No program loaded». Декомпиляция — только POST:
   `Invoke-WebRequest -Method Post http://127.0.0.1:8080/decompile -Body "FUN_..."`.

Не пользоваться `support\GhidraGo\ghidraGo.bat`: его слушатель имеет статус UNSTABLE
и во Frontend не загружается, клиент вечно ждёт или плодит вторые экземпляры Ghidra
(лок проекта). Подробности и грабли — в
[`MARKDOWN/RE_OFX_HWND_INTERACT_REPORT.md`](../../MARKDOWN/RE_OFX_HWND_INTERACT_REPORT.md).

Проект Ghidra с уже проанализированными бинарниками VEGAS:
`%TEMP%\opencode\ghidramcp\proj\vegas_ofx.gpr` (`Vfx1.ofx`, `TitlesAndText.ofx`).
Каталог `%TEMP%` может быть очищен — переимпорт описан в отчёте выше.
