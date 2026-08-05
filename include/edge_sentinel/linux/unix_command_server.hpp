#pragma once

#include <edge_sentinel/linux/unique_fd.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

namespace edge_sentinel::linux_platform {

class UnixCommandServer final {
public:
    using Handler = std::function<std::string(std::string_view)>;

    UnixCommandServer(std::string socket_path, Handler handler);
    ~UnixCommandServer();

    UnixCommandServer(const UnixCommandServer&) = delete;
    UnixCommandServer& operator=(const UnixCommandServer&) = delete;

    [[nodiscard]] bool start(std::string& error_message);
    void stop() noexcept;

    [[nodiscard]] const std::string& socket_path() const noexcept;

private:
    void accept_loop(std::stop_token token);
    void serve_client(int client_descriptor) const;

    std::string socket_path_;
    Handler handler_;
    UniqueFd listener_{};
    std::jthread thread_{};
    std::atomic<bool> running_{false};
    bool owns_socket_path_{false};
};

}  // namespace edge_sentinel::linux_platform
