#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "db/log_format.h"
#include "rocksdb/slice.h"

namespace ROCKSDB_NAMESPACE {

constexpr uint64_t kSeedJump = 676767677;
constexpr uint8_t kPerfectHashIndexMaxEntry = 255;
constexpr uint8_t kIsPerfectHashIndex = 1u << 7;
constexpr uint8_t kPerfectHashIndexMaxLevel = 16;
static_assert(kPerfectHashIndexMaxLevel < 128 &&
              "the high bit is used to indicate perfect hashing");

class DataBlockPerfectHashIndexBuilder {
 public:
#ifdef CSC494_MPH_STATISTICS
  bool stats_is_perfect{};
  size_t stats_bit_v_size{};
  size_t stats_rank_p_size{};
  size_t stats_num_levels{};
  size_t stats_size{};
  size_t stats_est_size{};
  size_t stats_entry_count{};
#endif

  DataBlockPerfectHashIndexBuilder() : valid_(false) {}

  void Initialize() { valid_ = true; }

  inline bool Valid() const { return valid_; }
  void Add(const Slice& key, const size_t restart_index);
  void Finish(std::string& buffer);
  void Reset();
  inline size_t EstimateSize() const {
    const size_t n = key_and_restart_pairs_.size();
    constexpr size_t kBytesPerKeyEstimate = 2;
    return n * kBytesPerKeyEstimate +
           kPerfectHashIndexMaxLevel * sizeof(uint8_t) + sizeof(uint8_t) +
           sizeof(uint16_t);
  }

 private:
  bool valid_;
  std::vector<std::pair<std::string, uint8_t>> key_and_restart_pairs_;
};

class DataBlockPerfectHashIndex {
 public:
#ifdef CSC494_MPH_STATISTICS
  mutable uint64_t stats_lookup_time{};
#endif

  DataBlockPerfectHashIndex() {}

  void Initialize(const char* data, uint16_t size, uint16_t* map_offset);

  uint8_t Lookup(const char* data, uint32_t map_offset, const Slice& key) const;

  inline bool Valid() const { return num_levels_ > 0; }

 private:
  bool is_perfect_{false};
  uint16_t bit_vector_size_{0};
  uint8_t num_levels_{0};
  uint8_t num_restarts_{0};
  uint8_t level_capacity_[kPerfectHashIndexMaxLevel] = {};
};

}  // namespace ROCKSDB_NAMESPACE
