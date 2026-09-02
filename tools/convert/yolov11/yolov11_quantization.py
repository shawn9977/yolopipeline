#!/usr/bin/env python3
"""
YOLO 模型 OpenVINO 导出 & INT8 量化脚本（支持静态/动态 shape）

用法示例：
  # 静态 shape（默认）
  python yolov11_quantization.py -m yolo11s --export-fp16
  python yolov11_quantization.py -m yolo11s --fp16-to-int8
  python yolov11_quantization.py -m yolo11s --export-fp32 --fp32-to-int8

  # 动态 shape
  python yolov11_quantization.py -m yolo11s --dynamic --export-fp16
  python yolov11_quantization.py -m yolo11s --dynamic --fp16-to-int8

  # 本地 .pt 文件
  python yolov11_quantization.py -m ./weights/yolo11s.pt --fp16-to-int8

  # 本地 OpenVINO 模型目录（跳过导出直接量化）
  python yolov11_quantization.py -m ./yolo11s_ov_fp16/ --fp16-to-int8

  # 指定输出目录
  python yolov11_quantization.py -m yolo11s --fp16-to-int8 -o ./output_int8
"""

import argparse
import shutil
import tempfile
from pathlib import Path
from zipfile import ZipFile
import re
import nncf
import openvino as ov
from ultralytics import YOLO
from ultralytics.cfg import get_cfg
from ultralytics.utils import DEFAULT_CFG


def parse_args():
    parser = argparse.ArgumentParser(
        description="YOLO 模型 OpenVINO 导出 & INT8 量化（支持静态/动态 shape）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "-m", "--model", required=True,
        help="模型路径或名称。可以是：\n"
             "  1) 本地 .pt 文件路径 (如 ./weights/yolo11s.pt)\n"
             "  2) 本地 OpenVINO 模型目录 (如 ./yolo11s_ov_fp16/，跳过导出直接量化)\n"
             "  3) 模型名称 (如 yolo11s，自动下载)",
    )
    parser.add_argument("--dynamic", action="store_true",
                        help="使用动态输入 shape（N/H/W 为 ?）。默认为静态 shape")
    parser.add_argument("--imgsz", type=int, default=640,
                        help="输入图像尺寸 (默认: 640)")
    parser.add_argument("--batch", type=int, default=1,
                        help="批量大小 (默认: 1)")
    parser.add_argument("--data-url",
                        default="http://images.cocodataset.org/zips/val2017.zip",
                        help="校准数据集 URL (默认: COCO val2017)")
    parser.add_argument("--export-fp32", action="store_true",
                        help="导出 FP32 OpenVINO IR")
    parser.add_argument("--export-fp16", action="store_true",
                        help="导出 FP16 OpenVINO IR")
    parser.add_argument("--fp32-to-int8", action="store_true",
                        help="将 FP32 模型量化为 INT8")
    parser.add_argument("--fp16-to-int8", action="store_true",
                        help="将 FP16 模型量化为 INT8")
    parser.add_argument("-o", "--output_dir", type=str, default=None,
                        help="INT8 模型输出目录 (默认: <模型名>_ov_<precision>_int8)")
    return parser.parse_args()


# ---------------------------------------------------------------------------
# 数据集准备
# ---------------------------------------------------------------------------

def download_if_missing(url: str, dst: Path):
    if dst.exists():
        return
    import requests
    dst.parent.mkdir(parents=True, exist_ok=True)
    print(f"[INFO] Downloading {url} -> {dst} ...")
    r = requests.get(url, stream=True)
    with open(dst, "wb") as f:
        shutil.copyfileobj(r.raw, f)
    print(f"[INFO] Downloaded {dst}")


def prepare_val2017(args):
    datasets_dir = Path.cwd() / "datasets"
    zip_path = datasets_dir / "val2017.zip"
    img_dir = datasets_dir / "coco/images/val2017"

    download_if_missing(args.data_url, zip_path)

    if not img_dir.exists():
        print(f"[INFO] Extracting {zip_path} ...")
        with ZipFile(zip_path, "r") as zf:
            zf.extractall(datasets_dir)

    return img_dir


# ---------------------------------------------------------------------------
# 导出
# ---------------------------------------------------------------------------

def export_openvino(yolo_model, args, precision):
    model_name = args.model_name
    shape_tag = "dynamic" if args.dynamic else "static"
    out_dir = Path(f"{model_name}_ov_{precision}_{shape_tag}")
    out_dir.mkdir(parents=True, exist_ok=True)

    xml_name = f"{model_name}_{precision}_{shape_tag}.xml"
    bin_name = f"{model_name}_{precision}_{shape_tag}.bin"
    xml_path = out_dir / xml_name

    if xml_path.exists() and (out_dir / bin_name).exists():
        print(f"[INFO] {xml_name} already exists in {out_dir}")
        return xml_path

    half = precision == "fp16"
    print(f"[INFO] Exporting {precision.upper()} OpenVINO model ({shape_tag} shape) ...")

    yolo_model.export(
        format="openvino",
        dynamic=args.dynamic,
        imgsz=args.imgsz,
        batch=args.batch,
        half=half,
    )

    # Ultralytics default export directory
    default_dir = Path(f"{model_name}_openvino_model")
    default_xml = default_dir / f"{model_name}.xml"
    default_bin = default_dir / f"{model_name}.bin"
    default_meta = default_dir / "metadata.yaml"

    # Move files to target directory
    shutil.move(str(default_xml), str(xml_path))
    shutil.move(str(default_bin), str(out_dir / bin_name))
    if default_meta.exists():
        shutil.move(str(default_meta), str(out_dir / "metadata.yaml"))

    try:
        default_dir.rmdir()
    except OSError:
        pass

    print(f"[INFO] {xml_name} saved to {out_dir}")
    return xml_path


# ---------------------------------------------------------------------------
# 校准数据集 & INT8 量化
# ---------------------------------------------------------------------------

def build_calibration_dataset(yolo_model, args):
    val_img_dir = prepare_val2017(args)

    cfg = get_cfg(cfg=DEFAULT_CFG)
    cfg.project = tempfile.mkdtemp()
    cfg.name = "dummy"
    cfg.save = False
    cfg.save_txt = False
    cfg.save_conf = False
    cfg.plots = False
    cfg.show = False
    cfg.exist_ok = True

    validator = yolo_model.task_map[yolo_model.task]["validator"](args=cfg)
    validator.data = {
        "train": "",
        "val": str(val_img_dir),
        "nc": 80,
        "names": [],
        "channels": 3,
    }
    validator.stride = 32

    dataloader = validator.get_dataloader(val_img_dir.parents[2], batch_size=1)

    def transform_fn(data_item):
        return validator.preprocess(data_item)["img"].numpy()

    return nncf.Dataset(dataloader, transform_fn)


def quantize_to_int8(xml_path: Path, yolo_model, args):
    model_name = args.model_name
    precision = "fp32" if "fp32" in xml_path.name else "fp16"

    shape_tag = "dynamic" if args.dynamic else "static"

    if args.output_dir:
        int8_dir = Path(args.output_dir)
    else:
        int8_dir = Path(f"{model_name}_ov_{precision}_{shape_tag}_int8")
    int8_dir.mkdir(parents=True, exist_ok=True)
    int8_xml = int8_dir / f"{model_name}_{precision}_{shape_tag}_int8.xml"

    if int8_xml.exists() and (int8_xml.parent / f"{model_name}_{precision}_{shape_tag}_int8.bin").exists():
        print(f"[INFO] INT8 model already exists in {int8_dir}")
        return int8_xml

    core = ov.Core()
    ov_model = core.read_model(xml_path)

    calibration_dataset = build_calibration_dataset(yolo_model, args)

    # Dynamically find the detection head module index from the model graph
    det_head_idx = max(
        int(m.group(1))
        for op in ov_model.get_ordered_ops()
        for m in [re.match(r'__module\.model\.(\d+)/aten::cat/Concat$', op.get_friendly_name())]
        if m
    )

    # Find all Concat nodes in the detection head and use the last one as output
    det_concat_pattern = re.compile(
        rf'^__module\.model\.{det_head_idx}/aten::cat/Concat(_\d+)?$'
    )
    det_concat_nodes = [
        op.get_friendly_name()
        for op in ov_model.get_ordered_ops()
        if det_concat_pattern.match(op.get_friendly_name())
    ]
    det_concat_nodes.sort(
        key=lambda n: int(m.group(1)) if (m := re.search(r'Concat_(\d+)$', n)) else 0
    )
    output_concat = det_concat_nodes[-1]
    print(f"[INFO] Detection head idx={det_head_idx}, "
          f"input Concats: Concat/Concat_1/Concat_2, output Concat: {output_concat}")

    ignored_scope = nncf.IgnoredScope( # post-processing
        subgraphs=[
            nncf.Subgraph(inputs=[f"__module.model.{det_head_idx}/aten::cat/Concat",
                                f"__module.model.{det_head_idx}/aten::cat/Concat_1",
                                f"__module.model.{det_head_idx}/aten::cat/Concat_2"],
                        outputs=[output_concat])
        ]
    )

    print(f"[INFO] Running INT8 quantization based on {precision.upper()} model ...")
    int8_model = nncf.quantize(
        ov_model,
        calibration_dataset,
        preset=nncf.QuantizationPreset.MIXED,
        ignored_scope=ignored_scope,
    )

    ov.save_model(int8_model, int8_xml)

    meta_src = xml_path.parent / "metadata.yaml"
    if meta_src.exists():
        shutil.copy(meta_src, int8_xml.parent / "metadata.yaml")

    print(f"[INFO] {int8_xml.name} saved to {int8_xml.parent}")
    return int8_xml


# ---------------------------------------------------------------------------
# 模型路径解析
# ---------------------------------------------------------------------------

def resolve_model(args):
    """解析 --model 参数，支持本地 .pt 文件、OpenVINO 目录或模型名称"""
    model_input = args.model
    model_path = Path(model_input)

    if model_path.is_dir():
        xml_files = list(model_path.glob("*.xml"))
        if not xml_files:
            raise FileNotFoundError(f"目录 {model_path} 中没有找到 .xml 模型文件")
        xml_path = xml_files[0]
        model_name = xml_path.stem
        for suffix in ("_fp16", "_fp32"):
            if model_name.endswith(suffix):
                model_name = model_name[: -len(suffix)]
                break
        print(f"[INFO] 使用本地 OpenVINO 模型目录: {model_path}")
        return model_name, None, xml_path

    if model_path.is_file() and model_input.endswith(".pt"):
        model_name = model_path.stem
        print(f"[INFO] 使用本地模型文件: {model_path}")
        return model_name, str(model_path), None

    return model_input, f"{model_input}.pt", None


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    args = parse_args()

    model_name, pt_path, ov_xml = resolve_model(args)
    args.model_name = model_name

    # 用户直接提供了 OpenVINO 目录，跳过导出
    if ov_xml is not None:
        yolo_model = YOLO(str(ov_xml.parent))
        if args.fp32_to_int8 or args.fp16_to_int8:
            quantize_to_int8(ov_xml, yolo_model, args)
        else:
            print("[INFO] 已加载 OpenVINO 模型目录，若需量化请添加 --fp32-to-int8 或 --fp16-to-int8")
        return

    yolo_model = YOLO(pt_path)

    # FP32 export
    fp32_xml = None
    if args.export_fp32 or args.fp32_to_int8:
        fp32_xml = export_openvino(yolo_model, args, "fp32")

    # FP16 export
    fp16_xml = None
    if args.export_fp16 or args.fp16_to_int8:
        fp16_xml = export_openvino(yolo_model, args, "fp16")

    # FP32 -> INT8
    if args.fp32_to_int8:
        if not fp32_xml.exists():
            fp32_xml = export_openvino(yolo_model, args, "fp32")
        quantize_to_int8(fp32_xml, yolo_model, args)

    # FP16 -> INT8
    if args.fp16_to_int8:
        if not fp16_xml.exists():
            fp16_xml = export_openvino(yolo_model, args, "fp16")
        quantize_to_int8(fp16_xml, yolo_model, args)


if __name__ == "__main__":
    main()
