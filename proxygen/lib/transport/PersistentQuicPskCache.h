/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/transport/PersistentFizzPskCache.h>

#include <folly/Optional.h>
#include <folly/dynamic.h>
#include <fizz/client/PskSerializationUtils.h>
#include <quic/client/handshake/QuicPskCache.h>
#include <wangle/client/persistence/FilePersistentCache.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace proxygen {

// TODO share quic::AppToken class for serialization
struct PersistentQuicCachedPsk {
  std::string fizzPsk;
  std::string quicParams;
  size_t uses{0};
};

class PersistentQuicPskCache : public quic::QuicPskCache {
 public:
  PersistentQuicPskCache(const std::string& filename,
                         wangle::PersistentCacheConfig config,
                         std::unique_ptr<fizz::Factory> factory =
                             std::make_unique<fizz::OpenSSLFactory>());

  void setMaxPskUses(size_t maxUses);

  folly::Optional<quic::QuicCachedPsk> getPsk(
      const std::string& identity) override;
  void putPsk(const std::string& identity,
              quic::QuicCachedPsk quicCachedPsk) override;
  void removePsk(const std::string& identity) override;

 private:
  wangle::FilePersistentCache<std::string, PersistentQuicCachedPsk> cache_;
  size_t maxPskUses_{5};
  std::unique_ptr<fizz::Factory> factory_;
};

} // namespace proxygen

namespace folly {
template <>
dynamic toDynamic(const proxygen::PersistentQuicCachedPsk& cached);
template <>
proxygen::PersistentQuicCachedPsk convertTo(const dynamic& d);
} // namespace folly
