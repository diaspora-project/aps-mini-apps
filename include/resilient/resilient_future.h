#ifndef RESILIENT_FUTURE_H
#define RESILIENT_FUTURE_H

#include <future>
#include <memory>
#include <stdexcept>

namespace resilient {

template <typename T>
class ResilientFuture {
private:
    std::shared_ptr<std::promise<T>> promise;
    std::shared_ptr<std::future<T>> future;

public:
    ResilientFuture() {
        promise = std::make_shared<std::promise<T>>();
        future = std::make_shared<std::future<T>>(promise->get_future());
    }

    // Set the result of the future
    void set_result(const T &result) {
        promise->set_value(result);
    }

    // Set an exception if the operation fails
    void set_exception(const std::exception_ptr &exception) {
        promise->set_exception(exception);
    }

    // Check if the future is ready
    bool is_ready() const {
        return future->wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    // Get the result (blocks if not ready)
    T get() {
        return future->get();
    }
};

} // namespace resilient

#endif // RESILIENT_FUTURE_H