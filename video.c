#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/videodev2.h>

#define BUFFER_COUNT 2




int main(int argc, char **argv)
{
    // Open the video capture device 
    int fd = open("/dev/video0", O_RDWR |O_NONBLOCK);
   
    if (fd < 0) {
        perror("Failed to open video capture device");
        exit(1);
    }

    // Here Setting the video capture device format and parameters
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;	//V4L2_PIX_FMT_H264; 	// for raw format
    fmt.fmt.pix.field =V4L2_FIELD_NONE; 	//V4L2_FIELD_ANY;	// for raw format
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Failed to set video format");
        		// if i change the loopback device it gives device busy (business man :P )
        		//  ==> sorry i am the dumb here, business man, i thought u a multitasker hhh
        exit(1);
    }

    // The output device,  i'm creating a new file to output it 
    int out_fd = open("out.mp4", O_CREAT | O_WRONLY | O_TRUNC |O_NONBLOCK, 0644); // it can be /dev/video1 or out.raw
    if (out_fd < 0) {
        perror("Failed to open output device");
        exit(1);
    }

    // Allocate buffers for the video capture device
    // source : nhttps://www.linuxtv.org/downloads/v4l-dvb-apis-old/mmap.html
    struct v4l2_requestbuffers req = {0};
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // allocate device buffers 
    req.memory = V4L2_MEMORY_MMAP;  	    // memory mapped buffers allocated in kernel
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to allocate buffers");
        exit(1);
    }

    		//  buffers specifications and map them into their @ space with mmap() 
    struct buffer {
        void *start;
        size_t length;
    } *buffers;
    buffers = calloc(req.count, sizeof(*buffers));
    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("Failed to query buffer");
            exit(1);
        }
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("Failed to map buffer");
            exit(1);
        }
    }

    // Enqueue the video buffers before processing
    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("Failed to enqueue buffer");
            exit(1);
        }
    }

    // Start capturing video frames
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start capturing");
        exit(1);
    }

    // Use poll to wait for video capture events 
    struct pollfd poll_fds[1];
    poll_fds[0].fd = fd;
    poll_fds[0].events = POLLIN;

    while (1) {
        int ret = poll(poll_fds, 1, 1000); // wait up to 1 second for events 
        				  // ( put -1 if it should wait forever )
        if (ret < 0) {
            perror("Failed to poll");
            exit(1);
        } else if (ret == 0) {
            printf("Poll timeout\n");
            continue;
        }

        if (poll_fds[0].revents & POLLIN) {
            // Dequeue the video buffer for sending it to the output/display
            struct v4l2_buffer buf = {0};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
                perror("Failed to dequeue buffer");
                exit(1);
            }

            // Write the video data to the output device
            write(out_fd, buffers[buf.index].start, buf.bytesused);

            // Enqueue the video buffer again
            if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                perror("Failed to enqueue buffer");
                exit(1);
            }
        }
    }

    // Stop capturing video frames
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Failed to stop capturing");
        exit(1);
    }

    // Unmap/free the video buffers
    for (int i = 0; i < req.count; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }

    // Close the video capture device and the output device
    close(fd);
    close(out_fd);

    return 0;
}

// To do :  
//	- Write the tools/cmds used to test 
//	- Try to add/implement events by v4l2_subscribe_event
//	- Try to put capturing/buffering in a thread by pipe 

