/*
 * Copyright (c) 2018 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/profiling_writer.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"

namespace roc {
namespace audio {

ProfilingWriter::ProfilingWriter(IWriter& writer,
                                 core::IAllocator& allocator,
                                 packet::channel_mask_t channels,
                                 size_t sample_rate,
                                 core::nanoseconds_t interval)
    : profiler_(allocator, channels, sample_rate, interval)
    , writer_(writer) {
}

void ProfilingWriter::write(Frame& frame) {
    const core::nanoseconds_t elapsed = write_(frame);

    profiler_.add_frame(frame.size(), elapsed);
}

core::nanoseconds_t ProfilingWriter::write_(Frame& frame) {
    const core::nanoseconds_t start = core::timestamp();

    writer_.write(frame);

    return core::timestamp() - start;
}

} // namespace audio
} // namespace roc
