#include "table/block_based/data_block_perfect_hash_index.h"

#include <string>
#include <vector>

#include "data_block_hash_index.h"
#include "rocksdb/slice.h"
#include "util/coding.h"
#include "util/hash.h"

namespace ROCKSDB_NAMESPACE {

void DataBlockPerfectHashIndexBuilder::Add(const Slice& key,
                                           const size_t restart_index) {
  assert(Valid());
  if (restart_index > kMaxBlockSizeSupportedByHashIndex) {
    valid_ = false;
    return;
  }

  key_and_restart_pairs_.emplace_back(key.ToString(),
                                      static_cast<uint8_t>(restart_index));

  if (key_and_restart_pairs_.size() > kMaxRestartSupportedByHashIndex) {
    valid_ = false;
  }
}

void DataBlockPerfectHashIndexBuilder::Finish(std::string& buffer) {
  assert(Valid());
  assert(key_and_restart_pairs_.size() <= kMaxRestartSupportedByHashIndex);

  std::vector<std::pair<Slice, uint8_t>> kvs;
  kvs.reserve(key_and_restart_pairs_.size());
  for (uint16_t i = 0; i < key_and_restart_pairs_.size(); ++i) {
    Slice s(key_and_restart_pairs_[i].first);
    uint8_t restart_index{key_and_restart_pairs_[i].second};
    kvs.emplace_back(s, restart_index);
  }

  // Constructing Minimal Perfect Hashing
  std::vector<bool> all;
  all.reserve(kvs.size() * 3);

  std::vector<uint16_t> level_capacity;
  std::vector<uint8_t> values;
  for (uint16_t level = 0; !kvs.empty(); ++level) {
    uint64_t seed = (level + 1) * kSeedJump;
    uint16_t cap = static_cast<uint16_t>(kvs.size());
    level_capacity.push_back(cap);

    uint16_t ones = 0;
    std::vector<uint16_t> used(cap, 0), h(cap, 0), h_inv(cap, 0);
    for (uint16_t i = 0; i < cap; ++i) {
      h[i] = GetSliceHash64(kvs[i].first, seed) % cap;
      h_inv[h[i]] = i;
      ++used[h[i]];

      ones += used[h[i]] == 1;
      ones -= used[h[i]] == 2;
    }

    all.reserve(all.size() + ones);
    for (uint16_t i = 0; i < cap; ++i) {
      if (used[i] == 1) {
        all.push_back(true);
        values.push_back(kvs[h_inv[i]].second);
      } else {
        all.push_back(false);
      }
    }

    std::vector<std::pair<Slice, uint8_t>> nxt;
    nxt.reserve(cap - ones);
    for (uint16_t i = 0; i < cap; ++i)
      if (used[h[i]] != 1) {
        nxt.push_back(kvs[i]);
      }
    kvs.swap(nxt);
  }

  // Constructing Bit Vector
  std::vector<uint16_t> bit_v((all.size() + 15) / 16, 0);
  for (uint16_t i = 0; i < all.size(); ++i)
    if (all[i]) {
      bit_v[i >> 4] |= 1 << (i & 15);
    }

  std::vector<uint16_t> rank_prefix(bit_v.size());
  rank_prefix[0] = __builtin_popcount(bit_v[0]);
  for (uint16_t i = 1; i < bit_v.size(); ++i) {
    rank_prefix[i] = rank_prefix[i - 1] + __builtin_popcount(bit_v[i]);
  }

  // Encoding [bit_v] [rank_prefix] [values] [level capacity] [NUM_LEVELS]
  // [BIT_VECTOR_SIZE]
  size_t mph_size = bit_v.size() * 16 + rank_prefix.size() * 16 +
                    values.size() * 8 + level_capacity.size() * 16 + 16 + 16;
  if (buffer.size() + mph_size > kMaxBlockSizeSupportedByHashIndex) {
    valid_ = false;
    return;
  }

  for (uint16_t bit_i : bit_v) {
    buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&bit_i)),
                  sizeof bit_i);
  }
  for (uint16_t rank_i : rank_prefix) {
    buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&rank_i)),
                  sizeof rank_i);
  }
  for (uint8_t restart_index : values) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&restart_index)),
        sizeof(restart_index));
  }
  for (uint16_t level_cap : level_capacity) {
    buffer.append(const_cast<const char*>(reinterpret_cast<char*>(&level_cap)),
                  sizeof level_cap);
  }

  // NUM_LEVELS
  PutFixed16(&buffer, static_cast<uint16_t>(level_capacity.size()));

  // BIT_VECTOR_SIZE
  PutFixed16(&buffer, static_cast<uint16_t>(bit_v.size()));
}

void DataBlockPerfectHashIndexBuilder::Reset() {
  key_and_restart_pairs_.clear();
  valid_ = true;
}

void DataBlockPerfectHashIndex::Initialize(const char* data, uint16_t size,
                                           uint16_t* map_offset) {
  assert(size >= sizeof(uint16_t) * 2);  // NUM_LEVELS + BIT_VECTOR_SIZE
  bit_vector_size_ = DecodeFixed16(data + size - sizeof(uint16_t));

  uint16_t num_levels = DecodeFixed16(data + size - 2 * sizeof(uint16_t));
  level_capacity_.resize(num_levels);
  for (uint16_t i = 0; i < num_levels; ++i) {
    level_capacity_[num_levels - i - 1] =
        DecodeFixed16(data + size - (2 + i + 1) * sizeof(uint16_t));
  }

  uint16_t num_restart_index = level_capacity_[0];
  uint16_t mph_size =
      (2 * bit_vector_size_ + num_levels + 2) * sizeof(uint16_t) +
      num_restart_index * sizeof(uint8_t);
  *map_offset = static_cast<uint16_t>(size - mph_size);
}

bool DataBlockPerfectHashIndex::get_bit(const char* data, uint32_t map_offset,
                                        uint16_t i) const {
  const char* bit_vector = data + map_offset;
  uint16_t block = (i >> 4);
  uint16_t bit_word = DecodeFixed16(bit_vector + block * sizeof(uint16_t));
  return (bit_word >> (i & 15)) & 1;
}

uint16_t DataBlockPerfectHashIndex::rank_bit(const char* data,
                                             uint32_t map_offset,
                                             uint16_t i) const {
  const char* bit_vector = data + map_offset;
  uint16_t block = (i >> 4);
  uint16_t bit_word = DecodeFixed16(bit_vector + block * sizeof(uint16_t));
  uint16_t mask = (1 << (i & 15)) - 1;
  if (block) {
    const char* rank_prefix = bit_vector + bit_vector_size_ * sizeof(uint16_t);
    uint16_t rank_p =
        DecodeFixed16(rank_prefix + (block - 1) * sizeof(uint16_t));
    return rank_p + __builtin_popcount(bit_word & mask);
  } else {
    return __builtin_popcount(bit_word & mask);
  }
}

uint8_t DataBlockPerfectHashIndex::Lookup(const char* data, uint32_t map_offset,
                                          const Slice& key) const {
  uint16_t pos = 0;
  for (uint16_t level = 0; level < level_capacity_.size(); ++level) {
    uint64_t seed = (level + 1) * kSeedJump;
    auto h = GetSliceHash64(key, seed) % level_capacity_[level];

    if (get_bit(data, map_offset, pos + h)) {
      uint16_t rank = rank_bit(data, map_offset, pos + h);

      const char* bit_vector = data + map_offset;
      const char* rank_prefix =
          bit_vector + bit_vector_size_ * sizeof(uint16_t);
      const char* restart_indices =
          rank_prefix + bit_vector_size_ * sizeof(uint16_t);

      return static_cast<uint8_t>(*(restart_indices + rank * sizeof(uint8_t)));
    }
    pos += level_capacity_[level];
  }

  return kNoEntry;
}

}  // namespace ROCKSDB_NAMESPACE
