#pragma once

#include <cstdint>

namespace flex_next_decoder {

    /**
     * @class FlexNextDecoder
     * @brief Base class for FLEX decoder components
     *
     * Provides common functionality and configuration for all FLEX decoder components.
     */
    class FlexNextDecoder {
    public:
        /**
         * @brief Constructor with default verbosity level
         */
        FlexNextDecoder() : verbosity_level_(2) {}

        /**
         * @brief Constructor with specified verbosity level
         * @param verbosity_level Debug output level
         */
        explicit FlexNextDecoder(int verbosity_level) : verbosity_level_(verbosity_level) {}

        /**
         * @brief Virtual destructor for proper inheritance
         */
        virtual ~FlexNextDecoder() = default;

        /**
         * @brief Get current verbosity level
         * @return Current verbosity level
         */
        int getVerbosityLevel() const { return verbosity_level_; }

        /**
         * @brief Set verbosity level
         * @param level New verbosity level
         */
        void setVerbosityLevel(int level) { verbosity_level_ = level; }

    protected:
        int verbosity_level_;  ///< Debug output level

    private:
        // Delete copy operations (derived classes should manage their own copy semantics)
        FlexNextDecoder(const FlexNextDecoder&) = delete;
        FlexNextDecoder& operator=(const FlexNextDecoder&) = delete;

        // Allow move operations
        FlexNextDecoder(FlexNextDecoder&&) = default;
        FlexNextDecoder& operator=(FlexNextDecoder&&) = default;
    };

} // namespace flex_next_decoder