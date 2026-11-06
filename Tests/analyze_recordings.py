"""Check the tone recordings produced by the VM tests (requires NumPy)."""

import json
import struct
import sys
from pathlib import Path

import numpy as np


def analyze(path):
    wav = path.read_bytes()
    assert wav[:4] == b"RIFF" and wav[8:12] == b"WAVE", path
    position, chunks = 12, {}
    while position + 8 <= len(wav):
        kind, size = struct.unpack_from("<4sI", wav, position)
        chunks[kind] = wav[position + 8 : position + 8 + size]
        position += 8 + size + (size % 2)
    tag, channels, rate, _, block, bits = struct.unpack_from("<HHIIHH", chunks[b"fmt "])
    if tag == 0xFFFE:
        assert chunks[b"fmt "][24:40] == bytes.fromhex(
            "0100000000001000800000aa00389b71"
        ), path
    else:
        assert tag == 1, path
    assert bits in (16, 24, 32) and block == channels * (bits // 8), path
    raw = chunks[b"data"]
    assert len(raw) % block == 0, path
    if bits == 24:
        samples = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        pcm = (samples[:, 0] | samples[:, 1] << 8 | samples[:, 2] << 16) << 8 >> 8
    else:
        pcm = np.frombuffer(raw, dtype="<i2" if bits == 16 else "<i4")
    pcm = pcm.reshape(-1, channels).astype(np.float64) / (2 ** (bits - 1))
    segments = []
    for start in np.arange(0, len(pcm) / rate - 0.25, 0.25):
        samples = pcm[int(start * rate) : int((start + 0.25) * rate)]
        time = np.arange(len(samples)) / rate
        amplitudes = {
            str(freq): [
                float(
                    abs(np.sum(samples[:, ch] * np.exp(-2j * np.pi * freq * time)))
                    * 2
                    / len(samples)
                )
                for ch in range(pcm.shape[1])
            ]
            for freq in [997, 1499]
        }
        segments.append({"start": round(float(start), 2), "amplitudes": amplitudes})
    result = {
        "file": path.name,
        "sample_rate": rate,
        "channels": pcm.shape[1],
        "seconds": len(pcm) / rate,
        "peak": float(np.max(np.abs(pcm))),
        "segments": segments,
    }
    assert 0.05 < result["peak"] < 0.3, f"invalid peak {result['peak']}: {path}"
    for channel in range(pcm.shape[1]):
        frequencies = (
            [997, 1499] if pcm.shape[1] == 1 else [997 if channel == 0 else 1499]
        )
        if path.name.startswith("continuous-") and pcm.shape[1] == 1:
            frequencies = [997]
        for frequency in frequencies:
            assert max(
                s["amplitudes"][str(frequency)][channel] for s in segments
            ) > 0.05, f"missing {frequency} Hz in channel {channel}: {path}"
            if path.name.startswith("continuous-"):
                middle = [
                    s for s in segments if 1 <= s["start"] < result["seconds"] - 3
                ]
                coverage = sum(
                    s["amplitudes"][str(frequency)][channel] > 0.05 for s in middle
                ) / len(middle)
                result.setdefault("tone_coverage", []).append(coverage)
                assert coverage > 0.95, f"tone missing from {1 - coverage:.1%} of windows: {path}"
    if path.name == "writer-reconnect.wav":
        for lower, upper in [(0, 1), (1.25, 2.75)]:
            assert max(
                s["amplitudes"]["997"][0]
                for s in segments
                if lower <= s["start"] < upper
            ) > 0.05
        assert max(
            s["amplitudes"]["997"][0] for s in segments if s["start"] > 3.5
        ) < 0.001
    return result


for argument in sys.argv[1:]:
    path = Path(argument)
    result = analyze(path)
    path.with_suffix(".analysis.json").write_text(
        json.dumps(result, indent=2), encoding="utf8"
    )
    print(json.dumps({key: value for key, value in result.items() if key != "segments"}))
