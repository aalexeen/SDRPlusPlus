#pragma once

#include <fstream>
#include <string>
#include <cstdlib>

namespace flex_next_decoder {

    /**
     * @class FlexFileLogger
     * @brief Opt-in, single-writer file log for FLEX decoding.
     *
     * Enabled only when the environment variable SDRPP_FLEX_LOG_DIR is set to a
     * non-empty writable directory; otherwise every call is a cheap no-op. One
     * file per instance name so multiple FLEX decoders never interleave.
     *
     * THREADING: this owns an std::ofstream that must be touched from a SINGLE
     * thread (the DSP thread in the SDR++ wrapper). It is deliberately free of
     * any SDR++ / GUI dependency so it can be unit-tested standalone and so it
     * never gets shared with the GUI thread (which would reintroduce the
     * flexMessages data race fixed in CRASH-01).
     *
     * Each line is flushed (std::endl) so a `tail -f` sees events immediately
     * and a crash loses nothing.
     */
    class FlexFileLogger {
    public:
        FlexFileLogger() = default;

        // Open <dir>/flex_<instance>.log in append mode when SDRPP_FLEX_LOG_DIR
        // is set. Safe to call once; a failed open leaves the logger disabled
        // (no throw). Returns true if logging is now enabled.
        bool open(const std::string &instance) {
            const char *dir = std::getenv("SDRPP_FLEX_LOG_DIR");
            if (!dir || dir[0] == '\0') {
                enabled_ = false;
                return false;
            }
            return openDir(std::string(dir), instance);
        }

        // Explicit-directory variant (used by tests; the env-var path funnels
        // here). Opening an unwritable/nonexistent dir fails gracefully.
        bool openDir(const std::string &dir, const std::string &instance) {
            std::string path = dir + "/flex_" + instance + ".log";
            file_.open(path, std::ios::out | std::ios::app);
            enabled_ = file_.is_open();
            if (enabled_) {
                path_ = path;
                file_ << "=== FLEX log opened (instance " << instance << ") ===" << std::endl;
            }
            return enabled_;
        }

        // Write one line (no-op unless enabled). DSP thread only.
        void line(const std::string &s) {
            if (!enabled_) { return; }
            file_ << s << std::endl;
        }

        bool enabled() const { return enabled_; }
        const std::string &path() const { return path_; }

    private:
        std::ofstream file_;
        std::string path_;
        bool enabled_ = false;
    };

} // namespace flex_next_decoder
