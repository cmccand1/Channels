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
  would_block,
  closed_and_drained
};

inline std::string error_string(channel_error e) {
  switch (e) {
    case channel_error::channel_closed:
      return "Chanel is closed";
    case channel_error::would_block:
      return "Operation would block, try again later";
    default:
      return "Unexpected channel error";
  }
}

template<std::movable T>
using result = std::expected<T, channel_error>;

template<std::movable T, std::size_t N = 1>
class channel final {
  public:
    explicit channel() = default;

    channel(const channel &other) = delete;

    channel(channel &&other) noexcept = delete;

    channel &operator=(const channel &other) = delete;

    channel &operator=(channel &&other) noexcept = delete;

    ~channel() = default;

    void put_unbuffered(T &&t);

    void put_unbuffered(const T &t);

    result<T> get_unbuffered();

    void put(const T &t);

    void put(T &&t);

    /**
     * Blocks until a value is available to get off the channel.
     * If the channel is closed, returns an error.
     */
    [[nodiscard]] result<T> get();

    /**
     * Non-blocking attempt to get a value from the channel.
     * If the channel is closed or no value is available, returns an error.
     */
    [[nodiscard]] result<T> try_get();

    /**
     * Checks if the channel is buffered (i.e., has a buffer size greater than 1).
     * @return true if the channel is buffered, false otherwise.
     */
    [[nodiscard]] bool is_buffered() const;

    /**
     * Checks if the channel is closed. A closed channel cannot accept new values,
     * but can still be read from until it is empty.
     * @return true if the channel is closed, false otherwise.
     */
    [[nodiscard]] bool is_closed() const;

    void close();

  private:
    // buffered channel
    std::array<T, N> buf_{};
    std::size_t in_{0};
    std::size_t out_{0};

    // unbuffered channel
    T val_{};
    bool has_val_{};

    // common
    std::mutex mutex_{};
    std::condition_variable consumers_{};
    std::condition_variable producers_{};
    bool closed_{};

    void put_buffered(T &&t);

    void put_buffered(const T &t);

    [[nodiscard]] result<T> get_buffered();

    result<T> try_get_unbuffered();

    [[nodiscard]] result<T> try_get_buffered();

    [[nodiscard]] bool is_empty() const;

    [[nodiscard]] bool is_full() const;
};

template<std::movable T, std::size_t N>
void channel<T, N>::put(const T &t) {
  if (closed_) {
    throw std::runtime_error("Error: attempted to put to a closed channel.");
  }
  if (is_buffered()) {
    put_buffered(t);
    return;
  }
  put_unbuffered(t);
}

template<std::movable T, std::size_t N>
void channel<T, N>::put(T &&t) {
  if (closed_) {
    throw std::runtime_error("Error: attempted to put to a closed channel.");
  }
  if (is_buffered()) {
    put_buffered(std::move(t));
    return;
  }
  put_unbuffered(std::move(t));
}

template<std::movable T, std::size_t N>
void channel<T, N>::put_buffered(const T &t) {
  //
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !is_full(); });
    buf_[in_] = t; // copy for lvalues
    in_ = (in_ + 1) % buf_.size();
  }
  consumers_.notify_all();
}

template<std::movable T, std::size_t N>
void channel<T, N>::put_buffered(T &&t) {
  //
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !is_full(); });
    buf_[in_] = std::move(t);
    in_ = (in_ + 1) % buf_.size();
  }
  consumers_.notify_all();
}

template<std::movable T, std::size_t N>
void channel<T, N>::put_unbuffered(const T &t) {
  //
  {
    chan_lock lock(mutex_);
    producers_.wait(lock, [this] { return !has_val_; });
    val_ = t; // copy for lvalues
    has_val_ = true;
  }
  consumers_.notify_all();
}

template<std::movable T, std::size_t N>
void channel<T, N>::put_unbuffered(T &&t) {
  //
  {
    chan_lock lock(mutex_);
    producers_.wait(lock,
                    [this] {
                      return !has_val_;
                    });
    val_ = std::move(t);
    has_val_ = true;
  }
  consumers_.notify_all();
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::get_unbuffered() {
  T val;
  //
  {
    chan_lock lock(mutex_);
    consumers_.wait(lock,
                    [this] { return has_val_; });
    val = std::move(val_);
    has_val_ = false;
  }
  producers_.notify_all();
  return result<T>(val);
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::get() {
  if (closed_) return std::unexpected(channel_error::channel_closed);
  if (is_buffered()) { return get_buffered(); }

  return get_unbuffered();
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::get_buffered() {
  T val;
  //
  {
    chan_lock lock(mutex_);
    consumers_.wait(lock, [this] { return !is_empty(); });
    val = std::move(buf_[out_]);
    out_ = (out_ + 1) % buf_.size();
  }
  producers_.notify_all();
  return result<T>(val);
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::try_get() {
  if (closed_) return std::unexpected(channel_error::channel_closed);
  if (is_buffered()) { return try_get_buffered(); }
  return try_get_unbuffered();
}

template<std::movable T, std::size_t N>
result<T> channel<T, N>::try_get_buffered() {
  T val;
  //
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
  }
  producers_.notify_one();
  return result<T>(val);
}

template <std::movable T, std::size_t N>
result<T> channel<T, N>::try_get_unbuffered() {
  T val;
  //
  {
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
  producers_.notify_all();
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
void channel<T, N>::close() {
  closed_ = true;
}

template<std::movable T, std::size_t N>
bool channel<T, N>::is_full() const {
  return (in_ + 1) % buf_.size() == out_;
}

#endif //CHANNELS_BUFFERED_CHANNEL_HPP
