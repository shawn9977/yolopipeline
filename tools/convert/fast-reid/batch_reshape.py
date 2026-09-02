#!/usr/bin/env python3
"""
Fix Reshape batch constants in dynamic-batch OpenVINO IR models.

Problem:
  When FastReID is exported via torch.onnx.export with batch=1 input,
  PyTorch traces attention blocks' view()/reshape() calls with literal 1
  for the batch dimension.  After ovc converts with --input [1..20,3,128,384],
  the model input becomes dynamic, but internal Reshape nodes still have
  hardcoded batch=1 in their target-shape constants (e.g. [1,1,2,-1],
  [1,2,1,-1], [1,128,1,1]).  This prevents model.reshape() from setting
  batch>1 at runtime.

Fix:
  For every Reshape node with special_zero=True whose target-shape constant
  has first element == 1, change it to 0.  Under special_zero semantics,
  0 means "copy from the corresponding input dimension", so the batch dim
  propagates correctly for any batch size.

Usage:
  python fix_reshape_batch.py -m fp16-dynamic/msmt_agw_S50.xml -o fp16-dynamic-fixed/msmt_agw_S50.xml

Insert this step between ovc and fastreid_quantization.py:
  1. ovc ... --input [1..20,3,128,384] --output_model fp16-dynamic/ ...
  2. python fix_reshape_batch.py -m fp16-dynamic/model.xml -o fp16-dynamic-fixed/model.xml
  3. python fastreid_quantization.py -m fp16-dynamic-fixed/model.xml ...
"""

import argparse
import numpy as np
import openvino as ov
from openvino.runtime import opset1


def fix_reshape_batch_constants(model):
    """
    For every Reshape(special_zero=True) node whose Constant target shape
    has first element == 1, change it to 0 so the batch dimension is
    copied from the input tensor instead of being hardcoded.
    """
    fixed = 0
    # Cache: original Constant node id -> new Constant output, so that
    # multiple Reshape nodes sharing the same Constant reuse one replacement.
    replaced_consts = {}

    for op in model.get_ops():
        if op.get_type_name() != "Reshape":
            continue
        # Check special_zero attribute
        if not op.get_attributes().get("special_zero", False):
            continue

        # input(1) is the target shape
        shape_input = op.input(1).get_source_output().get_node()
        if shape_input.get_type_name() != "Constant":
            continue

        const_id = id(shape_input)
        if const_id in replaced_consts:
            # Already created a replacement for this shared Constant
            op.input(1).replace_source_output(replaced_consts[const_id])
            fixed += 1
            continue

        target_shape = shape_input.get_data().flatten().tolist()
        if not target_shape or int(target_shape[0]) != 1:
            continue

        # Change first element from 1 to 0 (special_zero: copy from input dim 0)
        target_shape[0] = 0
        new_const = opset1.constant(
            np.array(target_shape, dtype=np.int64),
            name=shape_input.get_friendly_name() + f"_batch_fixed_{fixed}",
        )
        new_output = new_const.output(0)
        replaced_consts[const_id] = new_output
        print(f"Fixing Reshape node '{op.get_friendly_name()}': changing target shape {shape_input.get_data().flatten().tolist()} to {target_shape}")
        op.input(1).replace_source_output(new_output)
        fixed += 1

    if fixed > 0:
        model.validate_nodes_and_infer_types()
    return fixed

def main():
    parser = argparse.ArgumentParser(
        description="Fix Reshape batch constants in dynamic-batch OpenVINO IR models"
    )
    parser.add_argument("-m", "--model", required=True, help="Input IR model (.xml)")
    parser.add_argument("-o", "--output", required=True, help="Output fixed model (.xml)")
    args = parser.parse_args()

    core = ov.Core()
    model = core.read_model(args.model)

    input_shape = model.input().get_partial_shape()
    print(f"Input shape: {input_shape}")

    if input_shape[0].is_dynamic:
        print("Dynamic batch detected")
    else:
        print(f"Static batch={input_shape[0].get_length()}, fixing Reshape constants to allow batch reshaping")

    fixed = fix_reshape_batch_constants(model)
    print(f"Fixed {fixed} Reshape target-shape constants (1 -> 0 for batch dim)")

    ov.save_model(model, args.output)
    print(f"Saved fixed model to: {args.output}")


if __name__ == "__main__":
    main()
