# docs_veg

Разбор строения файлов **`.veg`** (VEGAS Pro 22) из каталога `sample_ui`.

## С чего начать

1. [00_format_overview.md](00_format_overview.md) — общая модель формата и выводы для OpenVegas  
2. [01_header_and_props.md](01_header_and_props.md) — заголовок, fps/sr, notes  
3. [02_sfk_peak_files.md](02_sfk_peak_files.md) — `.sfk` (SFPK) peaks + кэши; обзор Vegas Pro 22  
4. [files/README.md](files/README.md) — карточка по каждому сэмплу  

## Сырые данные / скрипты

| Файл | Назначение |
|------|------------|
| `_analyze_veg.py` | строки, GUID, краткий JSON |
| `_analyze_deep.py` | глубокий разбор + common prefix |
| `_analyze_props.py` | props/fps/sr, counts, diffs |
| `_analyze_sfk.py` | разбор `.sfk` (SFPK peak sidecars) |
| `_deep_analysis.json` | машинный дамп |
| `_analysis_raw.json` | машинный дамп |
| `*_strings.md` | извлечённые строки по файлам |

Перезапуск:

```bash
python docs_veg/_analyze_veg.py
python docs_veg/_analyze_deep.py
python docs_veg/_analyze_props.py
python docs_veg/_analyze_sfk.py
```
