#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <thread>
#include <cmath>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <string>


#include "vpldecenc.h"

#define CHECK_RET_CONTINUE(ret) \
    if ((ret) != 0) { \
        std::cout<<"pipeline "<<i<<" ran into error"<<std::endl; \
        continue; \
    } 
//if pipeline number is bigger than GROUP_MAX_SIZE, they will be put into
//different groups
#define GROUP_MAX_SIZE_DEFAULT 10 

bool ReadInputPaths(std::vector<std::string>& inputPaths) {
    namespace fs = std::filesystem;

    const fs::path fileName = "input.txt";
    if (!fs::exists(fileName) || !fs::is_regular_file(fileName)) {
        return false; // file not found
    }

    std::ifstream in(fileName);
    if (!in) {
        return false; // cannot open file (permissions, etc.)
    }

    inputPaths.clear();
    std::string line;

    // Optional: handle UTF-8 BOM if present (0xEF,0xBB,0xBF)
    {
        char c1 = static_cast<char>(in.peek());
        if (static_cast<unsigned char>(c1) == 0xEF) {
            char bom[3];
            in.read(bom, 3);
            // If not actually a BOM, put back what we read
            if (!(static_cast<unsigned char>(bom[0]) == 0xEF &&
                  static_cast<unsigned char>(bom[1]) == 0xBB &&
                  static_cast<unsigned char>(bom[2]) == 0xBF)) {
                // Not a proper BOM; reset stream (simple fallback: reopen)
                in.close();
                std::ifstream in2(fileName);
                if (!in2) return false;
                std::swap(in, in2);
            }
        }
    }

    while (std::getline(in, line)) {
        // Optionally trim CR in case of Windows-style line endings (\r\n)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        inputPaths.push_back(line);
    }

    return true;
}

//gpu_id_offset is used to specify different GPUs 
int group_main(int argc, char* argv[], int num_groups, int group_id, int gpu_id_offset)
{
    int pipeline_num = 10;
    int gpu_id = 0;

    if (argc > 3 ) {
        int total_pipe = atoi(argv[3]);
        if (num_groups == 1) {
            pipeline_num = total_pipe;
        } else {
            if ((group_id+1) == num_groups) {
                //last group
                int per_group = (total_pipe + num_groups - 1) / num_groups;
                pipeline_num = total_pipe - (per_group) * (num_groups - 1);
            } else {
                pipeline_num = (total_pipe + num_groups - 1) / num_groups;
            }
        }
    }

    if (argc > 4 ) {
        //gpu_id is the first GPU to use. If gpu_id_offset isn't zero, 
        //gpu_id_offset specfies which GPU this group runs on.
        gpu_id = atoi(argv[4]);
    }

    gpu_id += gpu_id_offset;

    bool save_jpeg = false;
    bool video_encode = false;
    bool loop_input = false;
    if (getenv("SAVE_JPG")) {
        save_jpeg = true;
    }

    if (getenv("ENCODE")) {
        video_encode = true;
    }

    if (getenv("LOOP")) {
        loop_input = true;
    }

    std::cout<<"Group id " << group_id <<", GPU id "<<gpu_id<<" , save jpg "<< save_jpeg<<std::endl;
    std::cout<<"Test "<<pipeline_num<<" pipelines"<<std::endl;

    VplDecEnc vplDecEnc;

    ///Device 0 is /dev/dri/renderD128, Device 1 is /dev/dri/renderD129
    if (vplDecEnc.Init(gpu_id) != 0) {
        std::cout<<"vplDecEnc.Init() failed!"<<std::endl;
        return -1;
    }

    if (vplDecEnc.CreateInferenceModel(argv[1], argv[2]) != 0) {
        std::cout<<"vplDecEnc.CreateInferenceModel failed"<<std::endl;
        return -1;
    }
 
    std::vector<std::thread> pipethreads;
    std::vector<std::string> inputPaths;
    ReadInputPaths(inputPaths); 
    char encodeOutFilename[64] = {};
    int pipeline_id_offset = pipeline_num * group_id; //assume each group get same number of pipeline
    for (int i = 0; i < pipeline_num; i++) {
        int id = i + pipeline_id_offset;

        CHECK_RET_CONTINUE(vplDecEnc.AddDecoder(id, MFX_CODEC_AVC));
        if (inputPaths.size() > 0) {
            int ri = i % inputPaths.size();
            CHECK_RET_CONTINUE(vplDecEnc.SetDecodeInput(id,inputPaths[ri].c_str()));
        } else {
            CHECK_RET_CONTINUE(vplDecEnc.SetDecodeInput(id,"../assets/1080p.h264"));
        }

        if (save_jpeg) {
            vplDecEnc.AddJpgEncoder(id);
        }

        if (video_encode) {
            CHECK_RET_CONTINUE(vplDecEnc.AddEncoder(id, MFX_CODEC_HEVC, 1920, 1080, 6000));
            if (loop_input) {
                //Avoid large  encoded file 
                snprintf(encodeOutFilename, 64, "/dev/null");
            }
            else {
                snprintf(encodeOutFilename, 64, "enc_%d.h265",id);
            }
            CHECK_RET_CONTINUE(vplDecEnc.SetEncodeOutput(id,encodeOutFilename));
        }
        
        std::cout<<"Group "<<group_id<<" start pipeline "<<id<<std::endl;
        pipethreads.push_back(std::thread(vplDecEnc.RunPipeline,std::ref(vplDecEnc), id));
    }

    for (int i = 0; i < pipeline_num; i++) {
        int id = i + pipeline_id_offset;
        if (i < pipethreads.size()) {
            pipethreads[i].join();
        }
        if (save_jpeg) {
            vplDecEnc.RemoveJpgEncoder(id);
        }

        if (video_encode) {
            vplDecEnc.RemoveEncoder(id);
        }

        std::cout<<"Remove decode "<<id<<" in group "<<group_id<<std::endl;
        vplDecEnc.RemoveDecoder(id);
    }
    return 0;
}


int main(int argc, char **argv) {
    std::vector<std::thread> threads;
    int num_groups = 1;
    int num_gpu = 1; 
    int group_max_size = GROUP_MAX_SIZE_DEFAULT; 

    if (argc < 2) {
        std::cout<<"yolov11s.xml fastreid.xml paths must be specified" <<std::endl;
        std::cout<<"Usage: "<<argv[0]<<" yolov11s.xml fastreid.xml pipeline_num [gpu_id] [gpu_num] [group_max_size]"<<std::endl;
        std::cout<<"If gpu_id is not set, GPU 0 (renderD128) will be used by default"<<std::endl; 
        std::cout<<"gpu_num is 1 by default. group_max_size is 10 by default"<<std::endl;
        std::cout<<"Example: Test 32 streams on GPU 0 (renderD128) "<<std::endl;
        std::cout<<"\t"<<argv[0]<<" yolov11s.xml fastreid.xml 32"<<std::endl;
        std::cout<<"\t Test 32 streams on GPU 1 (renderD129) "<<std::endl;
        std::cout<<"\t"<<argv[0]<<" yolov11s.xml fastreid.xml 32 1"<<std::endl;

        std::cout<<"\t Test 64 streams on GPU 0 and GPU 1 (renderD128/D129) "<<std::endl;
        std::cout<<"\t The 64 streams will be spit to (64 + group_max_size - 1)/group_max_size  groups. The group 0, 2, 4 will run on GPU 0 and grup 1, 3, 5 will run on GPU 1."<<std::endl;
        std::cout<<"\t"<<argv[0]<<" yolov11s.xml fastreid.xml 64 0 2"<<std::endl;
        std::cout<<"\t"<<"If you want to set group_max_size, below is a example to split 48 streams into 6 groups running on 2 GPU "<<std::endl;
        std::cout<<"\t"<<argv[0]<<" yolov11s.xml fastreid.xml 48 0 2 8"<<std::endl;
        return 0;
    }

    int total_num = 10;
    if (argc > 6) {
        group_max_size = atoi(argv[6]);
    }

    //If GPU number is set, the number of groups is equal to GPU number when
    //total number of pipeline is below  group_max_size*GPU number.
    if (argc > 5 && atoi(argv[5]) > 1) {
        num_gpu = atoi(argv[5]);
        total_num = atoi(argv[3]);
        if (total_num >  group_max_size * num_gpu) {
            num_groups = (total_num +  group_max_size - 1) / group_max_size ;
        } else {
            num_groups = num_gpu;
        } 
    } else if (argc > 3 && atoi(argv[3]) >  group_max_size) {
        total_num = atoi(argv[3]);
        num_groups = (total_num +  group_max_size - 1) /  group_max_size; 
    }

    
    int gpu_group_size = (total_num + num_groups - 1) / num_groups;

    std::cout<<"GPU number is "<<num_gpu<<std::endl;
    std::cout<<"Total pipeline number is "<<  atoi(argv[3]) <<" . Will split to "<< num_groups <<" groups. Each group contains "<< gpu_group_size <<" pipelines"<<std::endl;


    for (int i = 0; i < num_groups; ++i) {
        int gpu_offset = 0;
        if (num_gpu > 1) {
            if (num_groups > num_gpu) {
                //each GPU runs two groups
                gpu_offset = i % num_gpu;
            }
            else {
                //each GPU runs one group
                gpu_offset = i;
            }
        }

        threads.emplace_back([&, i]() {
                 group_main(argc, argv, num_groups,  i, gpu_offset);
                }); 

        std::cout << "Created thread " << i << " (ID: " << threads[i].get_id() << ")" << "for group "<<i<<" gpu_offset "<< gpu_offset<<std::endl;

        // Optional: Add small delay between thread creation to avoid resource contention
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }
    std::cout<<"All threads have finished."<<std::endl;
    return 0;
}
