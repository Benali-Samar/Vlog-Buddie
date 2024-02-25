#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/videodev2.h>

#define BUFFER_COUNT 2
#define DESIRED_FRAME_RATE 50
#define POLL_TIMEOUT_US (1000000 / DESIRED_FRAME_RATE)

struct buffer {
    void *start;
    size_t length;
};

// Cleanup stuff
void cleanup(int fd, int out_fd, struct buffer *buffers, int buffer_count) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);

    for (int i = 0; i < buffer_count; i++) {
        if (buffers[i].start != MAP_FAILED) {
            munmap(buffers[i].start, buffers[i].length);
        }
    }

    if (fd >= 0) {
        close(fd);
    }
    if (out_fd >= 0) {
        close(out_fd);
    }

    if (buffers != NULL) {
        free(buffers);
    }
}


int main(int argc, char **argv) {
    int fd = -1;
    int out_fd = -1;
    struct buffer *buffers = NULL;

    // Input file
    fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open video capture device");
        exit(EXIT_FAILURE);
    }

    // Setting the video capture device format and parameters
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;  
    fmt.fmt.pix.field = V4L2_FIELD_NONE;          
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Failed to set video format");
        cleanup(fd, out_fd, buffers, BUFFER_COUNT);
        exit(EXIT_FAILURE);
    }

    // The output file
    out_fd = open("out.mp4", O_CREAT | O_WRONLY | O_TRUNC | O_NONBLOCK, 0644);
    if (out_fd < 0) {
        perror("Failed to open output device");
        cleanup(fd, out_fd, buffers, BUFFER_COUNT);
        exit(EXIT_FAILURE);
    }

    // Allocate memory for the video buffers
    struct v4l2_requestbuffers req = {0};
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  
    req.memory = V4L2_MEMORY_MMAP;           // Memory mapped buffers allocated in kernel
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to allocate buffers");
        cleanup(fd, out_fd, buffers, BUFFER_COUNT);
        exit(EXIT_FAILURE);
    }

    // Allocate memory for buffer pointers
    buffers = calloc(req.count, sizeof(struct buffer));
    if (!buffers) {
        perror("Failed to allocate memory for buffers");
        cleanup(fd, out_fd, buffers, BUFFER_COUNT);
        exit(EXIT_FAILURE);
    }

    // Map buffers into memory
    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("Failed to query buffer");
            cleanup(fd, out_fd, buffers, BUFFER_COUNT);
            exit(EXIT_FAILURE);
        }
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("Failed to map buffer");
            cleanup(fd, out_fd, buffers, BUFFER_COUNT);
            exit(EXIT_FAILURE);
        }
        // Enqueue the video buffer
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("Failed to enqueue buffer");
            cleanup(fd, out_fd, buffers, BUFFER_COUNT);
            exit(EXIT_FAILURE);
        }
    }

    // Start capturing video frames
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start capturing");
        cleanup(fd, out_fd, buffers, BUFFER_COUNT);
        exit(EXIT_FAILURE);
    }

    // Use poll to wait for video capture events 
    struct pollfd poll_fds[1];
    poll_fds[0].fd = fd;
    poll_fds[0].events = POLLIN;

    while (1) {
        int ret = poll(poll_fds, 1, POLL_TIMEOUT_US); // Wait up to 1 second for events 
        if (ret < 0) {
            perror("Failed to poll");
            cleanup(fd, out_fd, buffers, BUFFER_COUNT);
            exit(EXIT_FAILURE);
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
                cleanup(fd, out_fd, buffers, BUFFER_COUNT);
                exit(EXIT_FAILURE);
            }

            // Write the video data to the output device
            write(out_fd, buffers[buf.index].start, buf.bytesused);

            // Enqueue the video buffer again
            if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
                perror("Failed to enqueue buffer");
                cleanup(fd, out_fd, buffers, BUFFER_COUNT);
                exit(EXIT_FAILURE);
            }
            
            usleep(1000000 / DESIRED_FRAME_RATE);
        }
    }

    cleanup(fd, out_fd, buffers, BUFFER_COUNT);
    return 0;
}

