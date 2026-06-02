#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace backend {

// Tiny, dependency-free WebSocket (RFC 6455) broadcast server.
//
// Scope: it accepts client connections, completes the opening handshake, and
// pushes server->client *text* frames via broadcast(). Inbound frames are
// drained and ignored except for close (drops the client) and ping (pong).
// That is all the telemetry-push use case needs. WebSocket++ / Asio would add
// a heavy dependency for no benefit here.
//
// Threading: start() then run() on a dedicated thread (run() blocks until
// stop()). broadcast() is safe to call from other threads.
class WebSocketServer {
public:
    WebSocketServer() = default;
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // Bind + listen on the port (all interfaces). Returns false on failure.
    bool start(std::uint16_t port);

    // Event loop. Blocks until stop() is called. Run on its own thread.
    void run();

    // Ask run() to return and close all sockets.
    void stop();

    // Broadcast a UTF-8 text message to every connected client.
    void broadcast(const std::string& message);

    std::size_t clientCount() const;
    const std::string& lastError() const { return error_; }

private:
    struct Client {
        int  fd        = -1;
        bool handshook = false;
        std::string inbuf;  // accumulates bytes until the handshake completes
    };

    void acceptClient();
    bool tryHandshake(Client& c);
    void serviceClient(std::size_t idx);  // read & handle inbound frames
    void dropClient(std::size_t idx);
    static bool sendTextFrame(int fd, const std::string& payload);

    void flushOutbox();

    int                      listen_fd_ = -1;
    int                      wake_pipe_[2] = {-1, -1};  // self-pipe to break poll()
    bool                     running_ = false;
    // All socket I/O happens on the run() thread, so clients_ needs no lock for
    // its own access. mutex_ guards the cross-thread bits: the outbox that
    // broadcast() fills and run() drains, plus client-count reads.
    std::vector<Client>      clients_;
    mutable std::mutex       mutex_;
    std::vector<std::string> outbox_;
    std::size_t              client_count_ = 0;  // mirror of clients_.size() under mutex_
    std::string              error_;
};

}  // namespace backend
