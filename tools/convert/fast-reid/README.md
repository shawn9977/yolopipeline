# FastReID OpenVINO INT8 Quantization Tool

本工具用于将 **FastReID 导出的 FP16 OpenVINO IR 模型**，通过 **NNCF Post-Training Quantization (PTQ)** 转换为 **INT8 OpenVINO IR 模型**，无需重新训练。

FastReID 原始模型格式为pth, 利用FastReID 的onnx_export.py 导出onnx 格式，利用OpenVINO 的ovc工具转出FP16格式的IR 格式模型。基于FP16 IR模型进行INT8量化。此方法适用于常见 FastReID 模型（如 ResNet50 / AGW 等）。

------

## 功能概览

### 输入

- **FP16 OpenVINO IR 模型**
  - `.xml` + `.bin`
- **ReID 训练集图片目录**
  - 如 MSMT17 / Market-1501（仅使用图片，不依赖标签）

### 输出

- **INT8 OpenVINO IR 模型**
  - `.xml` + `.bin`

------

## FastReID 环境搭建

### 1. 获取 FastReID 源码并安装依赖

```
git clone https://github.com/JDAI-CV/fast-reid.git
python3 -m venv venv
source venv/bin/activate

pip install -r fast-reid/docs/requirements.txt
pip install fastreid --no-deps
pip install torch torchvision onnxoptimizer onnxscript onnxsim openvino
```

------

## 模型转换流程说明

量化脚本**要求输入为 FP16 OpenVINO IR 模型**，推荐的完整转换流程如下。

------

### 1️⃣ PTH → ONNX（FastReID 官方导出）

> 注意：需将 `MODEL.DEVICE` 设置为 `cpu`

```
python fast-reid/tools/deploy/onnx_export.py \
  --config-file fast-reid/configs/MSMT17/AGW_S50.yml \
  --name msmt_agw_S50 \
  --output fastreid_onnx \
  --opts \
    MODEL.WEIGHTS msmt_agw_S50.pth \
    MODEL.DEVICE cpu
```

输出示例：

```
msmt_agw_S50.onnx
```

------

### 2️⃣ ONNX FP32 → OpenVINO IR FP16

可在此阶段**固定模型输入 shape**（推荐）：

```
ovc msmt_agw_S50.onnx \
  --input [1,3,128,384] \
  --output_model msmt_agw_s50_fp16 \
  --compress_to_fp16 True
```

> 本量化脚本所使用的 FP16 模型，均由该命令生成。

------

## 支持的数据集（校准数据）

可使用以下 **ReID 训练集图片目录** 作为量化校准数据：

### MSMT17

URL：https://huggingface.co/datasets/xianpeijie/MSMT17_V1

```
MSMT17_V1/train/
```

### Market-1501

URL：https://huggingface.co/datasets/aveocr/Market-1501-v15.09.15.zip

```
Market-1501-v15.09.15/bounding_box_train/
```

> 校准过程中仅使用图片，不依赖标签文件。

------

## 使用方法

### 基本用法

```
python fastreid_quantization.py \
  --model ./msmt_agw_S50.xml \
  --output ./msmt_agw_S50_fp16_int8.xml \
  --data /MSMT17_V1/train \
  --subset-size 300
```

------

### 参数说明

| 参数            | 说明                                 |
| --------------- | ------------------------------------ |
| `-m, --model`   | 输入 FP16 OpenVINO IR 模型（`.xml`） |
| `-o, --output`  | 输出 INT8 OpenVINO IR 模型（`.xml`） |
| `-d, --data`    | 校准图片目录                         |
| `--subset-size` | 校准图片数量（默认：300）            |

------

## 量化策略说明

- 量化方式：**NNCF Post-Training Quantization**
- Quantization Preset：`PERFORMANCE`
- 启用 **SmoothQuant**：

```
smooth_quant_alpha = 0.5
```

该配置在推理性能与精度之间取得较好平衡，适合 ReID 场景。

------

## 输出结果

量化完成后，将生成：

```
msmt_agw_S50_fp16_int8.xml
msmt_agw_S50_fp16_int8.bin
```

可直接用于 **OpenVINO Runtime** 进行 INT8 推理部署。

举例:
1. msmt_agw_S50:
python fast-reid-repo/tools/deploy/onnx_export.py --config-file fast-reid-repo/configs/MSMT17/AGW_S50.yml --name msmt_agw_S50 --output ../../models/ov/dmall/msmt_agw_S50/onnx --opts MODEL.WEIGHTS ../../models/origin/dmall/fast-reid/msmt_agw_S50.pth  MODEL.DEVICE cpu
ovc ./../../models/ov/dmall/msmt_agw_S50/onnx/msmt_agw_S50.onnx --input [1,3,128,384] --output_model ../../models/ov/dmall/msmt_agw_S50/fp16 --compress_to_fp16 True
ovc ./../../models/ov/dmall/msmt_agw_S50/onnx/msmt_agw_S50.onnx --input [1..20,3,128,384] --output_model ../../models/ov/dmall/msmt_agw_S50/fp16-dynamic --compress_to_fp16 True
python fastreid_quantization.py --model ../../../../models/ov/dmall/msmt_agw_S50/fp16/msmt_agw_S50.xml --output ../../../../models/ov/dmall/msmt_agw_S50/int8/msmt_agw_S50.xml --data ./datasets/MSMT17_V1/train --subset-size 300
python fastreid_quantization.py --model ../../../../models/ov/dmall/msmt_agw_S50/fp16-dynamic/msmt_agw_S50.xml --output ../../../../models/ov/dmall/msmt_agw_S50/int8-dynamic/msmt_agw_S50.xml --data ./datasets/MSMT17_V1/train --subset-size 300

2.market_bot_r50:
python fast-reid-repo/tools/deploy/onnx_export.py --config-file ./fast-reid-repo/configs/Market1501/bagtricks_R50.yml --name market_bot_r50 --output ../../models/ov/public/onnx --opts MODEL.WEIGHTS ../../models/origin/public/fast-reid/market_bot_R50.pth  MODEL.DEVICE cpu
ovc ./../../models/ov/public/onnx/market_bot_r50.onnx --input [1,3,128,384] --output_model ../../models/ov/public/fp16/ --compress_to_fp16 True
ovc ./../../models/ov/public/onnx/market_bot_r50.onnx --input [1..20,3,128,384] --output_model ../../models/ov/public/fp16-dynamic/ --compress_to_fp16 True
python fastreid_quantization.py --model ../../../../models/ov/public/fp16/market_bot_r50.xml --output ../../../../models/ov/public/int8/market_bot_r50.xml --data ./datasets/MSMT17_V1/train --subset-size 300
python fastreid_quantization.py --model ../../../../models/ov/public/fp16-dynamic/market_bot_r50.xml --output ../../../../models/ov/public/int8-dynamic/market_bot_r50.xml --data ./datasets/MSMT17_V1/train --subset-size 300
