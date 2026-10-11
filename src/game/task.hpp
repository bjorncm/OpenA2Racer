#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace opena2::game {

// Lazily started coroutine. Awaiting a Task runs it and resumes the awaiting
// coroutine when it finishes. This lets the original menu's blocking loops
// (handle_form, snd_wait, requesters...) be ported as straight-line code:
// each loop iteration awaits the next frame instead of pumping messages.
template <typename T = void>
class Task;

namespace detail {

struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept
    {
        auto continuation = handle.promise().continuation;
        return continuation ? continuation : std::noop_coroutine();
    }
    void await_resume() const noexcept {}
};

struct PromiseBase {
    std::coroutine_handle<> continuation;
    std::exception_ptr exception;

    std::suspend_always initial_suspend() const noexcept { return {}; }
    FinalAwaiter final_suspend() const noexcept { return {}; }
    void unhandled_exception() noexcept { exception = std::current_exception(); }
};

} // namespace detail

template <typename T>
class Task {
public:
    struct promise_type : detail::PromiseBase {
        std::optional<T> value;

        Task get_return_object()
        {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        void return_value(T result) { value = std::move(result); }
    };

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Task()
    {
        if (handle_) handle_.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle_.promise().continuation = caller;
        return handle_;
    }
    T await_resume()
    {
        if (handle_.promise().exception) std::rethrow_exception(handle_.promise().exception);
        return std::move(*handle_.promise().value);
    }

    // For the root task driven by the frame loop.
    void start() { handle_.resume(); }
    bool done() const noexcept { return !handle_ || handle_.done(); }
    T result()
    {
        if (handle_.promise().exception) std::rethrow_exception(handle_.promise().exception);
        return std::move(*handle_.promise().value);
    }

private:
    explicit Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

    std::coroutine_handle<promise_type> handle_;
};

template <>
class Task<void> {
public:
    struct promise_type : detail::PromiseBase {
        Task get_return_object()
        {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        void return_void() noexcept {}
    };

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Task()
    {
        if (handle_) handle_.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle_.promise().continuation = caller;
        return handle_;
    }
    void await_resume()
    {
        if (handle_.promise().exception) std::rethrow_exception(handle_.promise().exception);
    }

    void start() { handle_.resume(); }
    bool done() const noexcept { return !handle_ || handle_.done(); }
    void result()
    {
        if (handle_.promise().exception) std::rethrow_exception(handle_.promise().exception);
    }

private:
    explicit Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

    std::coroutine_handle<promise_type> handle_;
};

// Suspends the running coroutine chain until the next frame. The frame loop
// resumes `pending` once per frame.
struct FrameScheduler {
    std::coroutine_handle<> pending;

    struct Awaiter {
        FrameScheduler& scheduler;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept { scheduler.pending = handle; }
        void await_resume() const noexcept {}
    };

    Awaiter nextFrame() { return Awaiter{*this}; }

    // Runs the suspended coroutine for one frame.
    void resume()
    {
        auto handle = std::exchange(pending, {});
        if (handle) handle.resume();
    }
};

} // namespace opena2::game
