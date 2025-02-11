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

#include <fizz/protocol/Types.h>
#include <folly/ThreadLocal.h>
#include <proxygen/lib/stats/BaseStats.h>
#include <string>
#include <wangle/ssl/SSLStats.h>
#include <wangle/ssl/SSLUtil.h>

namespace proxygen {

class ProxygenSSLStats : public wangle::SSLStats {
 public:
  virtual ~ProxygenSSLStats() = default;

  virtual void recordReplayCacheRequestComplete(uint64_t duration,
                                                bool cacheHit) noexcept = 0;
  virtual void recordReplayCacheRequestError() noexcept = 0;

  virtual void recordNewSSLHandshakeShed() = 0;

  virtual void recordPskType(folly::Optional<fizz::PskType> pskType) = 0;
  /**
   * Keep track of SSL handshake errors
   */
  virtual void recordSSLHandshake(bool success) = 0;

  virtual void recordZeroHandshake(bool success) = 0;

  virtual void recordFizzHandshake(bool success) = 0;

  // Protocol level errors only
  virtual void recordFizzHandshakeProtocolError() = 0;

  virtual void recordTFOSuccess() = 0;

  virtual void recordZeroConfigUpdateSuccess() = 0;

  virtual void recordZeroConfigUpdateError() = 0;
};

class TLSSLStats : public ProxygenSSLStats {
 public:
  TLSSLStats(const std::string& prefix);
  virtual ~TLSSLStats() = default;

  // downstream
  void recordSSLAcceptLatency(int64_t latency) noexcept override;
  void recordTLSTicket(bool ticketNew, bool ticketHit) noexcept override;
  void recordSSLSession(bool sessionNew,
                        bool sessionHit,
                        bool foreign) noexcept override;
  void recordSSLSessionRemove() noexcept override;
  void recordSSLSessionFree(uint32_t freed) noexcept override;
  void recordSSLSessionSetError(uint32_t err) noexcept override;
  void recordSSLSessionGetError(uint32_t err) noexcept override;
  void recordClientRenegotiation() noexcept override;
  void recordSSLClientCertificateMismatch() noexcept override;
  void recordTLSTicketRotation(bool valid) noexcept override;

  // upstream
  void recordSSLUpstreamConnection(bool handshake) noexcept override;
  void recordSSLUpstreamConnectionError(bool verifyError) noexcept override;

  // ProxygenSSLStats interface.

  void recordReplayCacheRequestComplete(uint64_t duration,
                                        bool cacheHit) noexcept override;
  void recordReplayCacheRequestError() noexcept override;

  void recordNewSSLHandshakeShed() override;

  void recordPskType(folly::Optional<fizz::PskType> pskType) override;
  /**
   * Keep track of SSL handshake errors
   */
  void recordSSLHandshake(bool success) override;

  void recordZeroHandshake(bool success) override;

  void recordFizzHandshake(bool success) override;

  void recordFizzHandshakeProtocolError() override;

  void recordTFOSuccess() override;

  void recordZeroConfigUpdateSuccess() override;

  void recordZeroConfigUpdateError() override;

 private:
  // Forbidden copy constructor and assignment operator
  TLSSLStats(TLSSLStats const&) = delete;
  TLSSLStats& operator=(TLSSLStats const&) = delete;

  // downstream
  BaseStats::TLHistogram sslAcceptLatency_;
  BaseStats::TLTimeseries sslAcceptLatencyTS_;
  BaseStats::TLTimeseries tlsTicketNew_;
  BaseStats::TLTimeseries tlsTicketHit_;
  BaseStats::TLTimeseries tlsTicketMiss_;
  BaseStats::TLTimeseries sslSessionNew_;
  BaseStats::TLTimeseries sslSessionHit_;
  BaseStats::TLTimeseries sslSessionForeignHit_;
  BaseStats::TLTimeseries sslSessionTotalMiss_;
  BaseStats::TLTimeseries sslSessionRemove_;
  BaseStats::TLTimeseries sslSessionFree_;
  BaseStats::TLTimeseries sslSessionSetError_;
  BaseStats::TLTimeseries sslSessionGetError_;
  BaseStats::TLTimeseries sslClientRenegotiations_;
  BaseStats::TLTimeseries clientCertMismatch_;
  BaseStats::TLTimeseries tlsTicketInvalidRotation_;

  // upstream
  BaseStats::TLTimeseries sslUpstreamHandshakes_;
  BaseStats::TLTimeseries sslUpstreamResumes_;
  BaseStats::TLTimeseries sslUpstreamErrors_;
  BaseStats::TLTimeseries sslUpstreamVerifyErrors_;
  // replay_cache service
  BaseStats::TLTimeseries replayCacheNumRequests_;
  BaseStats::TLTimeseries replayCacheNumHits_;
  BaseStats::TLTimeseries replayCacheNumErrors_;
  BaseStats::TLHistogram replayCacheDuration_;

  // ssl handshake metrics
  BaseStats::TLTimeseries newSSLHandshakeShed_;
  BaseStats::TLTimeseries sslHandshakeErrors_;
  BaseStats::TLTimeseries sslHandshakeSuccesses_;
  BaseStats::TLTimeseries zeroHandshakeErrors_;
  BaseStats::TLTimeseries zeroHandshakeSuccesses_;
  BaseStats::TLTimeseries zeroConfigUpdateErrors_;
  BaseStats::TLTimeseries zeroConfigUpdateSuccesses_;
  BaseStats::TLTimeseries fizzHandshakeErrors_;
  BaseStats::TLTimeseries fizzHandshakeProtocolErrors_;
  BaseStats::TLTimeseries fizzHandshakeSuccesses_;
  BaseStats::TLTimeseries tfoSuccess_;

  // PskTypes counters
  BaseStats::TLTimeseries fizzPskTypeNotSupported_;
  BaseStats::TLTimeseries fizzPskTypeNotAttempted_;
  BaseStats::TLTimeseries fizzPskTypeRejected_;
  BaseStats::TLTimeseries fizzPskTypeExternal_;
  BaseStats::TLTimeseries fizzPskTypeResumption_;
};

} // namespace proxygen
