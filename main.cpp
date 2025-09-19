#include <thread>
#include <functional>

#include "buffered_channel.hpp"
#include "channel.hpp"
#include "unbuffered_channel.hpp"

template<typename T>
using owned_channel = std::unique_ptr<channel<T> >;

void producer(const owned_channel<int> &chan) {
  for (int i = 0; i < 1000; ++i) {
    chan->put(i);
  }
}

void consumer(const owned_channel<int> &chan) {
  for (int i = 0; i < 10'000; ++i) {
    chan->get();
  }
}

int main() {
  owned_channel<int> buf_chan = std::make_unique<buffered_channel<int, 10> >();
  owned_channel<int> chan = std::make_unique<unbuffered_channel<int> >();
  std::thread prod{producer, std::ref(chan)};
  std::thread cons{consumer, std::ref(chan)};

  prod.join();
  cons.join();
  return 0;
}
