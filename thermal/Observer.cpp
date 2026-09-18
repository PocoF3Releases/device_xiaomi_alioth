// SPDX-License-Identifier: Apache-2.0
#include "Observer.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace alioth::thermal {
namespace {
constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
std::string path(const std::string& root, const std::string& suffix) {
    return root == "/" ? suffix : root + suffix;
}
std::string readText(const std::string& file) {
    int fd = open(file.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Cannot open " + file + ": " + std::to_string(errno));
    std::string result;
    char buffer[4096];
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { int error = errno; close(fd); throw std::runtime_error("Cannot read " + file + ": " + std::to_string(error)); }
        if (n == 0) break;
        result.append(buffer, static_cast<size_t>(n));
        if (result.size() > 65536) { close(fd); throw std::runtime_error("Oversized sysfs value: " + file); }
    }
    close(fd);
    size_t first = result.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) throw std::runtime_error("Empty sysfs value: " + file);
    size_t last = result.find_last_not_of(" \t\r\n");
    return result.substr(first, last - first + 1);
}
int64_t integer(const std::string& text) {
    char* end = nullptr;
    errno = 0;
    const auto value = strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0')
        throw std::runtime_error("Invalid integer: " + text.substr(0, 64));
    return value;
}
struct Inventory { std::map<std::string, std::string> zones, cooling; };
Inventory inventory(const std::string& root) {
    Inventory result;
    const std::string base = path(root, "/sys/class/thermal");
    DIR* directory = opendir(base.c_str());
    if (!directory) throw std::runtime_error("Cannot enumerate " + base);
    while (const auto* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        const bool zone = name.rfind("thermal_zone", 0) == 0;
        const bool cooling = name.rfind("cooling_device", 0) == 0;
        if (!zone && !cooling) continue;
        try {
            const std::string full = base + "/" + name;
            const std::string type = readText(full + "/type");
            auto& table = zone ? result.zones : result.cooling;
            // Duplicate types cannot be matched safely by an arbitrary index.
            if (table.count(type)) table[type].clear();
            else table[type] = full;
        } catch (const std::exception&) { /* Try again on the next sample. */ }
    }
    closedir(directory);
    return result;
}
float temperature(const std::string& file, float divisor) {
    try {
        const auto value = static_cast<float>(integer(readText(file))) / divisor;
        return std::isfinite(value) && value >= -50 && value <= 160 ? value : NaN;
    } catch (const std::exception&) { return NaN; }
}
float maximumTemperature(const Inventory& inv, const std::string& prefix) {
    float value = NaN;
    for (const auto& [type, file] : inv.zones) {
        if (file.empty() || type.rfind(prefix, 0) != 0 ||
            type.size() < 4 || type.compare(type.size()-4, 4, "-usr") != 0) continue;
        const auto current = temperature(file + "/temp", 1000);
        if (std::isfinite(current) && (!std::isfinite(value) || current > value)) value = current;
    }
    return value;
}
Cooling readCooling(const std::string& root, const Inventory& inv, Kind kind,
                    const std::string& name, const std::string& frequencyFile) {
    const auto it = inv.cooling.find(name);
    if (it == inv.cooling.end() || it->second.empty())
        throw std::runtime_error("Missing or ambiguous cooling device " + name);
    const auto state = integer(readText(it->second + "/cur_state"));
    const auto maxState = integer(readText(it->second + "/max_state"));
    if (maxState <= 0 || maxState > 1024 || state < 0 || state > maxState)
        throw std::runtime_error("Invalid cooling state for " + name);
    std::istringstream stream(readText(path(root, frequencyFile)));
    std::vector<int64_t> frequencies;
    std::string token;
    while (stream >> token) {
        auto frequency = integer(token);
        if (frequency <= 0) throw std::runtime_error("Invalid frequency for " + name);
        frequencies.push_back(frequency);
    }
    std::sort(frequencies.begin(), frequencies.end(), std::greater<int64_t>());
    frequencies.erase(std::unique(frequencies.begin(), frequencies.end()), frequencies.end());
    // The reviewed 4.19 CPU/devfreq drivers use descending frequency states.
    // Refuse a mismatched table instead of guessing an index or thermal limit.
    if (frequencies.size() != static_cast<size_t>(maxState + 1))
        throw std::runtime_error("Frequency/cooling table cardinality mismatch for " + name);
    return {kind, name, state, maxState, frequencies[static_cast<size_t>(state)], frequencies.front()};
}
Severity batterySeverity(const std::string& health) {
    if (health == "Good") return Severity::None;
    if (health == "Warm" || health == "Cool") return Severity::Light;
    if (health == "Cold") return Severity::Moderate;
    if (health == "Overheat") return Severity::Severe;
    // Electrical failures and unknown health are not a temperature classification.
    throw std::runtime_error("Battery thermal health unavailable: " + health);
}
Severity maximum(Severity a, Severity b) { return a > b ? a : b; }
}  // namespace

Severity severityForCeiling(int64_t ceiling, int64_t nominal) {
    if (nominal <= 0 || ceiling <= 0 || ceiling > nominal)
        throw std::runtime_error("Invalid observed thermal ceiling");
    if (ceiling == nominal) return Severity::None;
    const double fraction = static_cast<double>(ceiling) / static_cast<double>(nominal);
    if (fraction >= 0.80) return Severity::Light;
    if (fraction >= 0.50) return Severity::Moderate;
    return Severity::Severe;
}
Snapshot Observer::sample() const {
    Snapshot result;
    try {
        const auto inv = inventory(root_);
        for (int cpu : {0, 4, 7}) {
            result.cooling.push_back(readCooling(root_, inv, Kind::Cpu,
                    "thermal-cpufreq-" + std::to_string(cpu),
                    "/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
                    "/cpufreq/scaling_available_frequencies"));
        }
        result.cooling.push_back(readCooling(root_, inv, Kind::Gpu, "thermal-devfreq-0",
                    "/sys/class/kgsl/kgsl-3d0/gpu_available_frequencies"));
        Severity cpu = Severity::None, gpu = Severity::None;
        for (const auto& cooling : result.cooling) {
            auto severity = severityForCeiling(cooling.ceiling, cooling.nominalCeiling);
            if (cooling.kind == Kind::Cpu) cpu = maximum(cpu, severity);
            else gpu = maximum(gpu, severity);
        }
        const auto health = readText(path(root_, "/sys/class/power_supply/battery/health"));
        const auto battery = batterySeverity(health);
        float skin = NaN;
        try {
            // This is mi_thermald's published virtual board temperature. Do not
            // recompute its encrypted, profile-dependent model in this service.
            if (readText(path(root_, "/sys/class/thermal/thermal_message/board_sensor")) == "VIRTUAL-SENSOR0")
                skin = temperature(path(root_, "/sys/class/thermal/thermal_message/board_sensor_temp"), 1000);
        } catch (const std::exception&) { /* Unknown is NaN, never zero. */ }
        result.temperatures = {
            {Kind::Cpu, "cpu-soc-max", maximumTemperature(inv, "cpuss-"), cpu},
            {Kind::Gpu, "gpu-soc-max", maximumTemperature(inv, "gpuss-"), gpu},
            {Kind::Battery, "battery", temperature(path(root_, "/sys/class/power_supply/battery/temp"), 10), battery},
            {Kind::Skin, "mi-skin", skin, maximum(maximum(cpu, gpu), battery)},
        };
        for (const auto& reading : result.temperatures) {
            if (!std::isfinite(reading.celsius)) result.diagnostics += reading.name + ": temperature unavailable\n";
        }
        for (const char* name : {"sconfig", "temp_state"}) {
            try { result.diagnostics += std::string(name) + "=" + readText(path(root_, std::string("/sys/class/thermal/thermal_message/") + name)) + "\n"; }
            catch (const std::exception&) { result.diagnostics += std::string(name) + "=unavailable\n"; }
        }
    } catch (const std::exception& e) {
        result.error = e.what();
        result.temperatures.clear();
        result.cooling.clear();
    }
    return result;
}
}  // namespace alioth::thermal
