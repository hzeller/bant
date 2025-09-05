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
#include <thread>

#include "absl/synchronization/mutex.h"

namespace bant {
ThreadPool::ThreadPool(int thread_count)
    : more_work_available_(
        +[](ThreadPool *p) { return !p->work_queue_.empty() || p->exiting_; },
        this) {
  for (int i = 0; i < thread_count; ++i) {
    threads_.push_back(new std::thread(&ThreadPool::Runner, this));
  }
}

ThreadPool::~ThreadPool() {
  CancelAllWork();
  for (auto *t : threads_) {
    t->join();
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
