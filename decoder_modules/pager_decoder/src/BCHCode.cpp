#include "BCHCode.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <ostream>
#include <stdexcept>

//=============================================================================
// BCHCode Class Implementation
//=============================================================================

BCHCode::BCHCode(const int *p, int m, int n, int k, int t) : m_(m), n_(n), k_(k), t_(t) {

    // Validate parameters
    if (m <= 0 || n <= 0 || k <= 0 || t <= 0 || k >= n) { throw std::invalid_argument("Invalid BCH parameters"); }

    if (p == nullptr) { throw std::invalid_argument("Primitive polynomial cannot be null"); }

    // Initialize vectors with appropriate sizes
    p_.resize(m + 1);
    alpha_to_.resize(n + 1);
    index_of_.resize(n + 1);
    g_.resize(n - k + 1);
    bb_.resize(n - k);

    // Copy primitive polynomial coefficients
    for (int i = 0; i <= m; i++) { p_[i] = p[i]; }

    // Generate Galois Field lookup tables
    generateGaloisField();

    // Debug: Print first few GF table entries
    /*std::cout << "DEBUG GF: First 10 alpha_to_ entries: ";
    for (int i = 0; i < std::min(10, n_); i++) { std::cout << alpha_to_[i] << " "; }
    std::cout << std::endl;

    std::cout << "DEBUG GF: First 10 index_of_ entries: ";
    for (int i = 0; i < std::min(10, n_); i++) { std::cout << index_of_[i] << " "; }
    std::cout << std::endl;*/

    // Generate BCH generator polynomial
    generatePolynomial();
}

void BCHCode::generateGaloisField() {
    /*
     * Generate GF(2**m) from the irreducible polynomial p(X) in p[0]..p[m]
     * Lookup tables:
     * - index->polynomial form: alpha_to[] contains j=alpha**i
     * - polynomial form -> index form: index_of[j=alpha**i] = i
     *
     * alpha=2 is the primitive element of GF(2**m)
     */

    int mask = 1;
    alpha_to_[m_] = 0;

    for (int i = 0; i < m_; i++) {
        alpha_to_[i] = mask;
        index_of_[alpha_to_[i]] = i;
        if (p_[i] != 0) { alpha_to_[m_] ^= mask; }
        mask <<= 1;
    }

    index_of_[alpha_to_[m_]] = m_;
    mask >>= 1;

    for (int i = m_ + 1; i < n_; i++) {
        if (alpha_to_[i - 1] >= mask) {
            alpha_to_[i] = alpha_to_[m_] ^ ((alpha_to_[i - 1] ^ mask) << 1);
        } else {
            alpha_to_[i] = alpha_to_[i - 1] << 1;
        }
        index_of_[alpha_to_[i]] = i;
    }

    index_of_[0] = -1;
}

void BCHCode::generatePolynomial() {
    /*
     * Compute generator polynomial of BCH code of length = 31, redundancy = 10
     * (OK, this is not very efficient, but we only do it once, right? :)
     */

    int cycle[15][6], size[15], min[11], zeros[11];
    int test, aux, nocycles, root, noterms, rdncy;
    int ll = 0; // Declare ll here at function scope

    // Generate cycle sets modulo n_
    cycle[0][0] = 0;
    size[0] = 1;
    cycle[1][0] = 1;
    size[1] = 1;
    int jj = 1; // cycle set index

    do {
        // Generate the jj-th cycle set
        int ii = 0;
        do {
            ii++;
            cycle[jj][ii] = (cycle[jj][ii - 1] * 2) % n_;
            size[jj]++;
            aux = (cycle[jj][ii] * 2) % n_;
        } while (aux != cycle[jj][0]);

        // Next cycle set representative
        ll = 0; // Reset ll here
        do {
            ll++;
            test = 0;
            for (ii = 1; ((ii <= jj) && (!test)); ii++) {
                // Examine previous cycle sets
                for (int kaux = 0; ((kaux < size[ii]) && (!test)); kaux++) {
                    if (ll == cycle[ii][kaux]) { test = 1; }
                }
            }
        } while ((test) && (ll < (n_ - 1)));

        if (!(test)) {
            jj++; // next cycle set index
            cycle[jj][0] = ll;
            size[jj] = 1;
        }
    } while (ll < (n_ - 1));

    nocycles = jj; // number of cycle sets modulo n_

    // Search for roots 1, 2, ..., d-1 in cycle sets
    int kaux = 0;
    rdncy = 0;
    for (int ii = 1; ii <= nocycles; ii++) {
        min[kaux] = 0;
        for (jj = 0; jj < size[ii]; jj++) {
            for (root = 1; root < (2 * t_ + 1); root++) {
                if (root == cycle[ii][jj]) { min[kaux] = ii; }
            }
        }
        if (min[kaux]) {
            rdncy += size[min[kaux]];
            kaux++;
        }
    }

    noterms = kaux;
    kaux = 1;
    for (int ii = 0; ii < noterms; ii++) {
        for (jj = 0; jj < size[min[ii]]; jj++) {
            zeros[kaux] = cycle[min[ii]][jj];
            kaux++;
        }
    }

    // Compute generator polynomial
    g_[0] = alpha_to_[zeros[1]];
    g_[1] = 1; // g(x) = (X + zeros[1]) initially

    for (int ii = 2; ii <= rdncy; ii++) {
        g_[ii] = 1;
        for (jj = ii - 1; jj > 0; jj--) {
            if (g_[jj] != 0) {
                g_[jj] = g_[jj - 1] ^ alpha_to_[(index_of_[g_[jj]] + zeros[ii]) % n_];
            } else {
                g_[jj] = g_[jj - 1];
            }
        }
        g_[0] = alpha_to_[(index_of_[g_[0]] + zeros[ii]) % n_];
    }
}

std::vector<int> BCHCode::encode(const std::vector<int> &data) {
    if (static_cast<int>(data.size()) != k_) { throw std::invalid_argument("Data size must equal k"); }

    // Create copy for in-place encoding
    std::vector<int> dataCopy(data);
    encode(dataCopy.data());

    // Combine data and redundancy bits
    std::vector<int> codeword(n_);

    // Copy data bits
    for (int i = 0; i < k_; i++) { codeword[i] = dataCopy[i]; }

    // Copy redundancy bits
    for (int i = 0; i < n_ - k_; i++) { codeword[k_ + i] = bb_[i]; }

    return codeword;
}

void BCHCode::encode(int *data) {
    /*
     * Calculate redundant bits bb[], codeword is c(X) = data(X)*X**(n-k)+ bb(X)
     */

    // Initialize redundancy bits
    for (int i = 0; i < n_ - k_; i++) { bb_[i] = 0; }

    for (int i = k_ - 1; i >= 0; i--) {
        int feedback = data[i] ^ bb_[n_ - k_ - 1];
        if (feedback != 0) {
            for (int j = n_ - k_ - 1; j > 0; j--) {
                if (g_[j] != 0) {
                    bb_[j] = bb_[j - 1] ^ feedback;
                } else {
                    bb_[j] = bb_[j - 1];
                }
            }
            bb_[0] = g_[0] && feedback;
        } else {
            for (int j = n_ - k_ - 1; j > 0; j--) { bb_[j] = bb_[j - 1]; }
            bb_[0] = 0;
        }
    }
}

// BCHCode_Decode
int BCHCode::decode(int *received) { // checked
    /*
     * We do not need the Berlekamp algorithm to decode.
     * We solve beforehand two equations in two variables.
     */

    int elp[3], s[5], s3;
    int count = 0, syn_error = 0;
    int loc[3], reg[3];
    int aux;
    int retval = 0;

    // First form the syndromes
    for (int i = 1; i <= 4; i++) {
        s[i] = 0;
        for (int j = 0; j < n_; j++) {
            if (received[j] != 0) { s[i] ^= alpha_to_[(i * j) % n_]; }
        }
        if (s[i] != 0) {
            syn_error = 1; // set flag if non-zero syndrome
        }
        // Convert syndrome from polynomial form to index form
        s[i] = index_of_[s[i]];
    }

    // Only print debug for actual errors
    /*if (syn_error) {
        std::cout << "DEBUG BCH: syn_error=" << syn_error << " s[1]=" << s[1] << " s[2]=" << s[2] << " s[3]=" << s[3]
                  << " s[4]=" << s[4] << std::endl;
    }*/

    if (syn_error) {
        if (s[1] != -1) {
            s3 = (s[1] * 3) % n_;
            if (s[3] == s3) {
                // Single error case
                received[s[1]] ^= 1;
                // std::cout << "DEBUG BCH: Single error corrected at position " << s[1] << std::endl;
            } else {
                // Two error case - ADD COMPREHENSIVE DEBUGGING
                // std::cout << "DEBUG BCH: Two-error case - s3=" << s3 << " s[3]=" << s[3] << std::endl;

                if (s[3] != -1) {
                    aux = alpha_to_[s3] ^ alpha_to_[s[3]];
                    /*std::cout << "DEBUG BCH: aux = alpha_to_[" << s3 << "] ^ alpha_to_[" << s[3]
                              << "] = " << alpha_to_[s3] << " ^ " << alpha_to_[s[3]] << " = " << aux << std::endl;*/
                } else {
                    aux = alpha_to_[s3];
                    /*std::cout << "DEBUG BCH: aux = alpha_to_[" << s3 << "] = " << aux << " (s[3] was -1)" <<
                     * std::endl;*/
                }

                // Check if aux is valid
                /*if (aux == 0 || index_of_[aux] == -1) {
                    //std::cout << "DEBUG BCH: Invalid aux calculation - uncorrectable" << std::endl;
                    retval = 1;
                } else {*/
                elp[0] = 0;
                elp[1] = (s[2] - index_of_[aux] + n_) % n_;
                elp[2] = (s[1] - index_of_[aux] + n_) % n_;

                /*std::cout << "DEBUG BCH: Error locator polynomial: elp[0]=" << elp[0] << " elp[1]=" << elp[1]
                          << " elp[2]=" << elp[2] << std::endl;*/

                // Chien search with detailed debugging
                for (int i = 1; i <= 2; i++) { reg[i] = elp[i]; }
                count = 0;

                /*std::cout << "DEBUG BCH: Starting Chien search..." << std::endl;*/
                for (int i = 1; i <= n_; i++) {
                    int q = 1;
                    for (int j = 1; j <= 2; j++) {
                        if (reg[j] != -1) {
                            reg[j] = (reg[j] + j) % n_;
                            q ^= alpha_to_[reg[j]];
                        }
                    }
                    if (!q) {
                        loc[count] = i % n_;
                        count++;
                        /*std::cout << "DEBUG BCH: Found error location at i=" << i << " position=" << (i % n_)
                                  << std::endl;*/
                    }
                    // Debug first few iterations
                    /*if (i <= 5) {
                        std::cout << "DEBUG BCH: i=" << i << " q=" << q << " reg[1]=" << reg[1]
                                  << " reg[2]=" << reg[2] << std::endl;
                    }*/
                }

                // std::cout << "DEBUG BCH: Chien search complete. Found " << count << " error locations" <<
                // std::endl;

                if (count == 2) {
                    for (int i = 0; i < 2; i++) { received[loc[i]] ^= 1; }
                    /*std::cout << "DEBUG BCH: Two errors corrected at positions " << loc[0] << " and " << loc[1]
                              << std::endl;*/
                } else {
                    // std::cout << "DEBUG BCH: Expected 2 errors but found " << count << std::endl;
                    retval = 1;
                }
                //}
            }
        } else if (s[2] != -1) {
            // std::cout << "DEBUG BCH: Error detection case" << std::endl;
            retval = 1;
        }
    }

    return retval;
}

std::vector<int> BCHCode::decode(const std::vector<int> &received) {
    if (static_cast<int>(received.size()) != n_) { throw std::invalid_argument("Received data size must equal n"); }

    // Create copy for in-place decoding
    std::vector<int> receivedCopy(received);

    int result = decode(receivedCopy.data());

    if (result != 0) {
        // Return empty vector if uncorrectable errors
        return std::vector<int>();
    }

    return receivedCopy;
}

//=============================================================================
// C-Style Wrapper Functions for Backward Compatibility
//=============================================================================

extern "C" {

struct BCHCode *BCHCode_New(int *p, int m, int n, int k, int t) {
    try {
        // Use placement new to create C++ object that can be treated as C struct
        BCHCode *bch = new BCHCode(p, m, n, k, t);
        return reinterpret_cast<struct BCHCode *>(bch);
    } catch (const std::exception &) { return nullptr; }
}

void BCHCode_Delete(struct BCHCode *bch) {
    if (bch != nullptr) {
        BCHCode *cpp_bch = reinterpret_cast<BCHCode *>(bch);
        delete cpp_bch;
    }
}

void BCHCode_Encode(struct BCHCode *bch, int *data) {
    if (bch != nullptr && data != nullptr) {
        BCHCode *cpp_bch = reinterpret_cast<BCHCode *>(bch);
        cpp_bch->encode(data);
    }
}

int BCHCode_Decode(struct BCHCode *bch, int *received) {
    if (bch == nullptr || received == nullptr) { return -1; }

    BCHCode *cpp_bch = reinterpret_cast<BCHCode *>(bch);
    return cpp_bch->decode(received);
}

} // extern "C"
