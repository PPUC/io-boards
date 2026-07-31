// Drives the shared protocol conformance checks under Unity.
//
// The checks themselves live in test/conformance/ and are compiled identically
// by libppuc, so both sides of the bus assert the same wire contract.

#include <unity.h>

#include "ProtocolConformance.h"

namespace {
size_t g_index = 0;
}

void setUp(void) {}
void tearDown(void) {}

void run_case(void) {
  const auto& c = ppuc_conformance::kCases[g_index];
  const auto result = c.fn();
  if (!result.ok) {
    TEST_FAIL_MESSAGE(result.detail.c_str());
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  for (g_index = 0; g_index < ppuc_conformance::kCaseCount; ++g_index) {
    UnitySetTestFile(ppuc_conformance::kCases[g_index].name);
    RUN_TEST(run_case);
  }
  return UNITY_END();
}
