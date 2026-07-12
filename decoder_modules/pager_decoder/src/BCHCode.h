#pragma once

#include <cstdint>
#include <memory>
#include <vector>

/**
 * @file BCHCode.h
 * @brief BCH(31,21,5) Error Correction Code Implementation
 * 
 * This is a C++ conversion of the original BCH encoder/decoder by Robert Morelos-Zaragoza.
 * 
 * BCH Code Parameters:
 * - m = 5 (order of the field GF(2^5))
 * - n = 31 (code length, 2^5 - 1)
 * - k = 21 (data bits, n - redundancy)  
 * - t = 2 (error correcting capability)
 * - d = 5 (designed minimum distance, 2*t + 1)
 * - Redundancy = 10 bits (n - k)
 * 
 * This code can correct up to 2 bit errors in a 31-bit codeword.
 * Used in FLEX paging protocol for error correction.
 */

/**
 * @class BCHCode
 * @brief Binary BCH(31,21,5) Error Correction Code
 * 
 * Implements encoding and decoding for a (31,21,5) binary BCH code.
 * This specific implementation is optimized for FLEX protocol usage
 * and doesn't require the full Berlekamp-Massey algorithm since
 * the error locator polynomial is at most degree 2.
 */
class BCHCode {
private:
    // BCH Code parameters
    int m_;                           // Field order (5 for GF(2^5))
    int n_;                           // Code length (31)
    int k_;                           // Data length (21) 
    int t_;                           // Error correction capability (2)
    
    // Galois Field tables
    std::vector<int> p_;              // Primitive polynomial coefficients
    std::vector<int> alpha_to_;       // Log table: index -> polynomial form
    std::vector<int> index_of_;       // Antilog table: polynomial -> index form
    
    // Generator polynomial and encoding
    std::vector<int> g_;              // Generator polynomial coefficients
    std::vector<int> bb_;             // Redundancy polynomial coefficients
    
    // Private helper methods
    void generateGaloisField();       // Generate GF(2^m) lookup tables
    void generatePolynomial();        // Generate BCH generator polynomial
    
public:
    /**
     * @brief Constructor for BCH(31,21,5) code
     * @param p Primitive polynomial coefficients (array of m+1 integers)
     * @param m Field order (should be 5)
     * @param n Code length (should be 31)
     * @param k Data length (should be 21)
     * @param t Error correction capability (should be 2)
     */
    BCHCode(const int* p, int m, int n, int k, int t);
    
    /**
     * @brief Destructor
     */
    ~BCHCode() = default;
    
    // Delete copy constructor and assignment operator to prevent accidental copying
    BCHCode(const BCHCode&) = delete;
    BCHCode& operator=(const BCHCode&) = delete;
    
    // Allow move constructor and assignment
    BCHCode(BCHCode&&) = default;
    BCHCode& operator=(BCHCode&&) = default;
    
    /**
     * @brief Encode data using BCH code
     * @param data Input data array (k bits)
     * @return Encoded codeword including redundancy bits
     * 
     * Calculates redundancy bits and returns complete codeword.
     * Input data should be k=21 bits, output will be n=31 bits.
     */
    std::vector<int> encode(const std::vector<int>& data);
    
    /**
     * @brief Encode data using BCH code (in-place version)
     * @param data Input data array (k integers, modified in place)
     * 
     * This version modifies the internal redundancy buffer.
     * Use getRedundancyBits() to retrieve the parity bits.
     */
    void encode(int* data);
    
    /**
     * @brief Decode received codeword and correct errors
     * @param received Received codeword (n=31 bits, modified in place if errors found)
     * @return 0 if successful, non-zero if uncorrectable errors detected
     * 
     * Attempts to correct up to t=2 bit errors in the received codeword.
     * The input array is modified in place with error corrections applied.
     */
    int decode(int* received);
    
    /**
     * @brief Decode received codeword and correct errors
     * @param received Received codeword (n=31 bits)
     * @return Corrected codeword, or empty vector if uncorrectable
     * 
     * Vector version that returns corrected data without modifying input.
     */
    std::vector<int> decode(const std::vector<int>& received);
    
    /**
     * @brief Get the redundancy bits from last encoding operation
     * @return Vector containing the redundancy/parity bits
     */
    const std::vector<int>& getRedundancyBits() const { return bb_; }
    
    /**
     * @brief Get the generator polynomial
     * @return Vector containing generator polynomial coefficients
     */
    const std::vector<int>& getGeneratorPolynomial() const { return g_; }
    
    // Getters for code parameters
    int getM() const { return m_; }      // Field order
    int getN() const { return n_; }      // Code length  
    int getK() const { return k_; }      // Data length
    int getT() const { return t_; }      // Error correction capability
    int getRedundancy() const { return n_ - k_; }  // Number of parity bits
};

// C-style wrapper functions for backward compatibility with existing C code
// These maintain the original API while using the C++ implementation internally

/**
 * @brief C-style constructor wrapper
 * @param p Primitive polynomial coefficients  
 * @param m Field order
 * @param n Code length
 * @param k Data length
 * @param t Error correction capability
 * @return Pointer to BCHCode instance (opaque to C code)
 */
extern "C" {
    struct BCHCode* BCHCode_New(int* p, int m, int n, int k, int t);
    void BCHCode_Delete(struct BCHCode* bch);
    void BCHCode_Encode(struct BCHCode* bch, int* data);
    int BCHCode_Decode(struct BCHCode* bch, int* received);
}

// Type alias for cleaner code
using BCHCodePtr = std::unique_ptr<BCHCode>;