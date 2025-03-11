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

#ifndef BANT_UTIL_THREAD_POOL_H
#define BANT_UTIL_THREAD_POOL_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace bant {
// Simple thread-pool.
// Passing in functions, returning futures.
//
// Why not use std::async() ? That standard is so generic and vaguely
// specified that in practice there is no implementation of a policy that
// provides a thread-pool behavior with a guaranteed upper bound of cores used
// on all platforms.
class ThreadPool {
 public:
  // Create thread pool with "thread_count" threads.
  // If that count is zero, functions will be executed synchronously.
  explicit ThreadPool(int thread_count);

  // Exit ASAP and leave remaining work in queue unfinished.
  ~ThreadPool();

  // Add a function returning T, that is to be executed asynchronously.
  // Return a std::future<T> with the eventual result.
  //
  // As a special case: if initialized with no threads, the function is
  // executed synchronously.
  template <class T>
  [[nodiscard]] std::future<T> ExecAsync(const std::function<T()> &f) {
    auto *p = new std::promise<T>();
    std::future<T> future_result = p->get_future();
    // NOLINT, as clang-tidy assumes memory leak where is none.
    auto promise_fulfiller = [p, f]() {  // NOLINT
      p->set_value(f());
      delete p;
    };
    EnqueueWork(promise_fulfiller);
    return future_result;
  }

  void CancelAllWork();

 private:
  void Runner();
  void EnqueueWork(const std::function<void()> &work);

  std::vector<std::thread *> threads_;
  std::mutex lock_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> work_queue_;
  bool exiting_ = false;
};

}  // namespace bant
#endif  // BANT_UTIL_THREAD_POOL_H
