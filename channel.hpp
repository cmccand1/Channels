#ifndef CHANNELS_BUFFERED_CHANNEL_HPP
#define CHANNELS_BUFFERED_CHANNEL_HPP
#include <mutex>
#include <array>
#include <condition_variable>
#include <expected>
#include <utility>
#include <concepts>
#include <stdexcept>

// A unique lock that will unlock on scope exit
using chan_lock = std::unique_lock<std::mutex>;

enum class channel_error {
  channel_closed,
  would_block,
};

inline std::string error_string(channel_error e) {
  switch (e) {
    case channel_error::channel_closed:
      return "Channel is closed";
    case channel_error::would_block:
      return "Operation would block, try again later";
    default:
      return "Unexpected channel error";
  }
}

template<std::movable T>
using result = std::expected<T, channel_error>;

// ============================================================================
// Primary template: buffered channel (N > 1)
// ============================================================================
template<std::movable T, std::size_t N = 1>
class channel final {
  public:
    explicit channel() = default;

    channel(const channel &other) = delete;
    channel(channel &&other) noexcept = delete;
    channel &operator=(const channel &other) = delete;
    channel &operator=(channel &&other) noexcept = delete;
    ~channel() = default;

    void put(const T &t);
    void put(T &&t);

    /**
     * Blocks until a value is available to get off the channel.
     * If the channel is closed and empty, returns an error.
     */
    [[nodiscard]] result<T> get();

    /**
     * Non-blocking attempt to get a value from the channel.
     * If the channel is closed and empty, or no value is available, returns an error.
     */
    [[nodiscard]] result<T> try_get();

    /**
     * Checks if the channel is buffered (i.e., has a buffer size greater than 1).
     * @return true if the channel is buffered, false otherwise.
     */
    [[nodiscard]] static constexpr bool is_buffered() { return true; }

    /**
     * Checks if the channel is closed. A closed channel cannot accept new values,
     * but can still be read from until it is empty.
     * @return true if the channel is closed, false otherwise.
     */
    [[nodiscard]] bool is_closed() const;

    void close();

  private:
    std::array<T, N> buf_{};
    std::size_t in_{0};
    std::size_t out_{0};

    mutable std::mutex mutex_{};
    std::condition_variable consumers_{};
    std::condition_variable producers_{};
    bool closed_{};

    [[nodiscard]] bool is_empty() const;
    [[nodiscard]] bool is_full() const;
};

// -- buffered put (lvalue) --
template<std::movable T, std::size_t N>
void channel<T, N>::put(const T &t) {
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !is_full() || closed_; });
    if (closed_) {
      throw std::runtime_error("Error: attempted to put to a closed channel.");
    }
    buf_[in_] = t;
    in_ = (in_ + 1) % buf_.size();
  }
  consumers_.notify_all();
}

// -- buffered put (rvalue) --
template<std::movable T, std::size_t N>
void channel<T, N>::put(T &&t) {
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !is_full() || closed_; });
    if (closed_) {
      throw std::runtime_error("Error: attempted to put to a closed channel.");
    }
    buf_[in_] = std::move(t);
    in_ = (in_ + 1) % buf_.size();
  }
  consumers_.notify_all();
}

// -- buffered get --
template<std::movable T, std::size_t N>
result<T> channel<T, N>::get() {
  T val;
  {
    chan_lock lock(mutex_);
    consumers_.wait(lock, [this] { return !is_empty() || closed_; });
    if (is_empty()) {
      return std::unexpected(channel_error::channel_closed);
    }
    val = std::move(buf_[out_]);
    out_ = (out_ + 1) % buf_.size();
  }
  producers_.notify_all();
  return result<T>(val);
}

// -- buffered try_get --
template<std::movable T, std::size_t N>
result<T> channel<T, N>::try_get() {
  T val;
  {
    chan_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return std::unexpected(channel_error::would_block);
    }
    if (is_empty()) {
      if (closed_) return std::unexpected(channel_error::channel_closed);
      return std::unexpected(channel_error::would_block);
    }
    val = std::move(buf_[out_]);
    out_ = (out_ + 1) % buf_.size();
  }
  producers_.notify_all();
  return result<T>(val);
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_empty() const {
  return in_ == out_;
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_full() const {
  return (in_ + 1) % buf_.size() == out_;
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_closed() const {
  chan_lock lock(mutex_);
  return closed_;
}

template<std::movable T, std::size_t N>
void channel<T, N>::close() {
  {
    chan_lock lock(mutex_);
    closed_ = true;
  }
  consumers_.notify_all();
  producers_.notify_all();
}

// ============================================================================
// Partial specialization: unbuffered channel (N == 1)
// ============================================================================
template<std::movable T>
class channel<T, 1> final {
  public:
    explicit channel() = default;

    channel(const channel &other) = delete;
    channel(channel &&other) noexcept = delete;
    channel &operator=(const channel &other) = delete;
    channel &operator=(channel &&other) noexcept = delete;
    ~channel() = default;

    void put(const T &t);
    void put(T &&t);

    /**
     * Blocks until a value is available to get off the channel.
     * If the channel is closed and empty, returns an error.
     */
    [[nodiscard]] result<T> get();

    /**
     * Non-blocking attempt to get a value from the channel.
     * If the channel is closed and empty, or no value is available, returns an error.
     */
    [[nodiscard]] result<T> try_get();

    [[nodiscard]] static constexpr bool is_buffered() { return false; }

    /**
     * Checks if the channel is closed. A closed channel cannot accept new values,
     * but can still be read from until it is empty.
     * @return true if the channel is closed, false otherwise.
     */
    [[nodiscard]] bool is_closed() const;

    void close();

  private:
    T val_{};
    bool has_val_{};

    mutable std::mutex mutex_{};
    std::condition_variable consumers_{};
    std::condition_variable producers_{};
    bool closed_{};
};

// -- unbuffered put (lvalue) --
template<std::movable T>
void channel<T, 1>::put(const T &t) {
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !has_val_ || closed_; });
    if (closed_) {
      throw std::runtime_error("Error: attempted to put to a closed channel.");
    }
    val_ = t;
    has_val_ = true;
  }
  consumers_.notify_all();
}

// -- unbuffered put (rvalue) --
template<std::movable T>
void channel<T, 1>::put(T &&t) {
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !has_val_ || closed_; });
    if (closed_) {
      throw std::runtime_error("Error: attempted to put to a closed channel.");
    }
    val_ = std::move(t);
    has_val_ = true;
  }
  consumers_.notify_all();
}

// -- unbuffered get --
template<std::movable T>
result<T> channel<T, 1>::get() {
  T val;
  {
    chan_lock lock(mutex_);
    consumers_.wait(lock, [this] { return has_val_ || closed_; });
    if (!has_val_) {
      return std::unexpected(channel_error::channel_closed);
    }
    val = std::move(val_);
    has_val_ = false;
  }
  producers_.notify_all();
  return result<T>(val);
}

// -- unbuffered try_get --
template<std::movable T>
result<T> channel<T, 1>::try_get() {
  T val;
  {
    chan_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return std::unexpected(channel_error::would_block);
    }
    if (!has_val_) {
      if (closed_) return std::unexpected(channel_error::channel_closed);
      return std::unexpected(channel_error::would_block);
    }
    val = std::move(val_);
    has_val_ = false;
  }
  producers_.notify_all();
  return result<T>(val);
}

template<std::movable T>
bool channel<T, 1>::is_closed() const {
  chan_lock lock(mutex_);
  return closed_;
}

template<std::movable T>
void channel<T, 1>::close() {
  {
    chan_lock lock(mutex_);
    closed_ = true;
  }
  consumers_.notify_all();
  producers_.notify_all();
}

#endif //CHANNELS_BUFFERED_CHANNEL_HPP
