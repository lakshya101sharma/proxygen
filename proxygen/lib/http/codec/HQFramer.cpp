/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/HQFramer.h>
#include <proxygen/lib/http/codec/HQUtils.h>
#include <quic/codec/QuicInteger.h>

using namespace folly::io;
using namespace folly;

namespace proxygen { namespace hq {

bool isGreaseId(uint64_t id) {
  if (id < 0x21 || id > quic::kEightByteLimit) {
    return false;
  }
  return (((id - 0x21) % 0x1F) == 0);
}

folly::Optional<uint64_t> getGreaseId(uint64_t n) {
  if (n > kMaxGreaseIdIndex) {
    return folly::none;
  }
  return (0x1F * n) + 0x21;
}

bool isInternalPushId(PushId pushId) {
  return pushId & kPushIdMask;
}

bool isExternalPushId(PushId pushId) {
  return !(pushId & kPushIdMask);
}

bool frameAffectsCompression(FrameType t) {
  return t == FrameType::HEADERS || t == FrameType::PUSH_PROMISE;
}

ParseResult parseData(folly::io::Cursor& cursor,
                      const FrameHeader& header,
                      std::unique_ptr<folly::IOBuf>& outBuf) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  // DATA frames MUST contain a non-zero-length payload
  if (header.length == 0) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_DATA;
  }
  cursor.clone(outBuf, header.length);
  return folly::none;
}

ParseResult parseHeaders(folly::io::Cursor& cursor,
                         const FrameHeader& header,
                         std::unique_ptr<folly::IOBuf>& outBuf) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  // for HEADERS frame, zero-length is allowed
  cursor.clone(outBuf, header.length);
  return folly::none;
}

uint8_t encodePriorityFlags(PriorityUpdate priority) {
  uint8_t flags = 0x00;
  flags |= (priority.prioritizedType << PRIORITIZED_TYPE_POS);
  flags |= (priority.dependencyType << DEPENDENCY_TYPE_POS);
  if (priority.exclusive) {
    flags |= PRIORITY_EXCLUSIVE_MASK;
  }
  return flags;
}

bool decodePriorityFlags(uint8_t flags, PriorityUpdate& outPriority) {
  outPriority.prioritizedType = static_cast<PriorityElementType>(
      (flags & (0x03 << PRIORITIZED_TYPE_POS)) >> PRIORITIZED_TYPE_POS);
  outPriority.dependencyType = static_cast<PriorityElementType>(
      (flags & (0x03 << DEPENDENCY_TYPE_POS)) >> DEPENDENCY_TYPE_POS);
  outPriority.exclusive = (flags & PRIORITY_EXCLUSIVE_MASK);
  uint8_t empty = flags & (0x07 << PRIORITY_EMPTY_POS);
  if (empty != 0) {
    return false;
  }
  return true;
}

ParseResult parsePriority(folly::io::Cursor& cursor,
                          const FrameHeader& header,
                          PriorityUpdate& outPriority) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  folly::IOBuf buf;
  auto frameLength = header.length;

  if (!cursor.canAdvance(sizeof(uint8_t))) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY;
  }

  uint8_t flags = cursor.readBE<uint8_t>();
  frameLength -= sizeof(uint8_t);
  bool res = decodePriorityFlags(flags, outPriority);
  if (!res) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY;
  }

  // A PRIORITY frame that prioritizes the root of the tree is not allowed
  if (outPriority.prioritizedType == PriorityElementType::TREE_ROOT) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY;
  }

  auto prioritizedElementId = quic::decodeQuicInteger(cursor, frameLength);
  if (!prioritizedElementId) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY;
  }
  outPriority.prioritizedElementId = prioritizedElementId->first;
  frameLength -= prioritizedElementId->second;

  if (outPriority.dependencyType != PriorityElementType::TREE_ROOT) {
    auto elementDependencyId = quic::decodeQuicInteger(cursor, frameLength);
    if (!elementDependencyId) {
      return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY;
    }
    outPriority.elementDependencyId = elementDependencyId->first;
    frameLength -= elementDependencyId->second;
  }

  if (!cursor.canAdvance(sizeof(uint8_t))) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY;
  }
  outPriority.weight = cursor.readBE<uint8_t>();
  frameLength -= sizeof(uint8_t);
  if (frameLength != 0) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY;
  }
  return folly::none;
}

ParseResult parseCancelPush(folly::io::Cursor& cursor,
                            const FrameHeader& header,
                            PushId& outPushId) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  folly::IOBuf buf;
  auto frameLength = header.length;

  auto pushId = quic::decodeQuicInteger(cursor, frameLength);
  if (!pushId) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_CANCEL_PUSH;
  }
  outPushId = pushId->first | kPushIdMask;
  frameLength -= pushId->second;
  if (frameLength != 0) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_CANCEL_PUSH;
  }

  return folly::none;
}

folly::Expected<folly::Optional<SettingValue>, HTTP3::ErrorCode>
decodeSettingValue(folly::io::Cursor& cursor,
                   uint64_t& frameLength,
                   SettingId settingId) {

  // read the setting value
  auto settingValue = quic::decodeQuicInteger(cursor, frameLength);
  if (!settingValue) {
    return folly::makeUnexpected(
        HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_SETTINGS);
  }
  auto value = settingValue->first;
  frameLength -= settingValue->second;

  // return the the value from the wire for known settings, folly::none for
  // unknown ones
  switch (settingId) {
    case SettingId::HEADER_TABLE_SIZE:
    case SettingId::NUM_PLACEHOLDERS:
    case SettingId::MAX_HEADER_LIST_SIZE:
    case SettingId::QPACK_BLOCKED_STREAMS:
      return value;
  }
  return folly::none;
}

ParseResult parseSettings(folly::io::Cursor& cursor,
                          const FrameHeader& header,
                          std::deque<SettingPair>& settings) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  folly::IOBuf buf;
  auto frameLength = header.length;

  while (frameLength > 0) {
    auto settingIdRes = quic::decodeQuicInteger(cursor, frameLength);
    if (!settingIdRes) {
      return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_SETTINGS;
    }
    frameLength -= settingIdRes->second;

    auto settingId = SettingId(settingIdRes->first);
    auto settingValue = decodeSettingValue(cursor, frameLength, settingId);
    if (settingValue.hasError()) {
      return settingValue.error();
    }

    if (settingValue->hasValue()) {
      settings.emplace_back(settingId, settingValue->value());
    }
  }
  return folly::none;
}

ParseResult parsePushPromise(folly::io::Cursor& cursor,
                             const FrameHeader& header,
                             PushId& outPushId,
                             std::unique_ptr<folly::IOBuf>& outBuf) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  folly::IOBuf buf;
  auto frameLength = header.length;

  auto pushId = quic::decodeQuicInteger(cursor, frameLength);
  if (!pushId) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PUSH_PROMISE;
  }
  outPushId = pushId->first | kPushIdMask;
  frameLength -= pushId->second;

  cursor.clone(outBuf, frameLength);
  return folly::none;
}

ParseResult parseGoaway(folly::io::Cursor& cursor,
                        const FrameHeader& header,
                        quic::StreamId& outStreamId) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  folly::IOBuf buf;
  auto frameLength = header.length;

  auto streamId = quic::decodeQuicInteger(cursor, frameLength);
  if (!streamId) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_GOAWAY;
  }
  outStreamId = streamId->first;
  frameLength -= streamId->second;
  if (frameLength != 0) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_GOAWAY;
  }

  return folly::none;
}

ParseResult parseMaxPushId(folly::io::Cursor& cursor,
                           const FrameHeader& header,
                           quic::StreamId& outPushId) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  folly::IOBuf buf;
  auto frameLength = header.length;

  auto pushId = quic::decodeQuicInteger(cursor, frameLength);
  if (!pushId) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_MAX_PUSH_ID;
  }
  outPushId = pushId->first | kPushIdMask;
  frameLength -= pushId->second;
  if (frameLength != 0) {
    return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_MAX_PUSH_ID;
  }

  return folly::none;
}

/**
 * Generate just the common frame header. Returns the total frame header length
 */
WriteResult writeFrameHeader(IOBufQueue& queue,
                             FrameType type,
                             uint64_t length) noexcept {
  QueueAppender appender(&queue, kMaxFrameHeaderSize);
  auto typeRes = quic::encodeQuicInteger(static_cast<uint64_t>(type), appender);
  if (typeRes.hasError()) {
    return typeRes;
  }
  auto lengthRes = quic::encodeQuicInteger(length, appender);
  if (lengthRes.hasError()) {
    return lengthRes;
  }
  return *typeRes + *lengthRes;
}

WriteResult writeSimpleFrame(IOBufQueue& queue,
                             FrameType type,
                             std::unique_ptr<folly::IOBuf> data) noexcept {
  DCHECK(data);
  auto payloadSize = data->computeChainDataLength();
  auto headerSize = writeFrameHeader(queue, type, payloadSize);
  if (headerSize.hasError()) {
    return headerSize;
  }
  queue.append(std::move(data));
  return *headerSize + payloadSize;
}

WriteResult writeData(IOBufQueue& queue,
                      std::unique_ptr<folly::IOBuf> data) noexcept {
  return writeSimpleFrame(queue, FrameType::DATA, std::move(data));
}

WriteResult writeUnframedBytes(IOBufQueue& queue,
                               std::unique_ptr<folly::IOBuf> data) noexcept {
  DCHECK(data);
  auto payloadSize = data->computeChainDataLength();
  queue.append(std::move(data));
  return payloadSize;
}

WriteResult writeHeaders(IOBufQueue& queue,
                         std::unique_ptr<folly::IOBuf> data) noexcept {
  return writeSimpleFrame(queue, FrameType::HEADERS, std::move(data));
}

WriteResult writePriority(IOBufQueue& queue, PriorityUpdate priority) noexcept {
  uint8_t flags = encodePriorityFlags(priority);

  auto prioritizedElementIdSize =
      quic::getQuicIntegerSize(priority.prioritizedElementId);
  if (prioritizedElementIdSize.hasError()) {
    return prioritizedElementIdSize;
  }


  size_t payloadSize = *prioritizedElementIdSize +
                       2 * sizeof(uint8_t);

  if (priority.dependencyType != PriorityElementType::TREE_ROOT) {
    auto elementDependencyIdSize =
      quic::getQuicIntegerSize(priority.elementDependencyId);
    if (elementDependencyIdSize.hasError()) {
      return elementDependencyIdSize;
    }
    payloadSize += *elementDependencyIdSize;
  }

  const auto headerSize =
      writeFrameHeader(queue, FrameType::PRIORITY, payloadSize);
  if (headerSize.hasError()) {
    return headerSize;
  }
  QueueAppender appender(&queue, payloadSize);
  appender.writeBE<uint8_t>(flags);
  quic::encodeQuicInteger(priority.prioritizedElementId, appender);
  if (priority.dependencyType != PriorityElementType::TREE_ROOT) {
    quic::encodeQuicInteger(priority.elementDependencyId, appender);
  }
  appender.writeBE<uint8_t>(priority.weight);
  return *headerSize + payloadSize;
}

WriteResult writeCancelPush(folly::IOBufQueue& writeBuf,
                            PushId pushId) noexcept {
  DCHECK(pushId & kPushIdMask);
  pushId = pushId & ~kPushIdMask;
  auto pushIdSize = quic::getQuicIntegerSize(pushId);
  if (pushIdSize.hasError()) {
    return pushIdSize;
  }
  IOBufQueue queue{IOBufQueue::cacheChainLength()};
  QueueAppender appender(&queue, *pushIdSize);
  quic::encodeQuicInteger(pushId, appender);
  return writeSimpleFrame(writeBuf, FrameType::CANCEL_PUSH, queue.move());
}

WriteResult writeSettings(IOBufQueue& queue,
                          const std::deque<SettingPair>& settings) {
  size_t settingsSize = 0;
  // iterate through the settings to compute the frame payload length
  for (const auto& setting : settings) {
    auto idSize =
        quic::getQuicIntegerSize(static_cast<uint64_t>(setting.first));
    if (idSize.hasError()) {
      return idSize;
    }
    auto valueSize = quic::getQuicIntegerSize(setting.second);
    if (valueSize.hasError()) {
      return valueSize;
    }
    settingsSize += *idSize + *valueSize;
  }
  // write the frame header
  auto headerSize = writeFrameHeader(queue, FrameType::SETTINGS, settingsSize);
  if (headerSize.hasError()) {
    return headerSize;
  }
  // write the frame payload
  QueueAppender appender(&queue, settingsSize);
  for (const auto& setting : settings) {
    quic::encodeQuicInteger(static_cast<uint64_t>(setting.first), appender);
    quic::encodeQuicInteger(setting.second, appender);
  }
  return *headerSize + settingsSize;
}

WriteResult writePushPromise(IOBufQueue& queue,
                             PushId pushId,
                             std::unique_ptr<folly::IOBuf> data) noexcept {
  DCHECK(data);
  DCHECK(pushId & kPushIdMask);
  pushId = pushId & ~kPushIdMask;
  auto pushIdSize = quic::getQuicIntegerSize(pushId);
  if (pushIdSize.hasError()) {
    return pushIdSize;
  }
  size_t payloadSize = *pushIdSize + data->computeChainDataLength();
  auto headerSize =
      writeFrameHeader(queue, FrameType::PUSH_PROMISE, payloadSize);
  if (headerSize.hasError()) {
    return headerSize;
  }
  QueueAppender appender(&queue, payloadSize);
  quic::encodeQuicInteger(pushId, appender);
  appender.insert(std::move(data));
  return *headerSize + payloadSize;
}

WriteResult writeGoaway(folly::IOBufQueue& writeBuf,
                        quic::StreamId lastStreamId) noexcept {
  auto lastStreamIdSize = quic::getQuicIntegerSize(lastStreamId);
  if (lastStreamIdSize.hasError()) {
    return lastStreamIdSize;
  }
  IOBufQueue queue{IOBufQueue::cacheChainLength()};
  QueueAppender appender(&queue, *lastStreamIdSize);
  quic::encodeQuicInteger(lastStreamId, appender);
  return writeSimpleFrame(writeBuf, FrameType::GOAWAY, queue.move());
}

WriteResult writeMaxPushId(folly::IOBufQueue& writeBuf,
                           PushId maxPushId) noexcept {
  DCHECK(maxPushId & kPushIdMask);
  maxPushId &= ~kPushIdMask;
  auto maxPushIdSize = quic::getQuicIntegerSize(maxPushId);
  if (maxPushIdSize.hasError()) {
    return maxPushIdSize;
  }
  IOBufQueue queue{IOBufQueue::cacheChainLength()};
  QueueAppender appender(&queue, *maxPushIdSize);
  quic::encodeQuicInteger(maxPushId, appender);
  return writeSimpleFrame(writeBuf, FrameType::MAX_PUSH_ID, queue.move());
}

const char* getFrameTypeString(FrameType type) {
  switch (type) {
    case FrameType::DATA:
      return "DATA";
    case FrameType::HEADERS:
      return "HEADERS";
    case FrameType::PRIORITY:
      return "PRIORITY";
    case FrameType::CANCEL_PUSH:
      return "CANCEL_PUSH";
    case FrameType::SETTINGS:
      return "SETTINGS";
    case FrameType::PUSH_PROMISE:
      return "PUSH_PROMISE";
    case FrameType::GOAWAY:
      return "GOAWAY";
    case FrameType::MAX_PUSH_ID:
      return "MAX_PUSH_ID";
    default:
      if (isGreaseId(static_cast<uint64_t>(type))) {
        return "GREASE";
      }
      // can happen when type was cast from uint8_t
      return "Unknown";
  }
  LOG(FATAL) << "Unreachable";
  return "";
}

}} // namespace proxygen::hq
