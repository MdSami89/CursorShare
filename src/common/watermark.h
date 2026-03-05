#pragma once
// =============================================================================
// CursorShare — Developer Watermark
//
// Copyright (c) 2026 Mohammad Sami (MdSami89)
// GitHub: https://github.com/MdSami89/CursorShare
//
// Licensed under the CursorShare Custom License (CSCL v1.0).
// Commercial use without written permission is prohibited.
// See LICENSE file for full terms.
//
// This file contains embedded authorship verification.
// Removal or modification of these watermarks violates CSCL v1.0 Section 4(c).
// =============================================================================

#include <cstdint>
#include <string>

namespace CursorShare {

// ---------------------------------------------------------------------------
// 1. Signature Variable — plaintext authorship marker compiled into binary
// ---------------------------------------------------------------------------
static const char *const CURSORSHARE_AUTHOR =
    "CursorShare by Mohammad Sami (MdSami89)";
static const char *const CURSORSHARE_LICENSE =
    "CSCL v1.0 — github.com/MdSami89/CursorShare";
static const char *const CURSORSHARE_VERSION = "0.1.0";

// ---------------------------------------------------------------------------
// 2. Encoded Watermark — Base64-encoded authorship string
//    Decodes to: "CursorShare (c) 2026 Mohammad Sami | github.com/MdSami89"
// ---------------------------------------------------------------------------
static const char *const CURSORSHARE_WATERMARK =
    "Q3Vyc29yU2hhcmUgKGMpIDIwMjYgTW9oYW1tYWQgU2FtaSB8IGdpdGh1Yi5jb20vTWRTYW1pOD"
    "k=";

// Decode the watermark (simple Base64 decode)
inline std::string DecodeWatermark() {
  static const char kBase64Table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const std::string &encoded = CURSORSHARE_WATERMARK;
  std::string decoded;
  decoded.reserve(encoded.size() * 3 / 4);

  uint32_t bits = 0;
  int bitCount = 0;
  for (char c : encoded) {
    if (c == '=')
      break;
    const char *pos = nullptr;
    for (int i = 0; i < 64; ++i) {
      if (kBase64Table[i] == c) {
        pos = &kBase64Table[i];
        break;
      }
    }
    if (!pos)
      continue;
    bits = (bits << 6) | static_cast<uint32_t>(pos - kBase64Table);
    bitCount += 6;
    if (bitCount >= 8) {
      bitCount -= 8;
      decoded.push_back(static_cast<char>((bits >> bitCount) & 0xFF));
    }
  }
  return decoded;
}

// ---------------------------------------------------------------------------
// 3. Hidden Author Function — returns authorship info (callable at runtime)
// ---------------------------------------------------------------------------
inline const char *GetProjectAuthor() { return CURSORSHARE_AUTHOR; }
inline const char *GetProjectLicense() { return CURSORSHARE_LICENSE; }
inline const char *GetProjectVersion() { return CURSORSHARE_VERSION; }

// Verify authorship integrity — returns true if watermarks are intact
inline bool VerifyAuthorship() {
  // Check signature variable exists and is not tampered
  if (std::string(CURSORSHARE_AUTHOR).find("MdSami89") == std::string::npos)
    return false;
  // Check encoded watermark decodes correctly
  std::string decoded = DecodeWatermark();
  if (decoded.find("Mohammad Sami") == std::string::npos)
    return false;
  return true;
}

// ---------------------------------------------------------------------------
// 4. Binary fingerprint — embeds author in compiled binary
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
#pragma section(".csmark", read)
__declspec(allocate(
    ".csmark")) static volatile const char cursorshare_fingerprint[] =
    "\x00CursorShare|MdSami89|CSCL-v1.0|github.com/MdSami89\x00";
#endif

} // namespace CursorShare
