// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 Parth Sinha

// The claim in the paper's parallel section, measured rather than argued.
//
// It comes at two levels that are not the same claim, and a test that only
// checks the concatenated stream does not reach the second:
//
//   stream level      the concatenation of a partitioned run is the same bytes
//                     for every worker count
//   simulation level  task j draws the same words whatever worker runs it and
//                     whatever else is running
//
// A run can satisfy the first and fail the second, by concatenating to the
// same bytes while handing those bytes to different tasks. That is exactly
// what happens when the counter offset is derived from the worker index, which
// is the mapping a benchmark wants and the wrong one for a simulation. So the
// first test below maps counters from the *task* index and varies both the
// worker count and the schedule; the second covers the stream level.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "vphilox/philox.hpp"

using vphilox::counter4;
using vphilox::counter_advanced;
using vphilox::engine;
using vphilox::key2;
using vphilox::u32;
using vphilox::u64;

namespace {

// Prime, so no worker count under test divides it and the last round is always
// ragged. An even split would hide an off-by-one in the claim loop.
constexpr std::size_t kTasks = 97;

// The stride S of the paper's task-to-counter mapping. 16 blocks is 64 words,
// comfortably above any task's demand below, which is what keeps the intervals
// disjoint.
constexpr u64 kBlocksPerTask = 16;

constexpr u64 kSeed = 0x5EED'1234'ABCD'0001ull;

// Unequal lengths on purpose. With equal lengths a dynamic schedule hands out
// exactly what a static one hands out, and the schedule axis tests nothing.
std::size_t words_in(std::size_t task) {
    return 1 + (task * 37) % 53;
}

// The mapping under test: ctr(j, i) = ctr0 + j*S + i, a function of the task
// index alone. No worker identity appears anywhere in it.
std::vector<u32> run_task(key2 k, counter4 origin, std::size_t task) {
    engine g{k, counter_advanced(origin, task * kBlocksPerTask)};
    std::vector<u32> out(words_in(task));
    g.generate_n(out.data(), out.size());
    return out;
}

std::vector<std::vector<u32>> serial_reference(key2 k, counter4 origin) {
    std::vector<std::vector<u32>> ref(kTasks);
    for (std::size_t j = 0; j < kTasks; ++j) ref[j] = run_task(k, origin, j);
    return ref;
}

}  // namespace

TEST(ThreadReproducibility, TaskDrawsAreIndependentOfWorkerCountAndSchedule) {
    const key2 k{{0x1234'5678u, 0x9ABC'DEF0u}};
    const counter4 origin = counter_advanced(counter4{}, kSeed);

    const auto reference = serial_reference(k, origin);

    for (std::size_t workers : {1u, 2u, 3u, 5u, 8u, 13u}) {
        // Two schedules. The static one splits tasks into contiguous blocks.
        // The dynamic one has workers claim tasks one at a time from an atomic
        // counter, so which worker runs which task is decided by the operating
        // system and differs between runs of the same binary.
        for (bool dynamic_schedule : {false, true}) {
            std::vector<std::vector<u32>> got(kTasks);
            std::atomic<std::size_t> next{0};
            std::vector<std::thread> pool;
            pool.reserve(workers);

            for (std::size_t t = 0; t < workers; ++t) {
                pool.emplace_back([&, t] {
                    if (dynamic_schedule) {
                        for (std::size_t j = next.fetch_add(1); j < kTasks; j = next.fetch_add(1)) {
                            got[j] = run_task(k, origin, j);
                        }
                    } else {
                        const std::size_t span  = (kTasks + workers - 1) / workers;
                        const std::size_t begin = t * span;
                        const std::size_t end   = std::min(begin + span, kTasks);
                        for (std::size_t j = begin; j < end; ++j) {
                            got[j] = run_task(k, origin, j);
                        }
                    }
                });
            }
            for (auto& th : pool) th.join();

            for (std::size_t j = 0; j < kTasks; ++j) {
                ASSERT_EQ(got[j], reference[j])
                    << "task " << j << " drew different words with " << workers
                    << " workers under a " << (dynamic_schedule ? "dynamic" : "static")
                    << " schedule";
            }
        }
    }
}

TEST(ThreadReproducibility, PartitionedStreamConcatenatesIdenticallyForEveryWorkerCount) {
    const key2 k{{0x0F1E'2D3Cu, 0x4B5A'6978u}};
    const counter4 origin = counter_advanced(counter4{}, kSeed);

    // A fixed workload, split every way. Total blocks is what is held constant;
    // the number of workers it is cut into is what varies.
    constexpr u64 kTotalBlocks   = 240;
    constexpr std::size_t kWords = static_cast<std::size_t>(kTotalBlocks) * 4;

    // What one engine produces in a single call, which is what any split has to
    // reproduce.
    std::vector<u32> single(kWords);
    engine{k, origin}.generate_n(single.data(), single.size());

    for (u64 workers : {1u, 2u, 3u, 4u, 5u, 6u, 8u, 10u, 12u}) {
        ASSERT_EQ(kTotalBlocks % workers, 0u) << "pick divisors, or the arithmetic below lies";
        const u64 blocks = kTotalBlocks / workers;

        std::vector<u32> joined(kWords);
        std::vector<std::thread> pool;
        pool.reserve(workers);
        for (u64 t = 0; t < workers; ++t) {
            pool.emplace_back([&, t] {
                // ctr(t, i) = ctr0 + t*B + i -- the partition by worker index.
                engine g{k, counter_advanced(origin, t * blocks)};
                g.generate_n(joined.data() + t * blocks * 4, static_cast<std::size_t>(blocks) * 4);
            });
        }
        for (auto& th : pool) th.join();

        ASSERT_EQ(joined, single) << "the concatenation moved at " << workers << " workers";
    }
}
