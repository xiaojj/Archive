// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
#define QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// Classes to track subscriptions to local tracks: the sequence numbers
// subscribed, the streams involved, and the subscribe IDs.
class QUICHE_EXPORT SubscribeWindow {
 public:
  // Creates a half-open window.
  SubscribeWindow(uint64_t subscribe_id, uint64_t start_group,
                  uint64_t start_object)
      : subscribe_id_(subscribe_id), start_({start_group, start_object}) {}

  // Creates a closed window.
  SubscribeWindow(uint64_t subscribe_id, uint64_t start_group,
                  uint64_t start_object, uint64_t end_group,
                  uint64_t end_object)
      : subscribe_id_(subscribe_id),
        start_({start_group, start_object}),
        end_(FullSequence(end_group, end_object)) {}

  uint64_t subscribe_id() const { return subscribe_id_; }

  bool InWindow(const FullSequence& seq) const;

  // Returns the stream to send |sequence| on, if already opened.
  std::optional<webtransport::StreamId> GetStreamForSequence(
      FullSequence sequence,
      MoqtForwardingPreference forwarding_preference) const;

  // Records what stream is being used for a track, group, or object depending
  // on |forwarding_preference|. Triggers QUIC_BUG if already assigned.
  void AddStream(MoqtForwardingPreference forwarding_preference,
                 uint64_t group_id, uint64_t object_id,
                 webtransport::StreamId stream_id);

  void RemoveStream(MoqtForwardingPreference forwarding_preference,
                    uint64_t group_id, uint64_t object_id);

 private:
  struct GroupStreams {
    std::optional<webtransport::StreamId> group_stream;
    absl::flat_hash_map<uint64_t, webtransport::StreamId> object_streams;
  };
  const uint64_t subscribe_id_;
  const FullSequence start_;
  const std::optional<FullSequence> end_ = std::nullopt;
  // Open streams for this subscription
  std::optional<webtransport::StreamId> track_stream_;
  absl::flat_hash_map<uint64_t, GroupStreams> group_streams_;
};

// Class to keep track of the sequence number blocks to which a peer is
// subscribed.
class QUICHE_EXPORT MoqtSubscribeWindows {
 public:
  MoqtSubscribeWindows() {}

  // Returns a vector of subscribe IDs that apply to the object. They will be in
  // reverse order of the AddWindow calls.
  std::vector<SubscribeWindow*> SequenceIsSubscribed(FullSequence sequence);

  // |window| has already been converted into absolute sequence numbers. An
  // optimization could consolidate overlapping subscribe windows.
  void AddWindow(SubscribeWindow window) {
    windows_.emplace(window.subscribe_id(), window);
  }
  void RemoveWindow(uint64_t subscribe_id) { windows_.erase(subscribe_id); }

  bool IsEmpty() const { return windows_.empty(); }

  SubscribeWindow* GetWindow(uint64_t subscribe_id) {
    auto it = windows_.find(subscribe_id);
    if (it == windows_.end()) {
      return nullptr;
    }
    return &it->second;
  }

 private:
  // Indexed by Subscribe ID.
  absl::node_hash_map<uint64_t, SubscribeWindow> windows_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
