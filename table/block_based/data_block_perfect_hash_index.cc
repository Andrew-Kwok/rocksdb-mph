#include "table/block_based/data_block_perfect_hash_index.h"

#include <chrono>
#include <string>
#include <vector>

#include "data_block_hash_index.h"
#include "rocksdb/slice.h"
#include "util/coding.h"
#include "util/hash.h"

namespace ROCKSDB_NAMESPACE {

inline uint64_t SplitMix64(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

void DataBlockPerfectHashIndexBuilder::Add(const Slice& key,
                                           const size_t restart_index) {
  assert(Valid());
  if (restart_index > kMaxRestartSupportedByHashIndex) {
    valid_ = false;
    return;
  }
  if (key_and_restart_pairs_.size() >=
      kPerfectHashIndexMaxEntry) {  // allow all mph encoding using only uint8_t
    valid_ = false;
    return;
  }

  key_and_restart_pairs_.emplace_back(key.ToString(),
                                       static_cast<uint8_t>(restart_index));

  if (EstimateSize() > kMaxBlockSizeSupportedByHashIndex) {
    valid_ = false;
  }
}

void DataBlockPerfectHashIndexBuilder::Finish(std::string& buffer) {
  assert(Valid());
  assert(key_and_restart_pairs_.size() <= kPerfectHashIndexMaxEntry);

#ifdef CSC494_MPH_STATISTICS
  stats_entry_count = key_and_restart_pairs_.size();
  stats_est_size = EstimateSize();
#endif

  std::vector<std::pair<Slice, uint8_t>> kvs;
  kvs.reserve(key_and_restart_pairs_.size());
  for (uint16_t i = 0; i < key_and_restart_pairs_.size(); ++i) {
    Slice s(key_and_restart_pairs_[i].first);
    uint8_t restart_index{key_and_restart_pairs_[i].second};
    kvs.emplace_back(s, restart_index);
  }

  // Constructing Minimal Perfect Hashing
  std::vector<bool> all;
  all.reserve(key_and_restart_pairs_.size() * 2);

  std::vector<uint8_t> level_capacity;
  std::vector<uint8_t> values;
  for (uint8_t level = 0;
       !kvs.empty() && level < kPerfectHashIndexMaxLevel;
       ++level) {
    uint64_t seed = (level + 1) * kSeedJump;
    uint8_t cap = static_cast<uint8_t>(kvs.size()) | 1;
    level_capacity.push_back(cap);

    uint8_t ones = 0;
    std::vector<uint8_t> used(cap, 0), h(cap, 0), h_inv(cap, 0);
    for (uint8_t i = 0; i < kvs.size(); ++i) {
      h[i] = FastRange64(GetSliceHash64(kvs[i].first, seed), cap);
      h_inv[h[i]] = i;
      ++used[h[i]];

      ones += used[h[i]] == 1;
      ones -= used[h[i]] == 2;
    }

    for (uint8_t i = 0; i < cap; ++i) {
      if (used[i] == 1) {
        all.push_back(true);
        values.push_back(kvs[h_inv[i]].second);
      } else {
        all.push_back(false);
      }
    }

    std::vector<std::pair<Slice, uint8_t>> nxt;
    nxt.reserve(kvs.size() - ones);
    for (uint8_t i = 0; i < kvs.size(); ++i)
      if (used[h[i]] != 1) {
        nxt.push_back(kvs[i]);
      }
    kvs.swap(nxt);
  }

  // Constructing Bit Vector
  std::vector<uint8_t> bit_v((all.size() + 7) / 8, 0);
  for (uint16_t i = 0; i < all.size(); ++i) {
    if (all[i]) {
      bit_v[i >> 3] |= 1 << (i & 7);
    }
  }

  std::vector<uint8_t> rank_prefix(bit_v.size());
  rank_prefix[0] = __builtin_popcount(bit_v[0]);
  for (uint8_t i = 1; i < bit_v.size(); ++i) {
    rank_prefix[i] = rank_prefix[i - 1] + __builtin_popcount(bit_v[i]);
  }

  // Encoding [bit_v] [rank_prefix] [values] [level capacity] [NUM_LEVELS]
  // [NUM_VALUES] [BIT_VECTOR_SIZE]
  size_t est_size = EstimateSize();
  size_t mph_size = bit_v.size() * sizeof(uint8_t) +
                    rank_prefix.size() * sizeof(uint8_t) +
                    values.size() * sizeof(uint8_t) +
                    level_capacity.size() * sizeof(uint8_t) + sizeof(uint8_t) +
                    sizeof(uint8_t) + sizeof(uint16_t);

  for (uint8_t bit_i : bit_v) {
    buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&bit_i)),
                  sizeof bit_i);
  }
  for (uint8_t rank_i : rank_prefix) {
    buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&rank_i)),
                  sizeof rank_i);
  }
  for (uint8_t restart_index : values) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&restart_index)),
        sizeof(restart_index));
  }
  for (uint8_t level_cap : level_capacity) {
    buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&level_cap)),
                  sizeof level_cap);
  }

  // NUM_LEVELS
  uint8_t num_levels = static_cast<uint8_t>(level_capacity.size());
  if (kvs.empty()) {
    num_levels |= kIsPerfectHashIndex;
  }
  buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&num_levels)),
                sizeof(num_levels));

  // NUM_RESTARTS
  uint8_t num_values = values.size();
  buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&num_values)),
                sizeof(num_values));

  // BIT_VECTOR_SIZE
  PutFixed16(&buffer, static_cast<uint16_t>(bit_v.size()));

#ifdef CSC494_MPH_STATISTICS
  {
    stats_is_perfect = (num_levels & kIsPerfectHashIndex) > 0;
    stats_bit_v_size = bit_v.size() * sizeof(uint8_t);
    stats_rank_p_size = rank_prefix.size() * sizeof(uint8_t);
    stats_num_levels = static_cast<uint8_t>(level_capacity.size());
    stats_size = mph_size;
  }
#endif
}

void DataBlockPerfectHashIndexBuilder::Reset() {
  key_and_restart_pairs_.clear();
  valid_ = true;

#ifdef CSC494_MPH_STATISTICS
  stats_is_perfect = false;
  stats_bit_v_size = 0;
  stats_rank_p_size = 0;
  stats_num_levels = 0;
  stats_size = 0;
  stats_est_size = 0;
  stats_entry_count = 0;
#endif
}

void DataBlockPerfectHashIndex::Initialize(const char* data, uint16_t size,
                                           uint16_t* map_offset) {
  assert(size >= 2 * sizeof(uint8_t) +
                     sizeof(uint16_t));  // NUM_LEVELS + BIT_VECTOR_SIZE

  bit_vector_size_ = DecodeFixed16(data + size - sizeof(uint16_t));
  num_restarts_ =
      DecodeFixed8(data + size - sizeof(uint16_t) - sizeof(uint8_t));
  num_levels_ =
      DecodeFixed8(data + size - sizeof(uint16_t) - 2 * sizeof(uint8_t));

  if (num_levels_ & kIsPerfectHashIndex) {
    is_perfect_ = true;
    num_levels_ ^= kIsPerfectHashIndex;
  }

  memcpy(&level_capacity_,
         data + size - sizeof(uint16_t) - (num_levels_ + 2) * sizeof(uint8_t),
         num_levels_);

  uint16_t mph_size = (2 * bit_vector_size_ + num_restarts_ + num_levels_ + 2) *
                          sizeof(uint8_t) +
                      sizeof(uint16_t);
  *map_offset = static_cast<uint16_t>(size - mph_size);
}

// static uint8_t RankBit(const uint8_t* bit_vector, uint32_t bit_vector_length,
// uint32_t bit_pos) {
//   uint8_t rank = 0;
//
//   // 1) process full 8-byte chunks with 64-bit popcount
//   const uint32_t full_words = bit_pos >> 6;
//   const uint32_t word_offset = bit_pos & 63;
//   const uint64_t* words = reinterpret_cast<const uint64_t*>(bit_vector);
//
//   for (uint32_t i = 0; i < full_words; ++i) {
//     rank += __builtin_popcountll(words[i]);
//   }
//
//   // 2) process remaining bits
//   const uint64_t mask = (1ull << word_offset) - 1;
//   if (mask) {
//     rank += __builtin_popcountll(words[full_words] & mask);
//   }
//
//   return rank;
// }

uint8_t DataBlockPerfectHashIndex::Lookup(const char* data, uint32_t map_offset,
                                          const Slice& key) const {
  const uint8_t* bit_vector =
      reinterpret_cast<const uint8_t*>(data + map_offset);
  const uint8_t* rank_prefix = bit_vector + bit_vector_size_ * sizeof(uint8_t);
  const uint8_t* restart_indices =
      rank_prefix + bit_vector_size_ * sizeof(uint8_t);

  uint16_t level_offset{0};
  for (uint8_t level = 0; level < num_levels_; ++level) {
    uint64_t seed = (level + 1) * kSeedJump;
    uint64_t h = FastRange64(GetSliceHash64(key, seed), level_capacity_[level]);

    const uint16_t pos = level_offset + h;
    const uint16_t block = pos >> 3;
    const uint16_t block_off = pos & 7;

    if (bit_vector[block] >> block_off & 1) {
      uint8_t rank =
          __builtin_popcount(bit_vector[block] & ((1 << block_off) - 1));
      if (block) {
        rank += rank_prefix[block - 1];
      }
      return restart_indices[rank];
    }
    level_offset += level_capacity_[level];
  }

  return is_perfect_ ? kNoEntry : kCollision;
}

}  // namespace ROCKSDB_NAMESPACE
