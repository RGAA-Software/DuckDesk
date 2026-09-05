//
// Created by RGAA on 2023-12-18.
//

#ifndef TC_APPLICATION_RANDOM_H
#define TC_APPLICATION_RANDOM_H

#include <algorithm>
#include <concepts>
#include <random>

namespace px {

class Random {
  public:
    template <std::integral T> static T RandT(T minimum, T maximum) {
        if (minimum > maximum) {
            std::swap(minimum, maximum);
        }
        std::uniform_int_distribution<T> distribution(minimum, maximum);
        return distribution(Generator());
    }

    template <std::floating_point T> static T RandT(T minimum, T maximum) {
        if (minimum > maximum) {
            std::swap(minimum, maximum);
        }
        std::uniform_real_distribution<T> distribution(minimum, maximum);
        return distribution(Generator());
    }

    // 0 ~ 1.0
    static double GenRandomNum() {
        std::uniform_real_distribution<double> dis(0.0, 1.0);
        return dis(Generator());
    }

  private:
    static std::mt19937_64& Generator() {
        thread_local std::mt19937_64 generator(std::random_device{}());
        return generator;
    }
};

} // namespace px

#endif // TC_APPLICATION_RANDOM_H
