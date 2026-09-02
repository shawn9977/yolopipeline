#source /opt/intel/openvino_2025/setupvars.sh
#Use shall run openvino setupvars.sh firstly
source ../scripts/setva.sh

if [ ! -d cache_dir ] 
then
    mkdir cache_dir
fi

#if JPEG encode is enabled, all jpeg files will be store under folder jpg
mkdir -p jpg


export VPL_DIR=/opt/intel/media/lib64/cmake/vpl/
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
cp multi_dec_infer_enc ../

