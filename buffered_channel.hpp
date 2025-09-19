#ifndef CHANNELS_BUFFERED_CHANNEL_HPP
#define CHANNELS_BUFFERED_CHANNEL_HPP
#include <iostream>
#include <mutex>
#include <queue>

#include "channel.hpp"

template<typename T, std::size_t N>
using ring_buffer = std::array<T, N>;

template<typename T, std::size_t N>
class buffered_channel final : public channel<T> {
  public:
    explicit buffered_channel() = default;

    buffered_channel(const buffered_channel &other) = delete;

    buffered_channel(buffered_channel &&other) noexcept = delete;

    buffered_channel &operator=(const buffered_channel &other) = delete;

    buffered_channel &operator=(buffered_channel &&other) noexcept = delete;

    ~buffered_channel() = default;

    void put(T t) override;

    T get() override;

  private:
    ring_buffer<T, N> buf_;
    std::size_t in_;
    std::size_t out_;
    std::mutex mutex_;
    std::condition_variable cv_;

    bool is_full() const;

    bool is_empty() const;

    std::string buf_string() const;
};

template<typename T, std::size_t N>
void buffered_channel<T, N>::put(T t) {
  // wait if at capacity
  {
    chan_lock lock(mutex_);
    cv_.wait(lock, [this] { return is_full(); });
    buf_[in_] = std::move(t);
    in_ = (in_ + 1) % buf_.size();
  } // unlocked
  cv_.notify_one();
}


template<typename T, std::size_t N>
T buffered_channel<T, N>::get() {
  T val;
  // wait if empty
  {
    chan_lock lock(mutex_);
    cv_.wait(lock, [this] { return is_empty(); });
    val = std::move(buf_[out_]);
    out_ = (out_ + 1) % buf_.size();
  } // unlocked
  cv_.notify_one();
  std::cout << "Consumer got: " << val << "\n";
  return val;
}

template<typename T, std::size_t N>
bool buffered_channel<T, N>::is_empty() const {
  return in_ == out_;
}

template<typename T, std::size_t N>
bool buffered_channel<T, N>::is_full() const {
  return (in_ + 1) % buf_.size() == out_;
}

template<typename T, std::size_t N>
std::string buffered_channel<T, N>::buf_string() const {
  std::size_t buf_len = buf_.size();
  std::string buf_str = std::string("[");
  for (std::size_t i = in_; i != out_; i = (i + 1) % buf_len) {
    buf_str.append(std::to_string(buf_[i]));
    if (i == (out_ - 1)) {
      buf_str.append("]");
    } else {
      buf_str.append(" ");
    }
  }
  return buf_str;
}

#endif //CHANNELS_BUFFERED_CHANNEL_HPP
