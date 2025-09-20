#ifndef CHANNELS_BUFFERED_CHANNEL_HPP
#define CHANNELS_BUFFERED_CHANNEL_HPP
#include <mutex>
#include <array>
#include <condition_variable>
#include <expected>
#include <utility>
#include <concepts>

// A unique lock that will unlock on scope exit
using chan_lock = std::unique_lock<std::mutex>;

enum class channel_error {
  channel_closed,
  would_block
};

template<std::movable T>
using result = std::expected<T, channel_error>;

std::string error_string(channel_error e) {
  switch (e) {
    case channel_error::channel_closed:
      return "Chanel is closed";
    case channel_error::would_block:
      return "Operation would block, try again later";
    default:
      return "Unexpected channel error";
  }
}

template<std::movable T, std::size_t N = 1>
class channel final {
  public:
    explicit channel() = default;

    // no copy constructor
    channel(const channel &other) = delete;

    // no move constructor
    channel(channel &&other) noexcept = delete;

    // no copy assignment
    channel &operator=(const channel &other) = delete;

    // no move assignment
    channel &operator=(channel &&other) noexcept = delete;

    ~channel() = default;

    void put(T t);

    result<T> get();

    /**
     * Makes a non-blocking attempt to get a value off the channel.
     *
     */
    result<T> try_get();

    bool is_buffered() const;

    bool is_closed() const;

  private:
    // buffered channel
    std::array<T, N> buf_{};
    std::size_t in_{0};
    std::size_t out_{0};
    // size N+1 to distinguish full vs empty

    // unbuffered channel
    T val_{};
    bool has_val_{};

    // common
    std::mutex mutex_{};
    std::condition_variable consumers_{};
    std::condition_variable producers_{};
    bool closed_{};

    void put_buffered(T &&t);

    result<T> get_buffered();

    result<T> try_get_buffered();

    bool is_empty() const;

    bool is_full() const;
};

template<std::movable T, std::size_t N>
void channel<T, N>::put(T t) {
  if (is_buffered()) {
    put_buffered(std::forward<T>(t));
    return;
  }
  // wait while full (i.e., value present)
  {
    chan_lock lock(mutex_);
    producers_.wait(lock,
             [this] {
               return !has_val_;
             });
    val_ = std::move(t);
    has_val_ = true;
  }
  // notify a waiting consumer that data is available
  consumers_.notify_one();
}


template<std::movable T, std::size_t N>
result<T> channel<T, N>::get() {
  if (closed_) return std::unexpected(channel_error::channel_closed);
  if (is_buffered()) {
    return get_buffered();
  }

  T val;
  // wait while empty
  {
    chan_lock lock(mutex_);
    consumers_.wait(lock,
             [this] { return has_val_; });
    val = std::move(val_);
    has_val_ = false;
  }
  // notify a waiting producer that space is available
  producers_.notify_one();
  return result<T>(val);
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::try_get() {
  if (closed_) return std::unexpected(channel_error::channel_closed);
  if (is_buffered()) {
    return try_get_buffered();
  }
  T val;
  {
    // Non-blocking attempt
    chan_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return std::unexpected(channel_error::would_block);
    }
    if (!has_val_) {
      return std::unexpected(channel_error::would_block);
    }
    val = std::move(val_);
    has_val_ = false;
  }
  // notify a waiting producer that space is available
  producers_.notify_one();
  return result<T>(val);
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_empty() const {
  return in_ == out_;
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_buffered() const {
  return buf_.size() > 1;
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_closed() const {
  return closed_;
}

template<std::movable T, std::size_t N>
void channel<T, N>::put_buffered(T &&t) {
  // wait while full
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !is_full(); });
    buf_[in_] = std::move(t);
    in_ = (in_ + 1) % buf_.size();
  } // unlocked
  // notify a waiting consumer that data is available
  consumers_.notify_one();
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::get_buffered() {
  T val;
  // wait while buffer is empty
  {
    chan_lock lock(mutex_);
    consumers_.wait(lock, [this] { return !is_empty(); });
    val = std::move(buf_[out_]);
    out_ = (out_ + 1) % buf_.size();
  } // unlocked
  // notify a waiting producer that space is available
  producers_.notify_one();
  return result<T>(val);
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::try_get_buffered() {
  T val;
  // Non-blocking attempt to get from buffer
  {
    chan_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return std::unexpected(channel_error::would_block);
    }
    if (is_empty()) {
      return std::unexpected(channel_error::would_block);
    }
    val = std::move(buf_[out_]);
    out_ = (out_ + 1) % buf_.size();
  } // unlocked
  // notify a waiting producer that space is available
  producers_.notify_one();
  return result<T>(val);
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_full() const {
  return (in_ + 1) % buf_.size() == out_;
}

#endif //CHANNELS_BUFFERED_CHANNEL_HPP
