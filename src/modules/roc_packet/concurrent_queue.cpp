/*
 * Copyright (c) 2015 Mikhail Baranov
 * Copyright (c) 2015 Victor Gaydov
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_packet/concurrent_queue.h"

namespace roc {
namespace packet {

ConcurrentQueue::ConcurrentQueue()
    : sem_(0) {
}

PacketPtr ConcurrentQueue::read() {
    sem_.pend();

    core::Mutex::Lock lock(mutex_);

    PacketPtr packet = list_.front();
    if (packet) {
        list_.remove(*packet);
    }

    return packet;
}

void ConcurrentQueue::write(const PacketPtr& packet) {
    if (packet) {
        core::Mutex::Lock lock(mutex_);

        list_.push_back(*packet);
    }

    sem_.post();
}

} // namespace packet
} // namespace roc
