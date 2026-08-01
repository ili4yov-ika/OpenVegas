# `example_project_with_2_videos.veg`

## Роль сэмпла

Проект с двумя видео / иным медиа-набором

## Идентификация

| Поле | Значение |
|------|----------|
| Size | 18464 bytes |
| MD5 | `24ccb8cb621a12f11bd8d08b94bc4321` |
| Filesize field @0x10 | 18464 |
| Early blob @0x38 | 3800 |
| VEGAS version @0x46 | 22 |
| Sample rate @0x4C | 48000 |
| FPS @0x50 | 59.940060 |
| Tempo @0x58 | 120.0 |
| GUID root | `46C429EF-904A-11D2-8722-00C04F8EDB8A` |
| GUID type | `B28F2D5A-230F-11D2-86AF-00C04F8EDB8A` |
| Const @0x04 | `2e91cf11a5d628db04c10000` |

## Путь проекта (UTF-16 внутри файла)

`D:\Devs\C++\OpenVegas\sample_ui\example_project_with_2_videos.veg`

## Медиа-ссылки

- `C:\Users\Admin\Videos\1untitled.mp4`

## Playback rate / time-stretch

_Нестандартных playback rate (≠ 1.0) не найдено._

## Лейблы событий (`sample_for_project_*`)

Число вхождений: **0**

- _(нет sample_for_project_*)_

## Track FX (уникальные строки)

Вхождений FX-строк всего: **6**

- `VEGAS Track Compressor`
- `VEGAS Track EQ`
- `VEGAS Track Noise Gate`

## Event FX (`{Svfx:...}`)

- `{Svfx:com.vegascreativesoftware:colorcorrector}`

## Прочие UTF-16 пути

- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_2_videos.veg`
- `C:\Users\Admin\Documents`
- `C:\Users\Admin\AppData\Local\VEGAS Pro\22.0\`
- `C:\Users\Admin\Videos\1untitled.mp4`
- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_video_and_audio_with_crossfade.veg`

## ProjectNotes / .NET type names (ASCII)

- `MProjectNotesLibrary, Version=22.0.0.250, Culture=neutral, PublicKeyToken=null`
- `0ScriptPortal.Vegas.ProjectNotes.ProjectNotesList`
- `System.Collections.Generic.List`1[[ScriptPortal.Vegas.ProjectNotes.ProjectNoteItem, ProjectNotesLibrary, Version=22.0.0.`
- `System.Collections.Generic.List`1[[ScriptPortal.Vegas.ProjectNotes.ProjectNoteItem, ProjectNotesLibrary, Version=22.0.0.`
- `1ScriptPortal.Vegas.ProjectNotes.ProjectNoteItem[]`
- `/ScriptPortal.Vegas.ProjectNotes.ProjectNoteItem`

## Заметные строки

- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_2_videos.veg`
- `C:\Users\Admin\AppData\Local\VEGAS Pro\22.0\`
- `C:\Users\Admin\Videos\1untitled.mp4`
- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_video_and_audio_with_crossfade.veg`
- `1untitled`
- `{Svfx:com.vegascreativesoftware:colorcorrector}`
- `example_project_with_video_and_audio_with_crossfade`
- `VEGAS Track Noise Gate`
- `VEGAS Track EQ`
- `VEGAS Track Compressor`

## Hex dump (первые 128 байт)

```
000000  72 69 66 66 2E 91 CF 11 A5 D6 28 DB 04 C1 00 00   riff......(.....
000010  20 48 00 00 00 00 00 00 EF 29 C4 46 4A 90 D2 11    H.......).FJ...
000020  87 22 00 C0 4F 8E DB 8A 5A 2D 8F B2 0F 23 D2 11   ."..O...Z-...#..
000030  86 AF 00 C0 4F 8E DB 8A D8 0E 00 00 00 00 00 00   ....O...........
000040  B8 00 00 00 00 00 16 00 05 00 00 00 80 BB 00 00   ................
000050  28 6B 55 E2 53 F8 4D 40 00 00 00 00 00 00 5E 40   (kU.S.M@......^@
000060  00 00 00 00 00 00 00 00 04 00 04 00 40 00 00 00   ............@...
000070  00 00 00 00 00 00 00 00 03 00 00 00 84 00 00 00   ................
```

## Рассуждения

- Ранний блоб (~3800 B) содержит props + путь проекта + ProjectNotes; дальше — медиа-пул и таймлайн.
- Размер **18464** согласуется с ролью «Проект с двумя видео / иным медиа-набором»: больше событий/сегментов → больше хвост файла.
- Для импорта в OpenVegas: медиа-пути + **playback rate** на событиях (velocity); точные in/out — через diff с sibling-сэмплами.
