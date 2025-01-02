#include <iostream>
#include <memory>
#include <exception>
#include <mutex>
#include <stack>
#include <thread>
#include <vector>
#include <cassert>
#include <algorithm>

struct empty_stack : std::exception {
    const char* what() const noexcept override {
        return "empty stack!";
    }
};

template<typename T>
class threadsafe_stack {
private:
    std::stack<T> data;
    mutable std::mutex m;

public:
    threadsafe_stack() = default;

    threadsafe_stack(const threadsafe_stack& other) {
        std::lock_guard<std::mutex> lock(other.m);
        data = other.data;
    }

    threadsafe_stack& operator=(const threadsafe_stack&) = delete;

    void push(T new_value) {
        std::lock_guard<std::mutex> lock(m);
        data.push(std::move(new_value)); // Use move semantics for efficiency
    }

    std::shared_ptr<T> pop() {
        std::lock_guard<std::mutex> lock(m);
        if (data.empty()) throw empty_stack();

        auto res = std::make_shared<T>(data.top());
        data.pop();
        return res;
    }

    void pop(T& value) {
        std::lock_guard<std::mutex> lock(m);
        if (data.empty()) throw empty_stack();

        value = data.top();
        data.pop();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m);
        return data.empty();
    }
};

void concurrent_push(threadsafe_stack<int>& stack, int start, int end) {
    for (int i = start; i < end; ++i) {
        stack.push(i);
    }
}

void concurrent_pop(threadsafe_stack<int>& stack, std::vector<int>& results, int count, std::mutex& results_mutex) {
    for (int i = 0; i < count; ++i) {
        try {
            auto value = stack.pop();
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(*value);
        } catch (const empty_stack&) {
            // Handle empty stack case
            // Optionally log or ignore
            // std::cerr << "Attempted to pop from an empty stack." << std::endl;
        }
    }
}

void test_concurrent_operations() {
    threadsafe_stack<int> stack;

    const int num_threads = 5;
    const int items_per_thread = 100;

    std::vector<std::thread> threads;

    // Start threads to push items into the stack
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(concurrent_push, std::ref(stack), i * items_per_thread, (i + 1) * items_per_thread);
    }

    // Wait for all pushing threads to finish
    for (auto& thread : threads) {
        thread.join();
    }

    threads.clear();

    std::vector<int> results;
    std::mutex results_mutex;

    // Start threads to pop items from the stack
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(concurrent_pop, std::ref(stack), std::ref(results), items_per_thread, std::ref(results_mutex));
    }

    // Wait for all popping threads to finish
    for (auto& thread : threads) {
        thread.join();
    }

    // Check if all items were popped correctly
    assert(results.size() == num_threads * items_per_thread);

    // Verify that results contain all expected values
    std::vector<int> expected_results(num_threads * items_per_thread);
    
    for (int i = 0; i < expected_results.size(); ++i) {
        expected_results[i] = i;
    }

    // Sort results and expected_results for comparison
    std::sort(results.begin(), results.end());
    
    assert(results == expected_results);
}

void test_sequential_operations() {
    threadsafe_stack<int> stack;

    // Test push and pop operations
    stack.push(1);
    stack.push(2);
    stack.push(3);

    int value;

    // Pop should return 3
    stack.pop(value);
    assert(value == 3);

    // Pop should return 2
    stack.pop(value);
    assert(value == 2);

    // Pop should return 1
    stack.pop(value);
    assert(value == 1);

    // Check if the stack is empty
    assert(stack.empty());
}

int main() {
   try {
       test_sequential_operations();
       test_concurrent_operations();
       std::cout << "All tests passed!" << std::endl;
   } catch (const std::exception& e) {
       std::cerr << "Test failed: " << e.what() << std::endl;
   }

   return 0;
}
