// =====================================================================
//  Shim de host: o minimo do Arduino para src/main_autoteste.cpp rodar
//  no PC (Serial.printf, millis, micros, F). Ver test/host/roda.sh.
// =====================================================================
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>

#define F(x) (x)

inline uint32_t micros() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<microseconds>(steady_clock::now() - t0).count();
}
inline uint32_t millis() {
  return micros() / 1000;
}
inline void delay(uint32_t) {}

struct SerialShim {
  void begin(unsigned long) {}
  int printf(const char* f, ...) {
    va_list a;
    va_start(a, f);
    const int n = vprintf(f, a);
    va_end(a);
    return n;
  }
  void println(const char* s = "") { ::printf("%s\n", s); }
  void print(const char* s) { ::printf("%s", s); }
};
extern SerialShim Serial;
