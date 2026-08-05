#include <edge_sentinel/ipc/command.hpp>
#include <edge_sentinel/linux/unix_command_server.hpp>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <array>
#include <string>
#include <utility>

namespace edge_sentinel::linux_platform {

UnixCommandServer::UnixCommandServer(std::string socket_path, Handler handler)
    : socket_path_(std::move(socket_path)), handler_(std::move(handler)) {}

UnixCommandServer::~UnixCommandServer() {
    stop();
}

bool UnixCommandServer::start(std::string& error_message) {
    if (running_.load(std::memory_order_acquire)) {
        error_message = "server_already_running";
        return false;
    }
    if (socket_path_.empty() || socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
        error_message = "invalid_socket_path";
        return false;
    }

    struct stat existing {};
    if (::lstat(socket_path_.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || existing.st_uid != ::geteuid()) {
            error_message = "refusing_to_remove_unowned_path";
            return false;
        }
        if (::unlink(socket_path_.c_str()) != 0) {
            error_message = std::strerror(errno);
            return false;
        }
    } else if (errno != ENOENT) {
        error_message = std::strerror(errno);
        return false;
    }

    UniqueFd candidate{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!candidate) {
        error_message = std::strerror(errno);
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.size() + 1);
    if (::bind(
            candidate.get(),
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0) {
        error_message = std::strerror(errno);
        return false;
    }
    owns_socket_path_ = true;

    if (::chmod(socket_path_.c_str(), S_IRUSR | S_IWUSR) != 0 ||
        ::listen(candidate.get(), 8) != 0) {
        error_message = std::strerror(errno);
        static_cast<void>(::unlink(socket_path_.c_str()));
        owns_socket_path_ = false;
        return false;
    }

    listener_ = std::move(candidate);
    running_.store(true, std::memory_order_release);
    thread_ = std::jthread([this](std::stop_token token) {
        accept_loop(token);
    });
    return true;
}

void UnixCommandServer::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    thread_.request_stop();
    if (listener_) {
        static_cast<void>(::shutdown(listener_.get(), SHUT_RDWR));
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    listener_.reset();
    if (owns_socket_path_) {
        static_cast<void>(::unlink(socket_path_.c_str()));
        owns_socket_path_ = false;
    }
}

const std::string& UnixCommandServer::socket_path() const noexcept {
    return socket_path_;
}

void UnixCommandServer::accept_loop(std::stop_token token) {
    while (!token.stop_requested()) {
        pollfd descriptor{listener_.get(), POLLIN, 0};
        const int poll_result = ::poll(&descriptor, 1, 100);
        if (poll_result <= 0 || (descriptor.revents & POLLIN) == 0) {
            continue;
        }

        UniqueFd client{::accept4(listener_.get(), nullptr, nullptr, SOCK_CLOEXEC)};
        if (!client) {
            if (errno == EINTR) {
                continue;
            }
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }
        serve_client(client.get());
    }
}

void UnixCommandServer::serve_client(int client_descriptor) const {
    std::array<char, ipc::kMaximumCommandLength + 2> buffer{};
    std::size_t used = 0;

    while (used < buffer.size()) {
        pollfd descriptor{client_descriptor, POLLIN, 0};
        const int poll_result = ::poll(&descriptor, 1, 1'000);
        if (poll_result < 0 && errno == EINTR) {
            continue;
        }
        if (poll_result <= 0 || (descriptor.revents & POLLIN) == 0) {
            return;
        }

        const ssize_t received =
            ::recv(client_descriptor, buffer.data() + used, buffer.size() - used, 0);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            break;
        }
        used += static_cast<std::size_t>(received);
        if (std::memchr(buffer.data(), '\n', used) != nullptr) {
            break;
        }
    }

    std::string response;
    if (used == 0) {
        return;
    }
    if (used > ipc::kMaximumCommandLength) {
        response = "error=too_long\n";
    } else {
        response = handler_(std::string_view{buffer.data(), used});
        if (response.empty() || response.back() != '\n') {
            response.push_back('\n');
        }
    }

    std::size_t sent = 0;
    while (sent < response.size()) {
        const ssize_t count = ::send(
            client_descriptor,
            response.data() + sent,
            response.size() - sent,
            MSG_NOSIGNAL);
        if (count <= 0) {
            break;
        }
        sent += static_cast<std::size_t>(count);
    }
}

}  // namespace edge_sentinel::linux_platform
