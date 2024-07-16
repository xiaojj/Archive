// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_outgoing_queue.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace moqt {

void MoqtOutgoingQueue::AddObject(quiche::QuicheMemSlice payload, bool key) {
  if (queue_.empty() && !key) {
    QUICHE_BUG(MoqtOutgoingQueue_AddObject_first_object_not_key)
        << "The first object ever added to the queue must have the \"key\" "
           "flag.";
    return;
  }

  if (key) {
    if (!queue_.empty()) {
      CloseStreamForGroup(current_group_id_);
    }

    if (queue_.size() == kMaxQueuedGroups) {
      queue_.erase(queue_.begin());
    }
    queue_.emplace_back();
    ++current_group_id_;
  }

  absl::string_view payload_view = payload.AsStringView();
  uint64_t object_id = queue_.back().size();
  queue_.back().push_back(std::move(payload));
  PublishObject(current_group_id_, object_id, payload_view);
}

absl::StatusOr<MoqtOutgoingQueue::PublishPastObjectsCallback>
MoqtOutgoingQueue::OnSubscribeForPast(const SubscribeWindow& window) {
  QUICHE_BUG_IF(
      MoqtOutgoingQueue_requires_kGroup,
      window.forwarding_preference() != MoqtForwardingPreference::kGroup)
      << "MoqtOutgoingQueue currently only supports kGroup.";
  return [this, &window]() {
    for (size_t i = 0; i < queue_.size(); ++i) {
      const uint64_t group_id = first_group_in_queue() + i;
      const Group& group = queue_[i];
      const bool is_last_group =
          ((i == queue_.size() - 1) ||
           !window.InWindow(FullSequence{group_id + 1, 0}));
      for (size_t j = 0; j < group.size(); ++j) {
        const FullSequence sequence{group_id, j};
        if (!window.InWindow(sequence)) {
          continue;
        }
        const bool is_last_object = (j == group.size() - 1);
        PublishObject(group_id, j, group[j].AsStringView());
        if (!is_last_group && is_last_object) {
          // Close the group
          CloseStreamForGroup(group_id);
        }
      }
    }
  };
}

void MoqtOutgoingQueue::CloseStreamForGroup(uint64_t group_id) {
  session_->PublishObject(track_, group_id, queue_[group_id].size(),
                          /*object_send_order=*/group_id,
                          MoqtObjectStatus::kEndOfGroup, "");
  session_->CloseObjectStream(track_, group_id);
}

void MoqtOutgoingQueue::PublishObject(uint64_t group_id, uint64_t object_id,
                                      absl::string_view payload) {
  session_->PublishObject(track_, group_id, object_id,
                          /*object_send_order=*/group_id,
                          MoqtObjectStatus::kNormal, payload);
}

}  // namespace moqt
