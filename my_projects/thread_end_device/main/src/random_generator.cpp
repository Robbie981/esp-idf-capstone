#include <random>

extern "C" {
    float generate_random(float lower, float upper) {
        static std::random_device rd;
        static std::mt19937 gen(rd());  // Static PRNG instance
        std::uniform_real_distribution<float> dist(lower, upper);
        return dist(gen);
    }
}