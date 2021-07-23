#pragma once

#include <functional>
#include <system_error>

#include "iouring.hpp"
#include "slotmap.hpp"

class IoQueue {
private:
    using CompletionHandler = std::function<void(const io_uring_cqe*)>;

    static constexpr auto Ignore = std::numeric_limits<uint64_t>::max();

public:
    using HandlerEc = std::function<void(std::error_code ec)>;
    using HandlerEcRes = std::function<void(std::error_code ec, int res)>;

    IoQueue()
        : completionHandlers_(1024)
    {
        ring_.init();
    }

    // res argument is socket fd
    void accept(int fd, sockaddr_in* addr, HandlerEcRes cb)
    {
        socklen_t addrlen = 0;
        addSqe(ring_.prepareAccept(fd, reinterpret_cast<sockaddr*>(addr), &addrlen), std::move(cb));
    }

    // res argument is sent bytes
    void send(int sockfd, const void* buf, size_t len, HandlerEcRes cb)
    {
        addSqe(ring_.prepareSend(sockfd, buf, len), std::move(cb));
    }

    // res argument is received bytes
    void recv(int sockfd, void* buf, size_t len, HandlerEcRes cb)
    {
        addSqe(ring_.prepareRecv(sockfd, buf, len), std::move(cb));
    }

    void recv(int sockfd, void* buf, size_t len, uint64_t timeoutMs, HandlerEcRes cb)
    {
        addSqe(ring_.prepareRecv(sockfd, buf, len), timeoutMs, std::move(cb));
    }

    void close(int fd, HandlerEc cb)
    {
        addSqe(ring_.prepareClose(fd), std::move(cb));
    }

    void run()
    {
        while (true) {
            const auto cqe = ring_.waitCqe();
            assert(cqe);

            if (cqe->user_data != Ignore) {
                assert(completionHandlers_.contains(cqe->user_data));
                auto ch = std::move(completionHandlers_[cqe->user_data]);
                ch(cqe);
                completionHandlers_.remove(cqe->user_data);
                ring_.advanceCq();
            } else {
                std::cout << "ignored" << std::endl;
            }
        }
    }

private:
    size_t addHandler(HandlerEc&& cb)
    {
        return completionHandlers_.emplace([cb = std::move(cb)](const io_uring_cqe* cqe) {
            if (cqe->res < 0) {
                cb(std::make_error_code(static_cast<std::errc>(-cqe->res)));
            } else {
                cb(std::error_code());
            }
        });
    }

    size_t addHandler(HandlerEcRes&& cb)
    {
        return completionHandlers_.emplace([cb = std::move(cb)](const io_uring_cqe* cqe) {
            if (cqe->res < 0) {
                cb(std::make_error_code(static_cast<std::errc>(-cqe->res)), -1);
            } else {
                cb(std::error_code(), cqe->res);
            }
        });
    }

    template <typename Callback>
    void addSqe(io_uring_sqe* sqe, Callback&& cb)
    {
        sqe->user_data = addHandler(std::move(cb));
        ring_.submitSqes();
    }

    template <typename Callback>
    void addSqe(io_uring_sqe* sqe, size_t timeoutMs, Callback&& cb)
    {
        sqe->user_data = addHandler(std::move(cb));
        sqe->flags |= IOSQE_IO_LINK;
        __kernel_timespec ts;
        ts.tv_sec = timeoutMs / 1000;
        ts.tv_nsec = (timeoutMs % 1000) * 1000 * 1000;
        auto timeoutSqe = ring_.prepareLinkTimeout(&ts);
        timeoutSqe->user_data = Ignore;
        ring_.submitSqes();
    }

    IoURing ring_;
    SlotMap<CompletionHandler> completionHandlers_;
};
