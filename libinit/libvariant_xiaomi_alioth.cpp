/*
 * Copyright (C) 2021-2025 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <libvariant.h>

static const variant_info aliothin_info = {
    .hwc_value = "INDIA",
    .sku_value = "",

    .mod_device = "aliothin",
    .name = "aliothin",
    .brand = "Mi",
    .device = "aliothin",
    .marketname = "Mi 11X",
    .model = "M2012K11AI",
    .build_fingerprint = "Mi/aliothin/aliothin:13/TKQ1.221114.001/V816.0.2.0.TKHINXM:user/release-keys",

    .nfc = false,
};

static const variant_info alioth_global_info = {
    .hwc_value = "GLOBAL",
    .sku_value = "",

    .mod_device = "alioth_global",
    .name = "alioth_global",
    .brand = "POCO",
    .device = "alioth",
    .marketname = "POCO F3",
    .model = "M2012K11AG",
    .build_fingerprint = "POCO/alioth_global/alioth:13/TKQ1.221114.001/V816.0.3.0.TKHMIXM:user/release-keys",

    .nfc = true,
};

static const variant_info alioth_info = {
    .hwc_value = "",
    .sku_value = "",

    .mod_device = "alioth",
    .name = "alioth",
    .brand = "Redmi",
    .device = "alioth",
    .marketname = "Redmi K40",
    .model = "M2012K11AC",
    .build_fingerprint = "Redmi/alioth/alioth:13/TKQ1.221114.001/V816.0.6.0.TKHCNXM:user/release-keys",

    .nfc = true,
};

const std::vector<variant_info> variants = {
    aliothin_info,
    alioth_global_info,
    alioth_info,
};
