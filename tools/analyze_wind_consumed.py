#!/usr/bin/env python3
"""Summarize private schema2/3/4/5 consumed VS inputs/outputs; no guest reads."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct

SITES = [
    {"kind": "wind", "original_pc": 0x001C, "byte_offset": 128, "words": 4, "first_sgpr": 12},
    {"kind": "view_projection", "original_pc": 0x14EC, "byte_offset": 800, "words": 16, "first_sgpr": 8},
]
CAMERA_SITE = {"kind": "camera", "original_pcs": [0x1500, 0x14F8, 0x14E8],
               "byte_offsets": [732, 748, 764], "sgprs": [0, 5, 4], "words": 3, "recorded_after_pc": 0x1500}
MATRIX_SITE = {"kind": "object_matrix", "original_pcs": [0x00B4, 0x00BC, 0x00C4],
               "first_vgprs": [23, 27, 31], "descriptor_sgprs": [16, 17, 18, 19], "index_vgprs": [0, 1, 2],
               "words": 12, "recorded_after_pc": 0x00C4, "selection": "one_sampled_original_row0_index", "mapping_null_status": "unobserved"}
SITES_BY_SCHEMA = {2: SITES, 3: [*SITES, CAMERA_SITE], 4: [*SITES, CAMERA_SITE, MATRIX_SITE],
                   5: [*SITES, CAMERA_SITE, MATRIX_SITE]}
WORDS_BY_SCHEMA = {2: 60, 3: 72, 4: 120, 5: 255}
FIXED_VERTEX_METADATA = {
    "vid": 0, "iid": 0, "before_pc_fetch": 0x000C, "after_pc_matrix": 0x00C4,
    "after_pc_undeformed": 0x00F8, "after_pc_deformed": 0x1534, "after_pc_clip": 0x1578,
    "words": 255, "vertex_descriptor_sgprs": [16, 35],
    "attribute_vgprs": [4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 22],
    "matrix_descriptor_sgprs": [16, 17, 18, 19], "matrix_index_vgprs": [0, 1, 2],
    "matrix_data_vgprs": [23, 34], "world_vgprs": [26, 30, 34], "clip_vgprs": [5, 6, 7, 8],
}
FIXED_VERTEX_LAYOUT = {
    "fetch": (120, 125, 161, 36), "matrix": (121, 197, 216, 19),
    "undeformed": (122, 235, 238, 3), "deformed": (123, 241, 244, 3), "clip": (124, 247, 251, 4),
}


def exact_metadata(value, expected):
    # False/0 and float/int compare equal in Python; the declared register/site
    # schema requires actual integers and exact keys/list lengths instead.
    if type(value) is not type(expected):
        return False
    if isinstance(expected, dict):
        return value.keys() == expected.keys() and all(exact_metadata(value[key], item) for key, item in expected.items())
    if isinstance(expected, list):
        return len(value) == len(expected) and all(exact_metadata(a, b) for a, b in zip(value, expected))
    return value == expected


def analyze(directory):
    observations = []
    for path in sorted(directory.glob("wind_consumed_f*_draw*.json")):
        reasons, raw, meta, schema = [], None, {}, None
        try:
            if path.stat().st_size > 65536:
                raise ValueError("oversized private metadata")
            parsed = json.loads(path.read_text())
            if not isinstance(parsed, dict):
                raise ValueError("private metadata is not an object")
            meta = parsed
            supplied_schema = meta.get("schema")
            schema = supplied_schema if type(supplied_schema) is int else None
            word_count = WORDS_BY_SCHEMA.get(schema)
            if not meta.get("complete"):
                reasons.append("completion/file write incomplete")
            if not word_count or meta.get("gpu_status") != 4 or meta.get("record_bytes") != word_count * 4:
                reasons.append("wrong completion status or declared record extent")
            if (meta.get("schema"), meta.get("shader_hash"), meta.get("sample_mask"), meta.get("sites")) != (
                schema if word_count else None, "e4abfca709b00ef3", 63, SITES_BY_SCHEMA.get(schema)
            ):
                reasons.append("wrong original shader/read sites or protocol")
            if schema == 5 and not exact_metadata(meta.get("fixed_vertex"), FIXED_VERTEX_METADATA):
                reasons.append("wrong exact fixed-vertex decoder/register context")
            binary = path.with_suffix(".bin")
            if not word_count or not binary.exists() or binary.stat().st_size != word_count * 4:
                reasons.append("missing or wrong-sized private record")
            else:
                raw = struct.unpack(f"<{word_count}I", binary.read_bytes())
                if raw[:2] != (0x57494E44, schema) or meta.get("record_words") != list(raw):
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
        observation = {"file": str(path), "frame": meta.get("frame"), "draw": meta.get("draw"), "schema": schema, "reasons": reasons}
        for name, samples, null, leaf, minimum, maximum, count in (
            ("wind", 3, 5, 7, 20, 24, 4), ("view_projection", 4, 6, 11, 28, 44, 16),
            ("camera", 60, 61, 62, 66, 69, 3)
        ):
            site = {"eligible": False, "recorded": bool(raw and maximum + count <= len(raw))}
            if site["recorded"]:
                site.update(samples=raw[samples], null_reads=raw[null],
                            selected_leaf_uniform=raw[leaf] == raw[leaf+1] and raw[leaf+2] == raw[leaf+3],
                            minimum_words=[f"0x{word:08x}" for word in raw[minimum:minimum+count]],
                            maximum_words=[f"0x{word:08x}" for word in raw[maximum:maximum+count]])
                site["eligible"] = not reasons and raw[samples] > 0 and raw[null] == 0
                site["varied_components"] = [index for index in range(count) if site["eligible"] and raw[samples] > 1
                                             and raw[minimum+index] != raw[maximum+index]]
            observation[name] = site
        matrix = {"recorded": bool(raw and len(raw) >= 120), "eligible": False,
                  "mapping_null_status": "unobserved", "mapped_input_proven": False}
        if matrix["recorded"]:
            selected = raw[77]
            descriptor_uniform = raw[82:86] == raw[86:90]
            rows_uniform = raw[90:93] == raw[93:96] and selected <= 0xFFFFFFFD and list(raw[90:93]) == [selected+k for k in range(3)]
            # Exact captured plain float4/stride16 form only. Unknown ADD_TID,
            # swizzle, index-stride or address bits can make equal original
            # row indices legitimately address different data across threads.
            # Captured bit15 is ignored padding, not address swizzling. Admit
            # that exact variant and zero padding; bits8..11 stay forbidden.
            base = raw[82] | ((raw[83] & 0xFF) << 32)
            plain_addressing = (descriptor_uniform and not (raw[82] & 3)
                                and (raw[83] & 0xFFFFFF00) in (0x00100000, 0x00108000)
                                and raw[85] == 0x77FAC and selected <= 0xFFFFFFFD
                                and base + (selected+3)*16 <= (1 << 40))
            identity_uniform = (descriptor_uniform and rows_uniform and plain_addressing and raw[78] == raw[79] == raw[82]
                                and raw[80] == raw[81] == raw[83] & 0xFFF
                                and all(row < raw[84] for row in raw[90:93]))
            accounting_valid = raw[73] + raw[74] + raw[75] + raw[76] == raw[72]
            matrix.update(candidates=raw[72], samples=raw[73], nonselected_indices=raw[74], claim_misses=raw[75],
                          rejected_bounds=raw[76], selected_index=None if selected == 0xFFFFFFFF else selected,
                          selected_identity_uniform=identity_uniform, plain_addressing_known=plain_addressing,
                          accounting_valid=accounting_valid,
                          minimum_words=[f"0x{word:08x}" for word in raw[96:108]],
                          maximum_words=[f"0x{word:08x}" for word in raw[108:120]],
                          descriptor_minimum_words=[f"0x{word:08x}" for word in raw[82:86]],
                          descriptor_maximum_words=[f"0x{word:08x}" for word in raw[86:90]],
                          original_row_minimum=list(raw[90:93]), original_row_maximum=list(raw[93:96]))
            matrix["eligible"] = not reasons and accounting_valid and raw[73] > 0 and rows_uniform
            matrix["varied_components"] = [k for k in range(12) if matrix["eligible"] and raw[73] > 1 and raw[96+k] != raw[108+k]]
        observation["object_matrix"] = matrix
        fixed = {"recorded": bool(raw and schema == 5 and len(raw) == 255), "eligible": False}
        if fixed["recorded"]:
            stages = {}
            for name, (counter, minimum, maximum, count) in FIXED_VERTEX_LAYOUT.items():
                lo, hi = raw[minimum:minimum+count], raw[maximum:maximum+count]
                stages[name] = {"samples": raw[counter], "uniform": lo == hi,
                                "minimum_words": [f"0x{word:08x}" for word in lo],
                                "maximum_words": [f"0x{word:08x}" for word in hi],
                                "varied_components": [k for k in range(count) if raw[counter] > 1 and lo[k] != hi[k]]}
            counts = [stage["samples"] for stage in stages.values()]
            fixed.update(stages=stages, stage_counts_equal=min(counts) > 0 and len(set(counts)) == 1,
                         uniform=all(stage["uniform"] for stage in stages.values()))
            fixed["eligible"] = not reasons and fixed["stage_counts_equal"]
        observation["fixed_vertex"] = fixed
        observations.append(observation)
    frames = Counter(observation["frame"] for observation in observations)
    summary = {"directory": str(directory.resolve()), "records": len(observations),
               "schemas": dict(Counter(row["schema"] for row in observations)),
               "frame_bound_ok": len(observations) <= 240 and all(count <= 2 for count in frames.values()),
               "invalid_records": sum(bool(record["reasons"]) for record in observations)}
    for site in ("wind", "view_projection", "camera"):
        eligible = [record[site] for record in observations if record[site]["eligible"]]
        summary[site] = {"recorded_observations": sum(row[site]["recorded"] for row in observations),
                         "eligible_observations": len(eligible), "selected_invocations": sum(row["samples"] for row in eligible),
                         "varied_draws": sum(bool(row["varied_components"]) for row in eligible),
                         "varied_draws_with_uniform_leaf": sum(bool(row["varied_components"]) and row["selected_leaf_uniform"] for row in eligible)}
    matrices = [row["object_matrix"] for row in observations if row["object_matrix"]["eligible"]]
    summary["object_matrix"] = {"recorded_observations": sum(row["object_matrix"]["recorded"] for row in observations),
                                "eligible_observations": len(matrices), "selected_invocations": sum(row["samples"] for row in matrices),
                                "varied_draws": sum(bool(row["varied_components"]) for row in matrices),
                                "varied_draws_with_uniform_identity": sum(bool(row["varied_components"]) and row["selected_identity_uniform"] for row in matrices),
                                "mapping_null_status": "unobserved", "mapped_input_proven": False}
    fixed = [row["fixed_vertex"] for row in observations if row["fixed_vertex"]["eligible"]]
    summary["fixed_vertex"] = {"recorded_observations": sum(row["fixed_vertex"]["recorded"] for row in observations),
                               "eligible_observations": len(fixed), "uniform_observations": sum(row["uniform"] for row in fixed),
                               "varied_observations": sum(not row["uniform"] for row in fixed), "vid": 0, "iid": 0}
    summary["limitations"] = ("Sparse already-loaded registers from selected actual VS invocations; atomics perturb timing; "
                              "equal samples do not prove all vertices or other shader inputs; schema2 has no camera and schemas2/3 have no matrix observation; "
                              "matrix samples cover one original index only and mapping-null status is unobserved, so stable zero data cannot prove mapping; unsigned bit minima/maxima are not float ordering; "
                              "uniform matrix identity requires the captured plain float4/stride16 descriptor form without ADD_TID or swizzling; "
                              "schema5 fixed-vertex samples cover original vid0/iid0 only and do not establish visible coverage or cross-frame resource ownership; "
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
