#include <edge_sentinel/ipc/command.hpp>
#include <edge_sentinel/linux/unique_fd.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string default_socket_path() {
    return "/tmp/edge-sentinel-" + std::to_string(::geteuid()) + ".sock";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = default_socket_path();
    std::string command;

    for (int index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--socket" && index + 1 < argc) {
            socket_path = argv[++index];
            continue;
        }
        if (!command.empty()) {
            command.push_back(' ');
        }
        command += argv[index];
    }

    if (command.empty() || command.size() > edge_sentinel::ipc::kMaximumCommandLength) {
        std::cerr << "usage: edgectl [--socket PATH] COMMAND [ARGS]\n";
        return 2;
    }
    if (socket_path.empty() || socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::cerr << "invalid_socket_path\n";
        return 2;
    }

    edge_sentinel::linux_platform::UniqueFd socket{
        ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!socket) {
        std::cerr << "socket_error=" << std::strerror(errno) << '\n';
        return 1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (::connect(
            socket.get(),
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0) {
        std::cerr << "connect_error=" << std::strerror(errno) << '\n';
        return 1;
    }

    command.push_back('\n');
    std::size_t sent = 0;
    while (sent < command.size()) {
        const ssize_t count = ::send(
            socket.get(), command.data() + sent, command.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) {
            std::cerr << "send_error=" << std::strerror(errno) << '\n';
            return 1;
        }
        sent += static_cast<std::size_t>(count);
    }
    static_cast<void>(::shutdown(socket.get(), SHUT_WR));

    std::array<char, 2'048> response{};
    const ssize_t received = ::recv(socket.get(), response.data(), response.size(), 0);
    if (received <= 0) {
        std::cerr << "receive_error=" << std::strerror(errno) << '\n';
        return 1;
    }
    std::cout.write(response.data(), received);
    return 0;
}
