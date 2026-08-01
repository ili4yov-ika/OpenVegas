# Заголовок и свойства проекта `.veg`

## Hex dump начала (пример: `example_project_with_video_and_audio.veg`)

```
000000  72 69 66 66 2E 91 CF 11 A5 D6 28 DB 04 C1 00 00  riff......(.....
000010  B0 37 00 00 00 00 00 00 EF 29 C4 46 4A 90 D2 11  .7.......).FJ...
000020  87 22 00 C0 4F 8E DB 8A 5A 2D 8F B2 0F 23 D2 11  ."..O...Z-...#..
000030  86 AF 00 C0 4F 8E DB 8A E8 0E 00 00 00 00 00 00  ....O...........
000040  B8 00 00 00 00 00 16 00 05 00 00 00 80 BB 00 00  ................
000050  28 6B 55 E2 53 F8 4D 40 00 00 00 00 00 00 5E 40  (kU.S.M@......^@
```

Интерпретация:

- `0x10`: `B0 37 00 00 00 00 00 00` → `0x37B0` = **14256** = размер файла
- `0x18`…`0x27`: GUID `46C429EF-904A-11D2-8722-00C04F8EDB8A`
- `0x28`…`0x37`: GUID `B28F2D5A-230F-11D2-86AF-00C04F8EDB8A`
- `0x38`: `E8 0E 00 00…` → **3816** — длина раннего блоба
- `0x46`: `16 00` → **22** (Pro 22)
- `0x4C`: `80 BB 00 00` → **48000** Hz
- `0x50`: double **≈59.94006**
- `0x58`: double **120.0** BPM

## Ранний блоб (field @0x38)

Во всех сэмплах ≈ **3800–3850 байт**. Внутри:

1. Числовые props / GUID сессии
2. UTF-16 путь к `.veg` (старт обычно **0xF8**)
3. `C:\Users\…\Documents`
4. `C:\Users\…\AppData\Local\VEGAS Pro\22.0\`
5. ASCII/.NET кусок **ProjectNotes** (`ScriptPortal.Vegas.ProjectNotes.ProjectNotesList`, `ProjectNoteItem`, `Version=22.0.0.250`)

Длина блоба **слегка растёт** с длиной пути к файлу проекта (видно на копиях `2_videos-*`).

## Медиа-пул (после notes)

Абсолютные UTF-16 пути:

- `…\screenshots\sample_for_project_audio.wav`
- `…\screenshots\sample_for_project_video.mp4`
- или `C:\Users\Admin\Videos\1untitled.mp4` (в проектах с 2 videos)

Рядом бинарно встречаются **1920/1080** (размер кадра).

## Таймлайн / события

Плотный бинарный хвост. Ориентиры:

- Повторяющиеся UTF-16 лейблы `sample_for_project_video` / `_audio`
- В trimmers-проекте лейблов **намного больше** при тех же 2 медиа-файлах → сегментация/тримы
- Crossfade vs base: почти тот же размер (+32 B), много мелких byte-diff (параметры фейдов/позиций), не новый медиафайл

## Track FX strings

Почти везде:

- `VEGAS Track Compressor`
- `VEGAS Track Noise Gate`
- `VEGAS Track EQ`

Повторы связаны с числом аудио-контекстов/треков, не обязательно с ручной расстановкой FX пользователем.

## Сравнение размеров (интуиция)

```
only_audio < only_video < video+audio ≈ crossfade < 2_videos≈compressed/stretched < trimmers < 2_videos(+FX)
   12.2K       13.6K         14.3K         14.3K              ~17.8K                    19.4K     18.5K
```

Сжатие/растяжение клипа (**playback rate**) почти не меняет размер файла относительно sibling `2_videos` без FX — в бинарнике это в основном `f64` rate + длины событий, а не новые медиа.
