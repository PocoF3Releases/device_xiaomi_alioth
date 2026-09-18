// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace alioth::thermal {
// These values are translated explicitly by the AIDL adapter.
enum class Kind { Cpu, Gpu, Battery, Skin };
enum class Severity { None, Light, Moderate, Severe };
struct Reading { Kind kind; std::string name; float celsius; Severity severity; };
struct Cooling { Kind kind; std::string name; int64_t state; int64_t maxState; int64_t ceiling; int64_t nominalCeiling; };
struct Snapshot {
    std::vector<Reading> temperatures;
    std::vector<Cooling> cooling;
    std::string error;
    std::string diagnostics;
};
// A new Android reporting policy, NOT an interpretation of temp_state bitfields.
// It classifies the observed remaining thermal frequency ceiling, not utilization.
// This is a calibration seed; do not mistake clock ratios for throughput ratios.
Severity severityForCeiling(int64_t ceiling, int64_t nominal);
class Observer {
public:
    // Root override is for host fixture tests only. Production always uses "/".
    explicit Observer(std::string root = "/") : root_(std::move(root)) {}
    Snapshot sample() const;
private:
    std::string root_;
};
}  // namespace alioth::thermal
