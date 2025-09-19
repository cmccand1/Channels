#ifndef CHANNELS_UNBUFFERED_CHANNEL_HPP
#define CHANNELS_UNBUFFERED_CHANNEL_HPP

#include <mutex>
#include <iostream>

#include "channel.hpp"

template<typename T>
class unbuffered_channel final : public channel<T> {
  public:
    explicit unbuffered_channel() = default;

    unbuffered_channel(const unbuffered_channel &other) = delete;

    unbuffered_channel(unbuffered_channel &&other) noexcept = delete;

    unbuffered_channel &operator=(const unbuffered_channel &other) = delete;

    unbuffered_channel &operator=(unbuffered_channel &&other) noexcept = delete;

    ~unbuffered_channel() = default;

    void put(T t) override;

    T get() override;

  private:
    T val_{};
    std::mutex mutex_;
    std::condition_variable cv_;
    bool ready_ = false;
};

/// Puts a value on the channel and blocks until
/// it is consumed by a consumer calling get()
template<typename T>
void unbuffered_channel<T>::put(T t) {
  // wait for the consumer
  {
    chan_lock lock(mutex_);
    cv_.wait(lock, [this] { return !ready_; });
    ready_ = true;
    val_ = std::move(t);
  } // unlocked
  cv_.notify_one();
}

/// Consumes a value from the channel, if present.
/// If there is no value to consume yet, this call will block until
/// the next value become available.
template<typename T>
T unbuffered_channel<T>::get() {
  T val;
  // wait for the producer
  {
    chan_lock lock(mutex_);
    cv_.wait(lock, [this] { return ready_; });
    val = std::move(val_);
    ready_ = false;
  } // unlocked
  cv_.notify_one();
  std::cout << "Consumer got: " << val << "\n";
  return val;
}


#endif //CHANNELS_UNBUFFERED_CHANNEL_HPP
