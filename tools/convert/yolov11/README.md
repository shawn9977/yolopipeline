# YOLO OpenVINO 导出 & INT8 量化（Static / Dynamic）

统一脚本 **`yolov11_quantization.py`**，支持静态/动态 shape 导出及 INT8 量化。

## 获取模型

```bash
wget https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11s.pt
wget https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11s-pose.pt
```

------

## 参数说明

```
usage: yolov11_quantization.py [-h]
    -m MODEL
    [--dynamic]
    [--imgsz IMGSZ]
    [--batch BATCH]
    [--data-url DATA_URL]
    [--export-fp32]
    [--export-fp16]
    [--fp32-to-int8]
    [--fp16-to-int8]
    [-o OUTPUT_DIR]
```

| 参数 | 说明 |
|------|------|
| `-m, --model` | 模型路径或名称。支持：本地 `.pt` 文件、OpenVINO 模型目录、模型名称（自动下载） |
| `--dynamic` | 使用动态输入 shape（N/H/W 为 `?`）。默认为静态 shape |
| `--imgsz` | 输入图像尺寸（默认: 640） |
| `--batch` | 批量大小（默认: 1） |
| `--export-fp32` | 导出 FP32 OpenVINO IR |
| `--export-fp16` | 导出 FP16 OpenVINO IR |
| `--fp32-to-int8` | 将 FP32 模型量化为 INT8 |
| `--fp16-to-int8` | 将 FP16 模型量化为 INT8 |
| `-o, --output_dir` | INT8 模型输出目录（默认: `<模型名>_ov_<precision>_<shape>_int8`） |

------

## 示例用法

### 静态 shape

#### 1️⃣ 导出 FP32 OpenVINO 模型

```bash
python yolov11_quantization.py -m yolo11s --export-fp32
```

#### 2️⃣ 导出 FP16 OpenVINO 模型

```bash
python yolov11_quantization.py -m yolo11s --export-fp16
```

#### 3️⃣ FP32 → INT8 量化

```bash
python yolov11_quantization.py -m yolo11s --fp32-to-int8
```

#### 4️⃣ FP16 → INT8 量化（推荐）

```bash
python yolov11_quantization.py -m yolo11s --fp16-to-int8
```

------

### 动态 shape

#### 5️⃣ 导出 FP16 动态模型

```bash
python yolov11_quantization.py -m yolo11s --dynamic --export-fp16
```

#### 6️⃣ FP16 动态模型 → INT8 量化

```bash
python yolov11_quantization.py -m yolo11s --dynamic --fp16-to-int8
```

------

### 使用本地模型

#### 本地 .pt 文件

```bash
python yolov11_quantization.py -m ./weights/yolo11s.pt --fp16-to-int8
```

#### 本地 OpenVINO 模型目录（跳过导出，直接量化）

```bash
python yolov11_quantization.py -m ./yolo11s_ov_fp16_static/ --fp16-to-int8
```

#### 指定输出目录

```bash
python yolov11_quantization.py -m yolo11s --fp16-to-int8 -o ./output_int8
```

------

## 输出目录命名规则

| 操作 | 输出目录示例 |
|------|-------------|
| `--export-fp16` | `yolo11s_ov_fp16_static/` |
| `--dynamic --export-fp16` | `yolo11s_ov_fp16_dynamic/` |
| `--fp16-to-int8` | `yolo11s_ov_fp16_static_int8/` |
| `--dynamic --fp16-to-int8` | `yolo11s_ov_fp16_dynamic_int8/` |

------

## 数据集与量化说明

- 使用 **COCO val2017** 作为校准数据集（自动下载）
- 仅用于 **INT8 calibration**，不参与训练
- 不需要标注文件
- 基于 **NNCF Post-Training Quantization (PTQ)**
- 使用 `QuantizationPreset.MIXED`，在精度与性能之间取得平衡
- 检测头后处理子图（DFL + box decode）保持浮点精度，避免精度损失

