// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_OVERRIDES_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_
#define NET_QUICHE_OVERRIDES_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_

#include <vector>

#include "third_party/googleurl-override/polyfills/base/check_op.h"
#include "third_party/googleurl/base/compiler_specific.h"
#include "third_party/googleurl-override/polyfills/base/logging.h"

#define QUICHE_LOG_IMPL(severity) QUICHE_YASS_LOG_##severity
#define QUICHE_VLOG_IMPL(verbose_level) VLOG(verbose_level)
#define QUICHE_LOG_EVERY_N_SEC_IMPL(severity, seconds) QUICHE_LOG_IMPL(severity)
#define QUICHE_LOG_FIRST_N_IMPL(severity, n) QUICHE_LOG_IMPL(severity)
#define QUICHE_DLOG_IMPL(severity) QUICHE_YASS_DLOG_##severity
#define QUICHE_PLOG_IMPL(severity) QUICHE_YASS_PLOG_##severity
#define QUICHE_LOG_IF_IMPL(severity, condition) \
  QUICHE_YASS_LOG_IF_##severity(condition)
#define QUICHE_DLOG_IF_IMPL(severity, condition) \
  QUICHE_YASS_DLOG_IF_##severity(condition)
#define QUICHE_PLOG_IF_IMPL(severity, condition) \
  QUICHE_YASS_PLOG_IF_##severity(condition)

#define QUICHE_YASS_LOG_INFO LOG(INFO)
#define QUICHE_YASS_LOG_WARNING DLOG(WARNING)
#define QUICHE_YASS_LOG_ERROR DLOG(ERROR)
#define QUICHE_YASS_LOG_FATAL LOG(FATAL)
#define QUICHE_YASS_LOG_DFATAL LOG(DFATAL)

#define QUICHE_YASS_DLOG_INFO DLOG(INFO)
#define QUICHE_YASS_DLOG_WARNING DLOG(WARNING)
#define QUICHE_YASS_DLOG_ERROR DLOG(ERROR)
#define QUICHE_YASS_DLOG_FATAL DLOG(FATAL)
#define QUICHE_YASS_DLOG_DFATAL DLOG(DFATAL)

#define QUICHE_YASS_PLOG_INFO PLOG(INFO)
#define QUICHE_YASS_PLOG_WARNING PLOG(WARNING)
#define QUICHE_YASS_PLOG_ERROR PLOG(ERROR)
#define QUICHE_YASS_PLOG_FATAL PLOG(FATAL)
#define QUICHE_YASS_PLOG_DFATAL PLOG(DFATAL)

#define QUICHE_YASS_LOG_IF_INFO(condition) LOG_IF(INFO, condition)
#define QUICHE_YASS_LOG_IF_WARNING(condition) DLOG_IF(WARNING, condition)
#define QUICHE_YASS_LOG_IF_ERROR(condition) DLOG_IF(ERROR, condition)
#define QUICHE_YASS_LOG_IF_FATAL(condition) LOG_IF(FATAL, condition)
#define QUICHE_YASS_LOG_IF_DFATAL(condition) LOG_IF(DFATAL, condition)

#define QUICHE_YASS_DLOG_IF_INFO(condition) DLOG_IF(INFO, condition)
#define QUICHE_YASS_DLOG_IF_WARNING(condition) DLOG_IF(WARNING, condition)
#define QUICHE_YASS_DLOG_IF_ERROR(condition) DLOG_IF(ERROR, condition)
#define QUICHE_YASS_DLOG_IF_FATAL(condition) DLOG_IF(FATAL, condition)
#define QUICHE_YASS_DLOG_IF_DFATAL(condition) DLOG_IF(DFATAL, condition)

#define QUICHE_DVLOG_IMPL(verbose_level) DVLOG(verbose_level)
#define QUICHE_DVLOG_IF_IMPL(verbose_level, condition) \
  DVLOG_IF(verbose_level, condition)

#define QUICHE_LOG_INFO_IS_ON_IMPL() 0
#ifdef NDEBUG
#define QUICHE_LOG_WARNING_IS_ON_IMPL() 0
#define QUICHE_LOG_ERROR_IS_ON_IMPL() 0
#else
#define QUICHE_LOG_WARNING_IS_ON_IMPL() 1
#define QUICHE_LOG_ERROR_IS_ON_IMPL() 1
#endif
#define QUICHE_DLOG_INFO_IS_ON_IMPL() 0

#ifdef OS_WIN
// wingdi.h defines ERROR to be 0. When we call QUICHE_DLOG(ERROR), it gets
// substituted with 0, and it expands to QUICHE_YASS_DLOG_0. To allow us to
// keep using this syntax, we define this macro to do the same thing as
// QUICHE_YASS_DLOG_ERROR.
#define QUICHE_YASS_LOG_0 QUICHE_YASS_LOG_ERROR
#define QUICHE_YASS_DLOG_0 QUICHE_YASS_DLOG_ERROR
#define QUICHE_YASS_PLOG_0 QUICHE_YASS_PLOG_ERROR
#define QUICHE_YASS_LOG_IF_0 QUICHE_YASS_LOG_IF_ERROR
#define QUICHE_YASS_DLOG_IF_0 QUICHE_YASS_DLOG_IF_ERROR
#define QUICHE_YASS_PLOG_IF_0 QUICHE_YASS_PLOG_IF_ERROR
#endif

#define QUICHE_PREDICT_FALSE_IMPL(x) x
#define QUICHE_PREDICT_TRUE_IMPL(x) x

#define QUICHE_NOTREACHED_IMPL() NOTREACHED()

#define QUICHE_CHECK_IMPL(condition) CHECK(condition)
#define QUICHE_CHECK_EQ_IMPL(val1, val2) CHECK_EQ(val1, val2)
#define QUICHE_CHECK_NE_IMPL(val1, val2) CHECK_NE(val1, val2)
#define QUICHE_CHECK_LE_IMPL(val1, val2) CHECK_LE(val1, val2)
#define QUICHE_CHECK_LT_IMPL(val1, val2) CHECK_LT(val1, val2)
#define QUICHE_CHECK_GE_IMPL(val1, val2) CHECK_GE(val1, val2)
#define QUICHE_CHECK_GT_IMPL(val1, val2) CHECK_GT(val1, val2)
#define QUICHE_CHECK_OK_IMPL(value) CHECK((value).ok())

#define QUICHE_DCHECK_IMPL(condition) DCHECK(condition)
#define QUICHE_DCHECK_EQ_IMPL(val1, val2) DCHECK_EQ(val1, val2)
#define QUICHE_DCHECK_NE_IMPL(val1, val2) DCHECK_NE(val1, val2)
#define QUICHE_DCHECK_LE_IMPL(val1, val2) DCHECK_LE(val1, val2)
#define QUICHE_DCHECK_LT_IMPL(val1, val2) DCHECK_LT(val1, val2)
#define QUICHE_DCHECK_GE_IMPL(val1, val2) DCHECK_GE(val1, val2)
#define QUICHE_DCHECK_GT_IMPL(val1, val2) DCHECK_GT(val1, val2)

namespace quic {
template <typename T>
inline std::ostream& operator<<(std::ostream& out,
                                const std::vector<T>& v) {
  out << "[";
  const char* sep = "";
  for (size_t i = 0; i < v.size(); ++i) {
    out << sep << v[i];
    sep = ", ";
  }
  return out << "]";
}
}  // namespace quic

#endif  // NET_QUICHE_OVERRIDES_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_
