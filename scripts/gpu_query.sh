#!/bin/bash
# GPU Query Script - combines sysfs, lspci, and clinfo

# Cache clinfo output once, extract only key fields per GPU device
declare -A CL
cur_bdf=""
# Temp vars to collect fields before we know the BDF
t_name="" t_cl_driver="" t_cl_ver="" t_cus="" t_clock="" t_gmem="" t_maxalloc=""
t_lmem_type="" t_lmem="" t_slices="" t_ss="" t_eu_ss="" t_thr_eu="" t_is_gpu=0

flush_device() {
  if [ -n "$cur_bdf" ] && [ "$t_is_gpu" = "1" ]; then
    CL[${cur_bdf}_name]="$t_name"
    CL[${cur_bdf}_cl_driver]="$t_cl_driver"
    CL[${cur_bdf}_cl_ver]="$t_cl_ver"
    CL[${cur_bdf}_cus]="$t_cus"
    CL[${cur_bdf}_clock]="$t_clock"
    CL[${cur_bdf}_gmem]="$t_gmem"
    CL[${cur_bdf}_maxalloc]="$t_maxalloc"
    CL[${cur_bdf}_lmem_type]="$t_lmem_type"
    CL[${cur_bdf}_lmem]="$t_lmem"
    CL[${cur_bdf}_slices]="$t_slices"
    CL[${cur_bdf}_ss]="$t_ss"
    CL[${cur_bdf}_eu_ss]="$t_eu_ss"
    CL[${cur_bdf}_thr_eu]="$t_thr_eu"
  fi
}

while IFS= read -r line; do
  case "$line" in
    *"Device Name"*)
      flush_device
      t_name=$(echo "$line" | sed 's/.*Device Name *//')
      cur_bdf=""; t_cl_driver=""; t_cl_ver=""; t_cus=""; t_clock=""
      t_gmem=""; t_maxalloc=""; t_lmem_type=""; t_lmem=""
      t_slices=""; t_ss=""; t_eu_ss=""; t_thr_eu=""; t_is_gpu=0
      ;;
    *"Device Type"*GPU*)    t_is_gpu=1;;
    *"Device PCI bus info"*)
      cur_bdf=$(echo "$line" | grep -oP '[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-9]')
      ;;
    *"Driver Version"*)     t_cl_driver=$(echo "$line" | sed 's/.*Driver Version *//');;
    *"Device Version"*)     t_cl_ver=$(echo "$line" | sed 's/.*Device Version *//');;
    *"Max compute units"*)  t_cus=$(echo "$line" | awk '{print $NF}');;
    *"Max clock frequency"*) t_clock=$(echo "$line" | awk '{print $NF}');;
    *"Global memory size"*) t_gmem=$(echo "$line" | grep -oP '\(.*?\)' | tr -d '()');;
    *"Max memory allocation"*) t_maxalloc=$(echo "$line" | grep -oP '\(.*?\)' | tr -d '()');;
    *"Local memory type"*)  t_lmem_type=$(echo "$line" | awk '{print $NF}');;
    *"Local memory size"*)  t_lmem=$(echo "$line" | grep -oP '\(.*?\)' | tr -d '()');;
    *"Slices (Intel)"*)     t_slices=$(echo "$line" | awk '{print $NF}');;
    *"Sub-slices per slice"*) t_ss=$(echo "$line" | awk '{print $NF}');;
    *"EUs per sub-slice"*)  t_eu_ss=$(echo "$line" | awk '{print $NF}');;
    *"Threads per EU"*)     t_thr_eu=$(echo "$line" | awk '{print $NF}');;
  esac
done < <(clinfo 2>/dev/null)
flush_device

for card in /dev/dri/card*; do
  n=$(basename $card)
  dev=/sys/class/drm/$n/device
  pci=$(readlink -f $dev | xargs basename)
  render=$(ls $dev/drm/ 2>/dev/null | grep render)
  desc=$(lspci -s $pci | sed 's/.*: //;s/ (.*//')
  drv=$(basename $(readlink -f $dev/driver) 2>/dev/null)
  drv_ver=$(modinfo $drv 2>/dev/null | awk '/^vermagic:/{print $2}')
  boot_vga=$(cat $dev/boot_vga 2>/dev/null)
  link_speed=$(cat $dev/current_link_speed 2>/dev/null)
  link_width=$(cat $dev/current_link_width 2>/dev/null)
  max_speed=$(cat $dev/max_link_speed 2>/dev/null)
  max_width=$(cat $dev/max_link_width 2>/dev/null)

  echo "=========================================="
  [ "$boot_vga" = "1" ] && gpu_type="iGPU" || gpu_type="dGPU"
  echo "$gpu_type: $pci ($desc)"
  echo "  DRI:            $n / $render"
  echo "  Kernel Driver:  $drv (kernel: $drv_ver)"
  echo "  OpenCL Driver:  ${CL[${pci}_cl_driver]:-N/A}"
  echo "  OpenCL Version: ${CL[${pci}_cl_ver]:-N/A}"
  echo "  Boot VGA:       $boot_vga"
  echo "  Slices:         ${CL[${pci}_slices]:-N/A} (sub-slices/slice: ${CL[${pci}_ss]:-N/A})"
  echo "  EUs (CUs):      ${CL[${pci}_cus]:-N/A} (EUs/sub-slice: ${CL[${pci}_eu_ss]:-N/A}, threads/EU: ${CL[${pci}_thr_eu]:-N/A})"
  echo "  Max Clock:      ${CL[${pci}_clock]:-N/A}"
  echo "  Global Memory:  ${CL[${pci}_gmem]:-N/A} (max alloc: ${CL[${pci}_maxalloc]:-N/A})"
  echo "  Local Memory:   ${CL[${pci}_lmem]:-N/A} (${CL[${pci}_lmem_type]:-N/A})"
  echo "  PCIe Link:      $link_speed x$link_width (max: $max_speed x$max_width)"
  echo ""
done