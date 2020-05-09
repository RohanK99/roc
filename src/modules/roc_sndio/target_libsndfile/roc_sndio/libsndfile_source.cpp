/*
 * Copyright (c) 2020 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_sndio/libsndfile_source.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"

namespace roc {
namespace sndio {

/**
 * Notes/Questions:
 * What should the files be named and the class had a hard time choosing
 * Can we phase out driver_name_ since we don't need it to read files
 * Can we remove the buffer since we can now write directly to frame because we directly read floats
 * Can we remove is_file_ and checks since we don't support devices in libsndfile
 * What to do about the config paramterest that aren't used eg. latency
 */

SndFile::SndFile(core::IAllocator& allocator, const Config& config)
    : driver_name_(allocator)
    , input_name_(allocator)
    , buffer_(allocator)
    , buffer_size_(0)
    , opened_(false)
    , is_file_(false)
    , eof_(false)
    , paused_(false)
    , valid_(false) {
    // SoxBackend::instance();

    n_channels_ = packet::num_channels(config.channels);
    if (n_channels_ == 0) {
        roc_log(LogError, "sndfile source: # of channels is zero");
        return;
    }

    if (config.latency != 0) {
        roc_log(LogError, "sndfile source: setting io latency not supported by sox backend");
        return;
    }

    frame_length_ = config.frame_length;
    channels_ = config.channels;
    sample_rate_ = config.sample_rate;

    if (frame_length_ == 0) {
        roc_log(LogError, "sox source: frame length is zero");
        return;
    }

    memset(&sinfo_, 0, sizeof(sinfo_));

    valid_ = true;
}

SndFile::~SndFile() {
    close_();
}

bool SndFile::valid() const {
    return valid_;
}

bool SndFile::open(const char* driver, const char* input) {
    roc_panic_if(!valid_);

    roc_log(LogInfo, "sndfile source: opening: driver=%s input=%s", driver, input);

    if (buffer_.size() != 0 || opened_) {
        roc_panic("sndfile source: can't call open() more than once");
    }

    if (!setup_names_(driver, input)) {
        return false;
    }

    if (!open_()) {
        return false;
    }

    if (!setup_buffer_()) {
        return false;
    }

    return true;
}

size_t SndFile::sample_rate() const {
    roc_panic_if(!valid_);

    if (!opened_) {
        roc_panic("sndfile source: sample_rate: non-open input file or device");
    }

    return sample_rate_;
}

size_t SndFile::num_channels() const {
    roc_panic_if(!valid_);

    if (!opened_) {
        roc_panic("sndfile source: num_samples: non-open input file or device");
    }

    return n_channels_;
}

bool SndFile::has_clock() const {
    roc_panic_if(!valid_);

    if (!opened_) {
        roc_panic("sndfile source: has_clock: non-open input file or device");
    }

    return !is_file_; // always false
}

ISource::State SndFile::state() const {
    roc_panic_if(!valid_);

    if (paused_) {
        return Paused;
    } else {
        return Playing;
    }
}

void SndFile::pause() {
    roc_panic_if(!valid_);

    if (paused_) {
        return;
    }

    if (!opened_) {
        roc_panic("sndfile source: pause: non-open input file or device");
    }

    roc_log(LogDebug, "sox source: pausing: driver=%s input=%s", driver_name_.c_str(), // TODO: do we have driver_names??? 
            input_name_.c_str());

    if (!is_file_) { // can delete since we don't care if its a file or not
        close_();
    }

    paused_ = true;
}

bool SndFile::resume() {
    roc_panic_if(!valid_);

    if (!paused_) {
        return true;
    }

    roc_log(LogDebug, "sndfile source: resuming: driver=%s input=%s", driver_name_.c_str(), // TODO: check for driver name
            input_name_.c_str());

    if (!opened_) {
        if (!open_()) {
            roc_log(LogError, "sndfile source: open failed when resuming: driver=%s input=%s",
                    driver_name_.c_str(), input_name_.c_str());
            return false;
        }
    }

    paused_ = false;
    return true;
}

bool SndFile::restart() {
    roc_panic_if(!valid_);

    roc_log(LogDebug, "sndfile source: restarting: driver=%s input=%s", driver_name_.c_str(), // check driver_name
            input_name_.c_str());

    if (is_file_ && !eof_) {
        if (!seek_(0)) {
            roc_log(LogError,
                    "sndfile source: seek failed when restarting: driver=%s input=%s", // check driver_name
                    driver_name_.c_str(), input_name_.c_str());
            return false;
        }
    } else {
        if (opened_) {
            close_();
        }

        if (!open_()) {
            roc_log(LogError,
                    "sndfile source: open failed when restarting: driver=%s input=%s",
                    driver_name_.c_str(), input_name_.c_str()); // check driver name
            return false;
        }
    }

    paused_ = false;
    eof_ = false;

    return true;
}

bool SndFile::read(audio::Frame& frame) {
    roc_panic_if(!valid_);

    if (paused_ || eof_) {
        return false;
    }

    if (!opened_) {
        roc_panic("sndfile source: read: non-open input file or device");
    }

    audio::sample_t* frame_data = frame.data();
    size_t frame_left = frame.size();

    // we can probably phase out the need for this buffer since the frame data seems like it could be the new buffer
    audio::sample_t* buffer_data = buffer_.data();

    size_t clips = 0;

    while (frame_left != 0) {
        sf_count_t n_samples = frame_left;
        if (n_samples > buffer_size_) {
            n_samples = buffer_size_;
        }

        n_samples = sf_readf_float(sfile_, buffer_data, n_samples);
        if (n_samples == 0) {
            roc_log(LogDebug, "sndfile source: got eof from sndfile");
            eof_ = true;
            break;
        }

        for (size_t n = 0; n < n_samples; n++) {
            frame_data[n] = buffer_data[n];
        }

        frame_data += n_samples;
        frame_left -= n_samples;
    }

    if (frame_left == frame.size()) {
        return false;
    }

    if (frame_left != 0) {
        memset(frame_data, 0, frame_left * sizeof(audio::sample_t));
    }

    return true;
}

bool SndFile::seek_(uint64_t offset) {
    roc_panic_if(!valid_);

    if (!opened_) {
        roc_panic("sndfile source: seek: non-open input file or device");
    }

    if (!is_file_) { // probably remove
        roc_panic("sox source: seek: not a file");
    }

    roc_log(LogDebug, "sndfile source: resetting position to %lu", (unsigned long)offset);

    int err = sf_seek(sfile_, (sf_count_t)offset, SEEK_SET);
    if (err != SF_ERR_NO_ERROR) {
        roc_log(LogError, "sndfile source: can't reset position to %lu: %s",
                (unsigned long)offset, sf_strerror(sfile_));
        return false;
    }

    return true;
}

bool SndFile::setup_names_(const char* driver, const char* input) {
    if (driver) {
        if (!driver_name_.set_str(driver)) {
            roc_log(LogError, "sndfile source: can't allocate string");
            return false;
        }
    }

    if (input) {
        if (!input_name_.set_str(input)) {
            roc_log(LogError, "sndfile source: can't allocate string");
            return false;
        }
    }

    return true;
}

bool SndFile::setup_buffer_() {
    buffer_size_ = packet::ns_to_size(frame_length_, sample_rate(), channels_);
    if (buffer_size_ == 0) {
        roc_log(LogError, "sndfile source: buffer size is zero");
        return false;
    }
    if (!buffer_.resize(buffer_size_)) {
        roc_log(LogError, "sndfile source: can't allocate sample buffer");
        return false;
    }

    return true;
}

bool SndFile::open_() {
    if (opened_) {
        roc_panic("sndfile source: already opened");
    }

    if ((sfile_ = sf_open(input_name_.is_empty() ? NULL : input_name_.c_str(), SFM_READ, &sinfo_)) == NULL) {
        roc_log(LogError, "sndfile source: can't open: input=%s due to error %s", input_name_.c_str(), sf_strerror(sfile_));     
        sf_close (sfile_);
        return false;
    }

    // temporarily set is_file_ to true phase out since all libsndfile formats are files
    is_file_ = true;

    if (sinfo_.channels != n_channels_) {
        roc_log(LogError, "sndfile source: can't open: unsupported # of channels: "
                          "expected=%lu actual=%lu",
                          (unsigned long)n_channels_, (unsigned long)sinfo_.channels);
        return false;
    }

    if (sinfo_.samplerate != sample_rate_) {
        roc_log(LogError, "sndfile source: can't open: unsupported sample rate: "
                          "expected=%lu actual=%lu",
                          (unsigned long)sample_rate_, (unsigned long)sinfo_.samplerate);
        return false;
    }

    return true;
}

void SndFile::close_() {
    if (!opened_) {
        return;
    }

    roc_log(LogInfo, "sndfile source: closing input");

    int err = sf_close(sfile_);
    if (err != SF_ERR_NO_ERROR) {
        roc_panic("sndfile source: can't close input: %s", sf_strerror(sfile_));
    }

    sfile_ = NULL;
}

} // namespace sndio
} // namespace roc
