#include <thread>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>

#include "channel.hpp"

namespace {
std::mutex io_mutex;
}

template<std::size_t Size>
void producer(channel<int, Size> &chan) {
  for (int i = 0; i < 10'000; ++i) {
    chan.put(i);
  }
}

template<std::size_t Size>
void consumer(channel<int, Size> &chan, std::atomic<int> &failed_reads) {
  for (int i = 0; i < 10'000; ++i) {
    if (auto res = chan.try_get()) {
      std::lock_guard lock(io_mutex);
      std::cout << *res << "\n";
    } else {
      failed_reads.fetch_add(1, std::memory_order::relaxed);
      chan.get();
      std::lock_guard lock(io_mutex);
      std::cerr << "Error: " << error_string(res.error()) << "\n";
    }
  }
}

template<std::size_t Size>
void single_producer(channel<std::string, Size> &chan) {
  chan.put("Hello!");
}

template<std::size_t Size>
void single_consumer(channel<std::string, Size> &chan, std::atomic<int> &failed_reads) {
  if (auto res = chan.get()) {
    std::lock_guard lock(io_mutex);
    std::cout << *res << "\n";
  } else {
    failed_reads.fetch_add(1, std::memory_order::relaxed);
    std::lock_guard lock(io_mutex);
    std::cerr << "Error: " << error_string(res.error()) << "\n";
  }
}


int main() {
  constexpr std::size_t BUF_SIZE = 1000;
  std::atomic failed_reads{0};

  channel<std::string, BUF_SIZE> buf_chan{};

  const auto start = std::chrono::high_resolution_clock::now();

  constexpr int NUM_CONSUMERS = 1000;
  constexpr int NUM_PRODUCERS = 1000;
  std::vector<std::thread> producers;
  producers.reserve(NUM_PRODUCERS);
  for (int i = 0; i < NUM_PRODUCERS; ++i) {
    producers.emplace_back(single_producer<BUF_SIZE>, std::ref(buf_chan));
  }

  std::vector<std::thread> consumers;
  consumers.reserve(NUM_CONSUMERS);
  for (int i = 0; i < NUM_CONSUMERS; ++i) {
    consumers.emplace_back(single_consumer<BUF_SIZE>, std::ref(buf_chan), std::ref(failed_reads));
  }

  // join 'em
  for (auto &prod : producers) {
    prod.join();
  }

  for (auto &cons : consumers) {
    cons.join();
  }

  const auto end = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "Buf Size: " << BUF_SIZE << "\n";
  std::cout << "Time taken: " << duration.count() << " ms\n";
  std::cout << "Failed reads: " << failed_reads.load() << "\n";

  std::this_thread::sleep_for(std::chrono::seconds(1));

  return 0;
}
