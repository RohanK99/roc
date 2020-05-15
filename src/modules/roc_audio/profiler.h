/*
 * Copyright (c) 2019 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_audio/profiler.h
//! @brief Profiler.

#ifndef ROC_AUDIO_PROFILER_H_
#define ROC_AUDIO_PROFILER_H_

#include "roc_audio/frame.h"
#include "roc_core/array.h"
#include "roc_core/iallocator.h"
#include "roc_core/list.h"
#include "roc_core/noncopyable.h"
#include "roc_core/rate_limiter.h"
#include "roc_core/refcnt.h"
#include "roc_core/time.h"
#include "roc_packet/units.h"

namespace roc {
namespace audio {

//! Profiler
//! The role of the profiler is to report the average processing speed (# of samples
//! processed per time unit) during the last N seconds. We want to calculate the average
//! processing speed efficiently (with O(1) complexity, without allocations, and as
//! lightweight as possible). The problems with this are that the we have variable-sized
//! frames and SMA requires fixed size chunks. To efficiently perform this calculation a
//! ring buffer is employed. The idea behind the ring buffer is that each chunk of the
//! buffer is the average speed of 10ms worth of samples. The ring buffer is initialized
//! with fixed size (N * 1000)ms / (10ms) chunks. Within each chunk a weighted mean is
//! used to calculate the average speed during those 10ms. Each frame will contribute a
//! different number of samples to each chunk, the chunk speed is then weighted based on
//! how many samples are contributed at what frame speed. As the chunks get populated the
//! moving average is calculated. When the buffer is not entirely full the cumulative
//! moving average algorithm is used and once the buffer is full the simple moving average
//! algorithm is used.
class Profiler : public core::NonCopyable<> {
public:
    //! Initialization.
    Profiler(core::IAllocator& allocator,
             packet::channel_mask_t channels,
             size_t sample_rate,
             core::nanoseconds_t interval);

    //! Check if the profiler was succefully constructed.
    bool valid() const;

    //! Profile frame speed
    void add_frame(size_t frame_size, core::nanoseconds_t elapsed);

    //! For Testing Only
    double get_moving_avg() {
        return moving_avg_;
    }

private:
    core::RateLimiter rate_limiter_;

    core::nanoseconds_t interval_;

    const size_t chunk_length_;
    const size_t num_chunks_;
    core::Array<double> chunks_;
    size_t first_chunk_num_;
    size_t last_chunk_num_;
    size_t last_chunk_samples_;

    double moving_avg_;

    const size_t sample_rate_;
    const size_t num_channels_;

    bool valid_;
};

} // namespace audio
} // namespace roc

#endif // ROC_AUDIO_PROFILER_H_