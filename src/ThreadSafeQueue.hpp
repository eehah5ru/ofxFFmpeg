#ifndef SAFE_QUEUE
#define SAFE_QUEUE

#include <condition_variable>
#include <mutex>
#include <queue>
#include <optional>

template<typename T>
class ThreadsafeQueue {
  std::queue<T> queue_;
  mutable std::mutex mutex_;

  // Moved out of public interface to prevent races between this
  // and pop().
  bool empty() const {
    return queue_.empty();
  }

 public:
  ThreadsafeQueue() = default;
  ThreadsafeQueue(const ThreadsafeQueue<T> &) = delete ;
  ThreadsafeQueue& operator=(const ThreadsafeQueue<T> &) = delete ;

  ThreadsafeQueue(ThreadsafeQueue<T>&& other) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = std::move(other.queue_);
  }

  virtual ~ThreadsafeQueue() { }

  unsigned long size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  std::optional<T> pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return {};
    }
    T tmp = queue_.front();
    queue_.pop();
    return tmp;
  }

  void push(const T &item) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(item);
  }
};

// A threadsafe-queue.
// template <class T>
// class SafeQueue
// {
// public:
//     SafeQueue() : q(), m(), c() {}

//     ~SafeQueue() {}

//     // Add an element to the queue.
//     void enqueue(T t)
//     {
//         std::lock_guard<std::mutex> lock(m);
//         q.push(t);
//         c.notify_one();
//     }

//     // Get the front element.
//     // If the queue is empty, wait till a element is avaiable.
//     T dequeue(void)
//     {
//         std::unique_lock<std::mutex> lock(m);
//         while (q.empty())
//         {
//             // release lock as long as the wait and reaquire it afterwards.
//             c.wait(lock);
//         }
//         T val = q.front();
//         q.pop();
//         return val;
//     }

// private:
//     std::queue<T> q;
//     mutable std::mutex m;
//     std::condition_variable c;
// };
#endif
