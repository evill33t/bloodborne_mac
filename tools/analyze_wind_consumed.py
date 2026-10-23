#!/usr/bin/env python3
"""Summarize bounded private schema2 wind/VP records; no guest source reads."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct

SITES = [
    {"kind": "wind", "original_pc": 0x001C, "byte_offset": 128, "words": 4, "first_sgpr": 12},
    {"kind": "view_projection", "original_pc": 0x14EC, "byte_offset": 800, "words": 16, "first_sgpr": 8},
]


def analyze(directory):
    observations = []
    for path in sorted(directory.glob("wind_consumed_f*_draw*.json")):
        reasons, raw, meta = [], None, {}
        try:
            if path.stat().st_size > 65536:
                raise ValueError("oversized private metadata")
            parsed = json.loads(path.read_text())
            if not isinstance(parsed, dict):
                raise ValueError("private metadata is not an object")
            meta = parsed
            if not meta.get("complete"):
                reasons.append("completion/file write incomplete")
            if meta.get("gpu_status") != 4 or meta.get("record_bytes") != 240:
                reasons.append("wrong completion status or declared record extent")
            if (meta.get("schema"), meta.get("shader_hash"), meta.get("sample_mask"), meta.get("sites")) != (
                2, "e4abfca709b00ef3", 63, SITES
            ):
                reasons.append("wrong original shader/read sites or protocol")
            binary = path.with_suffix(".bin")
            if not binary.exists() or binary.stat().st_size != 240:
                reasons.append("missing or wrong-sized private record")
            else:
                raw = struct.unpack("<60I", binary.read_bytes())
                if raw[:2] != (0x57494E44, 2) or meta.get("record_words") != list(raw):
                    reasons.append("record identity/binary metadata mismatch")
                if raw[2]:
                    reasons.append("sample counter overflow")
            frame, first, frames = meta.get("frame", -1), meta.get("window_first_frame", -1), meta.get("window_frames", 0)
            if not (1 <= frames <= 120 and first <= frame < first + frames):
                reasons.append("outside bounded requested frame window")
            if not 0 <= meta.get("record_slot", -1) < frames * 2:
                reasons.append("invalid per-draw record slot")
        except (OSError, ValueError, TypeError) as error:
            reasons.append(str(error))
        observation = {"file": str(path), "frame": meta.get("frame"), "draw": meta.get("draw"), "reasons": reasons}
        for name, samples, null, leaf, minimum, maximum, count in (
            ("wind", 3, 5, 7, 20, 24, 4), ("view_projection", 4, 6, 11, 28, 44, 16)
        ):
            site = {"eligible": False}
            if raw:
                site.update(samples=raw[samples], null_reads=raw[null],
                            selected_leaf_uniform=raw[leaf] == raw[leaf+1] and raw[leaf+2] == raw[leaf+3],
                            minimum_words=[f"0x{word:08x}" for word in raw[minimum:minimum+count]],
                            maximum_words=[f"0x{word:08x}" for word in raw[maximum:maximum+count]])
                site["eligible"] = not reasons and raw[samples] > 0 and raw[null] == 0
                site["varied_components"] = [index for index in range(count) if site["eligible"] and raw[samples] > 1
                                             and raw[minimum+index] != raw[maximum+index]]
            observation[name] = site
        observations.append(observation)
    frames = Counter(observation["frame"] for observation in observations)
    summary = {"directory": str(directory.resolve()), "records": len(observations),
               "frame_bound_ok": len(observations) <= 240 and all(count <= 2 for count in frames.values()),
               "invalid_records": sum(bool(record["reasons"]) for record in observations)}
    for site in ("wind", "view_projection"):
        eligible = [record[site] for record in observations if record[site]["eligible"]]
        summary[site] = {"eligible_observations": len(eligible), "selected_invocations": sum(row["samples"] for row in eligible),
                         "varied_draws": sum(bool(row["varied_components"]) for row in eligible),
                         "varied_draws_with_uniform_leaf": sum(bool(row["varied_components"]) and row["selected_leaf_uniform"] for row in eligible)}
    summary["limitations"] = ("Sparse already-loaded registers from selected actual VS invocations; atomics perturb timing; "
                              "equal samples do not prove all vertices or other shader inputs; unsigned bit minima/maxima are not float ordering; "
                              "variation does not identify a writer or establish authored animation speed.")
    summary["observations"] = observations
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = analyze(args.directory)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "observations"}, indent=2))
