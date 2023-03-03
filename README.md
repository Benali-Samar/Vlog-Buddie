#Video-capture code using V4L2, poll, mmap and ioctl

I- Discription

This repo contains a simple manipulation to just video capturing 
and sends the frames to the output file.

1- Opens the video capture device
2- Set the video formats and parametres
3- Opens the output device
4- Allocate buffers
5- Memory mapping the buffers
6- Enqueue the video buffers
7- Start Capturing video frames
8- Polling on the fd of the device
9- Dequeue the video buffers
10- Writes the video data to the output device
11- Clean up (stop capturing, unmap buffers and close devices) 

II- Testing

Just compile by " gcc -o test video.c" and execute "./test" and here it is!
