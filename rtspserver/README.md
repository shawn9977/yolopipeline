# Define rtsp sources  
list.config  
#file_name    framerate    isLoop( >0 is loop)  
1080p.h264    30           0  
1080p2.h264   30           0  
  
#Note: put the test file under the same folder,Important!  
#Each file map one rtsp streaming, each Simulator_Server port can map multi streaming.  

# Define rtsp streaming   
rtsp_server.sh  
./Simulator_Server list.conf 1554 &  
./Simulator_Server list.conf 1555 &  
./Simulator_Server list.conf 1556 &  
./Simulator_Server list.conf 1557 &  
  
# Start rtsp_server.sh  
Create RTSP server at port 1554  
streaming 1080p.h264 @ 30.000000 simu0000  
streaming 1080p2.h264 @ 30.000000 simu0001  
Create RTSP server at port 1555  
streaming 1080p.h264 @ 30.000000 simu0000  
streaming 1080p2.h264 @ 30.000000 simu0001  
Create RTSP server at port 1556  
streaming 1080p.h264 @ 30.000000 simu0000  
streaming 1080p2.h264 @ 30.000000 simu0001  
Create RTSP server at port 1557  
streaming 1080p.h264 @ 30.000000 simu0000  
streaming 1080p2.h264 @ 30.000000 simu0001  

# Test  
sudo apt-get install ffmpeg  
ffplay rtsp://ip:1554/simu0000  
ffplay rtsp://ip:1554/simu0001  
