#!/usr/bin/env python3
"""Fill unfinished translation entries after lupdate (per-message, safe XML)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "translations"

EN_MAP = {
    "файл не найден или недоступен": "file not found or inaccessible",
    "неподдерживаемый формат": "unsupported format",
    "нет доступа к файлу": "no file access",
    "неизвестная ошибка": "unknown error",
    "не удалось декодировать аудиоданные": "failed to decode audio data",
    "OpenVegas — разметка тестовых файлов": "OpenVegas — Test File Markup",
    "&Открыть…": "&Open…",
    "&Сохранить…": "&Save…",
    "В&ыход": "E&xit",
    "Привязать метки к сетке": "Snap markers to grid",
    "Размер:": "Time sig:",
    "◀ сетка": "◀ grid",
    "сетка ▶": "grid ▶",
    "Привязать метки": "Snap markers",
    "Откройте аудиофайл с постоянным BPM": "Open an audio file with steady BPM",
    "Сохранить изменения?": "Save changes?",
    "Сохранить метки и аудио перед продолжением?": "Save markers and audio before continuing?",
    "Открыть аудио": "Open audio",
    "Аудио (*.wav *.mp3 *.flac);;Все файлы (*)": "Audio (*.wav *.mp3 *.flac);;All files (*)",
    "Декодирование…": "Decoding…",
    "Ошибка": "Error",
    "Не удалось декодировать файл: %1": "Failed to decode file: %1",
    "Загружены метки из %1": "Markers loaded from %1",
    "OpenVegas — %1": "OpenVegas — %1",
    "Анализ BPM": "BPM analysis",
    "Не удалось определить BPM. Укажите BPM вручную.": "Could not detect BPM. Enter BPM manually.",
    "BPM: %1 — метки на каждой доле": "BPM: %1 — marker on every beat",
    "Сетка сдвинута %1 на 1 удар": "Grid shifted %1 by 1 beat",
    "назад": "back",
    "вперёд": "forward",
    "Метки привязаны к тактовой сетке": "Markers snapped to beat grid",
    "Сохранить аудиофайл": "Save Audio File",
    "WAV (*.wav);;MP3 — копия исходника (*.mp3);;Все файлы (*)":
        "WAV (*.wav);;MP3 — copy of source (*.mp3);;All files (*)",
    "Не удалось скопировать аудиофайл.": "Failed to copy audio file.",
    "Сохранено: %1 и %2": "Saved: %1 and %2",
    "Анализировать": "Analyze",
    "Анализ... %p%": "Analyzing... %p%",
    "Анализ тональности и нот...": "Analyzing key and notes...",
    "Анализ завершён: %1, найдено нот: %2": "Analysis finished: %1, notes found: %2",
    "Изменить высоту ноты": "Change note pitch",
    "Применить коррекцию высоты нот": "Apply note pitch correction",
    "Пересчитать звук по изменённым нотам пианоролла":
        "Re-render audio using the edited piano roll notes",
    "Сначала выполните анализ нот (кнопка «Анализировать»)":
        "Run note analysis first (the \"Analyze\" button)",
    "Нет изменённых нот — сдвиньте ноты на пианоролле":
        "No edited notes — drag notes on the piano roll",
    "Коррекция высоты нот": "Note pitch correction",
    "Сначала примените сжатие-растяжение (Ctrl+T), затем коррекцию высоты нот.":
        "Apply time stretch first (Ctrl+T), then note pitch correction.",
    "Коррекция высоты нот...": "Applying note pitch correction...",
    "Ошибка при коррекции высоты нот": "Error while applying note pitch correction",
    "Коррекция высоты нот применена": "Note pitch correction applied",
    "Сдвинуть тактовую сетку на один удар назад (Shift — вместе с метками)\n"
    "Shift + перетаскивание ЛКМ на волне — тонкая подстройка сетки":
        "Shift beat grid one beat back (Shift — move markers too)\n"
        "Shift + LMB drag on waveform — fine grid adjustment",
    "Сдвинуть тактовую сетку на один удар вперёд (Shift — вместе с метками)\n"
    "Shift + перетаскивание ЛКМ на волне — тонкая подстройка сетки":
        "Shift beat grid one beat forward (Shift — move markers too)\n"
        "Shift + LMB drag on waveform — fine grid adjustment",
}

KEEP_AS_IS = {"▶", "■", "00:00.0", "❚❚", "BPM:", "&Файл", "&Правка"}


def last_source(message: str) -> str:
    sources = re.findall(r"<source>(.*?)</source>", message, re.DOTALL)
    return sources[-1] if sources else ""


def fix_message(message: str, lang: str) -> tuple[str, bool]:
    match = re.search(
        r"<translation(?P<attrs>[^>]*)>(?P<body>.*?)</translation>",
        message,
        re.DOTALL,
    )
    if not match or 'type="unfinished"' not in match.group("attrs"):
        return message, False

    source = last_source(message)
    body = match.group("body")

    if lang == "ru":
        translation = body if body.strip() else source
    elif body.strip():
        translation = body
    elif source in EN_MAP:
        translation = EN_MAP[source]
    elif source in KEEP_AS_IS:
        translation = source.replace("&Файл", "&File").replace("&Правка", "&Edit")
    else:
        translation = source

    new_tag = f"<translation>{translation}</translation>"
    new_message = message[: match.start()] + new_tag + message[match.end() :]
    return new_message, True


def fix_file(path: Path, lang: str) -> int:
    text = path.read_text(encoding="utf-8")
    parts = re.split(r"(<message>.*?</message>)", text, flags=re.DOTALL)
    fixed = 0
    for i, part in enumerate(parts):
        if part.startswith("<message>"):
            parts[i], changed = fix_message(part, lang)
            if changed:
                fixed += 1
    path.write_text("".join(parts), encoding="utf-8")
    return fixed


if __name__ == "__main__":
    for name, lang in (("en_US.ts", "en"), ("ru_RU.ts", "ru")):
        count = fix_file(ROOT / name, lang)
        print(f"{name}: fixed {count} unfinished entries")
