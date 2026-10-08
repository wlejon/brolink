#pragma once
// Bytes from the operating system's CSPRNG: getrandom() on Linux,
// arc4random_buf() on the BSDs and macOS, BCryptGenRandom() on Windows.

#include <cstddef>
#include <string>

namespace brolink {

// Fills `out` with `n` random bytes. False (with *err) when the OS refused,
// which a caller must treat as fatal for anything secret: there is no
// weaker fallback.
bool random_bytes(void* out, size_t n, std::string* err = nullptr);

}  // namespace brolink
