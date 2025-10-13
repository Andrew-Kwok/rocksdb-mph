#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    // TODO
    return SIZE_MAX;
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
