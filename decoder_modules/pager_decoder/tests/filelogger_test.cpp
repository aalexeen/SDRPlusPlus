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

    if (failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("%d check(s) failed\n", failures);
    return 1;
}
