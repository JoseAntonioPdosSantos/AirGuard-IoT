#include "test_framework.h"

int g_failures = 0;
int g_checks = 0;

static TestCase g_tests[256];
static int g_count = 0;

void registerTest(const char* name, void (*fn)()) {
  if (g_count < 256) g_tests[g_count++] = {name, fn};
}

int main() {
  int failedTests = 0;
  for (int i = 0; i < g_count; ++i) {
    const int before = g_failures;
    g_tests[i].fn();
    const bool ok = g_failures == before;
    if (!ok) ++failedTests;
    printf("[%s] %s\n", ok ? " OK " : "FAIL", g_tests[i].name);
  }
  printf("\n%d tests, %d checks, %d failed checks, %d failed tests\n", g_count, g_checks,
         g_failures, failedTests);
  return g_failures == 0 ? 0 : 1;
}
