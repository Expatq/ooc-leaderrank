#pragma once

#include "size_literals.hpp"

#include <cstdint>

namespace lr::common {

inline constexpr uint64_t kIoChunkNumerator = 3;
inline constexpr uint64_t kIoChunkDenominatorPerThread = 20;
inline constexpr uint64_t kMinIoChunkBytes = 256_KiB;
inline constexpr uint64_t kMaxIoChunkBytes = 4_MiB;

inline constexpr uint64_t kUsableNumerator = 85;
inline constexpr uint64_t kUsableDenominator = 100;

inline constexpr uint64_t kColumnBytesPerVertex = 20;
inline constexpr uint64_t kRankBytesPerVertex = 8;
inline constexpr uint64_t kDegreeBytesPerVertex = 4;
inline constexpr uint64_t kEdgeBytes = 8;
inline constexpr uint64_t kBlockAlignBytes = 4_KiB;
inline constexpr uint64_t kScatterMinBufferBytes = 8_KiB;
inline constexpr uint64_t kSparseIdRatioLimit = 64;

inline constexpr uint32_t kMaxVertexId = 2'147'483'647;

inline constexpr double kDefaultEps = 1e-9;
inline constexpr uint32_t kDefaultMaxIterations = 500;
inline constexpr double kMassTolerancePerVertex = 1e-9;

} // namespace lr::common
