#include <memory>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <unordered_set>

template<typename T>
class threadsafe_queue {
private:
    struct node {
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };

    std::mutex head_mutex;
    std::mutex tail_mutex;
    std::unique_ptr<node> head;
    node* tail;
    std::condition_variable data_cond;

    node* get_tail();
    std::unique_ptr<node> pop_head();
    
    std::unique_lock<std::mutex> wait_for_data();
    std::unique_ptr<node> wait_pop_head();
    std::unique_ptr<node> wait_pop_head(T& value);
    std::unique_ptr<node> try_pop_head();
    std::unique_ptr<node> try_pop_head(T& value);

public:
    threadsafe_queue();
    
    threadsafe_queue(const threadsafe_queue& other) = delete;
    threadsafe_queue& operator=(const threadsafe_queue& other) = delete;

    std::shared_ptr<T> try_pop();
    bool try_pop(T& value);
    std::shared_ptr<T> wait_and_pop();
    void wait_and_pop(T& value);
    void push(T new_value);
    bool empty();
};


// Function implementations

template<typename T>
threadsafe_queue<T>::threadsafe_queue() :
    head(new node), tail(head.get()) {}

template<typename T>
void threadsafe_queue<T>::push(T new_value) {
    std::shared_ptr<T> new_data(std::make_shared<T>(std::move(new_value)));
    std::unique_ptr<node> p(new node);
    
    {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        tail->data = new_data;
        node* const new_tail = p.get();
        tail->next = std::move(p);
        tail = new_tail;
    }
    
    data_cond.notify_one();
}

template<typename T>
typename threadsafe_queue<T>::node* threadsafe_queue<T>::get_tail() {
    std::lock_guard<std::mutex> tail_lock(tail_mutex);
    return tail;
}

template<typename T>
std::unique_ptr<typename threadsafe_queue<T>::node> threadsafe_queue<T>::pop_head() {
    std::unique_ptr<typename threadsafe_queue<T>::node> old_head = std::move(head);
    head = std::move(old_head->next);
    return old_head;
}

template<typename T>
bool threadsafe_queue<T>::empty() {
    std::lock_guard<std::mutex> head_lock(head_mutex);
    return (head.get() == get_tail());
}

template<typename T>
std::shared_ptr<T> threadsafe_queue<T>::try_pop() {
    std::unique_ptr<typename threadsafe_queue<T>::node> const old_head = try_pop_head();
    return old_head ? old_head->data : std::shared_ptr<T>();
}

template<typename T>
bool threadsafe_queue<T>::try_pop(T& value) {
    std::unique_ptr<typename threadsafe_queue<T>::node> const old_head = try_pop_head(value);
    return old_head != nullptr;
}

template<typename T>
std::shared_ptr<T> threadsafe_queue<T>::wait_and_pop() {
    std::unique_ptr<node> const old_head = wait_pop_head();
    return old_head->data;
}

template<typename T>
void threadsafe_queue<T>::wait_and_pop(T& value) {
    std::unique_ptr<node> const old_head = wait_pop_head(value);
}

template<typename T>
std::unique_lock<std::mutex> threadsafe_queue<T>::wait_for_data() {
    std::unique_lock<std::mutex> head_lock(head_mutex);
    data_cond.wait(head_lock, [&] { return head.get() != get_tail(); });
    return std::move(head_lock);
}

template<typename T>
std::unique_ptr<typename threadsafe_queue<T>::node> threadsafe_queue<T>::wait_pop_head() {
    std::unique_lock<std::mutex> head_lock(wait_for_data());
    return pop_head();
}

template<typename T>
std::unique_ptr<typename threadsafe_queue<T>::node> threadsafe_queue<T>::wait_pop_head(T& value) {
    std::unique_lock<std::mutex> head_lock(wait_for_data());
    value = std::move(*head->data);
    return pop_head();
}

template<typename T>
std::unique_ptr<typename threadsafe_queue<T>::node> threadsafe_queue<T>::try_pop_head() {
    std::lock_guard<std::mutex> head_lock(head_mutex);
    
    if (head.get() == get_tail())
        return nullptr;

    return pop_head();
}

template<typename T>
std::unique_ptr<typename threadsafe_queue<T>::node> threadsafe_queue<T>::try_pop_head(T& value) {
    std::lock_guard<std::mutex> head_lock(head_mutex);

    if (head.get() == get_tail())
        return nullptr;

   value = std::move(*head->data);
   return pop_head();
}



// testing

// Test concurrent operations
// Test concurrent operations with multiple producers and consumers
void test_concurrent_operations() {
    threadsafe_queue<int> queue;
    const int num_producers = 5; // Number of producers
    const int num_consumers = 5;  // Number of consumers
    const int items_per_producer = 10; // Number of items each producer generates
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::unordered_set<int> expected_values; // To track expected values
    std::mutex results_mutex;
    std::unordered_set<int> results; // To track consumed values

    // Initialize expected values
    for (int i = 0; i < num_producers * items_per_producer; ++i) {
        expected_values.insert(i);
    }

    auto producer = [&](int id) {
        for (int i = 0; i < items_per_producer; ++i) {
            int value = id * items_per_producer + i;
            queue.push(value);
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Simulate work
        }
    };

    auto consumer = [&]() {
        for (int i = 0; i < num_producers * items_per_producer / num_consumers; ++i) {
            int item;
            queue.wait_and_pop(item);
            std::lock_guard<std::mutex> lock(results_mutex);
            results.insert(item);
        }
    };

    // Create producer threads
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back(producer, i);
    }

    // Create consumer threads
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back(consumer);
    }

    // Wait for all threads to finish
    for (auto& p : producers) {
        p.join();
    }
    
    for (auto& c : consumers) {
        c.join();
    }

    // Check if all expected values were consumed
    bool correct = true;
    
    for (const auto& value : expected_values) {
        if (results.find(value) == results.end()) {
            correct = false;
            break;
        }
    }

    // Output result
    if (correct) {
        std::cout << "All values were produced and consumed correctly." << std::endl;
    } else {
        std::cout << "Some values were missing in the results." << std::endl;
    }
}

// Test sequential operations
void test_sequential_operations() {
    threadsafe_queue<int> queue;
    std::vector<int> results;

    // Sequentially push elements
    for (int i = 0; i < 10; ++i) {
        queue.push(i);
    }

    // Sequentially pop elements
    while (!queue.empty()) {
        int item;
        if (queue.try_pop(item)) {
            results.push_back(item);
        }
    }

    // Output results
    std::cout << "Sequential Results: ";
    for (const auto& item : results) {
        std::cout << item << " ";
    }
    std::cout << std::endl;
}

int main() {
    test_concurrent_operations();
    test_sequential_operations();
    
    return 0;
}