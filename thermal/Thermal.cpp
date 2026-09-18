// SPDX-License-Identifier: Apache-2.0
#include "Thermal.h"
#include <android/log.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>
#include <sstream>

namespace alioth::thermal {
namespace at = aidlthermal;
namespace {
at::TemperatureType type(Kind kind) {
    switch (kind) {
        case Kind::Cpu: return at::TemperatureType::CPU;
        case Kind::Gpu: return at::TemperatureType::GPU;
        case Kind::Battery: return at::TemperatureType::BATTERY;
        case Kind::Skin: return at::TemperatureType::SKIN;
    }
    return at::TemperatureType::UNKNOWN;
}
at::CoolingType coolingType(Kind kind) {
    return kind == Kind::Cpu ? at::CoolingType::CPU : at::CoolingType::GPU;
}
at::ThrottlingSeverity severity(Severity value) {
    switch (value) {
        case Severity::None: return at::ThrottlingSeverity::NONE;
        case Severity::Light: return at::ThrottlingSeverity::LIGHT;
        case Severity::Moderate: return at::ThrottlingSeverity::MODERATE;
        case Severity::Severe: return at::ThrottlingSeverity::SEVERE;
    }
    return at::ThrottlingSeverity::NONE;
}
at::Temperature convert(const Reading& reading) {
    at::Temperature result;
    result.type = type(reading.kind); result.name = reading.name;
    result.value = reading.celsius; result.throttlingStatus = severity(reading.severity);
    return result;
}
ndk::ScopedAStatus unavailable(const std::string& message) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_STATE, message.c_str());
}
ndk::ScopedAStatus invalid(const char* message) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT, message);
}
bool same(const std::shared_ptr<at::IThermalChangedCallback>& a,
          const std::shared_ptr<at::IThermalChangedCallback>& b) {
    return a && b && a->asBinder().get() == b->asBinder().get();
}
}
Thermal::Thermal() : snapshot_(observer_.sample()), sampled_(std::chrono::steady_clock::now()), worker_([this] { run(); }) {}
Thermal::~Thermal() {
    { std::lock_guard<std::mutex> guard(lock_); stop_ = true; condition_.notify_all(); }
    if (worker_.joinable()) worker_.join();
}
Snapshot Thermal::current() {
    std::lock_guard<std::mutex> guard(lock_);
    if (std::chrono::steady_clock::now() - sampled_ > std::chrono::seconds(10)) {
        Snapshot result; result.error = "Thermal observer sample is stale"; return result;
    }
    return snapshot_;
}
ndk::ScopedAStatus Thermal::getTemperatures(std::vector<at::Temperature>* out) {
    out->clear(); auto sample = current();
    if (!sample.error.empty()) return unavailable(sample.error);
    for (const auto& reading : sample.temperatures) out->push_back(convert(reading));
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus Thermal::getTemperaturesWithType(at::TemperatureType wanted, std::vector<at::Temperature>* out) {
    auto result = getTemperatures(out);
    if (result.isOk()) out->erase(std::remove_if(out->begin(), out->end(), [&](const auto& x) { return x.type != wanted; }), out->end());
    return result;
}
ndk::ScopedAStatus Thermal::getCoolingDevices(std::vector<at::CoolingDevice>* out) {
    out->clear(); auto sample = current();
    if (!sample.error.empty()) return unavailable(sample.error);
    for (const auto& c : sample.cooling) {
        at::CoolingDevice value; value.type = coolingType(c.kind); value.name = c.name;
        value.value = c.state; out->push_back(std::move(value));
    }
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus Thermal::getCoolingDevicesWithType(at::CoolingType wanted, std::vector<at::CoolingDevice>* out) {
    auto result = getCoolingDevices(out);
    if (result.isOk()) out->erase(std::remove_if(out->begin(), out->end(), [&](const auto& x) { return x.type != wanted; }), out->end());
    return result;
}
ndk::ScopedAStatus Thermal::getTemperatureThresholds(std::vector<at::TemperatureThreshold>* out) {
    // These stock algorithms have profile-dependent hysteresis and actions.
    // A thermal ceiling is not invertible to one immutable temperature trip.
    // AIDL explicitly uses NaN for unavailable thresholds. Do not invent trips.
    out->clear();
    for (const auto& [kind, name] : std::array<std::pair<Kind, const char*>, 4>{{
             {Kind::Cpu, "cpu-soc-max"}, {Kind::Gpu, "gpu-soc-max"},
             {Kind::Battery, "battery"}, {Kind::Skin, "mi-skin"}}}) {
        at::TemperatureThreshold value;
        value.type = type(kind); value.name = name;
        value.hotThrottlingThresholds.assign(7, std::numeric_limits<float>::quiet_NaN());
        value.coldThrottlingThresholds = value.hotThrottlingThresholds;
        out->push_back(std::move(value));
    }
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus Thermal::getTemperatureThresholdsWithType(at::TemperatureType wanted, std::vector<at::TemperatureThreshold>* out) {
    auto result = getTemperatureThresholds(out);
    out->erase(std::remove_if(out->begin(), out->end(), [&](const auto& x) { return x.type != wanted; }), out->end());
    return result;
}
ndk::ScopedAStatus Thermal::addCallback(const std::shared_ptr<at::IThermalChangedCallback>& callback, std::optional<at::TemperatureType> wanted) {
    if (!callback) return invalid("Null callback");
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto& entry : callbacks_) if (same(entry.client, callback)) return invalid("Callback already registered");
    if (callbacks_.size() >= 64) return unavailable("Thermal callback limit reached");
    callbacks_.push_back({callback, wanted, {}, ++nextId_}); condition_.notify_all();
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus Thermal::registerThermalChangedCallback(const std::shared_ptr<at::IThermalChangedCallback>& callback) { return addCallback(callback, std::nullopt); }
ndk::ScopedAStatus Thermal::registerThermalChangedCallbackWithType(const std::shared_ptr<at::IThermalChangedCallback>& callback, at::TemperatureType wanted) { return addCallback(callback, wanted); }
ndk::ScopedAStatus Thermal::unregisterThermalChangedCallback(const std::shared_ptr<at::IThermalChangedCallback>& callback) {
    if (!callback) return invalid("Null callback");
    std::lock_guard<std::mutex> guard(lock_);
    auto it = std::find_if(callbacks_.begin(), callbacks_.end(), [&](const auto& entry) { return same(entry.client, callback); });
    if (it == callbacks_.end()) return invalid("Callback not registered");
    callbacks_.erase(it); return ndk::ScopedAStatus::ok();
}
void Thermal::run() {
    std::string previousError;
    while (true) {
        auto sample = observer_.sample();
        if (sample.error != previousError) {
            __android_log_print(sample.error.empty() ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                    "AliothThermalObserver", "%s", sample.error.empty() ? "Read-only thermal observation ready" : sample.error.c_str());
            previousError = sample.error;
        }
        std::vector<Callback> callbacks;
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (stop_) return;
            snapshot_ = sample; sampled_ = std::chrono::steady_clock::now(); callbacks = callbacks_;
        }
        if (sample.error.empty()) for (auto& entry : callbacks) {
            bool failed = false;
            for (size_t i = 0; i < sample.temperatures.size(); ++i) {
                const auto& reading = sample.temperatures[i];
                if (entry.type && *entry.type != type(reading.kind)) continue;
                if (entry.delivered.size() == sample.temperatures.size() && entry.delivered[i] == std::make_pair(reading.severity, std::isfinite(reading.celsius))) continue;
                // The AIDL callback is oneway. No observer mutex is held during IPC.
                auto result = entry.client->notifyThrottling(convert(reading));
                if (!result.isOk()) { failed = true; break; }
            }
            std::lock_guard<std::mutex> guard(lock_);
            auto it = std::find_if(callbacks_.begin(), callbacks_.end(), [&](const auto& candidate) { return candidate.id == entry.id; });
            if (it == callbacks_.end()) continue;
            if (failed) callbacks_.erase(it);
            else {
                it->delivered.clear();
                for (const auto& reading : sample.temperatures) it->delivered.emplace_back(reading.severity, std::isfinite(reading.celsius));
            }
        }
        std::unique_lock<std::mutex> guard(lock_);
        condition_.wait_for(guard, std::chrono::seconds(2), [&] { return stop_; });
        if (stop_) return;
    }
}
binder_status_t Thermal::dump(int fd, const char**, uint32_t) {
    auto sample = current();
    dprintf(fd, "Alioth read-only Mi thermal observer (AIDL v1)\nNo thermal, frequency, charging or powerhint writes.\n");
    dprintf(fd, "Severity is an initial cooling-ceiling classification, not a decoded Xiaomi severity.\n");
    dprintf(fd, "Observer status: %s\n", sample.error.empty() ? "ready" : sample.error.c_str());
    for (const auto& c : sample.cooling)
        dprintf(fd, "%s state=%lld/%lld ceiling=%lld nominal=%lld\n", c.name.c_str(),
                static_cast<long long>(c.state), static_cast<long long>(c.maxState),
                static_cast<long long>(c.ceiling), static_cast<long long>(c.nominalCeiling));
    for (const auto& r : sample.temperatures)
        dprintf(fd, "%s Celsius=%g severity=%d\n", r.name.c_str(), r.celsius, static_cast<int>(r.severity));
    dprintf(fd, "%s", sample.diagnostics.c_str());
    return STATUS_OK;
}
}  // namespace alioth::thermal
