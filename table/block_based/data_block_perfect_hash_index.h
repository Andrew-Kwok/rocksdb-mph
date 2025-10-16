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
  DataBlockPerfectHashIndexBuilder() : valid_(false) {}

  void Initialize() { valid_ = true; }

  inline bool Valid() const { return valid_; }
  void Add(const Slice& key, const size_t restart_index);
  void Finish(std::string& buffer);
  void Reset();
  inline size_t EstimateSize() const {
    size_t estimated_num_bits = key_and_restart_pairs_.size() *
                                3;  // expected n*e bits under poisson(1)
    size_t bit_size = ((estimated_num_bits + 15) / 16) * 16;
    size_t rank_prefix_size = bit_size;
    size_t restart_indices_size = key_and_restart_pairs_.size() * 8;
    size_t level_capacities_size =
        std::ceil(log2(key_and_restart_pairs_.size())) * 16;

    return bit_size + rank_prefix_size + restart_indices_size +
           level_capacities_size + 16 + 16;
  }

 private:
  bool valid_;
  std::vector<std::pair<std::string, uint8_t>> key_and_restart_pairs_;
  // std::vector<std::pair<Slice, uint8_t>> slice_and_restart_pairs_;
};

class DataBlockPerfectHashIndex {
 public:
  DataBlockPerfectHashIndex() {}

  void Initialize(const char* data, uint16_t size, uint16_t* map_offset);

  uint8_t Lookup(const char* data, uint32_t map_offset, const Slice& key) const;

  inline bool Valid() const { return true; }

 private:
  uint16_t bit_vector_size_;
  std::vector<uint16_t> level_capacity_;

  bool get_bit(const char* data, uint32_t map_offset, uint16_t i) const;

  uint16_t rank_bit(const char* data, uint32_t map_offset, uint16_t i) const;
};

}  // namespace ROCKSDB_NAMESPACE
