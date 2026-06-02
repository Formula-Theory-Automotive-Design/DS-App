#include "ws_server.h"

#include "sha1.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>

namespace backend {

namespace {

const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string base64(const std::string& in) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(tbl[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(tbl[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// Case-insensitive header lookup; returns the trimmed value or "".
std::string headerValue(const std::string& req, const std::string& key) {
    std::string lowReq = req, lowKey = key;
    for (char& ch : lowReq) ch = static_cast<char>(::tolower(ch));
    for (char& ch : lowKey) ch = static_cast<char>(::tolower(ch));
    std::size_t pos = lowReq.find(lowKey + ":");
    if (pos == std::string::npos) return "";
    pos += lowKey.size() + 1;
    std::size_t end = req.find("\r\n", pos);
    if (end == std::string::npos) end = req.size();
    std::string v = req.substr(pos, end - pos);
    std::size_t a = v.find_first_not_of(" \t");
    std::size_t b = v.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return v.substr(a, b - a + 1);
}

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start(std::uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        error_ = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        error_ = std::string("bind(") + std::to_string(port) + "): " + std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (listen(listen_fd_, 8) < 0) {
        error_ = std::string("listen: ") + std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    setNonBlocking(listen_fd_);

    if (pipe(wake_pipe_) != 0) {
        error_ = std::string("pipe: ") + std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    setNonBlocking(wake_pipe_[0]);

    running_ = true;
    error_.clear();
    return true;
}

void WebSocketServer::stop() {
    if (!running_ && listen_fd_ < 0) return;
    running_ = false;
    // Wake the poll loop so run() can return promptly.
    if (wake_pipe_[1] >= 0) {
        const char b = 1;
        ssize_t r = ::write(wake_pipe_[1], &b, 1);
        (void)r;
    }
}

void WebSocketServer::broadcast(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        outbox_.push_back(message);
    }
    if (wake_pipe_[1] >= 0) {
        const char b = 1;
        ssize_t r = ::write(wake_pipe_[1], &b, 1);
        (void)r;
    }
}

std::size_t WebSocketServer::clientCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_count_;
}

void WebSocketServer::run() {
    std::vector<pollfd> pfds;
    while (running_) {
        pfds.clear();
        pfds.push_back({listen_fd_, POLLIN, 0});
        pfds.push_back({wake_pipe_[0], POLLIN, 0});
        for (const Client& c : clients_) {
            pfds.push_back({c.fd, POLLIN, 0});
        }

        int rc = ::poll(pfds.data(), pfds.size(), 200);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!running_) break;

        if (pfds[1].revents & POLLIN) {
            char drain[64];
            while (::read(wake_pipe_[0], drain, sizeof(drain)) > 0) {}
            flushOutbox();
        }
        if (pfds[0].revents & POLLIN) {
            acceptClient();
        }
        // Walk client pollfds (offset by the 2 fixed entries). Iterate by
        // matching fd so index drift from drops doesn't matter.
        for (std::size_t i = 2; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            for (std::size_t ci = 0; ci < clients_.size(); ++ci) {
                if (clients_[ci].fd == pfds[i].fd) {
                    serviceClient(ci);
                    break;
                }
            }
        }
    }

    for (Client& c : clients_) {
        if (c.fd >= 0) ::close(c.fd);
    }
    clients_.clear();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client_count_ = 0;
    }
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
    if (wake_pipe_[0] >= 0) { ::close(wake_pipe_[0]); wake_pipe_[0] = -1; }
    if (wake_pipe_[1] >= 0) { ::close(wake_pipe_[1]); wake_pipe_[1] = -1; }
}

void WebSocketServer::acceptClient() {
    for (;;) {
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) break;  // EAGAIN: no more pending
        setNonBlocking(fd);
        int yes = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        clients_.push_back(Client{fd, false, {}});
        std::lock_guard<std::mutex> lock(mutex_);
        client_count_ = clients_.size();
    }
}

bool WebSocketServer::tryHandshake(Client& c) {
    char buf[2048];
    ssize_t n = ::read(c.fd, buf, sizeof(buf));
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        return false;  // closed or error
    }
    c.inbuf.append(buf, static_cast<std::size_t>(n));
    if (c.inbuf.find("\r\n\r\n") == std::string::npos) {
        return c.inbuf.size() < 16384;  // keep waiting unless absurdly large
    }

    std::string key = headerValue(c.inbuf, "Sec-WebSocket-Key");
    if (key.empty()) return false;

    std::string accept = base64(sha1(key + kWsGuid));
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (::write(c.fd, resp.data(), resp.size()) < 0) return false;

    c.handshook = true;
    c.inbuf.clear();
    return true;
}

void WebSocketServer::serviceClient(std::size_t idx) {
    Client& c = clients_[idx];
    if (!c.handshook) {
        if (!tryHandshake(c)) dropClient(idx);
        return;
    }

    // Drain inbound frames. We only care about close (0x8) and ping (0x9);
    // any data frames are ignored (this is a push-only server).
    std::uint8_t hdr[2];
    ssize_t n = ::read(c.fd, hdr, 2);
    if (n == 0) { dropClient(idx); return; }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        dropClient(idx);
        return;
    }
    if (n < 2) return;  // partial; fine to drop on the floor for our purposes

    const std::uint8_t opcode = hdr[0] & 0x0F;
    std::uint64_t len = hdr[1] & 0x7F;
    const bool masked = (hdr[1] & 0x80) != 0;

    std::uint8_t ext[8];
    if (len == 126) { if (::read(c.fd, ext, 2) == 2) len = (ext[0] << 8) | ext[1]; }
    else if (len == 127) {
        if (::read(c.fd, ext, 8) == 8) {
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
        }
    }
    std::uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) { if (::read(c.fd, mask, 4) != 4) { dropClient(idx); return; } }

    // Consume (and discard) the payload.
    std::uint64_t remaining = len;
    char tmp[512];
    while (remaining > 0) {
        ssize_t r = ::read(c.fd, tmp,
                           remaining < sizeof(tmp) ? remaining : sizeof(tmp));
        if (r <= 0) break;
        remaining -= static_cast<std::uint64_t>(r);
    }

    if (opcode == 0x8) {            // close
        dropClient(idx);
    } else if (opcode == 0x9) {     // ping -> pong (empty)
        std::uint8_t pong[2] = {0x8A, 0x00};
        if (::write(c.fd, pong, 2) < 0) dropClient(idx);
    }
}

void WebSocketServer::dropClient(std::size_t idx) {
    if (idx >= clients_.size()) return;
    if (clients_[idx].fd >= 0) ::close(clients_[idx].fd);
    clients_.erase(clients_.begin() + idx);
    std::lock_guard<std::mutex> lock(mutex_);
    client_count_ = clients_.size();
}

void WebSocketServer::flushOutbox() {
    std::vector<std::string> msgs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        msgs.swap(outbox_);
    }
    if (msgs.empty()) return;

    for (std::size_t ci = 0; ci < clients_.size();) {
        Client& c = clients_[ci];
        if (!c.handshook) { ++ci; continue; }
        bool ok = true;
        for (const std::string& m : msgs) {
            if (!sendTextFrame(c.fd, m)) { ok = false; break; }
        }
        if (!ok) {
            dropClient(ci);  // erases; do not advance
        } else {
            ++ci;
        }
    }
}

bool WebSocketServer::sendTextFrame(int fd, const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));  // FIN + text opcode
    const std::size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len < 65536) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
        }
    }
    frame += payload;

    std::size_t sent = 0;
    while (sent < frame.size()) {
        ssize_t w = ::send(fd, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // brief backpressure
            return false;
        }
        sent += static_cast<std::size_t>(w);
    }
    return true;
}

}  // namespace backend
