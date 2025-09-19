#ifndef CHANNELS_CHANNEL_HPP
#define CHANNELS_CHANNEL_HPP

using chan_lock = std::unique_lock<std::mutex>;

template<typename T>
class channel {
  public:
    virtual ~channel() = default;

    virtual void put(T t) = 0;

    virtual T get() = 0;
};

#endif //CHANNELS_CHANNEL_HPP
