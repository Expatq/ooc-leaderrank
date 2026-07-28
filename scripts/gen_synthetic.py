#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

import numpy as np

POWER_LAW_EXPONENT = 2.1
MAX_DEGREE_SHARE = 0.5
SIZE_TUNE_ROUNDS = 8


def fail(message):
    print(f"gen_synthetic: {message}", file=sys.stderr)
    sys.exit(1)


def parse_size(text):
    suffixes = {"K": 2**10, "M": 2**20, "G": 2**30}
    try:
        if text and text[-1].upper() in suffixes:
            value = int(float(text[:-1]) * suffixes[text[-1].upper()])
        else:
            value = int(text)
    except (OverflowError, ValueError) as error:
        raise argparse.ArgumentTypeError(f"invalid size: {text!r}") from error
    if value <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return value


def parse_hypernodes(text):
    try:
        count_text, degree_text = text.split(":", maxsplit=1)
        count, degree = int(count_text), int(degree_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "hypernodes must use COUNT:DEGREE, for example 3:50000"
        ) from error
    if count < 0 or degree < 0:
        raise argparse.ArgumentTypeError("hypernode count and degree must be non-negative")
    return count, degree


def sample_out_degrees(rng, vertices, avg_degree):
    raw = rng.zipf(POWER_LAW_EXPONENT, size=vertices).astype(np.float64)
    cap = max(2.0, vertices * MAX_DEGREE_SHARE)
    raw = np.minimum(raw, cap)
    scaled = raw * (avg_degree / raw.mean())
    degrees = np.maximum(scaled.round().astype(np.int64), 0)
    return np.minimum(degrees, vertices - 1)


def generate_edges(rng, vertices, avg_degree, hypernode_count, hypernode_degree):
    degrees = sample_out_degrees(rng, vertices, avg_degree)
    sources = np.repeat(np.arange(vertices, dtype=np.int64), degrees)
    targets = rng.integers(0, vertices, size=sources.size, dtype=np.int64)

    if hypernode_count > 0:
        hubs = rng.choice(vertices, size=hypernode_count, replace=False)
        hub_targets = np.repeat(hubs, hypernode_degree)
        hub_sources = rng.integers(0, vertices, size=hub_targets.size, dtype=np.int64)
        sources = np.concatenate([sources, hub_sources])
        targets = np.concatenate([targets, hub_targets])

    keep = sources != targets
    return sources[keep], targets[keep]


WRITE_CHUNK_ROWS = 1_000_000


def write_csv(path, sources, targets):
    with open(path, "w") as file:
        file.write("from,to\n")
        for begin in range(0, sources.size, WRITE_CHUNK_ROWS):
            end = begin + WRITE_CHUNK_ROWS
            block = np.stack([sources[begin:end], targets[begin:end]], axis=1)
            np.savetxt(file, block, fmt="%d,%d")


def estimate_vertices(target_bytes, avg_degree):
    vertices = max(2, target_bytes // (avg_degree * 16))
    for _ in range(SIZE_TUNE_ROUNDS):
        digits = len(str(vertices - 1))
        bytes_per_edge = 2 * digits + 2
        vertices = max(2, target_bytes // (avg_degree * bytes_per_edge))
    return int(vertices)


def main():
    parser = argparse.ArgumentParser(
        description="Generate a reproducible directed graph with optional hypernodes."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--vertices", type=int, help="number of vertices")
    group.add_argument("--target-size", type=parse_size,
                       help="approximate CSV size, for example 512M or 2G")
    parser.add_argument("--avg-degree", type=int, default=16,
                        help="target average out-degree (default: 16)")
    parser.add_argument("--hypernodes", type=parse_hypernodes, default=(0, 0),
                        metavar="COUNT:DEGREE",
                        help="add high in-degree vertices, for example 3:50000")
    parser.add_argument("--seed", type=int, required=True,
                        help="PCG64 seed for reproducible output")
    parser.add_argument("--out", required=True, help="output CSV path")
    args = parser.parse_args()

    if args.avg_degree <= 0:
        fail("--avg-degree must be positive")
    if args.vertices is not None:
        vertices = args.vertices
    else:
        vertices = estimate_vertices(args.target_size, args.avg_degree)
    if vertices < 2:
        fail("need at least 2 vertices")
    hypernode_count, hypernode_degree = args.hypernodes
    if hypernode_count > vertices:
        fail("more hypernodes than vertices")

    rng = np.random.Generator(np.random.PCG64(args.seed))
    sources, targets = generate_edges(rng, vertices, args.avg_degree,
                                      hypernode_count, hypernode_degree)
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    write_csv(args.out, sources, targets)
    print(f"vertices={vertices} edges={sources.size} out={args.out}")


if __name__ == "__main__":
    main()
