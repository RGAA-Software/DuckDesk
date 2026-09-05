//
// Created by RGAA on 20/09/2025.
//

#include "cpu_frequency.h"

#ifdef WIN32
#include <chrono>
#include <memory>
#include <thread>
#endif

namespace px {

#ifdef WIN32
namespace {

struct PdhQueryCloser final {
    void operator()(void* query) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): opaque PDH query boundary.
        if (query != nullptr) {
            PdhCloseQuery(query);
        }
    }
};

using UniquePdhQuery = std::unique_ptr<void, PdhQueryCloser>;

}  // namespace
#endif

double CpuFrequency::GetCurrentCpuSpeed() {
#ifdef WIN32
    HQUERY query_raw{};  // NOLINT(gammaray-raw-pointer-boundary): initialized by PdhOpenQuery and immediately RAII-wrapped.
    if (PdhOpenQuery(nullptr, 0, &query_raw) != ERROR_SUCCESS) {
        return -1.0;
    }
    UniquePdhQuery query{query_raw};

    HCOUNTER performance_counter{};  // NOLINT(gammaray-raw-pointer-boundary): counter lifetime is owned by the query.
    if (PdhAddCounterW(query.get(), L"\\Processor Information(_Total)\\% Processor Performance", 0, &performance_counter) != ERROR_SUCCESS) {
        return -1.0;
    }

    HCOUNTER frequency_counter{};  // NOLINT(gammaray-raw-pointer-boundary): counter lifetime is owned by the query.
    if (PdhAddCounterW(query.get(), L"\\Processor Information(_Total)\\Processor Frequency", 0, &frequency_counter) != ERROR_SUCCESS) {
        return -1.0;
    }

    if (PdhCollectQueryData(query.get()) != ERROR_SUCCESS) {
        return -1.0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    if (PdhCollectQueryData(query.get()) != ERROR_SUCCESS) {
        return -1.0;
    }

    PDH_FMT_COUNTERVALUE value{};
    DWORD value_type{};
    if (PdhGetFormattedCounterValue(performance_counter, PDH_FMT_DOUBLE, &value_type, &value) != ERROR_SUCCESS) {
        return -1.0;
    }
    const double cpu_performance = value.doubleValue / 100.0;

    if (PdhGetFormattedCounterValue(frequency_counter, PDH_FMT_DOUBLE, &value_type, &value) != ERROR_SUCCESS) {
        return -1.0;
    }
    return cpu_performance * value.doubleValue / 1000.0;
#else
    return 0.0;
#endif
}

}  // namespace px
