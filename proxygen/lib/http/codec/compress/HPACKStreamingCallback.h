/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/HPACKHeaderName.h>

namespace proxygen { namespace HPACK {
  class StreamingCallback {
   public:
    virtual ~StreamingCallback() {}

    virtual void onHeader(const HPACKHeaderName& name,
                          const folly::fbstring& value) = 0;
    virtual void onHeadersComplete(HTTPHeaderSize decodedSize,
                                   bool acknowledge) = 0;
    virtual void onDecodeError(HPACK::DecodeError decodeError) = 0;
    HeaderCodec::Stats* stats{nullptr};
  };


}}
