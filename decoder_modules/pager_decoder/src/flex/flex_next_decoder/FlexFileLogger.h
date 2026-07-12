#pragma once

#include <fstream>
#include <string>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

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

        // --- Raw audio capture (separate, opt-in via SDRPP_FLEX_RAW_DIR) ------
        // Dumps the exact float samples the decoder consumes as signed-16-bit LE
        // mono, so the capture replays byte-for-byte through the standalone test
        // harness AND multimon-ng (-a FLEX_NEXT -t raw) at the decoder's sample
        // rate. The float→s16 scale (x32768, int16-clamped) is the exact inverse
        // of the harness's s16→float read (x 1/32768), so a live capture becomes
        // a golden fixture. Size-capped so a forgotten flag can't fill the disk.

        // Open <dir>/flex_<instance>.s16 when SDRPP_FLEX_RAW_DIR is set.
        bool openRaw(const std::string &instance) {
            const char *dir = std::getenv("SDRPP_FLEX_RAW_DIR");
            if (!dir || dir[0] == '\0') {
                raw_enabled_ = false;
                return false;
            }
            return openRawDir(std::string(dir), instance);
        }

        // Explicit-directory variant (tests funnel here). Optional cap override.
        bool openRawDir(const std::string &dir, const std::string &instance,
                        uint64_t max_bytes = DEFAULT_RAW_CAP_BYTES) {
            std::string path = dir + "/flex_" + instance + ".s16";
            raw_file_.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
            raw_enabled_ = raw_file_.is_open();
            if (raw_enabled_) {
                raw_path_ = path;
                raw_cap_bytes_ = max_bytes;
                raw_bytes_ = 0;
            }
            return raw_enabled_;
        }

        // Append `count` float samples as int16 LE (no-op unless raw-enabled).
        // Stops silently once the size cap is hit (capped_() reports it).
        void raw(const float *samples, int count) {
            if (!raw_enabled_ || !samples || count <= 0) { return; }
            for (int i = 0; i < count; i++) {
                if (raw_bytes_ + 2 > raw_cap_bytes_) { return; } // cap reached
                // x32768 then clamp to int16 range (post-AGC floats are ~±1 but
                // the wrapper only clamps to ±10, so guard the cast explicitly).
                float scaled = samples[i] * 32768.0f;
                if (scaled > 32767.0f) { scaled = 32767.0f; }
                if (scaled < -32768.0f) { scaled = -32768.0f; }
                int16_t s = static_cast<int16_t>(scaled);
                char le[2] = { static_cast<char>(s & 0xFF),
                               static_cast<char>((s >> 8) & 0xFF) };
                raw_file_.write(le, 2);
                raw_bytes_ += 2;
            }
        }

        // Flush buffered raw samples to disk (call sparingly — e.g. on stop, or
        // in tests before readback). Not called per-sample to keep the hot path
        // cheap; the OS flushes on close/exit anyway.
        void flushRaw() {
            if (raw_enabled_) { raw_file_.flush(); }
        }

        bool rawEnabled() const { return raw_enabled_; }
        const std::string &rawPath() const { return raw_path_; }
        uint64_t rawBytes() const { return raw_bytes_; }
        bool rawCapped() const { return raw_enabled_ && raw_bytes_ + 2 > raw_cap_bytes_; }

    private:
        std::ofstream file_;
        std::string path_;
        bool enabled_ = false;

        // ~64 MiB ≈ 25 min of 22050 Hz s16 — plenty for a diagnostic capture,
        // small enough that a forgotten flag can't fill a disk.
        static constexpr uint64_t DEFAULT_RAW_CAP_BYTES = 64ull * 1024 * 1024;
        std::ofstream raw_file_;
        std::string raw_path_;
        bool raw_enabled_ = false;
        uint64_t raw_cap_bytes_ = DEFAULT_RAW_CAP_BYTES;
        uint64_t raw_bytes_ = 0;
    };

} // namespace flex_next_decoder
