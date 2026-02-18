#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

#include "channel.hpp"

// ============================================================================
// Buffered channel tests
// ============================================================================

TEST(BufferedChannel, PutAndGet) {
  channel<int, 4> ch;
  ch.put(42);
  auto res = ch.get();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 42);
}

TEST(BufferedChannel, FIFO_Order) {
  channel<int, 4> ch;
  ch.put(1);
  ch.put(2);
  ch.put(3);
  EXPECT_EQ(*ch.get(), 1);
  EXPECT_EQ(*ch.get(), 2);
  EXPECT_EQ(*ch.get(), 3);
}

TEST(BufferedChannel, MoveSemantics) {
  channel<std::string, 4> ch;
  std::string msg = "hello";
  ch.put(std::move(msg));
  auto res = ch.get();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, "hello");
}

TEST(BufferedChannel, LvalueSemantics) {
  channel<std::string, 4> ch;
  std::string msg = "hello";
  ch.put(msg);
  EXPECT_EQ(msg, "hello"); // original should be unchanged
  auto res = ch.get();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, "hello");
}

TEST(BufferedChannel, TryGetReturnsValueWhenAvailable) {
  channel<int, 4> ch;
  ch.put(7);
  auto res = ch.try_get();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 7);
}

TEST(BufferedChannel, TryGetReturnsWouldBlockWhenEmpty) {
  channel<int, 4> ch;
  auto res = ch.try_get();
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), channel_error::would_block);
}

TEST(BufferedChannel, CloseUnblocksBlockedGet) {
  channel<int, 4> ch;
  std::atomic<bool> got_error{false};

  std::thread t([&] {
    auto res = ch.get();
    if (!res.has_value() && res.error() == channel_error::channel_closed) {
      got_error.store(true);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ch.close();
  t.join();
  EXPECT_TRUE(got_error.load());
}

TEST(BufferedChannel, CloseUnblocksBlockedPut) {
  channel<int, 2> ch;
  // Fill the buffer (capacity is N-1 = 1 for a ring buffer of size 2)
  ch.put(1);

  std::atomic<bool> got_exception{false};

  std::thread t([&] {
    try {
      ch.put(2); // should block because buffer is full
    } catch (const std::runtime_error &) {
      got_exception.store(true);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ch.close();
  t.join();
  EXPECT_TRUE(got_exception.load());
}

TEST(BufferedChannel, PutToClosedChannelThrows) {
  channel<int, 4> ch;
  ch.close();
  EXPECT_THROW(ch.put(1), std::runtime_error);
}

TEST(BufferedChannel, GetFromClosedEmptyReturnsError) {
  channel<int, 4> ch;
  ch.close();
  auto res = ch.get();
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), channel_error::channel_closed);
}

TEST(BufferedChannel, DrainAfterClose) {
  channel<int, 4> ch;
  ch.put(10);
  ch.put(20);
  ch.close();

  // Should still be able to read buffered values
  auto r1 = ch.get();
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(*r1, 10);

  auto r2 = ch.get();
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(*r2, 20);

  // Now it should report closed
  auto r3 = ch.get();
  ASSERT_FALSE(r3.has_value());
  EXPECT_EQ(r3.error(), channel_error::channel_closed);
}

TEST(BufferedChannel, TryGetFromClosedEmptyReturnsChannelClosed) {
  channel<int, 4> ch;
  ch.close();
  auto res = ch.try_get();
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), channel_error::channel_closed);
}

TEST(BufferedChannel, IsBuffered) {
  static_assert(channel<int, 4>::is_buffered());
  channel<int, 4> ch;
  EXPECT_TRUE(ch.is_buffered());
}

TEST(BufferedChannel, IsClosedReflectsState) {
  channel<int, 4> ch;
  EXPECT_FALSE(ch.is_closed());
  ch.close();
  EXPECT_TRUE(ch.is_closed());
}

// ============================================================================
// Unbuffered channel tests
// ============================================================================

TEST(UnbufferedChannel, PutAndGet) {
  channel<int> ch; // default N=1, unbuffered

  std::thread producer([&] {
    ch.put(42);
  });

  auto res = ch.get();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, 42);
  producer.join();
}

TEST(UnbufferedChannel, MoveSemantics) {
  channel<std::string> ch;

  std::thread producer([&] {
    ch.put(std::string("world"));
  });

  auto res = ch.get();
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, "world");
  producer.join();
}

TEST(UnbufferedChannel, CloseUnblocksBlockedGet) {
  channel<int> ch;
  std::atomic<bool> got_error{false};

  std::thread t([&] {
    auto res = ch.get();
    if (!res.has_value() && res.error() == channel_error::channel_closed) {
      got_error.store(true);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ch.close();
  t.join();
  EXPECT_TRUE(got_error.load());
}

TEST(UnbufferedChannel, CloseUnblocksBlockedPut) {
  channel<int> ch;

  // First put will block waiting for a consumer (unbuffered).
  // But if channel has no val yet, put will succeed and block waiting for
  // a consumer to take it. Let's fill it first.
  std::atomic<bool> first_put_done{false};
  std::thread filler([&] {
    ch.put(1);
    first_put_done.store(true);
  });

  // Wait a bit for filler to deposit its value
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::atomic<bool> got_exception{false};
  std::thread t([&] {
    try {
      ch.put(2); // should block because has_val_ is true
    } catch (const std::runtime_error &) {
      got_exception.store(true);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ch.close();
  t.join();
  EXPECT_TRUE(got_exception.load());

  // Clean up: drain the first value so filler thread's put completes
  // (it already deposited, so get should return immediately)
  [[maybe_unused]] auto drain = ch.get();
  filler.join();
}

TEST(UnbufferedChannel, PutToClosedChannelThrows) {
  channel<int> ch;
  ch.close();
  EXPECT_THROW(ch.put(1), std::runtime_error);
}

TEST(UnbufferedChannel, GetFromClosedEmptyReturnsError) {
  channel<int> ch;
  ch.close();
  auto res = ch.get();
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), channel_error::channel_closed);
}

TEST(UnbufferedChannel, TryGetReturnsWouldBlockWhenEmpty) {
  channel<int> ch;
  auto res = ch.try_get();
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), channel_error::would_block);
}

TEST(UnbufferedChannel, IsNotBuffered) {
  static_assert(!channel<int>::is_buffered());
  channel<int> ch;
  EXPECT_FALSE(ch.is_buffered());
}

// ============================================================================
// Concurrency stress tests
// ============================================================================

TEST(ConcurrencyTest, ManyProducersManyConsumers) {
  constexpr int NUM_THREADS = 100;
  constexpr int ITEMS_PER_THREAD = 100;
  channel<int, 64> ch;

  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};

  std::vector<std::thread> producers;
  producers.reserve(NUM_THREADS);
  for (int i = 0; i < NUM_THREADS; ++i) {
    producers.emplace_back([&] {
      for (int j = 0; j < ITEMS_PER_THREAD; ++j) {
        ch.put(j);
        total_produced.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::vector<std::thread> consumers;
  consumers.reserve(NUM_THREADS);
  for (int i = 0; i < NUM_THREADS; ++i) {
    consumers.emplace_back([&] {
      for (int j = 0; j < ITEMS_PER_THREAD; ++j) {
        auto res = ch.get();
        if (res.has_value()) {
          total_consumed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto &t : producers) t.join();
  for (auto &t : consumers) t.join();

  EXPECT_EQ(total_produced.load(), NUM_THREADS * ITEMS_PER_THREAD);
  EXPECT_EQ(total_consumed.load(), NUM_THREADS * ITEMS_PER_THREAD);
}

// ============================================================================
// error_string tests
// ============================================================================

TEST(ErrorString, ChannelClosed) {
  EXPECT_EQ(error_string(channel_error::channel_closed), "Channel is closed");
}

TEST(ErrorString, WouldBlock) {
  EXPECT_EQ(error_string(channel_error::would_block), "Operation would block, try again later");
}
