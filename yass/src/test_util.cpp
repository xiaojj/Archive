// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022-2024 Chilledheart  */

#include "test_util.hpp"

namespace testing {

void hexdump(FILE* fp, const char* msg, const void* in, size_t len) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(in);

  fputs(msg, fp);
  for (size_t i = 0; i < len; i++) {
    fprintf(fp, "%02x", data[i]);
  }
  fputs("\n", fp);
}

static bool FromHexDigit(uint8_t* out, char c) {
  if ('0' <= c && c <= '9') {
    *out = c - '0';
    return true;
  }
  if ('a' <= c && c <= 'f') {
    *out = c - 'a' + 10;
    return true;
  }
  if ('A' <= c && c <= 'F') {
    *out = c - 'A' + 10;
    return true;
  }
  return false;
}

bool DecodeHex(std::vector<uint8_t>* out, const std::string& in) {
  out->clear();
  if (in.size() % 2 != 0) {
    return false;
  }
  out->reserve(in.size() / 2);
  for (size_t i = 0; i < in.size(); i += 2) {
    uint8_t hi, lo;
    if (!FromHexDigit(&hi, in[i]) || !FromHexDigit(&lo, in[i + 1])) {
      return false;
    }
    out->push_back((hi << 4) | lo);
  }
  return true;
}

std::string EncodeHex(span<const uint8_t> in) {
  static const char kHexDigits[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(in.size() * 2);
  for (uint8_t b : in) {
    ret += kHexDigits[b >> 4];
    ret += kHexDigits[b & 0xf];
  }
  return ret;
}

}  // namespace testing
