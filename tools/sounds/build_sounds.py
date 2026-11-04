#!/usr/bin/env python3
"""Builds the recorded sound bank from CC0 Freesound recordings.

    python3 tools/sounds/build_sounds.py

For every entry of tools/sound_manifest.json:
  1. downloads the high-quality preview into .cache/sounds/<id>.mp3 (not committed),
  2. checks that the sound page states Creative Commons 0 (anything else is skipped),
  3. decodes it with ffmpeg to 48 kHz mono,
  4. cuts the hits it needs (onset detection on the envelope), fades and peak-normalizes them,
  5. writes assets/sounds/<name>_NN.flac and assets/sounds/CREDITS.md.

The game loads every assets/sounds/<name>_NN.flac as variants of <name> and falls back to its
synthesized sounds when a name has no files. Requires ffmpeg.
"""
import array
import json
import math
import pathlib
import re
import subprocess
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache" / "sounds"
OUT = ROOT / "assets" / "sounds"
RATE = 48000
AGENT = {"User-Agent": "origon-asset-fetch"}


def fetch(url):
    request = urllib.request.Request(url, headers=AGENT)
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def sound_page(sound_id):
    """Page HTML (cached): license check, title, author and preview URL."""
    path = CACHE / f"{sound_id}.html"
    if not path.exists():
        path.write_bytes(fetch(f"https://freesound.org/s/{sound_id}/"))
    return path.read_text("utf-8", "replace")


def page_info(text):
    cc0 = "creativecommons.org/publicdomain/zero" in text
    preview = re.search(r"https://cdn\.freesound\.org/previews/[^\"']+-hq\.mp3", text)
    title = re.search(r'<meta property="og:title" content="([^"]*)"', text)
    author = re.search(r'<meta property="og:audio:artist" content="([^"]*)"', text) or re.search(
        r'href="/people/([^/"]+)/"', text)
    url = re.search(r'<meta property="og:url" content="([^"]*)"', text)
    return cc0, preview.group(0) if preview else None, title.group(1) if title else "?", \
        author.group(1) if author else "?", url.group(1) if url else ""


def decode(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    samples = array.array("h")
    samples.frombytes(raw)
    return [s / 32768.0 for s in samples]


def envelope(samples, window):
    return [max(abs(s) for s in samples[i:i + window]) for i in range(0, len(samples), window)]


def transients(env, peak):
    """Indices (5 ms windows) where the level jumps: above 30% of the peak and 2.2x the quietest
    of the previous 15 ms. Catches every shot of a burst, where the level never falls to silence."""
    hits, last = [], -100
    for i in range(3, len(env)):
        before = min(env[max(0, i - 3):i])
        if env[i] > 0.3 * peak and env[i] > 2.2 * before and i - last > 8:
            hits.append(i)
            last = i
    if not hits and env:
        hits = [max(range(len(env)), key=lambda i: env[i])]
    return hits


def find_events(samples, max_seconds):
    """One clip per hit: from the transient to the next one (so a burst gives single shots), or until
    the sound stays 45 dB below its own peak, capped at max_seconds."""
    window = RATE // 200  # 5 ms
    env = envelope(samples, window)
    if not env:
        return []
    events = transients(env, max(env))
    out = []
    for k, start in enumerate(events):
        stop_limit = events[k + 1] - 1 if k + 1 < len(events) else len(env)
        stop_limit = min(stop_limit, start + int(max_seconds * 200))
        local_peak = max(env[start:stop_limit] or [0.0])
        end = stop_limit
        silent = 0
        for j in range(start, stop_limit):
            silent = silent + 1 if env[j] < local_peak * 0.0056 else 0
            if silent >= 20:  # 100 ms below -45 dB
                end = j - 19
                break
        a = max(0, start * window - RATE // 200)  # 5 ms pre-roll
        b = min(len(samples), end * window)
        if b - a > RATE // 50:
            out.append((local_peak, samples[a:b]))
    return out


# Loudness targets (RMS of the first 300 ms, dBFS) per sound family, so variants and families sit
# together in the mix. The engine applies the per-event volumes on top.
TARGETS = {"shot_far": -17.0, "shot": -12.0, "bolt": -20.0, "reload": -21.0, "casing": -28.0, "step": -24.0,
           "knife": -19.0, "body_hit": -17.0, "land": -19.0}
LOOP_TARGETS = {"ambience_forest": -22.0, "ambience_wind": -27.0, "ambience_stream": -21.0}


def target_for(name):
    for prefix in sorted(TARGETS, key=len, reverse=True):
        if name.startswith(prefix):
            return TARGETS[prefix]
    return -16.0


def shape(clip, fade_in=0.003, fade_out=0.03, rms_db=-16.0, peak_db=-0.5):
    clip = list(clip)
    n_in = min(len(clip), int(fade_in * RATE))
    n_out = min(len(clip), int(fade_out * RATE))
    for i in range(n_in):
        clip[i] *= i / n_in
    for i in range(n_out):
        clip[-1 - i] *= i / n_out
    head = clip[: int(0.3 * RATE)]
    rms = math.sqrt(sum(s * s for s in head) / max(len(head), 1))
    peak = max((abs(s) for s in clip), default=0.0)
    if rms < 1e-6 or peak < 1e-6:
        return clip
    gain = min(10 ** (rms_db / 20.0) / rms, 10 ** (peak_db / 20.0) / peak)
    return [s * gain for s in clip]


def lowpass(samples, cutoff, passes=2):
    """One-pole low-pass, applied twice (12 dB/octave): tames hiss in ambience loops."""
    a = math.exp(-2.0 * math.pi * cutoff / RATE)
    for _ in range(passes):
        y = 0.0
        out = []
        for x in samples:
            y = (1.0 - a) * x + a * y
            out.append(y)
        samples = out
    return samples


def make_loop(samples, start, length, rms_db, fade=1.5):
    """Seamless loop: the segment's end crossfades into its beginning (equal power)."""
    a = int(start * RATE)
    n = int(length * RATE)
    f = int(fade * RATE)
    segment = samples[a:a + n + f]
    if len(segment) < n + f:
        segment = samples[: n + f]
    body = segment[:n]
    tail = segment[n:n + f]
    for i in range(len(tail)):
        t = i / len(tail)
        body[i] = body[i] * math.sin(t * math.pi / 2) + tail[i] * math.cos(t * math.pi / 2)
    rms = math.sqrt(sum(s * s for s in body) / max(len(body), 1))
    gain = 10 ** (rms_db / 20.0) / rms if rms > 1e-6 else 1.0
    return [max(-1.0, min(1.0, s * gain)) for s in body]


def write_flac(path, clip):
    """16-bit mono FLAC (lossless, about half the size of WAV) through ffmpeg."""
    data = array.array("h", (int(max(-1.0, min(1.0, s)) * 32767) for s in clip))
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar", str(RATE), "-ac", "1", "-i", "-",
                    "-compression_level", "8", str(path)], input=data.tobytes(), check=True)


def main():
    manifest = json.loads((ROOT / "tools" / "sound_manifest.json").read_text())
    CACHE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    for old in list(OUT.glob("*.wav")) + list(OUT.glob("*.flac")):
        old.unlink()
    counters, credits = {}, []
    for entry in manifest["sounds"]:
        sound_id, name = entry["id"], entry["name"]
        try:
            cc0, preview, title, author, url = page_info(sound_page(sound_id))
        except Exception as error:  # Network or page errors: keep going with the rest.
            print(f"  {sound_id}: page error {error}")
            continue
        if not cc0 or not preview:
            print(f"  {sound_id}: skipped (license is not CC0 or no preview)")
            continue
        audio = CACHE / f"{sound_id}.mp3"
        if not audio.exists():
            audio.write_bytes(fetch(preview))
        samples = decode(audio)
        mode = entry.get("mode", "first")
        if mode == "loop":
            if entry.get("lowpass"):
                samples = lowpass(samples, entry["lowpass"])
            clips = [make_loop(samples, entry.get("start", 0.0), entry.get("length", 20.0), LOOP_TARGETS.get(name, -22.0))]
        elif mode == "whole":
            # A full sequence (reloads, bolt cycles): from the first onset, up to "max" seconds.
            events = find_events(samples, 0.05)
            start = 0
            if events:
                first = events[0][1]
                for i in range(len(samples) - len(first) + 1):
                    if samples[i] == first[0] and samples[i:i + 8] == first[:8]:
                        start = i
                        break
            clips = [shape(samples[start:start + int(entry.get("max", 2.0) * RATE)], fade_out=0.08,
                           rms_db=target_for(name) + entry.get("gain_db", 0.0))]
        else:
            events = find_events(samples, entry.get("max", 1.5))
            # Drop tiny fragments (clicks between steps, handling noise).
            events = [e for e in events if len(e[1]) >= entry.get("min", 0.12) * RATE] or events[:1]
            if mode == "first":
                events = events[:1]
            else:
                # The strongest hits, kept in recording order.
                strongest = sorted(events, key=lambda e: -e[0])[: entry.get("count", 1)]
                events = [e for e in events if any(e is s for s in strongest)]
            clips = [shape(clip, rms_db=target_for(name) + entry.get("gain_db", 0.0)) for _, clip in events]
        for clip in clips:
            counters[name] = counters.get(name, 0) + 1
            path = OUT / f"{name}_{counters[name]:02d}.flac"
            write_flac(path, clip)
            print(f"  {path.name:28s} {len(clip) / RATE:5.2f} s  from {sound_id} '{title}'")
        credits.append(f"| {name} | [{title}]({url}) | {author} | CC0 |")
    (OUT / "CREDITS.md").write_text(
        "# Sound credits\n\nRecordings from Freesound, all released under Creative Commons 0 (public domain).\n"
        "Built by tools/sounds/build_sounds.py from tools/sound_manifest.json.\n\n"
        "| Use | Sound | Author | License |\n|---|---|---|---|\n" + "\n".join(credits) + "\n")
    print(f"done: {sum(counters.values())} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
