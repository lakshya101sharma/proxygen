/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <proxygen/lib/http/codec/TransportDirection.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h> // table info
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/QPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/QPACKEncoder.h>
#include <string>
#include <vector>

namespace folly { namespace io {
class Cursor;
}} // namespace folly::io

namespace proxygen {

class HPACKHeader;

/*
 * Current version of the wire protocol. When we're making changes to the wire
 * protocol we need to change this version and the ALPN string so that old
 * clients will not be able to negotiate it anymore.
 */

class QPACKCodec : public HeaderCodec {
 public:
  QPACKCodec();
  ~QPACKCodec() override {
  }

  // QPACK encode: id is used for internal tracking of references
  QPACKEncoder::EncodeResult encode(
      std::vector<compress::Header>& headers,
      uint64_t id,
      uint32_t maxEncoderStreamBytes =
          std::numeric_limits<uint32_t>::max()) noexcept;

  HPACK::DecodeError decodeEncoderStream(std::unique_ptr<folly::IOBuf> buf) {
    // stats?
    return decoder_.decodeEncoderStream(std::move(buf));
  }

  // QPACK blocking decode.  The decoder may queue the block if there are
  // unsatisfied dependencies
  void decodeStreaming(uint64_t streamId,
                       std::unique_ptr<folly::IOBuf> block,
                       uint32_t length,
                       HPACK::StreamingCallback* streamingCb) noexcept;

  // This function sets both the decoder's advertised max and the size the
  // encoder will use.  The encoder has a limit of 64k.  This function can
  // only be called once with a unique non-zero value.
  //
  // Returns false if it was previously called with a different non-zero value.
  bool setEncoderHeaderTableSize(uint32_t size) {
    VLOG(4) << __func__ << " size=" << size;
    return encoder_.setHeaderTableSize(size);
  }

  void setDecoderHeaderTableMaxSize(uint32_t size) {
    decoder_.setHeaderTableMaxSize(size);
  }

  // Process bytes on the decoder stream
  HPACK::DecodeError decodeDecoderStream(std::unique_ptr<folly::IOBuf> buf) {
    return encoder_.decodeDecoderStream(std::move(buf));
  }

  // QPACK when a stream is reset.  Clears all reference counts for outstanding
  // blocks
  void onStreamReset(uint64_t streamId) {
    encoder_.onHeaderAck(streamId, true);
  }

  std::unique_ptr<folly::IOBuf> encodeInsertCountInc() {
    return decoder_.encodeInsertCountInc();
  }

  std::unique_ptr<folly::IOBuf> encodeHeaderAck(uint64_t streamId) {
    return decoder_.encodeHeaderAck(streamId);
  }

  std::unique_ptr<folly::IOBuf> encodeCancelStream(uint64_t streamId) {
    return decoder_.encodeCancelStream(streamId);
  }

  void describe(std::ostream& os) const;

  void setMaxUncompressed(uint64_t maxUncompressed) override {
    HeaderCodec::setMaxUncompressed(maxUncompressed);
    decoder_.setMaxUncompressed(maxUncompressed);
  }

  CompressionInfo getCompressionInfo() const {
    return CompressionInfo(encoder_.getTableSize(),
                           encoder_.getBytesStored(),
                           encoder_.getHeadersStored(),
                           encoder_.getInsertCount(),
                           encoder_.getBlockedInserts(),
                           encoder_.getDuplications(),
                           encoder_.getStaticRefs(),
                           decoder_.getTableSize(),
                           decoder_.getBytesStored(),
                           decoder_.getHeadersStored(),
                           decoder_.getInsertCount(),
                           0, // decoder can't track blocked inserts
                           decoder_.getDuplications(),
                           decoder_.getStaticRefs());
  }

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    encoder_.setHeaderIndexingStrategy(indexingStrat);
  }
  const HeaderIndexingStrategy* getHeaderIndexingStrategy() const {
    return encoder_.getHeaderIndexingStrategy();
  }

  uint64_t getHolBlockCount() const {
    return decoder_.getHolBlockCount();
  }

  uint64_t getQueuedBytes() const {
    return decoder_.getQueuedBytes();
  }

  void setMaxVulnerable(uint32_t maxVulnerable) {
    encoder_.setMaxVulnerable(maxVulnerable);
  }

  void setMaxBlocking(uint32_t maxBlocking) {
    decoder_.setMaxBlocking(maxBlocking);
  }

  void setMaxNumOutstandingBlocks(uint32_t value) {
    encoder_.setMaxNumOutstandingBlocks(value);
  }

 protected:
  QPACKEncoder encoder_;
  QPACKDecoder decoder_;

 private:
  void recordCompressedSize(const QPACKEncoder::EncodeResult& encodeRes);

  std::vector<HPACKHeader> decodedHeaders_;
};

std::ostream& operator<<(std::ostream& os, const QPACKCodec& codec);
} // namespace proxygen
