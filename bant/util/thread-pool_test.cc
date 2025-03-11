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

#include <chrono>  // IWYU pragma: keep for chrono_literals
#include <future>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"

using namespace std::chrono_literals;

#include "gtest/gtest.h"

namespace bant {
namespace {

// Use up some time to make it more likely to tickle the actual thread
// exeuction.
static void PretendWork(int ms) { absl::SleepFor(absl::Milliseconds(ms)); }

TEST(ThreadPoolTest, SynchronousExecutionIfThreadCountZero) {
  ThreadPool pool(0);
  std::future<int> foo_ture = pool.ExecAsync<int>([]() -> int {
    PretendWork(200);
    return 42;
  });

  EXPECT_EQ(std::future_status::ready, foo_ture.wait_for(1ms))  // NOLINT
    << "Must be available immediately after return";
  EXPECT_EQ(foo_ture.get(), 42);
}

TEST(ThreadPoolTest, WorkIsCompleted) {
  constexpr int kLoops = 100;
  ThreadPool pool(3);

  std::vector<std::future<int>> results;
  results.reserve(kLoops);
  for (int i = 0; i < kLoops; ++i) {
    results.emplace_back(pool.ExecAsync<int>([i]() -> int {
      PretendWork(10);
      return i;
    }));
  }

  // Can't easily make a blackbox test that verifies that the functions are
  // even executed in different threads, but at least let's verify that all
  // of them finish with the expected result.
  for (int i = 0; i < kLoops; ++i) {
    EXPECT_EQ(results[i].get(), i);
  }
}

}  // namespace
}  // namespace bant
