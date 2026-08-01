# Diff: `example_project_with_2_videos-one_compressed.veg` vs `example_project_with_2_videos-one_stretched.veg`

| | compressed | stretched |
|---|------------|-----------|
| Size | 19096 | 18496 |
| MD5 | `309fbfafc93abd6cd62cb0f58b62e149` | `4d8df3f724a5dde3b66b4f99ad416f20` |
| Δ size | | -600 B |
| Differing bytes (aligned prefix) | | **7462** / 18496 |
| Diff regions (~clustered) | | **151** |

## Playback rates

| | compressed | stretched |
|---|------------|-----------|
| Rate | **245.6%** | **36.3%** |
| Timeline len | 2.452450 s | 16.583233 s |
| Source used (len×rate) | 6.022683 s | 6.022683 s |
| Product check | same media slice ≈ **6.02 s** |

## Semantic takeaway

- Оба файла — один проект (2 клипа: long crossfade-render + Untitled), отличаются **playback rate** Untitled video/audio pair.
- compressed: rate≈**2.456** (245,6%), короткое событие (~2.45 s), size **19096**.
- stretched: rate≈**0.363** (36,3%), длинное событие (~16.58 s), size **18496**.
- `source_used ≈ timeline_len × rate` = **6.022683 s** в обоих → один media in/out, разный time-stretch.
- Оба содержат Event FX `{Svfx:...colorcorrector}`; Track FX набор одинаковый (Compressor / Noise Gate / EQ).
- compressed дополнительно ссылается на `…-one_stretched.veg` в UTF-16 путях (история Save As / sibling).
- Δ size **−600 B** (stretched меньше): длины/оффсеты событий и хвост таймлайна, не новый медиафайл.
