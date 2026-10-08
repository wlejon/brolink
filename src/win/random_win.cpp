#include "brolink/random.h"
#include "win_util.h"

#include <bcrypt.h>

namespace brolink {

bool random_bytes(void* out, size_t n, std::string* err) {
    auto* p = static_cast<unsigned char*>(out);
    while (n > 0) {
        const ULONG chunk = ULONG(n > 0x7FFFFFFF ? 0x7FFFFFFF : n);
        const NTSTATUS st = BCryptGenRandom(nullptr, p, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (st < 0) {
            if (err) *err = "BCryptGenRandom failed (NTSTATUS " + std::to_string(long(st)) + ")";
            return false;
        }
        p += chunk;
        n -= chunk;
    }
    return true;
}

}  // namespace brolink
