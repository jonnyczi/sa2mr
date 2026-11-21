// Deterministic RNG + hashing. We never use <random> distributions because their
// output is implementation-defined; everything here is bit-exact across platforms.
#pragma once
#include <cstdint>
#include <string>

namespace rr {

inline uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

struct Hasher {
    uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a 64
    void bytes(const void* p, size_t n) {
        auto* c = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 0x100000001b3ull; }
    }
    void i64(int64_t v) { bytes(&v, sizeof v); }
    void u64(uint64_t v) { bytes(&v, sizeof v); }
    void str(const std::string& s) { bytes(s.data(), s.size()); i64((int64_t)s.size()); }
    uint64_t value() const { uint64_t x = h; return splitmix64(x); }
};

class Rng {
public:
    explicit Rng(uint64_t seed = 1) { reseed(seed); }
    void reseed(uint64_t seed) {
        uint64_t x = seed;
        for (auto& v : s_) v = splitmix64(x);
    }
    uint64_t next() {  // xoshiro256**
        uint64_t r = rotl(s_[1] * 5, 7) * 9, t = s_[1] << 17;
        s_[2] ^= s_[0]; s_[3] ^= s_[1]; s_[1] ^= s_[2]; s_[0] ^= s_[3];
        s_[2] ^= t; s_[3] = rotl(s_[3], 45);
        return r;
    }
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }  // [0,1)
    double range(double a, double b) { return a + (b - a) * uniform(); }
    int irange(int a, int b) { return a + (int)(uniform() * (b - a + 1)); }  // inclusive
    bool chance(double p) { return uniform() < p; }
    uint64_t fork() { return next() ^ 0x5851F42D4C957F2Dull; }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
};

}  // namespace rr
