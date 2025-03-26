// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_TABLE_ITERATOR_WRAPPER_H_
#define STORAGE_LEVELDB_TABLE_ITERATOR_WRAPPER_H_

#include "leveldb/iterator.h"
#include "leveldb/slice.h"

namespace leveldb {

// A internal wrapper class with an interface similar to Iterator that
// caches the valid() and key() results for an underlying iterator.
// This can help avoid virtual function calls and also gives better
// cache locality.
class IteratorWrapper {
 public:
  IteratorWrapper() : iter_(nullptr), valid_(false) {
    // fprintf(stdout, "IteratorWrapper: Created with null iterator.\n");
  }

  explicit IteratorWrapper(Iterator* iter) : iter_(nullptr) { Set(iter); }

  ~IteratorWrapper() { 
      if (iter_ != nullptr) {
        // fprintf(stdout, "IteratorWrapper: Deleting iterator at address %p, iter_: %p.\n", this, iter_);
        delete iter_; 
      } else {
        // fprintf(stdout, "IteratorWrapper: Destructing with null iterator.\n");
      }
    }

  Iterator* iter() const { return iter_; }


  // Takes ownership of "iter" and will delete it when destroyed, or
  // when Set() is invoked again.
  void Set(Iterator* iter) {
    if (iter_ != nullptr) {
      // fprintf(stdout, "IteratorWrapper: Deleting existing iterator at address %p, iter_: %p.\n", this, iter_);
      delete iter_;
    }
    iter_ = iter;
    if (iter_ == nullptr) {
      valid_ = false;
      // fprintf(stdout, "IteratorWrapper: Set to null iterator at address %p.\n", this);
    } else {
      Update();
      // fprintf(stdout, "IteratorWrapper: Set to new iterator at address %p, iter_: %p.\n", this, iter_);
    }
  }

  // Iterator interface methods
  bool Valid() const { return valid_; }
  Slice key() const {
    assert(Valid());
    return key_;
  }
  Slice value() const {
    assert(Valid());
    return iter_->value();
  }
  // Methods below require iter() != nullptr
  Status status() const {
    assert(iter_);
    return iter_->status();
  }
  void Next() {
    assert(iter_);
    iter_->Next();
    Update();
  }
  void Prev() {
    assert(iter_);
    iter_->Prev();
    Update();
  }
  void Seek(const Slice& k) {
    assert(iter_);
    iter_->Seek(k);
    Update();
  }
  void SeekToFirst() {
    assert(iter_);
    iter_->SeekToFirst();
    Update();
  }
  void SeekToLast() {
    assert(iter_);
    iter_->SeekToLast();
    Update();
  }

 private:
  void Update() {
    valid_ = iter_->Valid();
    if (valid_) {
      key_ = iter_->key();
    }
    // fprintf(stdout, "IteratorWrapper: Update at address %p - valid: %d, key: %s\n", this, valid_, key_.ToString().c_str());
  }

  Iterator* iter_;
  bool valid_;
  Slice key_;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_TABLE_ITERATOR_WRAPPER_H_
