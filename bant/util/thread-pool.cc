// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "bant/util/thread-pool.h"

#include <functional>

#include "absl/synchronization/mutex.h"

// The ThreadPool::ThreadAdapter needs to call ThreadPool::Runner in a thread,
// and have conceptually a Start() and Join() method.
// Easily adapted to platform-specific executor-like things; default
// implementation here based on std::thread.

// begin-thread-adapter
#include <thread>
class bant::ThreadPool::ThreadAdapter : public std::thread {
 public:
  explicit ThreadAdapter(ThreadPool *pool)
      : std::thread(&ThreadPool::Runner, pool) {}
  void Start() {}  // std::thread starts immediately.
  void Join() { join(); }
};
// end-thread-adapter

namespace bant {
ThreadPool::ThreadPool(int thread_count)
    : more_work_available_(
        +[](ThreadPool *p) {
          p->lock_.AssertReaderHeld();
          return !p->work_queue_.empty() || p->exiting_;
        },
        this) {
  for (int i = 0; i < thread_count; ++i) {
    threads_.emplace_back(new ThreadAdapter(this))->Start();
  }
}

ThreadPool::~ThreadPool() {
  CancelAllWork();
  for (auto *t : threads_) {
    t->Join();
    delete t;
  }
}

void ThreadPool::Runner() {
  std::function<void()> process_work_item;
  for (;;) {
    {
      const absl::MutexLock l(&lock_, more_work_available_);
      if (exiting_) return;
      process_work_item = work_queue_.front();
      work_queue_.pop_front();
    }
    process_work_item();
  }
}

void ThreadPool::ExecAsync(const std::function<void()> &fun) {
  if (threads_.empty()) {
    fun();  // synchronous execution
    return;
  }

  const absl::MutexLock l(&lock_);
  work_queue_.emplace_back(fun);
}

void ThreadPool::CancelAllWork() {
  const absl::MutexLock l(&lock_);
  exiting_ = true;
}
}  // namespace bant
