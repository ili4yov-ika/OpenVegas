# `example_project_with_2_videos-one_stretched.veg`

## Роль сэмпла

Два видеоклипа: Untitled **растянут/замедлен** (playback rate < 100%)

## Идентификация

| Поле | Значение |
|------|----------|
| Size | 18496 bytes |
| MD5 | `4d8df3f724a5dde3b66b4f99ad416f20` |
| Filesize field @0x10 | 18496 |
| Early blob @0x38 | 3832 |
| VEGAS version @0x46 | 22 |
| Sample rate @0x4C | 48000 |
| FPS @0x50 | 59.940060 |
| Tempo @0x58 | 120.0 |
| GUID root | `46C429EF-904A-11D2-8722-00C04F8EDB8A` |
| GUID type | `B28F2D5A-230F-11D2-86AF-00C04F8EDB8A` |
| Const @0x04 | `2e91cf11a5d628db04c10000` |

## Путь проекта (UTF-16 внутри файла)

`D:\Devs\C++\OpenVegas\sample_ui\example_project_with_2_videos-one_stretched.veg`

## Медиа-ссылки

- `C:\Users\Admin\Videos\1untitled.mp4`

Доп. ссылка на nested project (UTF-16 `.veg`):
- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_video_and_audio_with_crossfade.veg`

## Playback rate / time-stretch

| Offset | Rate (f64) | % | Длина на таймлайне | ≈ source used (len×rate) |
|--------|------------|---|--------------------|--------------------------|
| `0x1728` | 0.363179077007 | **36.3%** | 16.583233 s | 6.022683 s |
| `0x2F70` | 0.363179077007 | **36.3%** | 16.583233 s | 6.022683 s |

Паттерн: `u64 ?`, `u64 length_ticks`, `u64 0`, **`f64 rate`**, `u64 0`.  
Единица времени ≈ **10 000 000 ticks/сек**. UI VEGAS: `rate × 100` → **36.3%** (с запятой в локали).

## Track FX (уникальные)

Вхождений FX-строк: **6**

- `VEGAS Track Compressor`
- `VEGAS Track EQ`
- `VEGAS Track Noise Gate`

## Event FX (`{Svfx:...}`)

- `{Svfx:com.vegascreativesoftware:colorcorrector}`

## Прочие UTF-16 пути

- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_2_videos-one_stretched.veg`
- `C:\Users\Admin\Documents`
- `C:\Users\Admin\AppData\Local\VEGAS Pro\22.0\`
- `C:\Users\Admin\Videos\1untitled.mp4`
- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_video_and_audio_with_crossfade.veg`

## Заметные строки

- `D:\Devs\C++\OpenVegas\sample_ui\example_project_with_2_videos-one_stretched.veg`
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
000010  40 48 00 00 00 00 00 00 EF 29 C4 46 4A 90 D2 11   @H.......).FJ...
000020  87 22 00 C0 4F 8E DB 8A 5A 2D 8F B2 0F 23 D2 11   ."..O...Z-...#..
000030  86 AF 00 C0 4F 8E DB 8A F8 0E 00 00 00 00 00 00   ....O...........
000040  B8 00 00 00 00 00 16 00 05 00 00 00 80 BB 00 00   ................
000050  28 6B 55 E2 53 F8 4D 40 00 00 00 00 00 00 5E 40   (kU.S.M@......^@
000060  00 00 00 00 00 00 00 00 04 00 04 00 40 00 00 00   ............@...
000070  00 00 00 00 00 00 00 00 03 00 00 00 A0 00 00 00   ................
```

## Рассуждения

- Ранний блоб (~3832 B) — props + путь + ProjectNotes; далее media pool и timeline.
- Time-stretch: **36.3%** ×2 (video+audio pair).
- Rate < 1 → **растяжение / замедление** (accordion реже, badge <100%).
- Один и тот же кусок медиа (~**6.02 s**) укладывается в событие **16.583 s** на таймлайне.
- Для OpenVegas: читать **f64 playback rate** на событии + длину в ticks/1e7; UI — гармошка на video, процент на audio.
