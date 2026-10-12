#include "common/fd.hpp"
#include <fcntl.h>
#include <cerrno>

namespace netstream {

bool set_nonblocking(int fd) {
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
    return true;
}

bool set_cloexec(int fd) {
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) return false;
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
    return true;
}

} // namespace netstream
