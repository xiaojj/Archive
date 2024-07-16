// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_subscribe_windows.h"

#include <cstdint>
#include <optional>

#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/platform/api/quic_expect_bug.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace moqt {

namespace test {

class QUICHE_EXPORT SubscribeWindowTest : public quic::test::QuicTest {
 public:
  SubscribeWindowTest() {}

  const uint64_t subscribe_id_ = 2;
  const FullSequence right_edge_{4, 5};
  const FullSequence start_{4, 0};
  const FullSequence end_{5, 5};
};

TEST_F(SubscribeWindowTest, Queries) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         right_edge_, start_, end_);
  EXPECT_EQ(window.subscribe_id(), 2);
  EXPECT_TRUE(window.InWindow(FullSequence(4, 0)));
  EXPECT_TRUE(window.InWindow(FullSequence(5, 5)));
  EXPECT_FALSE(window.InWindow(FullSequence(5, 6)));
  EXPECT_FALSE(window.InWindow(FullSequence(6, 0)));
  EXPECT_FALSE(window.InWindow(FullSequence(3, 12)));
}

TEST_F(SubscribeWindowTest, AddQueryRemoveStreamIdTrack) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kTrack,
                         right_edge_, start_, end_);
  window.AddStream(4, 0, 2);
  EXPECT_QUIC_BUG(window.AddStream(5, 2, 6), "Stream already added");
  EXPECT_EQ(*window.GetStreamForSequence(FullSequence(5, 2)), 2);
  window.RemoveStream(7, 2);
  EXPECT_FALSE(window.GetStreamForSequence(FullSequence(4, 0)).has_value());
}

TEST_F(SubscribeWindowTest, AddQueryRemoveStreamIdGroup) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kGroup,
                         right_edge_, start_, end_);
  window.AddStream(4, 0, 2);
  EXPECT_FALSE(window.GetStreamForSequence(FullSequence(5, 0)).has_value());
  window.AddStream(5, 2, 6);
  EXPECT_QUIC_BUG(window.AddStream(5, 3, 6), "Stream already added");
  EXPECT_EQ(*window.GetStreamForSequence(FullSequence(4, 1)), 2);
  EXPECT_EQ(*window.GetStreamForSequence(FullSequence(5, 0)), 6);
  window.RemoveStream(5, 1);
  EXPECT_FALSE(window.GetStreamForSequence(FullSequence(5, 2)).has_value());
}

TEST_F(SubscribeWindowTest, AddQueryRemoveStreamIdObject) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         right_edge_, start_, end_);
  window.AddStream(4, 0, 2);
  window.AddStream(4, 1, 6);
  window.AddStream(4, 2, 10);
  EXPECT_QUIC_BUG(window.AddStream(4, 2, 14), "Stream already added");
  EXPECT_EQ(*window.GetStreamForSequence(FullSequence(4, 0)), 2);
  EXPECT_EQ(*window.GetStreamForSequence(FullSequence(4, 2)), 10);
  EXPECT_FALSE(window.GetStreamForSequence(FullSequence(4, 4)).has_value());
  EXPECT_FALSE(window.GetStreamForSequence(FullSequence(5, 0)).has_value());
  window.RemoveStream(4, 2);
  EXPECT_FALSE(window.GetStreamForSequence(FullSequence(4, 2)).has_value());
}

TEST_F(SubscribeWindowTest, AddQueryRemoveStreamIdDatagram) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kDatagram,
                         right_edge_, start_, end_);
  EXPECT_QUIC_BUG(window.AddStream(4, 0, 2), "Adding a stream for datagram");
}

TEST_F(SubscribeWindowTest, OnObjectSent) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         right_edge_, start_, end_);
  EXPECT_FALSE(window.largest_delivered().has_value());
  EXPECT_FALSE(
      window.OnObjectSent(FullSequence(4, 1), MoqtObjectStatus::kNormal));
  EXPECT_TRUE(window.largest_delivered().has_value());
  EXPECT_EQ(window.largest_delivered().value(), FullSequence(4, 1));
  EXPECT_FALSE(
      window.OnObjectSent(FullSequence(4, 2), MoqtObjectStatus::kNormal));
  EXPECT_EQ(window.largest_delivered().value(), FullSequence(4, 2));
  EXPECT_FALSE(
      window.OnObjectSent(FullSequence(4, 0), MoqtObjectStatus::kNormal));
  EXPECT_EQ(window.largest_delivered().value(), FullSequence(4, 2));
}

TEST_F(SubscribeWindowTest, AllObjectsUnpublishedAtStart) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         FullSequence(0, 0), FullSequence(0, 0),
                         FullSequence(0, 1));
  EXPECT_FALSE(
      window.OnObjectSent(FullSequence(0, 0), MoqtObjectStatus::kNormal));
  EXPECT_TRUE(
      window.OnObjectSent(FullSequence(0, 1), MoqtObjectStatus::kNormal));
}

TEST_F(SubscribeWindowTest, AllObjectsPublishedAtStart) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         FullSequence(4, 0), FullSequence(0, 0),
                         FullSequence(0, 1));
  EXPECT_FALSE(
      window.OnObjectSent(FullSequence(0, 0), MoqtObjectStatus::kNormal));
  EXPECT_TRUE(
      window.OnObjectSent(FullSequence(0, 1), MoqtObjectStatus::kNormal));
}

TEST_F(SubscribeWindowTest, SomeObjectsUnpublishedAtStart) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         FullSequence(0, 1), FullSequence(0, 0),
                         FullSequence(0, 1));
  EXPECT_FALSE(
      window.OnObjectSent(FullSequence(0, 0), MoqtObjectStatus::kNormal));
  EXPECT_TRUE(
      window.OnObjectSent(FullSequence(0, 1), MoqtObjectStatus::kNormal));
}

TEST_F(SubscribeWindowTest, UpdateStartEnd) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         right_edge_, start_, end_);
  EXPECT_TRUE(window.UpdateStartEnd(start_.next(),
                                    FullSequence(end_.group, end_.object - 1)));
  EXPECT_FALSE(window.InWindow(FullSequence(start_.group, start_.object)));
  EXPECT_FALSE(window.InWindow(FullSequence(end_.group, end_.object)));
  EXPECT_FALSE(
      window.UpdateStartEnd(start_, FullSequence(end_.group, end_.object - 1)));
  EXPECT_FALSE(window.UpdateStartEnd(start_.next(), end_));
}

TEST_F(SubscribeWindowTest, UpdateStartEndOpenEnded) {
  SubscribeWindow window(subscribe_id_, MoqtForwardingPreference::kObject,
                         right_edge_, start_, std::nullopt);
  EXPECT_TRUE(window.UpdateStartEnd(start_, end_));
  EXPECT_FALSE(window.InWindow(end_.next()));
  EXPECT_FALSE(window.UpdateStartEnd(start_, std::nullopt));
}

class QUICHE_EXPORT MoqtSubscribeWindowsTest : public quic::test::QuicTest {
 public:
  MoqtSubscribeWindowsTest() : windows_(MoqtForwardingPreference::kObject) {}
  MoqtSubscribeWindows windows_;
};

TEST_F(MoqtSubscribeWindowsTest, IsEmpty) {
  EXPECT_TRUE(windows_.IsEmpty());
  windows_.AddWindow(0, FullSequence(2, 1), 1, 3);
  EXPECT_FALSE(windows_.IsEmpty());
}

TEST_F(MoqtSubscribeWindowsTest, IsSubscribed) {
  EXPECT_TRUE(windows_.IsEmpty());
  // The first two windows overlap; the third is open-ended.
  windows_.AddWindow(0, FullSequence(0, 0), 1, 0, 3, 9);
  windows_.AddWindow(1, FullSequence(0, 0), 2, 4, 4, 3);
  windows_.AddWindow(2, FullSequence(0, 0), 10, 0);
  EXPECT_FALSE(windows_.IsEmpty());
  EXPECT_TRUE(windows_.SequenceIsSubscribed(FullSequence(0, 8)).empty());
  auto hits = windows_.SequenceIsSubscribed(FullSequence(1, 0));
  EXPECT_EQ(hits.size(), 1);
  EXPECT_EQ(hits[0]->subscribe_id(), 0);
  EXPECT_TRUE(windows_.SequenceIsSubscribed(FullSequence(4, 4)).empty());
  EXPECT_TRUE(windows_.SequenceIsSubscribed(FullSequence(8, 3)).empty());
  hits = windows_.SequenceIsSubscribed(FullSequence(100, 7));
  EXPECT_EQ(hits.size(), 1);
  EXPECT_EQ(hits[0]->subscribe_id(), 2);
  hits = windows_.SequenceIsSubscribed(FullSequence(3, 0));
  EXPECT_EQ(hits.size(), 2);
  EXPECT_EQ(hits[0]->subscribe_id() + hits[1]->subscribe_id(), 1);
}

TEST_F(MoqtSubscribeWindowsTest, AddGetRemoveWindow) {
  windows_.AddWindow(0, FullSequence(2, 5), 1, 0, 3, 9);
  SubscribeWindow* window = windows_.GetWindow(0);
  EXPECT_EQ(window->subscribe_id(), 0);
  EXPECT_EQ(windows_.GetWindow(1), nullptr);
  windows_.RemoveWindow(0);
  EXPECT_EQ(windows_.GetWindow(0), nullptr);
}

}  // namespace test

}  // namespace moqt
