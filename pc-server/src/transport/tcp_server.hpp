#pragma once

#include "protocol/protocol.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace argb::transport {

class TcpServer {
public:
    using MessageHandler = std::function<void(const protocol::Message&)>;
    using ConnectHandler = std::function<void()>;

    TcpServer() = default;
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool start(std::uint16_t port, MessageHandler handler, ConnectHandler onConnect = {});
    void stop();

    bool send(protocol::MessageType type, const std::vector<std::uint8_t>& payload);
    bool isConnected() const { return connected_.load(); }
    std::uint64_t droppedMessages() const { return droppedMessages_.load(); }
    std::uint16_t port() const { return port_; }

private:
    struct Outgoing {
        protocol::Header header;
        std::vector<std::uint8_t> payload;
    };

    static constexpr std::uintptr_t kInvalidSocket = ~std::uintptr_t(0);
    static constexpr std::size_t kMaxQueuedMessages = 32;

    void acceptLoop();
    void receiveLoop(std::uintptr_t clientSocketValue);
    void sendLoop(std::uintptr_t clientSocketValue);
    void disconnect();
    void closeListenSocket();

    std::uint16_t port_ = 0;
    MessageHandler handler_;
    ConnectHandler onConnect_;
    std::uintptr_t listenSocket_ = kInvalidSocket;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint64_t> droppedMessages_{0};

    std::thread acceptThread_;
    std::thread receiveThread_;
    std::thread sendThread_;

    mutable std::mutex socketMutex_;
    std::uintptr_t clientSocket_ = kInvalidSocket;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Outgoing> queue_;
    std::uint32_t sequence_ = 0;
};

} // namespace argb::transport
