// SPDX-License-Identifier: Apache-2.0
#include "Thermal.h"
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <cstdlib>
#include <string>
int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(3);
    auto service = ndk::SharedRefBase::make<alioth::thermal::Thermal>();
    const std::string instance = std::string(alioth::thermal::aidlthermal::IThermal::descriptor) + "/default";
    const auto result = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    if (result != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "AliothThermalObserver", "Cannot register %s: %d", instance.c_str(), result);
        return EXIT_FAILURE;
    }
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
