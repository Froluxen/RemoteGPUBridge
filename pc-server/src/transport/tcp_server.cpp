#include "transport/tcp_server.hpp"

#include "core/log.hpp"

#include <format>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace argb::transport {
namespace {

bool sendAll(SOCKET socket, const std::uint8_t* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const int result = ::send(socket,
                                  reinterpret_cast<const char*>(data + sent),
                                  static_cast<int>(size - sent),
                                  0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool recvExact(SOCKET socket, std::uint8_t* data, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const int result = ::recv(socket,
                                  reinterpret_cast<char*>(data + received),
                                  static_cast<int>(size - received),
                                  0);
        if (result <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(result);
    }
    return true;
}

} // namespace

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start(std::uint16_t port, MessageHandler handler, ConnectHandler onConnect) {
    if (running_.load()) {
        return false;
    }

    port_ = port;
    handler_ = std::move(handler);
    onConnect_ = std::move(onConnect);

    WSADATA wsaData{};
    const int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        log::error(std::format("WSAStartup fallo: {}", wsaResult));
        return false;
    }

    const SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        log::error(std::format("socket() fallo: {}", WSAGetLastError()));
        WSACleanup();
        return false;
    }

    int reuse = 1;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (::bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        log::error(std::format("bind() fallo en 127.0.0.1:{}: {}", port, WSAGetLastError()));
        ::closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    if (::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        log::error(std::format("listen() fallo en puerto {}: {}", port, WSAGetLastError()));
        ::closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    listenSocket_ = static_cast<std::uintptr_t>(listenSocket);
    running_.store(true);
    acceptThread_ = std::thread(&TcpServer::acceptLoop, this);
    log::info(std::format("TCP escuchando en 127.0.0.1:{}", port));
    return true;
}

void TcpServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    closeListenSocket();
    disconnect();
    queueCv_.notify_all();

    if (acceptThread_.joinable()) acceptThread_.join();
    if (receiveThread_.joinable()) receiveThread_.join();
    if (sendThread_.joinable()) sendThread_.join();

    WSACleanup();
}

void TcpServer::closeListenSocket() {
    if (listenSocket_ != kInvalidSocket) {
        ::closesocket(static_cast<SOCKET>(listenSocket_));
        listenSocket_ = kInvalidSocket;
    }
}

void TcpServer::disconnect() {
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (clientSocket_ != kInvalidSocket) {
        ::closesocket(static_cast<SOCKET>(clientSocket_));
        clientSocket_ = kInvalidSocket;
    }
    connected_.store(false);
    queueCv_.notify_all();
}

void TcpServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in clientAddress{};
        int clientLength = sizeof(clientAddress);
        const SOCKET client = ::accept(static_cast<SOCKET>(listenSocket_),
                                       reinterpret_cast<sockaddr*>(&clientAddress),
                                       &clientLength);
        if (client == INVALID_SOCKET) {
            if (!running_.load()) break;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            clientSocket_ = static_cast<std::uintptr_t>(client);
        }
        connected_.store(true);
        log::info(std::format("Cliente conectado en puerto {}", port_));

        if (onConnect_) {
            onConnect_();
        }

        receiveThread_ = std::thread(&TcpServer::receiveLoop, this, static_cast<std::uintptr_t>(client));
        sendThread_ = std::thread(&TcpServer::sendLoop, this, static_cast<std::uintptr_t>(client));

        if (receiveThread_.joinable()) receiveThread_.join();
        if (sendThread_.joinable()) sendThread_.join();

        connected_.store(false);
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            clientSocket_ = kInvalidSocket;
        }
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.clear();
        }
        log::info(std::format("Cliente desconectado en puerto {}", port_));
    }
}

void TcpServer::receiveLoop(std::uintptr_t clientSocketValue) {
    const SOCKET client = static_cast<SOCKET>(clientSocketValue);
    std::vector<std::uint8_t> headerBuffer(protocol::kHeaderSize);

    while (running_.load() && connected_.load()) {
        if (!recvExact(client, headerBuffer.data(), headerBuffer.size())) {
            break;
        }

        protocol::Header header;
        if (!protocol::decodeHeader(headerBuffer.data(), header)) {
            log::warn(std::format("Header invalido en puerto {}; cerrando conexion.", port_));
            break;
        }

        std::vector<std::uint8_t> payload(header.length);
        if (header.length > 0 && !recvExact(client, payload.data(), payload.size())) {
            break;
        }

        protocol::Message message;
        message.type = header.type;
        message.payload = std::move(payload);
        message.sequence = header.sequence;
        if (handler_) {
            handler_(message);
        }
    }

    disconnect();
}

void TcpServer::sendLoop(std::uintptr_t clientSocketValue) {
    const SOCKET client = static_cast<SOCKET>(clientSocketValue);

    while (running_.load() && connected_.load()) {
        Outgoing outgoing;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] {
                return !running_.load() || !connected_.load() || !queue_.empty();
            });
            if (!running_.load() || !connected_.load()) {
                break;
            }
            outgoing = std::move(queue_.front());
            queue_.pop_front();
        }

        const auto headerBytes = protocol::encodeHeader(outgoing.header);
        if (!sendAll(client, headerBytes.data(), headerBytes.size()) ||
            (!outgoing.payload.empty() &&
             !sendAll(client, outgoing.payload.data(), outgoing.payload.size()))) {
            break;
        }
    }

    disconnect();
}

bool TcpServer::send(protocol::MessageType type, const std::vector<std::uint8_t>& payload) {
    if (!connected_.load()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(queueMutex_);
    if (!connected_.load()) {
        return false;
    }

    if (queue_.size() >= kMaxQueuedMessages) {
        queue_.pop_front();
        droppedMessages_.fetch_add(1);
    }

    Outgoing outgoing;
    outgoing.header.type = type;
    outgoing.header.length = static_cast<std::uint32_t>(payload.size());
    outgoing.header.sequence = sequence_++;
    outgoing.payload = payload;
    queue_.push_back(std::move(outgoing));
    queueCv_.notify_one();
    return true;
}

} // namespace argb::transport
