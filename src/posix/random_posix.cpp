#include "brolink/random.h"
#include "posix_util.h"

#if defined(__linux__)
#include <sys/random.h>
#else
#include <cstdlib>
#endif

namespace brolink {

bool random_bytes(void* out, size_t n, std::string* err) {
#if defined(__linux__)
    auto* p = static_cast<unsigned char*>(out);
    while (n > 0) {
        const ssize_t r = ::getrandom(p, n, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (err) *err = "getrandom: " + posix::errno_text(errno);
            return false;
        }
        p += r;
        n -= size_t(r);
    }
    return true;
#else
    (void)err;
    ::arc4random_buf(out, n);
    return true;
#endif
}

}  // namespace brolink
