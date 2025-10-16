// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <cstdio>
#include <iostream>
#include <string>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::WriteBatch;
using ROCKSDB_NAMESPACE::WriteOptions;

#if defined(OS_WIN)
std::string kDBPath = "C:\\Windows\\TEMP\\rocksdb_simple_example";
#else
std::string kDBPath = "/tmp/rocksdb_simple_example";
#endif

int main() {
  DB* db;
  Options options;

  rocksdb::BlockBasedTableOptions tbo;
  tbo.data_block_index_type =
      rocksdb::BlockBasedTableOptions::kDataBlockBinaryAndPerfectHash;

  options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(tbo));

  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options.IncreaseParallelism();
  options.OptimizeLevelStyleCompaction();
  // create the DB if it's not already present
  options.create_if_missing = true;

  std::cout << "Table Factory: " << options.table_factory->Name() << '\n';

  // open DB
  Status s = DB::Open(options, kDBPath, &db);
  assert(s.ok());

  const auto Key = [](int i) { return "Key" + std::to_string(i); };
  const auto Value = [](int i) { return "Value" + std::to_string(i); };

  for (int i = 0; i < 100000; ++i) {
    db->Put(WriteOptions{}, Key(i), Value(i));
  }
  db->Flush(rocksdb::FlushOptions{});
  db->CompactRange(rocksdb::CompactRangeOptions{}, /*begin=*/nullptr,
                   /*end=*/nullptr);

  for (int i = 0; i < 100000; ++i) {
    std::string v;
    db->Get(ReadOptions{}, Key(i), &v);
    assert(v == Value(i));
  }

  // // Put key-value
  // s = db->Put(WriteOptions(), "key1", "value");
  // assert(s.ok());
  // std::string value;
  // // get value
  // s = db->Get(ReadOptions(), "key1", &value);
  // assert(s.ok());
  // assert(value == "value");
  //
  // // atomically apply a set of updates
  // {
  //   WriteBatch batch;
  //   batch.Delete("key1");
  //   batch.Put("key2", value);
  //   s = db->Write(WriteOptions(), &batch);
  // }
  //
  // s = db->Get(ReadOptions(), "key1", &value);
  // assert(s.IsNotFound());
  //
  // db->Get(ReadOptions(), "key2", &value);
  // assert(value == "value");
  //
  // {
  //   PinnableSlice pinnable_val;
  //   db->Get(ReadOptions(), db->DefaultColumnFamily(), "key2", &pinnable_val);
  //   assert(pinnable_val == "value");
  // }
  //
  // {
  //   std::string string_val;
  //   // If it cannot pin the value, it copies the value to its internal
  //   buffer.
  //   // The intenral buffer could be set during construction.
  //   PinnableSlice pinnable_val(&string_val);
  //   db->Get(ReadOptions(), db->DefaultColumnFamily(), "key2", &pinnable_val);
  //   assert(pinnable_val == "value");
  //   // If the value is not pinned, the internal buffer must have the value.
  //   assert(pinnable_val.IsPinned() || string_val == "value");
  // }
  //
  // PinnableSlice pinnable_val;
  // s = db->Get(ReadOptions(), db->DefaultColumnFamily(), "key1",
  // &pinnable_val); assert(s.IsNotFound());
  // // Reset PinnableSlice after each use and before each reuse
  // pinnable_val.Reset();
  // db->Get(ReadOptions(), db->DefaultColumnFamily(), "key2", &pinnable_val);
  // assert(pinnable_val == "value");
  // pinnable_val.Reset();
  // // The Slice pointed by pinnable_val is not valid after this point

  puts("simple_example: ok");

  delete db;

  return 0;
}
