# vpl_samples
libVPL multiple video decode, inference samples

* Pipeline Overview  
By default, the pipeline includes Decode + YOLO + FastReid + JPEG encode.  

## Environment
Platform: ARL-H  
OS: Ubuntu 24.04  
Kernel: 6.14.0-37  

## Install Driver
Follow the guide to install GPU and NPU driver.  
GPU: https://dgpu-docs.intel.com/driver/client/overview.html  
NPU: https://github.com/intel/linux-npu-driver/releases  

## Install software stack

Please reboot to ensure the entire software stack is correctly loaded.

### Media Stack
```bash
$wget https://github.com/intel/vpl-gpu-rt/releases/download/intel-onevpl-25.3.4/MediaStack.tar.gz
tar -xvf MediaStack.tar.gz
cd MediaStack
sudo ./install_media.sh
```
### Install Openvino runtime
```bash
wget https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
sudo apt-key add GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
echo "deb https://apt.repos.intel.com/openvino ubuntu20 main" | sudo tee /etc/apt/sources.list.d/intel-openvino.list
echo "deb https://apt.repos.intel.com/openvino ubuntu22 main" | sudo tee /etc/apt/sources.list.d/intel-openvino.list
echo "deb https://apt.repos.intel.com/openvino ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-openvino.list
sudo apt update
apt-cache search openvino
sudo apt install openvino-2025.4.0
```

### Install OpenCL GPU runtime
```bash
sudo apt update
sudo apt install ocl-icd-opencl-dev opencl-headers

# TBD

```

## Build multi_dec_infer_enc sample 

```bash
$source /opt/intel/openvino_2025/setupvars.sh
$cd multi_dec_infer_enc
$./build.sh
```
The build.sh will create a cache_dir under current folder which will be used
as OpenVINO kernel cache to shorten the time of first inference.

## Prepare the CNN models(public) and h264 file.
```bash
$cd vpl_samples/assets
$mkdir fast-reid
$tar -xvf fast-reid.tar.gz -C fast-reid
$tar -xvf fast-reid-dmall.tar.gz -C fast-reid
$tar -xvf yolov11.tar.gz
```

```bash
$sudo apt install ffmpeg

eg:

$ffmpeg -i test-people-2.mp4 -c copy -bsf:v h264_mp4toannexb test-people-2.h264
or
$ffmpeg -ss 00:01:00 -i test-people-2.mp4 -t 00:00:30 -c copy -bsf:v hevc_mp4toannexb test-people-2.h265
```


## Run

```bash
Command:
multi_dec_infer_enc -h
Usage: ./multi_dec_infer_enc yolov11s.xml fastreid.xml pipeline_num [gpu_id] [gpu_num] [group_max_size]

pipeline_num: the total pipeline counts, also as the input channels.
gpu_id: optinal, default 0, GPU 0 :renderD128 and GPU 1 : renderD129.
gpu_num: optinal, default 1, how many GPU cards to be used in the application.
group_max_size: optinal, default is 10, divide pipeline_num into serveral groups, each group have max pipelines within the limitation of group_max_size.
```

```bash
$source ./setva.sh
$./multi_dec_infer_enc ../assets/yolov11/yolov11sint8/yolo11s.xml ../assets/fast-reid/public/market_bot_r50/int8-dynamic/market_bot_r50_int8.xml 30 0 1 6
```

* Environment Variables & Configuration  
    * Verbose Logging:  
    Enable detailed logs: VERBOSE=true ./multi_dec_infer_enc xx
    * YOLO ROI Visualization:  
    To save detection ROIs as images: SAVE_JPG=true ./multi_dec_infer_enc xx (Files are stored as /tmp/pic*.jpg).  
    * Pipeline Customization:  
    Excluding FastReid: NO_FASTREID=true ./multi_dec_infer_enc xx
    Loop input video for long time test: LOOP=10 ./multi_dec_infer_enc
        When LOOP=10, the application will stop reading the input stream file after reach the end of file 10 times.
    Excluding YOLO: NO_YOLO=true ./multi_dec_infer_enc
        When yolo is excluded, fastReid must be exclued as well. This variable is just for debugging. For example, to find the maximum decode fps of 30 streams.
    * GPU Device selection:
    By default, the first GPU "/dev/dri/renderD128" is used. If you want to use "/dev/dri/renderD129", modify
    vplDecEnc.Init(0) to vplDecEnc.Init(1) in multi_dec_infer_enc.cpp. Then run ./build.sh

