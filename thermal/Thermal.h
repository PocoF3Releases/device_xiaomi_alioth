// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Observer.h"
#include <aidl/android/hardware/thermal/BnThermal.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace alioth::thermal {
namespace aidlthermal = ::aidl::android::hardware::thermal;
class Thermal final : public aidlthermal::BnThermal {
public:
    Thermal();
    ~Thermal() override;
    ndk::ScopedAStatus getCoolingDevices(std::vector<aidlthermal::CoolingDevice>*) override;
    ndk::ScopedAStatus getCoolingDevicesWithType(aidlthermal::CoolingType, std::vector<aidlthermal::CoolingDevice>*) override;
    ndk::ScopedAStatus getTemperatures(std::vector<aidlthermal::Temperature>*) override;
    ndk::ScopedAStatus getTemperaturesWithType(aidlthermal::TemperatureType, std::vector<aidlthermal::Temperature>*) override;
    ndk::ScopedAStatus getTemperatureThresholds(std::vector<aidlthermal::TemperatureThreshold>*) override;
    ndk::ScopedAStatus getTemperatureThresholdsWithType(aidlthermal::TemperatureType, std::vector<aidlthermal::TemperatureThreshold>*) override;
    ndk::ScopedAStatus registerThermalChangedCallback(const std::shared_ptr<aidlthermal::IThermalChangedCallback>&) override;
    ndk::ScopedAStatus registerThermalChangedCallbackWithType(const std::shared_ptr<aidlthermal::IThermalChangedCallback>&, aidlthermal::TemperatureType) override;
    ndk::ScopedAStatus unregisterThermalChangedCallback(const std::shared_ptr<aidlthermal::IThermalChangedCallback>&) override;
    binder_status_t dump(int fd, const char** args, uint32_t count) override;
private:
    struct Callback {
        std::shared_ptr<aidlthermal::IThermalChangedCallback> client;
        std::optional<aidlthermal::TemperatureType> type;
        std::vector<std::pair<Severity, bool>> delivered;
        uint64_t id;
    };
    Observer observer_;
    std::mutex lock_;
    std::condition_variable condition_;
    bool stop_ = false;
    uint64_t nextId_ = 0;
    Snapshot snapshot_;
    std::chrono::steady_clock::time_point sampled_;
    std::vector<Callback> callbacks_;
    std::thread worker_;
    Snapshot current();
    void run();
    ndk::ScopedAStatus addCallback(const std::shared_ptr<aidlthermal::IThermalChangedCallback>&, std::optional<aidlthermal::TemperatureType>);
};
}  // namespace alioth::thermal
