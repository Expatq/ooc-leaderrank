#!/usr/bin/env python3
import argparse
import sys

import numpy as np

INT32_MAX = 2**31 - 1
MAX_EXACT_VERTICES = 10_000
SMALL_FILE_BYTES = 10 * 2**20
MASS_TOLERANCE = 1e-12
SPARSE_EPS = 1e-12
SPARSE_MAX_ITERATIONS = 2000
WRITE_CHUNK_ROWS = 1_000_000


def fail(message):
    print(f"reference_rank: {message}", file=sys.stderr)
    sys.exit(1)


def is_integer(token):
    try:
        int(token)
    except ValueError:
        return False
    return True


def parse_id(token, path, line_no):
    if not is_integer(token):
        fail(f"{path}:{line_no}: not a number: {token!r}")
    value = int(token)
    if value < 0:
        fail(f"{path}:{line_no}: negative id: {value}")
    if value > INT32_MAX:
        fail(f"{path}:{line_no}: id exceeds int32: {value}")
    return value


def read_edges(path, transpose):
    edges = []
    first_data_line = True
    with open(path, newline="") as file:
        for line_no, raw in enumerate(file, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            separator = "\t" if "\t" in line else ","
            tokens = [t.strip() for t in line.split(separator)]
            if len(tokens) < 2:
                fail(f"{path}:{line_no}: expected two columns")
            if first_data_line:
                first_data_line = False
                if not (is_integer(tokens[0]) and is_integer(tokens[1])):
                    continue
            src = parse_id(tokens[0], path, line_no)
            dst = parse_id(tokens[1], path, line_no)
            edges.append((dst, src) if transpose else (src, dst))
    return edges


def simple_graph(edges):
    return sorted({(u, v) for u, v in edges if u != v})


def leader_rank_exact(edges):
    vertices = sorted({u for edge in edges for u in edge})
    n = len(vertices)
    if n > MAX_EXACT_VERTICES:
        fail(f"{n} vertices: exact solver supports at most {MAX_EXACT_VERTICES}")
    index = {v: i for i, v in enumerate(vertices)}
    kout = np.zeros(n)
    for u, _ in edges:
        kout[index[u]] += 1.0
    inv = 1.0 / (kout + 1.0)
    transition = np.zeros((n + 1, n + 1))
    for u, v in edges:
        transition[index[v], index[u]] = inv[index[u]]
    transition[n, :n] = inv
    transition[:n, n] = 1.0 / n
    system = np.eye(n + 1) - transition
    system[n, :] = 1.0
    rhs = np.zeros(n + 1)
    rhs[n] = float(n)
    s = np.linalg.solve(system, rhs)
    ranks = (s[:n] + s[n] / n) / n
    if abs(float(ranks.sum()) - 1.0) > MASS_TOLERANCE:
        fail(f"mass invariant violated: rank sum {ranks.sum():.17g}")
    return vertices, ranks


def load_edges_fast(path, transpose):
    import pandas as pd

    separator = ","
    with open(path) as file:
        for line in file:
            stripped = line.strip()
            if stripped and not stripped.startswith("#"):
                separator = "\t" if "\t" in stripped else ","
                break
    frame = pd.read_csv(path, sep=separator, comment="#", header=None,
                        names=["a", "b"], skiprows=0, engine="c",
                        on_bad_lines="error", dtype=str)
    frame = frame.dropna()
    numeric = frame["a"].str.strip().str.lstrip("-").str.isdigit()
    frame = frame[numeric]
    src = frame["a"].astype(np.int64).to_numpy()
    dst = frame["b"].astype(np.int64).to_numpy()
    if src.size == 0:
        fail("graph is empty after dropping self-loops and duplicates")
    if src.min() < 0 or dst.min() < 0:
        fail("negative id")
    if max(src.max(), dst.max()) > INT32_MAX:
        fail("id exceeds int32")
    if transpose:
        src, dst = dst, src
    keep = src != dst
    src, dst = src[keep], dst[keep]
    packed = np.unique((src.astype(np.uint64) << np.uint64(32)) | dst.astype(np.uint64))
    return (packed >> np.uint64(32)).astype(np.int64), (packed & np.uint64(0xFFFFFFFF)).astype(np.int64)


def leader_rank_sparse(src, dst):
    from scipy import sparse

    vertices = np.unique(np.concatenate([src, dst]))
    n = vertices.size
    src_index = np.searchsorted(vertices, src)
    dst_index = np.searchsorted(vertices, dst)
    kout = np.bincount(src_index, minlength=n).astype(np.float64)
    inv = 1.0 / (kout + 1.0)
    transition = sparse.csr_matrix((inv[src_index], (dst_index, src_index)), shape=(n, n))

    s = np.ones(n)
    ground = 0.0
    for _ in range(SPARSE_MAX_ITERATIONS):
        s_next = transition.dot(s) + ground / n
        ground_next = float(inv.dot(s))
        delta = float(np.abs(s_next - s).sum())
        mass = float(s_next.sum()) + ground_next
        if abs(mass - n) > 1e-9 * n:
            fail(f"mass invariant violated: {mass!r} instead of {n}")
        s, ground = s_next, ground_next
        if delta / n < SPARSE_EPS:
            break
    else:
        fail(f"did not converge within {SPARSE_MAX_ITERATIONS} iterations")

    ranks = (s + ground / n) / n
    if abs(float(ranks.sum()) - 1.0) > 1e-9:
        fail(f"normalization violated: rank sum {ranks.sum():.17g}")
    return vertices, ranks


def write_ranks(path, vertices, ranks):
    with open(path, "w") as file:
        file.write("vertex,rank\n")
        for begin in range(0, len(vertices), WRITE_CHUNK_ROWS):
            end = begin + WRITE_CHUNK_ROWS
            for vertex, rank in zip(vertices[begin:end], ranks[begin:end]):
                file.write(f"{vertex},{rank:.15g}\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("edges")
    parser.add_argument("--out", required=True)
    parser.add_argument("--transpose", action="store_true")
    args = parser.parse_args()
    import os

    if os.path.getsize(args.edges) > SMALL_FILE_BYTES:
        src, dst = load_edges_fast(args.edges, args.transpose)
        vertices, ranks = leader_rank_sparse(src, dst)
    else:
        edges = simple_graph(read_edges(args.edges, args.transpose))
        if not edges:
            fail("graph is empty after dropping self-loops and duplicates")
        vertices, ranks = leader_rank_exact(edges)
    write_ranks(args.out, vertices, ranks)


if __name__ == "__main__":
    main()
