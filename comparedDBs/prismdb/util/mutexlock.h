// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_UTIL_MUTEXLOCK_H_
#define STORAGE_LEVELDB_UTIL_MUTEXLOCK_H_

#include "port/port.h"
#include "port/thread_annotations.h"

namespace leveldb {

// #include <csignal>
// #include <execinfo.h>  

// Helper class that locks a mutex on construction and unlocks the mutex when
// the destructor of the MutexLock object is invoked.
//
// Typical usage:
//
//   void MyClass::MyMethod() {
//     MutexLock l(&mu_);       // mu_ is an instance variable
//     ... some complex code, possibly with multiple return paths ...
//   }
// 信号处理程序
// void signal_handler(int sig) {
//     void *array[10];
//     size_t size;

//     // 获取调用堆栈
//     size = backtrace(array, 10);
//     fprintf(stderr, "Error: signal received.\n");
//     backtrace_symbols_fd(array, size, STDERR_FILENO);
// }
// 输出即将加锁前的调试信息
    // fprintf(stderr, "MutexLock: about to lock mu_ = %p gbgsbbsbfbs\n", mu_);
    // try {
    //   // signal(SIGSEGV, signal_handler);
    //   if(this->mu_ == nullptr){
    //     fprintf(stderr,"the mu_ in MutexLock is empty!\n");
    //   }
    //   this->mu_->Lock();
    // } catch (const std::system_error& e) {
    //   // std::cerr << "Mutex lock failed: " << e.what() << std::endl;
    //   fprintf(stderr,"Mutex lock failed:%s \n ",e.what());
    // }
    // 输出加锁成功后的信息
    // fprintf(stderr, "MutexLock: successfully locked mu_ = %p\n", mu_);

class SCOPED_LOCKABLE MutexLock {
 public:
  explicit MutexLock(port::Mutex* mu) EXCLUSIVE_LOCK_FUNCTION(mu) : mu_(mu) {
    this->mu_->Lock();
  }
  ~MutexLock() UNLOCK_FUNCTION() { this->mu_->Unlock(); }

  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;

 private:
  port::Mutex* mu_;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_MUTEXLOCK_H_
