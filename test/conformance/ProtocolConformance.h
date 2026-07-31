#pragma once

// Shared conformance checks for the v2 wire protocol.
//
// PPUCProtocolV2.h is the protocol source of truth, and both sides of the bus
// call its helpers directly: libppuc uses ppuc::v2::Crc16Ccitt and
// ppuc::v2::SetPackedNibble when building frames, the firmware uses the same
// functions when parsing them. A change to any of them silently changes the
// wire format for everyone.
//
// These checks are deliberately framework-neutral so that *the identical code*
// runs in both repositories: io-boards drives them from Unity, libppuc from
// doctest. Anything framework-specific would mean two suites that drift.
//
// PPUCProtocolV2.h depends only on <stddef.h> and <stdint.h>, so this compiles
// anywhere - no Arduino, no board, no serial port.
//
// SCOPE, and the gap it does not close: these cover the shared *helpers*, not
// the field encoding. Multi-byte fields are written big-endian by hand in
// libppuc and parsed by hand in the firmware, with no shared implementation, so
// no test here can catch the two sides disagreeing about byte order. Closing
// that needs shared encode/decode functions in the header - see
// STABILIZATION_PLAN.md 3.1.

#include <cstddef>
#include <string>

namespace ppuc_conformance {

struct Result {
  bool ok = true;
  std::string detail;  // populated only on failure
};

using CaseFn = Result (*)();

struct Case {
  const char* name;
  CaseFn fn;
};

extern const Case kCases[];
extern const size_t kCaseCount;

}  // namespace ppuc_conformance
