// Unit test for FlexFileLogger (the std-only file-logging layer used by
// decoder_next.h). The full GUI wrapper can't be compiled standalone, so this
// exercises the exact open/append/flush/enable logic directly.
//
// Prints "PASS" on success, "FAIL: <reason>" otherwise; exit code mirrors it.

#include "FlexFileLogger.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sstream>

using flex_next_decoder::FlexFileLogger;

static int failures = 0;
static void check(bool cond, const char *what) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static std::string slurp(const std::string &path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <writable-tmp-dir>\n", argv[0]);
        return 2;
    }
    std::string dir = argv[1];

    // 1) Enabled path: openDir succeeds, two lines land in the file (append +
    //    flush), plus the header line written on open.
    {
        FlexFileLogger log;
        bool ok = log.openDir(dir, "unit");
        check(ok, "openDir on writable dir returns true");
        check(log.enabled(), "enabled() true after successful open");
        log.line("MSG one");
        log.line("drop|A|idx=1|bad-address-word|raw=0xdead");
        std::string content = slurp(dir + "/flex_unit.log");
        check(content.find("MSG one") != std::string::npos, "first line written");
        check(content.find("bad-address-word") != std::string::npos, "second (drop) line written");
        check(content.find("=== FLEX log opened") != std::string::npos, "header line written on open");
    }

    // 2) Disabled path: env unset + open() → no file, disabled, line() no-ops.
    {
        unsetenv("SDRPP_FLEX_LOG_DIR");
        FlexFileLogger log;
        bool ok = log.open("noenv");
        check(!ok, "open() returns false when SDRPP_FLEX_LOG_DIR unset");
        check(!log.enabled(), "enabled() false when env unset");
        log.line("should be dropped"); // must not crash
    }

    // 3) Env-set path: open() funnels through env var to the same dir.
    {
        setenv("SDRPP_FLEX_LOG_DIR", dir.c_str(), 1);
        FlexFileLogger log;
        bool ok = log.open("viaenv");
        check(ok, "open() returns true when env var set to writable dir");
        log.line("ENV line");
        std::string content = slurp(dir + "/flex_viaenv.log");
        check(content.find("ENV line") != std::string::npos, "line written via env-var open");
        unsetenv("SDRPP_FLEX_LOG_DIR");
    }

    // 4) Unwritable dir → graceful: no throw, disabled.
    {
        FlexFileLogger log;
        bool ok = log.openDir("/nonexistent_dir_xyz_12345", "bad");
        check(!ok, "openDir on nonexistent dir returns false");
        check(!log.enabled(), "enabled() false on failed open");
        log.line("nowhere"); // must not crash
    }

    // 5) Raw capture: float→s16 scale is the exact inverse of the harness read
    //    (x32768), int16 clamp holds, and the readback round-trips.
    {
        FlexFileLogger log;
        bool ok = log.openRawDir(dir, "raw");
        check(ok, "openRawDir on writable dir returns true");
        check(log.rawEnabled(), "rawEnabled() true after open");
        // 1.0 -> +32767 (clamped from 32768), -1.0 -> -32768, 0 -> 0,
        // and an over-range 5.0 must clamp to +32767 (not int overflow-wrap).
        float in[] = { 0.0f, 1.0f, -1.0f, 0.5f, 5.0f };
        log.raw(in, 5);
        log.flushRaw();
        check(log.rawBytes() == 10, "raw wrote 2 bytes per sample");
        // Read back the s16 LE and confirm values.
        std::ifstream f(dir + "/flex_raw.s16", std::ios::binary);
        int16_t s[5];
        f.read(reinterpret_cast<char *>(s), 10);
        check(s[0] == 0, "0.0 -> 0");
        check(s[1] == 32767, "1.0 -> +32767 (clamped)");
        check(s[2] == -32768, "-1.0 -> -32768");
        check(s[3] == 16384, "0.5 -> 16384");
        check(s[4] == 32767, "5.0 over-range -> +32767 (no overflow wrap)");
    }

    // 6) Raw size cap: writing past the cap stops silently, file stays bounded.
    {
        FlexFileLogger log;
        // Cap at 8 bytes = 4 samples.
        bool ok = log.openRawDir(dir, "rawcap", 8);
        check(ok, "openRawDir with cap returns true");
        float buf[10] = { 0 };
        log.raw(buf, 10); // ask for 20 bytes, cap is 8
        check(log.rawBytes() == 8, "raw honors the size cap (8 bytes)");
        check(log.rawCapped(), "rawCapped() true after cap hit");
    }

    // 7) Raw disabled path: env unset + openRaw -> no file, no-op.
    {
        unsetenv("SDRPP_FLEX_RAW_DIR");
        FlexFileLogger log;
        bool ok = log.openRaw("norawenv");
        check(!ok, "openRaw returns false when SDRPP_FLEX_RAW_DIR unset");
        check(!log.rawEnabled(), "rawEnabled() false when env unset");
        float buf[4] = { 0 };
        log.raw(buf, 4); // must not crash
    }

    if (failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("%d check(s) failed\n", failures);
    return 1;
}
