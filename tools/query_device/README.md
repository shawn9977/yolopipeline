# Query Device (C++)

Query all available OpenVINO devices and their supported properties.

This is the C++ equivalent of the Python `query_device.py` script.

## Prerequisites

- OpenVINO Runtime installed (e.g. `/opt/intel/openvino_2025/`)
- CMake >= 3.12
- C++17 compatible compiler

## Build

```bash
# Source OpenVINO environment first
source /opt/intel/openvino_2025/setupvars.sh

# Build using the provided script
chmod +x build.sh
./build.sh
```

Or build manually:

```bash
source /opt/intel/openvino_2025/setupvars.sh
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Run

```bash
./query_device
```

## Sample Output

```
Available devices:
GPU :
    SUPPORTED_PROPERTIES:
        AVAILABLE_DEVICES: 0
        FULL_DEVICE_NAME: Intel(R) ...
        DEVICE_TYPE: integrated
        ...
```
