// Minimal dependency-free unit test framework (host PC, g++).
#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>

struct TestCase { const char* name; void (*fn)(); };
extern int g_failures;
extern int g_checks;
void registerTest(const char* name, void (*fn)());

#define TEST(name)                                                   \
  static void name();                                                \
  static struct name##_reg { name##_reg() { registerTest(#name, name); } } name##_inst; \
  static void name()

#define CHECK(cond)                                                              \
  do { ++g_checks; if (!(cond)) { ++g_failures;                                  \
    printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ(a, b)                                                           \
  do { ++g_checks; long long _a = (long long)(a), _b = (long long)(b);           \
    if (_a != _b) { ++g_failures;                                                \
      printf("    FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
  do { ++g_checks; double _a = (a), _b = (b);                                    \
    if (fabs(_a - _b) > (tol)) { ++g_failures;                                   \
      printf("    FAIL %s:%d: %s ~= %s (%f vs %f)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_STR(a, b)                                                          \
  do { ++g_checks; if (strcmp((a), (b)) != 0) { ++g_failures;                    \
    printf("    FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)
