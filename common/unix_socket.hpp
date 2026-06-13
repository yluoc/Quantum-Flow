#pragma once

// Thin, non-logging helpers for AF_UNIX datagram sockets shared by the engine
// (bridge ingress + pipeline control sender) and the pipeline (control listener).
// Each helper does one mechanical step and reports success/failure via return
// value + errno, leaving error-message wording and fatal/non-fatal policy to the
// caller.

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace quantumflow {

/// True if `path` fits in sockaddr_un::sun_path (including the NUL terminator).
inline bool unix_path_fits(const std::string& path) {
    return path.size() < sizeof(sockaddr_un::sun_path);
}

/// Create an AF_UNIX SOCK_DGRAM socket. Returns the fd, or -1 with errno set.
inline int make_unix_dgram_socket() {
    return ::socket(AF_UNIX, SOCK_DGRAM, 0);
}

/// Best-effort O_NONBLOCK on `fd`. Silently does nothing if the flags can't be read.
inline void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

/// Bind `fd` to `path` for receiving: unlink any stale node, then bind.
/// The path is copied into sun_path with snprintf (truncating if over-long, to
/// match the existing call sites). Returns true on success; on failure returns
/// false with errno set by bind().
inline bool bind_unix_dgram(int fd, const std::string& path) {
    (void)::unlink(path.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    return ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}

/// One-shot send of a datagram to `path`: open a transient socket, sendto, close.
/// Returns the number of bytes sent, or -1 with errno set (from socket() or
/// sendto()). Does not validate path length — call unix_path_fits() first.
inline ssize_t send_unix_dgram(const std::string& path, const void* data, size_t len) {
    int fd = make_unix_dgram_socket();
    if (fd < 0) {
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

    ssize_t sent = ::sendto(fd, data, len, 0,
                            reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    int saved_errno = errno;
    ::close(fd);
    errno = saved_errno;
    return sent;
}

} // namespace quantumflow
