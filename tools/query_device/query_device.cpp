// Copyright (C) 2018-2026 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <string>
#include <vector>

#include <openvino/openvino.hpp>

int main() {
    // Step 1. Initialize OpenVINO Runtime Core
    ov::Core core;

    // Step 2. Get metrics of available devices
    std::cout << "Available devices:" << std::endl;

    for (const auto& device : core.get_available_devices()) {
        std::cout << device << " :" << std::endl;
        std::cout << "\tSUPPORTED_PROPERTIES:" << std::endl;

        auto supported_properties =
            core.get_property(device, ov::supported_properties);

        for (const auto& property_key : supported_properties) {
            if (property_key == ov::supported_properties.name())
                continue;

            try {
                ov::Any value = core.get_property(device, property_key);
                std::cout << "\t\t" << property_key << ": "
                          << value.as<std::string>() << std::endl;
            } catch (...) {
                std::cout << "\t\t" << property_key << ": UNSUPPORTED TYPE"
                          << std::endl;
            }
        }
        std::cout << std::endl;
    }

    return 0;
}
