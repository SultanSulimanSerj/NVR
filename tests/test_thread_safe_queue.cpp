#include "nvr/ThreadSafeQueue.hpp"

#include <gtest/gtest.h>

TEST(ThreadSafeQueue, PushPop) {
    nvr::ThreadSafeQueue<int> q(4);
    ASSERT_TRUE(q.tryPush(1));
    ASSERT_TRUE(q.tryPush(2));
    int v = 0;
    ASSERT_TRUE(q.waitAndPop(v));
    EXPECT_EQ(v, 1);
}

TEST(ThreadSafeQueue, DropNewWhenFull) {
    nvr::ThreadSafeQueue<int> q(2);
    ASSERT_TRUE(q.tryPush(1));
    ASSERT_TRUE(q.tryPush(2));
    ASSERT_FALSE(q.tryPush(3));
    EXPECT_EQ(q.droppedCount(), 1u);
}

TEST(ThreadSafeQueue, DropOldestEvictsFront) {
    nvr::ThreadSafeQueue<int> q(2, nvr::OverflowPolicy::DropOldest);
    q.tryPush(1);
    q.tryPush(2);
    q.tryPush(3);
    int v = 0;
    ASSERT_TRUE(q.waitAndPop(v));
    EXPECT_EQ(v, 2);
}
