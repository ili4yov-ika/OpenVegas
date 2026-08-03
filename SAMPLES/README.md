# SAMPLES — OpenVegas

Эталонные материалы для разработки OpenVegas и разбора формата проектов **VEGAS Pro 22**.

Здесь собраны:

- каталог [`veg_project/`](veg_project/) — актуальные `.veg`, медиа и interchange-экспорты (EDL / XML / FCPXML / Premiere);
- документация и скрипты анализа `.veg` — [`veg_analyzators/`](veg_analyzators/);
- эталонные скриншоты Vegas Pro — [`screenshots/`](screenshots/) (если есть);
- снимок Program Files Vegas Pro 22 — [`VEGAS-PRO-22-PROGRAM-FILES/`](VEGAS-PRO-22-PROGRAM-FILES/) (справочник по runtime/API).

Цель — зафиксировать поведение Vegas (таймлайн, events, fades/crossfades, grouping, reverse/loop, FX), чтобы OpenVegas мог повторить те же паттерны.

---

## Быстрый старт

1. Актуальные проекты Vegas: [`veg_project/README.md`](veg_project/README.md).
2. Реверс формата `.veg`: [`veg_analyzators/README.md`](veg_analyzators/README.md).
3. Анализ установки Vegas: [`VEGAS-PRO-22-PROGRAM-FILES/README.md`](VEGAS-PRO-22-PROGRAM-FILES/README.md).

Открыть проект в OpenVegas:

```text
build\windows-mingw-x64\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny.veg
```

---

## Структура каталога

```
SAMPLES/
├── README.md                       # этот файл
├── screenshots/                    # эталонные скриншоты Vegas Pro (опционально)
├── veg_project/                    # актуальные .veg + медиа + Export-эталоны
├── veg_analyzators/                # реверс .veg (docs + скрипты)
└── VEGAS-PRO-22-PROGRAM-FILES/     # снимок установки Vegas Pro 22 (+ README)
```

| Путь | Назначение |
|------|------------|
| `veg_project/` | Рабочие `.veg`, медиа (BBB, wav), EDL/XML/FCPXML/Premiere sidecar |
| `veg_analyzators/` | Документация и скрипты разбора бинарного `.veg` |
| `screenshots/` | Скриншоты UI Vegas для сверки |
| `VEGAS-PRO-22-PROGRAM-FILES/` | Справочный снимок Program Files (не публиковать runtime в git) |

Подробности по проектам: [`veg_project/README.md`](veg_project/README.md).
