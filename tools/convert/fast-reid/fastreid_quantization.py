import argparse
from pathlib import Path
import cv2
import numpy as np
import openvino as ov
import nncf


def parse_args():
    parser = argparse.ArgumentParser(
        description="FastReID OpenVINO INT8 Post-Training Quantization"
    )
    parser.add_argument(
        "-m", "--model",
        required=True,
        help="Path to FP16 OpenVINO model (.xml)"
    )
    parser.add_argument(
        "-o", "--output",
        required=True,
        help="Output INT8 model path (.xml)"
    )
    parser.add_argument(
        "-d", "--data",
        required=True,
        help="Calibration image directory (MSMT17_V1/train folder or Market-1501-v15.09.15/bounding_box_train folder)"
    )
    parser.add_argument(
        "--subset-size",
        type=int,
        default=300,
        help="Number of calibration images (default: 300)"
    )
    return parser.parse_args()


def preprocess_image(img_path, w, h):
    img = cv2.imread(str(img_path))                 # BGR, HWC
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)      # RGB
    img = cv2.resize(img, (w, h))                   # width, height
    img = img.astype(np.float32) / 255.0

    # FastReID mean / std (ImageNet)
    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std  = np.array([0.229, 0.224, 0.225], dtype=np.float32)
    img = (img - mean) / std                        # HWC

    img = img.transpose(2, 1, 0)                    # HWC -> CWH
    img = np.expand_dims(img, axis=0)               # NCWH
    return img


def calibration_data_generator(image_dir, input_name, w, h):
    image_paths = sorted(Path(image_dir).rglob("*.jpg"))
    for img_path in image_paths:
        yield {input_name: preprocess_image(img_path, w, h)}


def main():
    args = parse_args()

    print("[STEP 1] Load OpenVINO model...")
    core = ov.Core()
    model = core.read_model(args.model)

    input_tensor = model.input(0)
    input_name = input_tensor.get_any_name()

    shape = input_tensor.partial_shape

    # Expected input layout: NCWH
    w = shape[2].get_length()  # width
    h = shape[3].get_length()  # height

    print("[STEP 2] Build NNCF calibration dataset...")
    calibration_dataset = nncf.Dataset(
        calibration_data_generator(args.data, input_name, w, h)
    )

    print("[STEP 3] Start INT8 post-training quantization...")
    quantized_model = nncf.quantize(
        model=model,
        calibration_dataset=calibration_dataset,
        subset_size=args.subset_size,
        preset=nncf.QuantizationPreset.PERFORMANCE,
        advanced_parameters=nncf.AdvancedQuantizationParameters(
            smooth_quant_alpha=0.5  # balance accuracy and performance
        )
    )

    print("[STEP 4] Save INT8 model...")
    ov.save_model(quantized_model, args.output)
    print("[DONE] INT8 model saved as:", args.output)


if __name__ == "__main__":
    main()
