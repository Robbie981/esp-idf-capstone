#include <deque>
#include <mutex>
#include <cstdio>

extern "C" {  // Allows C files to call these functions
    void add_item(float item);
    float remove_item();
    int buffer_size();
    void print_buffer();
    float get_average();
}

static std::deque<float> buffer;
static std::mutex buffer_mutex;  // Thread safety (optional)

#define MAX_ITEMS 5

// Add a float item, removing the oldest if full
void add_item(float item) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    if (buffer.size() >= MAX_ITEMS) {
        buffer.pop_front();  // Remove oldest item
    }
    buffer.push_back(item);
}

// Remove and return the oldest item
float remove_item() {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    if (buffer.empty()) return -1.0f; // Error: empty buffer
    float item = buffer.front();
    buffer.pop_front();
    return item;
}

// Get the current buffer size
int buffer_size() {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    return buffer.size();
}

// Print buffer (for debugging)
void print_buffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    printf("Buffer: ");
    for (float item : buffer) {
        printf("%.2f ", item);  // Print floats with 2 decimal places
    }
    printf("\n");
}

float get_average() {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    if (buffer.empty()) return 0.0f;  // Return 0 if buffer is empty

    float sum = 0.0f;
    for (float item : buffer) {
        sum += item;
    }

    return sum / buffer.size();
}