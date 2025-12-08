#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "db/log_format.h"
#include "rocksdb/slice.h"

namespace ROCKSDB_NAMESPACE {

const uint64_t kSeedJump = 676767677;

class DataBlockPerfectHashIndexBuilder {
 public:
  size_t stats_num_levels{};
  size_t stats_size{};
  size_t stats_est_size{};
  size_t stats_entry_count{};

  DataBlockPerfectHashIndexBuilder() : valid_(false) {}

  void Initialize() { valid_ = true; }

  inline bool Valid() const { return valid_; }
  void Add(const Slice& key, const size_t restart_index);
  void Finish(std::string& buffer);
  void Reset();
  inline size_t EstimateSize() const {
    const size_t n = key_and_restart_pairs_.size();
    // constexpr size_t kBytesPerKeyEstimate = 3;
    // size_t estimated_num_bits = key_and_restart_pairs_.size() * 5;  // expected n*e bits under poisson(1)
    // size_t bit_size = (estimated_num_bits + 7) / 8 * sizeof(uint8_t);
    // size_t rank_prefix_size = bit_size;
    // size_t restart_indices_size = key_and_restart_pairs_.size() * sizeof(uint8_t);
    size_t expected_num_level =
        std::ceil(log2(key_and_restart_pairs_.size()));

    return size_t(n * 2.7) + expected_num_level * sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t);
  }

 private:
  bool valid_;
  std::vector<std::pair<std::string, uint8_t>> key_and_restart_pairs_;
  // std::vector<std::pair<Slice, uint8_t>> slice_and_restart_pairs_;
};

class DataBlockPerfectHashIndex {
public:
  mutable uint64_t stats_lookup_time{};

  DataBlockPerfectHashIndex() {}

  void Initialize(const char* data, uint16_t size, uint16_t* map_offset);

  uint8_t Lookup(const char* data, uint32_t map_offset, const Slice& key) const;

  inline bool Valid() const { return !level_capacity_.empty(); }

 private:
  uint16_t bit_vector_size_;
  std::vector<uint16_t> level_capacity_;

  bool get_bit(const char* data, uint32_t map_offset, uint16_t i) const;

  uint16_t rank_bit(const char* data, uint32_t map_offset, uint16_t i) const;
};

}  // namespace ROCKSDB_NAMESPACE
