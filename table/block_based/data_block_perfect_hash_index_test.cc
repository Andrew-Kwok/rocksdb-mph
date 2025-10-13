#include "table/block_based/data_block_perfect_hash_index.h"

#include "test_util/testharness.h"

namespace ROCKSDB_NAMESPACE {

TEST(DataBlockPerfectHashIndex, Simple) {
  DataBlockPerfectHashIndexBuilder builder;
  builder.Initialize();

  constexpr int n_keys = 10;

  for (int i = 0; i < n_keys; ++i) {
    std::string key("key" + std::to_string(i));
    uint8_t restart_point = i;
    builder.Add(key, restart_point);
  }

  std::string buffer("fake"), buffer2;
  builder.Finish(buffer);

  buffer2 = buffer;  // test for the correctness of relative offset

  Slice s(buffer2);
  DataBlockPerfectHashIndex index;
  uint16_t map_offset;
  index.Initialize(s.data(), static_cast<uint16_t>(s.size()), &map_offset);

  for (int i = 0; i < n_keys; ++i) {
    std::string key("key" + std::to_string(i));
    uint8_t restart_point = i;
    ASSERT_EQ(index.Lookup(s.data(), map_offset, key), restart_point);
  }
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
