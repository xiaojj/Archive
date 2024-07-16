// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_session.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/web_transport/web_transport.h"

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "MoQT Server: " : "MoQT Client: ")

namespace moqt {

using ::quic::Perspective;

MoqtSession::ControlStream* MoqtSession::GetControlStream() {
  if (!control_stream_.has_value()) {
    return nullptr;
  }
  webtransport::Stream* raw_stream = session_->GetStreamById(*control_stream_);
  if (raw_stream == nullptr) {
    return nullptr;
  }
  return static_cast<ControlStream*>(raw_stream->visitor());
}

void MoqtSession::SendControlMessage(quiche::QuicheBuffer message) {
  ControlStream* control_stream = GetControlStream();
  if (control_stream == nullptr) {
    QUICHE_LOG(DFATAL) << "Trying to send a message on the control stream "
                          "while it does not exist";
    return;
  }
  control_stream->SendOrBufferMessage(std::move(message));
}

void MoqtSession::OnSessionReady() {
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session ready";
  if (parameters_.perspective == Perspective::IS_SERVER) {
    return;
  }

  webtransport::Stream* control_stream =
      session_->OpenOutgoingBidirectionalStream();
  if (control_stream == nullptr) {
    Error(MoqtError::kInternalError, "Unable to open a control stream");
    return;
  }
  control_stream->SetVisitor(
      std::make_unique<ControlStream>(this, control_stream));
  control_stream_ = control_stream->GetStreamId();
  MoqtClientSetup setup = MoqtClientSetup{
      .supported_versions = std::vector<MoqtVersion>{parameters_.version},
      .role = MoqtRole::kPubSub,
  };
  if (!parameters_.using_webtrans) {
    setup.path = parameters_.path;
  }
  SendControlMessage(framer_.SerializeClientSetup(setup));
  QUIC_DLOG(INFO) << ENDPOINT << "Send the SETUP message";
}

void MoqtSession::OnSessionClosed(webtransport::SessionErrorCode,
                                  const std::string& error_message) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session closed with message: "
                    << error_message;
  error_ = error_message;
  std::move(callbacks_.session_terminated_callback)(error_message);
}

void MoqtSession::OnIncomingBidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingBidirectionalStream()) {
    if (control_stream_.has_value()) {
      Error(MoqtError::kProtocolViolation, "Bidirectional stream already open");
      return;
    }
    stream->SetVisitor(std::make_unique<ControlStream>(this, stream));
    stream->visitor()->OnCanRead();
  }
}
void MoqtSession::OnIncomingUnidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingUnidirectionalStream()) {
    stream->SetVisitor(std::make_unique<IncomingDataStream>(this, stream));
    stream->visitor()->OnCanRead();
  }
}

void MoqtSession::OnDatagramReceived(absl::string_view datagram) {
  MoqtObject message;
  absl::string_view payload = MoqtParser::ProcessDatagram(datagram, message);
  if (payload.empty()) {
    Error(MoqtError::kProtocolViolation, "Malformed datagram");
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT
                    << "Received OBJECT message in datagram for subscribe_id "
                    << message.subscribe_id << " for track alias "
                    << message.track_alias << " with sequence "
                    << message.group_id << ":" << message.object_id
                    << " send_order " << message.object_send_order << " length "
                    << payload.size();
  auto [full_track_name, visitor] = TrackPropertiesFromAlias(message);
  if (visitor != nullptr) {
    visitor->OnObjectFragment(full_track_name, message.group_id,
                              message.object_id, message.object_send_order,
                              message.object_status,
                              message.forwarding_preference, payload, true);
  }
}

void MoqtSession::Error(MoqtError code, absl::string_view error) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "MOQT session closed with code: "
                    << static_cast<int>(code) << " and message: " << error;
  error_ = std::string(error);
  session_->CloseSession(static_cast<uint64_t>(code), error);
  std::move(callbacks_.session_terminated_callback)(error);
}

void MoqtSession::AddLocalTrack(const FullTrackName& full_track_name,
                                MoqtForwardingPreference forwarding_preference,
                                LocalTrack::Visitor* visitor) {
  local_tracks_.try_emplace(full_track_name, full_track_name,
                            forwarding_preference, visitor);
}

// TODO: Create state that allows ANNOUNCE_OK/ERROR on spurious namespaces to
// trigger session errors.
void MoqtSession::Announce(absl::string_view track_namespace,
                           MoqtOutgoingAnnounceCallback announce_callback) {
  if (peer_role_ == MoqtRole::kPublisher) {
    std::move(announce_callback)(
        track_namespace,
        MoqtAnnounceErrorReason{MoqtAnnounceErrorCode::kInternalError,
                                "ANNOUNCE cannot be sent to Publisher"});
    return;
  }
  if (pending_outgoing_announces_.contains(track_namespace)) {
    std::move(announce_callback)(
        track_namespace,
        MoqtAnnounceErrorReason{
            MoqtAnnounceErrorCode::kInternalError,
            "ANNOUNCE message already outstanding for namespace"});
    return;
  }
  MoqtAnnounce message;
  message.track_namespace = track_namespace;
  SendControlMessage(framer_.SerializeAnnounce(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent ANNOUNCE message for "
                  << message.track_namespace;
  pending_outgoing_announces_[track_namespace] = std::move(announce_callback);
}

bool MoqtSession::HasSubscribers(const FullTrackName& full_track_name) const {
  auto it = local_tracks_.find(full_track_name);
  return (it != local_tracks_.end() && it->second.HasSubscriber());
}

void MoqtSession::CancelAnnounce(absl::string_view track_namespace) {
  for (auto it = local_tracks_.begin(); it != local_tracks_.end(); ++it) {
    if (it->first.track_namespace == track_namespace) {
      it->second.set_announce_cancel();
    }
  }
  absl::erase_if(local_tracks_, [&](const auto& it) {
    return it.first.track_namespace == track_namespace &&
           !it.second.HasSubscriber();
  });
}

bool MoqtSession::SubscribeAbsolute(absl::string_view track_namespace,
                                    absl::string_view name,
                                    uint64_t start_group, uint64_t start_object,
                                    RemoteTrack::Visitor* visitor,
                                    absl::string_view auth_info) {
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  message.start_group = start_group;
  message.start_object = start_object;
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeAbsolute(absl::string_view track_namespace,
                                    absl::string_view name,
                                    uint64_t start_group, uint64_t start_object,
                                    uint64_t end_group,
                                    RemoteTrack::Visitor* visitor,
                                    absl::string_view auth_info) {
  if (end_group < start_group) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  message.start_group = start_group;
  message.start_object = start_object;
  message.end_group = end_group;
  message.end_object = std::nullopt;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeAbsolute(absl::string_view track_namespace,
                                    absl::string_view name,
                                    uint64_t start_group, uint64_t start_object,
                                    uint64_t end_group, uint64_t end_object,
                                    RemoteTrack::Visitor* visitor,
                                    absl::string_view auth_info) {
  if (end_group < start_group) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  if (end_group == start_group && end_object < start_object) {
    QUIC_DLOG(ERROR) << "Subscription end is before beginning";
    return false;
  }
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  message.start_group = start_group;
  message.start_object = start_object;
  message.end_group = end_group;
  message.end_object = end_object;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeCurrentObject(absl::string_view track_namespace,
                                         absl::string_view name,
                                         RemoteTrack::Visitor* visitor,
                                         absl::string_view auth_info) {
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  message.start_group = std::nullopt;
  message.start_object = std::nullopt;
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeCurrentGroup(absl::string_view track_namespace,
                                        absl::string_view name,
                                        RemoteTrack::Visitor* visitor,
                                        absl::string_view auth_info) {
  MoqtSubscribe message;
  message.track_namespace = track_namespace;
  message.track_name = name;
  // First object of current group.
  message.start_group = std::nullopt;
  message.start_object = 0;
  message.end_group = std::nullopt;
  message.end_object = std::nullopt;
  if (!auth_info.empty()) {
    message.authorization_info = std::move(auth_info);
  }
  return Subscribe(message, visitor);
}

bool MoqtSession::SubscribeIsDone(uint64_t subscribe_id, SubscribeDoneCode code,
                                  absl::string_view reason_phrase) {
  // Search all the tracks to find the subscribe ID.
  auto name_it = local_track_by_subscribe_id_.find(subscribe_id);
  if (name_it == local_track_by_subscribe_id_.end()) {
    return false;
  }
  auto track_it = local_tracks_.find(name_it->second);
  if (track_it == local_tracks_.end()) {
    return false;
  }
  LocalTrack& track = track_it->second;
  MoqtSubscribeDone subscribe_done;
  subscribe_done.subscribe_id = subscribe_id;
  subscribe_done.status_code = code;
  subscribe_done.reason_phrase = reason_phrase;
  SubscribeWindow* window = track.GetWindow(subscribe_id);
  if (window == nullptr) {
    return false;
  }
  subscribe_done.final_id = window->largest_delivered();
  SendControlMessage(framer_.SerializeSubscribeDone(subscribe_done));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE_DONE message for "
                  << subscribe_id;
  // Clean up the subscription
  track.DeleteWindow(subscribe_id);
  local_track_by_subscribe_id_.erase(name_it);
  if (track.canceled() && !track.HasSubscriber()) {
    local_tracks_.erase(track_it);
  }
  return true;
}

bool MoqtSession::Subscribe(MoqtSubscribe& message,
                            RemoteTrack::Visitor* visitor) {
  if (peer_role_ == MoqtRole::kSubscriber) {
    QUIC_DLOG(INFO) << ENDPOINT << "Tried to send SUBSCRIBE to subscriber peer";
    return false;
  }
  // TODO(martinduke): support authorization info
  message.subscribe_id = next_subscribe_id_++;
  FullTrackName ftn(std::string(message.track_namespace),
                    std::string(message.track_name));
  auto it = remote_track_aliases_.find(ftn);
  if (it != remote_track_aliases_.end()) {
    message.track_alias = it->second;
    if (message.track_alias >= next_remote_track_alias_) {
      next_remote_track_alias_ = message.track_alias + 1;
    }
  } else {
    message.track_alias = next_remote_track_alias_++;
  }
  SendControlMessage(framer_.SerializeSubscribe(message));
  QUIC_DLOG(INFO) << ENDPOINT << "Sent SUBSCRIBE message for "
                  << message.track_namespace << ":" << message.track_name;
  active_subscribes_.try_emplace(message.subscribe_id, message, visitor);
  return true;
}

std::optional<webtransport::StreamId> MoqtSession::OpenUnidirectionalStream() {
  if (!session_->CanOpenNextOutgoingUnidirectionalStream()) {
    return std::nullopt;
  }
  webtransport::Stream* new_stream =
      session_->OpenOutgoingUnidirectionalStream();
  if (new_stream == nullptr) {
    return std::nullopt;
  }
  new_stream->SetVisitor(
      std::make_unique<OutgoingDataStream>(this, new_stream));
  return new_stream->GetStreamId();
}

std::pair<FullTrackName, RemoteTrack::Visitor*>
MoqtSession::TrackPropertiesFromAlias(const MoqtObject& message) {
  auto it = remote_tracks_.find(message.track_alias);
  RemoteTrack::Visitor* visitor = nullptr;
  if (it == remote_tracks_.end()) {
    // SUBSCRIBE_OK has not arrived yet, but deliver it.
    auto subscribe_it = active_subscribes_.find(message.subscribe_id);
    if (subscribe_it == active_subscribes_.end()) {
      return std::pair<FullTrackName, RemoteTrack::Visitor*>(
          {{"", ""}, nullptr});
    }
    ActiveSubscribe& subscribe = subscribe_it->second;
    visitor = subscribe.visitor;
    subscribe.received_object = true;
    if (subscribe.forwarding_preference.has_value()) {
      if (message.forwarding_preference != *subscribe.forwarding_preference) {
        Error(MoqtError::kProtocolViolation,
              "Forwarding preference changes mid-track");
        return std::pair<FullTrackName, RemoteTrack::Visitor*>(
            {{"", ""}, nullptr});
      }
    } else {
      subscribe.forwarding_preference = message.forwarding_preference;
    }
    return std::pair<FullTrackName, RemoteTrack::Visitor*>(
        {{subscribe.message.track_namespace, subscribe.message.track_name},
         subscribe.visitor});
  }
  RemoteTrack& track = it->second;
  if (!track.CheckForwardingPreference(message.forwarding_preference)) {
    // Incorrect forwarding preference.
    Error(MoqtError::kProtocolViolation,
          "Forwarding preference changes mid-track");
    return std::pair<FullTrackName, RemoteTrack::Visitor*>({{"", ""}, nullptr});
  }
  return std::pair<FullTrackName, RemoteTrack::Visitor*>(
      {{track.full_track_name().track_namespace,
        track.full_track_name().track_name},
       track.visitor()});
}

// TODO(martinduke): Throw errors if the object status is inconsistent with
// sequence numbers we have already observed on the track.
bool MoqtSession::PublishObject(const FullTrackName& full_track_name,
                                uint64_t group_id, uint64_t object_id,
                                uint64_t object_send_order,
                                MoqtObjectStatus status,
                                absl::string_view payload) {
  auto track_it = local_tracks_.find(full_track_name);
  if (track_it == local_tracks_.end()) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT for nonexistent track";
    return false;
  }
  // TODO(martinduke): Write a test for this QUIC_BUG.
  QUIC_BUG_IF(moqt_publish_abnormal_with_payload,
              status != MoqtObjectStatus::kNormal && !payload.empty());
  LocalTrack& track = track_it->second;
  bool end_of_stream = false;
  MoqtForwardingPreference forwarding_preference =
      track.forwarding_preference();
  switch (forwarding_preference) {
    case MoqtForwardingPreference::kTrack:
      end_of_stream = (status == MoqtObjectStatus::kEndOfTrack);
      break;
    case MoqtForwardingPreference::kObject:
    case MoqtForwardingPreference::kDatagram:
      end_of_stream = true;
      break;
    case MoqtForwardingPreference::kGroup:
      end_of_stream = (status == MoqtObjectStatus::kEndOfGroup ||
                       status == MoqtObjectStatus::kGroupDoesNotExist ||
                       status == MoqtObjectStatus::kEndOfTrack);
      break;
  }
  FullSequence sequence{group_id, object_id};
  track.SentSequence(sequence, status);
  std::vector<SubscribeWindow*> subscriptions =
      track.ShouldSend({group_id, object_id});
  if (subscriptions.empty()) {
    return true;
  }
  MoqtObject object;
  QUICHE_DCHECK(track.track_alias().has_value());
  object.track_alias = *track.track_alias();
  object.group_id = group_id;
  object.object_id = object_id;
  object.object_send_order = object_send_order;
  object.object_status = status;
  object.forwarding_preference = forwarding_preference;
  object.payload_length = payload.size();
  int failures = 0;
  quiche::StreamWriteOptions write_options;
  write_options.set_send_fin(end_of_stream);
  absl::flat_hash_set<uint64_t> subscribes_to_close;
  for (auto subscription : subscriptions) {
    if (subscription->OnObjectSent(sequence, status)) {
      subscribes_to_close.insert(subscription->subscribe_id());
    }
    if (forwarding_preference == MoqtForwardingPreference::kDatagram) {
      object.subscribe_id = subscription->subscribe_id();
      quiche::QuicheBuffer datagram =
          framer_.SerializeObjectDatagram(object, payload);
      // TODO(martinduke): It's OK to just silently fail, but better to notify
      // the app on errors.
      session_->SendOrQueueDatagram(datagram.AsStringView());
      continue;
    }
    bool new_stream = false;
    std::optional<webtransport::StreamId> stream_id =
        subscription->GetStreamForSequence(sequence);
    if (!stream_id.has_value()) {
      new_stream = true;
      stream_id = OpenUnidirectionalStream();
      if (!stream_id.has_value()) {
        QUICHE_DLOG(ERROR) << ENDPOINT
                           << "Sending OBJECT to nonexistent stream";
        ++failures;
        continue;
      }
      if (!end_of_stream) {
        subscription->AddStream(group_id, object_id, *stream_id);
      }
    }
    webtransport::Stream* stream = session_->GetStreamById(*stream_id);
    if (stream == nullptr) {
      QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT to nonexistent stream "
                         << *stream_id;
      ++failures;
      continue;
    }
    object.subscribe_id = subscription->subscribe_id();
    quiche::QuicheBuffer header =
        framer_.SerializeObjectHeader(object, new_stream);
    std::array<absl::string_view, 2> views = {header.AsStringView(), payload};
    if (!stream->Writev(views, write_options).ok()) {
      QUICHE_DLOG(ERROR) << ENDPOINT << "Failed to write OBJECT message";
      ++failures;
      continue;
    }
    QUICHE_DVLOG(1) << ENDPOINT << "Sending object length " << payload.length()
                    << " for " << full_track_name.track_namespace << ":"
                    << full_track_name.track_name << " with sequence "
                    << object.group_id << ":" << object.object_id
                    << " on stream " << *stream_id;
    if (end_of_stream && !new_stream) {
      subscription->RemoveStream(group_id, object_id);
    }
  }
  for (uint64_t subscribe_id : subscribes_to_close) {
    SubscribeIsDone(subscribe_id, SubscribeDoneCode::kSubscriptionEnded, "");
  }
  return (failures == 0);
}

void MoqtSession::CloseObjectStream(const FullTrackName& full_track_name,
                                    uint64_t group_id) {
  auto track_it = local_tracks_.find(full_track_name);
  if (track_it == local_tracks_.end()) {
    QUICHE_DLOG(ERROR) << ENDPOINT << "Sending OBJECT for nonexistent track";
    return;
  }
  LocalTrack& track = track_it->second;

  MoqtForwardingPreference forwarding_preference =
      track.forwarding_preference();
  if (forwarding_preference == MoqtForwardingPreference::kObject ||
      forwarding_preference == MoqtForwardingPreference::kDatagram) {
    QUIC_BUG(MoqtSession_CloseStreamObject_wrong_type)
        << "Forwarding preferences of Object or Datagram require stream to be "
           "immediately closed, and thus are not valid CloseObjectStream() "
           "targets";
    return;
  }

  std::vector<SubscribeWindow*> subscriptions =
      track.ShouldSend({group_id, /*object=*/0});
  for (SubscribeWindow* subscription : subscriptions) {
    std::optional<webtransport::StreamId> stream_id =
        subscription->GetStreamForSequence(
            FullSequence(group_id, /*object=*/0));
    if (!stream_id.has_value()) {
      continue;
    }
    webtransport::Stream* stream = session_->GetStreamById(*stream_id);
    if (stream == nullptr) {
      continue;
    }
    bool success = stream->SendFin();
    QUICHE_BUG_IF(MoqtSession_CloseObjectStream_fin_failed, !success);
  }
}

static void ForwardStreamDataToParser(webtransport::Stream& stream,
                                      MoqtParser& parser) {
  bool fin =
      quiche::ProcessAllReadableRegions(stream, [&](absl::string_view chunk) {
        parser.ProcessData(chunk, /*end_of_stream=*/false);
      });
  if (fin) {
    parser.ProcessData("", /*end_of_stream=*/true);
  }
}

void MoqtSession::ControlStream::OnCanRead() {
  ForwardStreamDataToParser(*stream_, parser_);
}
void MoqtSession::ControlStream::OnCanWrite() {
  // We buffer serialized control frames unconditionally, thus OnCanWrite()
  // requires no handling for control streams.
}

void MoqtSession::ControlStream::OnResetStreamReceived(
    webtransport::StreamErrorCode error) {
  session_->Error(MoqtError::kProtocolViolation,
                  absl::StrCat("Control stream reset with error code ", error));
}
void MoqtSession::ControlStream::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  session_->Error(MoqtError::kProtocolViolation,
                  absl::StrCat("Control stream reset with error code ", error));
}

void MoqtSession::ControlStream::OnObjectMessage(const MoqtObject& message,
                                                 absl::string_view payload,
                                                 bool end_of_message) {
  session_->Error(MoqtError::kProtocolViolation,
                  "Received OBJECT message on control stream");
}

void MoqtSession::ControlStream::OnClientSetupMessage(
    const MoqtClientSetup& message) {
  session_->control_stream_ = stream_->GetStreamId();
  if (perspective() == Perspective::IS_CLIENT) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received CLIENT_SETUP from server");
    return;
  }
  if (absl::c_find(message.supported_versions, session_->parameters_.version) ==
      message.supported_versions.end()) {
    // TODO(martinduke): Is this the right error code? See issue #346.
    session_->Error(MoqtError::kProtocolViolation,
                    absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  if (session_->parameters_.perspective == Perspective::IS_SERVER) {
    MoqtServerSetup response;
    response.selected_version = session_->parameters_.version;
    response.role = MoqtRole::kPubSub;
    SendOrBufferMessage(session_->framer_.SerializeServerSetup(response));
    QUIC_DLOG(INFO) << ENDPOINT << "Sent the SETUP message";
  }
  // TODO: handle role and path.
  std::move(session_->callbacks_.session_established_callback)();
  session_->peer_role_ = *message.role;
}

void MoqtSession::ControlStream::OnServerSetupMessage(
    const MoqtServerSetup& message) {
  if (perspective() == Perspective::IS_SERVER) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SERVER_SETUP from client");
    return;
  }
  if (message.selected_version != session_->parameters_.version) {
    // TODO(martinduke): Is this the right error code? See issue #346.
    session_->Error(MoqtError::kProtocolViolation,
                    absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  // TODO: handle role and path.
  std::move(session_->callbacks_.session_established_callback)();
  session_->peer_role_ = *message.role;
}

void MoqtSession::ControlStream::SendSubscribeError(
    const MoqtSubscribe& message, SubscribeErrorCode error_code,
    absl::string_view reason_phrase, uint64_t track_alias) {
  MoqtSubscribeError subscribe_error;
  subscribe_error.subscribe_id = message.subscribe_id;
  subscribe_error.error_code = error_code;
  subscribe_error.reason_phrase = reason_phrase;
  subscribe_error.track_alias = track_alias;
  SendOrBufferMessage(
      session_->framer_.SerializeSubscribeError(subscribe_error));
}

void MoqtSession::ControlStream::OnSubscribeMessage(
    const MoqtSubscribe& message) {
  std::string reason_phrase = "";
  if (session_->peer_role_ == MoqtRole::kPublisher) {
    QUIC_DLOG(INFO) << ENDPOINT << "Publisher peer sent SUBSCRIBE";
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE from publisher");
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Received a SUBSCRIBE for "
                  << message.track_namespace << ":" << message.track_name;
  auto it = session_->local_tracks_.find(FullTrackName(
      std::string(message.track_namespace), std::string(message.track_name)));
  if (it == session_->local_tracks_.end()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Rejected because "
                    << message.track_namespace << ":" << message.track_name
                    << " does not exist";
    SendSubscribeError(message, SubscribeErrorCode::kInternalError,
                       "Track does not exist", message.track_alias);
    return;
  }
  LocalTrack& track = it->second;
  if (it->second.canceled()) {
    // Note that if the track has already been deleted, there will not be a
    // protocol violation, which the spec says there SHOULD be. It's not worth
    // keeping state on deleted tracks.
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE for canceled track");
    return;
  }
  if ((track.track_alias().has_value() &&
       message.track_alias != *track.track_alias()) ||
      session_->used_track_aliases_.contains(message.track_alias)) {
    // Propose a different track_alias.
    SendSubscribeError(message, SubscribeErrorCode::kRetryTrackAlias,
                       "Track alias already exists",
                       session_->next_local_track_alias_++);
    return;
  } else {  // Use client-provided alias.
    track.set_track_alias(message.track_alias);
    if (message.track_alias >= session_->next_local_track_alias_) {
      session_->next_local_track_alias_ = message.track_alias + 1;
    }
    session_->used_track_aliases_.insert(message.track_alias);
  }
  FullSequence start;
  if (message.start_group.has_value()) {
    // The filter is AbsoluteStart or AbsoluteRange.
    QUIC_BUG_IF(quic_bug_invalid_subscribe, !message.start_object.has_value())
        << "Start group without start object";
    start = FullSequence(*message.start_group, *message.start_object);
  } else {
    // The filter is LatestObject or LatestGroup.
    start = track.next_sequence();
    if (message.start_object.has_value()) {
      // The filter is LatestGroup.
      QUIC_BUG_IF(quic_bug_invalid_subscribe, *message.start_object != 0)
          << "LatestGroup does not start with zero";
      start.object = 0;
    } else {
      --start.object;
    }
  }
  LocalTrack::Visitor::PublishPastObjectsCallback publish_past_objects;
  std::optional<SubscribeWindow> past_window;
  if (start < track.next_sequence() && track.visitor() != nullptr) {
    // Pull a copy of objects that have already been published.
    FullSequence end_of_past_subscription{
        message.end_group.has_value() ? *message.end_group : UINT64_MAX,
        message.end_object.has_value() ? *message.end_object : UINT64_MAX};
    end_of_past_subscription =
        std::min(end_of_past_subscription, track.next_sequence());
    past_window.emplace(message.subscribe_id, track.forwarding_preference(),
                        track.next_sequence(), start, end_of_past_subscription);
    absl::StatusOr<LocalTrack::Visitor::PublishPastObjectsCallback>
        past_objects_available =
            track.visitor()->OnSubscribeForPast(*past_window);
    if (!past_objects_available.ok()) {
      SendSubscribeError(message, SubscribeErrorCode::kInternalError,
                         past_objects_available.status().message(),
                         message.track_alias);
      return;
    }
    publish_past_objects = *std::move(past_objects_available);
  }
  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.subscribe_id = message.subscribe_id;
  SendOrBufferMessage(session_->framer_.SerializeSubscribeOk(subscribe_ok));
  QUIC_DLOG(INFO) << ENDPOINT << "Created subscription for "
                  << message.track_namespace << ":" << message.track_name;
  if (!message.end_group.has_value()) {
    track.AddWindow(message.subscribe_id, start.group, start.object);
  } else if (message.end_object.has_value()) {
    track.AddWindow(message.subscribe_id, start.group, start.object,
                    *message.end_group, *message.end_object);
  } else {
    track.AddWindow(message.subscribe_id, start.group, start.object,
                    *message.end_group);
  }
  session_->local_track_by_subscribe_id_.emplace(message.subscribe_id,
                                                 track.full_track_name());
  if (publish_past_objects) {
    QUICHE_DCHECK(past_window.has_value());
    std::move(publish_past_objects)();
  }
}

void MoqtSession::ControlStream::OnSubscribeOkMessage(
    const MoqtSubscribeOk& message) {
  auto it = session_->active_subscribes_.find(message.subscribe_id);
  if (it == session_->active_subscribes_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_OK for nonexistent subscribe");
    return;
  }
  MoqtSubscribe& subscribe = it->second.message;
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_OK for "
                  << "subscribe_id = " << message.subscribe_id << " "
                  << subscribe.track_namespace << ":" << subscribe.track_name;
  // Copy the Remote Track from session_->active_subscribes_ to
  // session_->remote_tracks_.
  FullTrackName ftn(subscribe.track_namespace, subscribe.track_name);
  RemoteTrack::Visitor* visitor = it->second.visitor;
  auto [track_iter, new_entry] = session_->remote_tracks_.try_emplace(
      subscribe.track_alias, ftn, subscribe.track_alias, visitor);
  if (it->second.forwarding_preference.has_value()) {
    if (!track_iter->second.CheckForwardingPreference(
            *it->second.forwarding_preference)) {
      session_->Error(MoqtError::kProtocolViolation,
                      "Forwarding preference different in early objects");
      return;
    }
  }
  // TODO: handle expires.
  if (visitor != nullptr) {
    visitor->OnReply(ftn, std::nullopt);
  }
  session_->active_subscribes_.erase(it);
}

void MoqtSession::ControlStream::OnSubscribeErrorMessage(
    const MoqtSubscribeError& message) {
  auto it = session_->active_subscribes_.find(message.subscribe_id);
  if (it == session_->active_subscribes_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_ERROR for nonexistent subscribe");
    return;
  }
  if (it->second.received_object) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received SUBSCRIBE_ERROR after object");
    return;
  }
  MoqtSubscribe& subscribe = it->second.message;
  QUIC_DLOG(INFO) << ENDPOINT << "Received the SUBSCRIBE_ERROR for "
                  << "subscribe_id = " << message.subscribe_id << " ("
                  << subscribe.track_namespace << ":" << subscribe.track_name
                  << ")" << ", error = " << static_cast<int>(message.error_code)
                  << " (" << message.reason_phrase << ")";
  RemoteTrack::Visitor* visitor = it->second.visitor;
  FullTrackName ftn(subscribe.track_namespace, subscribe.track_name);
  if (message.error_code == SubscribeErrorCode::kRetryTrackAlias) {
    // Automatically resubscribe with new alias.
    session_->remote_track_aliases_[ftn] = message.track_alias;
    session_->Subscribe(subscribe, visitor);
  } else if (visitor != nullptr) {
    visitor->OnReply(ftn, message.reason_phrase);
  }
  session_->active_subscribes_.erase(it);
}

void MoqtSession::ControlStream::OnUnsubscribeMessage(
    const MoqtUnsubscribe& message) {
  session_->SubscribeIsDone(message.subscribe_id,
                            SubscribeDoneCode::kUnsubscribed, "");
}

void MoqtSession::ControlStream::OnSubscribeUpdateMessage(
    const MoqtSubscribeUpdate& message) {
  // Search all the tracks to find the subscribe ID.
  auto name_it =
      session_->local_track_by_subscribe_id_.find(message.subscribe_id);
  if (name_it == session_->local_track_by_subscribe_id_.end()) {
    return;
  }
  auto track_it = session_->local_tracks_.find(name_it->second);
  if (track_it == session_->local_tracks_.end()) {
    return;
  }
  LocalTrack& track = track_it->second;
  SubscribeWindow* window = track.GetWindow(message.subscribe_id);
  if (window == nullptr) {
    return;
  }
  FullSequence start(message.start_group, message.start_object);
  std::optional<FullSequence> end;
  if (message.end_group.has_value()) {
    end = FullSequence(*message.end_group, message.end_object.has_value()
                                               ? *message.end_object
                                               : UINT64_MAX);
  }
  // TODO(martinduke): Handle the case where the update range is invalid.
  if (window->UpdateStartEnd(start, end)) {
    std::optional<FullSequence> largest_delivered = window->largest_delivered();
    if (largest_delivered.has_value() && end <= *largest_delivered) {
      session_->SubscribeIsDone(message.subscribe_id,
                                SubscribeDoneCode::kSubscriptionEnded,
                                "SUBSCRIBE_UPDATE moved subscription end");
    }
  }
}

void MoqtSession::ControlStream::OnAnnounceMessage(
    const MoqtAnnounce& message) {
  if (session_->peer_role_ == MoqtRole::kSubscriber) {
    QUIC_DLOG(INFO) << ENDPOINT << "Subscriber peer sent SUBSCRIBE";
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE from Subscriber");
    return;
  }
  std::optional<MoqtAnnounceErrorReason> error =
      session_->callbacks_.incoming_announce_callback(message.track_namespace);
  if (error.has_value()) {
    MoqtAnnounceError reply;
    reply.track_namespace = message.track_namespace;
    reply.error_code = error->error_code;
    reply.reason_phrase = error->reason_phrase;
    SendOrBufferMessage(session_->framer_.SerializeAnnounceError(reply));
    return;
  }
  MoqtAnnounceOk ok;
  ok.track_namespace = message.track_namespace;
  SendOrBufferMessage(session_->framer_.SerializeAnnounceOk(ok));
}

void MoqtSession::ControlStream::OnAnnounceOkMessage(
    const MoqtAnnounceOk& message) {
  auto it = session_->pending_outgoing_announces_.find(message.track_namespace);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE_OK for nonexistent announce");
    return;
  }
  std::move(it->second)(message.track_namespace, std::nullopt);
  session_->pending_outgoing_announces_.erase(it);
}

void MoqtSession::ControlStream::OnAnnounceErrorMessage(
    const MoqtAnnounceError& message) {
  auto it = session_->pending_outgoing_announces_.find(message.track_namespace);
  if (it == session_->pending_outgoing_announces_.end()) {
    session_->Error(MoqtError::kProtocolViolation,
                    "Received ANNOUNCE_ERROR for nonexistent announce");
    return;
  }
  std::move(it->second)(
      message.track_namespace,
      MoqtAnnounceErrorReason{message.error_code,
                              std::string(message.reason_phrase)});
  session_->pending_outgoing_announces_.erase(it);
}

void MoqtSession::ControlStream::OnAnnounceCancelMessage(
    const MoqtAnnounceCancel& message) {
  session_->CancelAnnounce(message.track_namespace);
}

void MoqtSession::ControlStream::OnParsingError(MoqtError error_code,
                                                absl::string_view reason) {
  session_->Error(error_code, absl::StrCat("Parse error: ", reason));
}

void MoqtSession::ControlStream::SendOrBufferMessage(
    quiche::QuicheBuffer message, bool fin) {
  quiche::StreamWriteOptions options;
  options.set_send_fin(fin);
  // TODO: while we buffer unconditionally, we should still at some point tear
  // down the connection if we've buffered too many control messages; otherwise,
  // there is potential for memory exhaustion attacks.
  options.set_buffer_unconditionally(true);
  std::array<absl::string_view, 1> write_vector = {message.AsStringView()};
  absl::Status success = stream_->Writev(absl::MakeSpan(write_vector), options);
  if (!success.ok()) {
    session_->Error(MoqtError::kInternalError,
                    "Failed to write a control message");
  }
}

void MoqtSession::IncomingDataStream::OnObjectMessage(const MoqtObject& message,
                                                      absl::string_view payload,
                                                      bool end_of_message) {
  QUICHE_DVLOG(1)
      << ENDPOINT << "Received OBJECT message on stream "
      << stream_->GetStreamId() << " for subscribe_id " << message.subscribe_id
      << " for track alias " << message.track_alias << " with sequence "
      << message.group_id << ":" << message.object_id << " send_order "
      << message.object_send_order << " forwarding_preference "
      << MoqtForwardingPreferenceToString(message.forwarding_preference)
      << " length " << payload.size() << " explicit length "
      << (message.payload_length.has_value() ? (int)*message.payload_length
                                             : -1)
      << (end_of_message ? "F" : "");
  if (!session_->parameters_.deliver_partial_objects) {
    if (!end_of_message) {  // Buffer partial object.
      absl::StrAppend(&partial_object_, payload);
      return;
    }
    if (!partial_object_.empty()) {  // Completes the object
      absl::StrAppend(&partial_object_, payload);
      payload = absl::string_view(partial_object_);
    }
  }
  auto [full_track_name, visitor] = session_->TrackPropertiesFromAlias(message);
  if (visitor != nullptr) {
    visitor->OnObjectFragment(
        full_track_name, message.group_id, message.object_id,
        message.object_send_order, message.object_status,
        message.forwarding_preference, payload, end_of_message);
  }
  partial_object_.clear();
}

void MoqtSession::IncomingDataStream::OnCanRead() {
  ForwardStreamDataToParser(*stream_, parser_);
}

void MoqtSession::IncomingDataStream::OnControlMessageReceived() {
  session_->Error(MoqtError::kProtocolViolation,
                  "Received a control message on a data stream");
}

void MoqtSession::IncomingDataStream::OnParsingError(MoqtError error_code,
                                                     absl::string_view reason) {
  session_->Error(error_code, absl::StrCat("Parse error: ", reason));
}

void MoqtSession::OutgoingDataStream::OnCanWrite() {
  // TODO: handle backpressure on data streams.
}

}  // namespace moqt
