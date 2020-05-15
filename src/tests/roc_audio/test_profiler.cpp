/*
 * Copyright (c) 2019 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <CppUTest/TestHarness.h>

#include "roc_audio/profiler.h"
#include "roc_core/heap_allocator.h"

namespace roc {
namespace audio {

namespace {
struct TestFrame {
    TestFrame(size_t sz, core::nanoseconds_t tm)
        : size(sz)
        , time(tm) {
    }

    size_t size;
    core::nanoseconds_t time;
};

const core::nanoseconds_t interval = 50 * core::Millisecond; // 5 chunks
const int sample_rate = 5000;                                // 50 samples / chunk
const int num_channels = 1;
core::HeapAllocator allocator;

} // namespace

TEST_GROUP(profiler) {};

TEST(profiler, test_moving_average) {
    Profiler profiler(allocator, num_channels, sample_rate, interval);

    TestFrame frames[] = {
        TestFrame(50, 50 * core::Second),      TestFrame(25, 25 * core::Second),
        TestFrame(25, 25 * core::Second),      TestFrame(25, 25 * core::Second),
        TestFrame(25, 25 * core::Second / 2),  TestFrame(40, 40 * core::Second),
        TestFrame(60, 60 * core::Second / 3),  TestFrame(50, 50 * core::Second),
        TestFrame(125, 125 * core::Second / 3)
    };

    double frame_speeds[ROC_ARRAY_SIZE(frames)];
    // populate frame speeds
    for (size_t i = 0; i < ROC_ARRAY_SIZE(frames); ++i) {
        frame_speeds[i] =
            double(frames[i].size * core::Second) / frames[i].time / num_channels;
    }

    double expected_average[ROC_ARRAY_SIZE(frames)];
    expected_average[0] = (frame_speeds[0]) / 1;

    // 2nd chunk not full
    expected_average[1] = (frame_speeds[0]) / 1;

    // second chunk populated
    expected_average[2] =
        (frame_speeds[0] + (0.5 * frame_speeds[1] + 0.5 * frame_speeds[2])) / 2;

    // 3rd chunk not populated
    expected_average[3] =
        (frame_speeds[0] + (0.5 * frame_speeds[1] + 0.5 * frame_speeds[2])) / 2;

    // 3rd chunk full
    expected_average[4] =
        (frame_speeds[0] + (0.5 * frame_speeds[1] + 0.5 * frame_speeds[2])
         + (0.5 * frame_speeds[3] + 0.5 * frame_speeds[4]))
        / 3;

    // 4th chunk not fully populated
    expected_average[5] =
        (frame_speeds[0] + (0.5 * frame_speeds[1] + 0.5 * frame_speeds[2])
         + (0.5 * frame_speeds[3] + 0.5 * frame_speeds[4]))
        / 3;

    // 4th and 5th chunk filled
    expected_average[6] =
        (frame_speeds[0] + (0.5 * frame_speeds[1] + 0.5 * frame_speeds[2])
         + (0.5 * frame_speeds[3] + 0.5 * frame_speeds[4])
         + (0.8 * frame_speeds[5] + 0.2 * frame_speeds[6]) + frame_speeds[6])
        / 5;

    // 1st chunk overwritten
    expected_average[7] = ((0.5 * frame_speeds[1] + 0.5 * frame_speeds[2])
                           + (0.5 * frame_speeds[3] + 0.5 * frame_speeds[4])
                           + (0.8 * frame_speeds[5] + 0.2 * frame_speeds[6])
                           + frame_speeds[6] + frame_speeds[7])
        / 5;

    // 2nd and 3rd chunk overwritten 4th partially filled
    expected_average[8] = ((0.8 * frame_speeds[5] + 0.2 * frame_speeds[6])
                           + frame_speeds[6] + frame_speeds[7] + frame_speeds[8] * 2)
        / 5;

    for (size_t i = 0; i < ROC_ARRAY_SIZE(frames); ++i) {
        profiler.add_frame(frames[i].size, frames[i].time);
        LONGS_EQUAL(expected_average[i], profiler.get_moving_avg());
    }
}

} // namespace audio
} // namespace roc