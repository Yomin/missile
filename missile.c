/*
 * Copyright (c) 2014 Martin Rödel aka Yomin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define _POSIX_SOURCE

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <libusb-1.0/libusb.h>


#define VENDOR  0x2123
#define PRODUCT 0x1010

#define REQUESTTYPE LIBUSB_RECIPIENT_INTERFACE| \
                    LIBUSB_REQUEST_TYPE_CLASS|LIBUSB_ENDPOINT_OUT
#define REQUEST     LIBUSB_REQUEST_SET_CONFIGURATION
#define VALUE       0x00
#define INDEX       0x00
#define LENGTH      8

#define CMD_UP    0
#define CMD_DOWN  1
#define CMD_LEFT  2
#define CMD_RIGHT 3
#define CMD_STOP  4
#define CMD_FIRE  5


unsigned char cmds[][LENGTH] =
{
    { 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

int attached, claimed;
struct libusb_context *ctx;
struct libusb_device **devices;
struct libusb_device_handle *handle;


int getch()
{
    int c, fd;
    struct termios tnew, told;
    
    fd = fileno(stdin);
    tcgetattr(fd, &told);
    tnew = told;
    tnew.c_lflag &= ~(ICANON|ECHO);
    tcsetattr(fd, TCSANOW, &tnew);
    c = getchar();
    tcsetattr(fd, TCSANOW, &told);
    
    return c;
}

int get()
{
    int c;
    
    if((c = getch()) == 27) // escape
        if((c = getch()) == '[')
            return getch();
    
    return c;
}

int cleanup(int err)
{
    int ret;
    
    if(devices)
        libusb_free_device_list(devices, 1);
    if(claimed && (ret = libusb_release_interface(handle, 0)))
    {
        libusb_close(handle);
        libusb_exit(ctx);
        printf("Failed to release interface: %s\n",
            libusb_strerror(ret));
        return err ? err : 6;
    }
    if(attached && (ret = libusb_attach_kernel_driver(handle, 0)))
    {
        libusb_close(handle);
        libusb_exit(ctx);
        printf("Failed to reattach kernel driver: %s\n",
            libusb_strerror(ret));
        return err ? err : 7;
    }
    if(handle)
        libusb_close(handle);
    if(ctx)
        libusb_exit(ctx);
    
    return err;
}

void handler(int signal)
{
    cleanup(0);
}

int main(int argc, char *argv[])
{
    int ret, cmd = -1;
    struct libusb_device **ptr;
    struct libusb_device_descriptor desc;
    
    ctx = 0;
    devices = 0;
    handle = 0;
    attached = claimed = 0;
    
    signal(SIGINT, handler);
    
    libusb_init(&ctx);
    
    if((ret = libusb_get_device_list(ctx, &devices)) < 0)
    {
        printf("Failed to get device list: %s\n", libusb_strerror(ret));
        return 1;
    }
    
    ptr = devices;
    while(*ptr)
    {
        libusb_get_device_descriptor(*ptr, &desc);
        if(desc.idVendor == VENDOR && desc.idProduct == PRODUCT)
            break;
        ptr++;
    }
    
    if(!*ptr)
    {
        printf("Device not found\n");
        return cleanup(0);
    }
    
    if((ret = libusb_open(*ptr, &handle)))
    {
        printf("Failed to open device: %s\n", libusb_strerror(ret));
        return cleanup(2);
    }
    
    libusb_free_device_list(devices, 1);
    devices = 0;
    
    switch((ret = libusb_kernel_driver_active(handle, 0)))
    {
    case 0:
        break;
    case 1:
        attached = 1;
        if((ret = libusb_detach_kernel_driver(handle, 0)))
        {
            printf("Failed to detach kernel driver: %s\n",
                libusb_strerror(ret));
            return cleanup(3);
        }
        break;
    default:
        printf("Failed to determine if kernel driver active: %s\n",
            libusb_strerror(ret));
        return cleanup(4);
    }
    
    if((ret = libusb_claim_interface(handle, 0)))
    {
        printf("Failed to claim interface: %s\n", libusb_strerror(ret));
        return cleanup(5);
    }
    
    claimed = 1;
    
    printf("Arrow keys for control. Press again for stop.\n");
    printf("Enter for fire. Space for stop. q for quit.\n");
    
    while((ret = get()))
    {
        switch(ret)
        {
        case 'A': cmd = cmd == CMD_UP    ? CMD_STOP : CMD_UP;    break;
        case 'B': cmd = cmd == CMD_DOWN  ? CMD_STOP : CMD_DOWN;  break;
        case 'C': cmd = cmd == CMD_RIGHT ? CMD_STOP : CMD_RIGHT; break;
        case 'D': cmd = cmd == CMD_LEFT  ? CMD_STOP : CMD_LEFT;  break;
        case 0xa: cmd = CMD_FIRE; break;
        case ' ': cmd = CMD_STOP; break;
        case 'q': return cleanup(0);
        default:
            printf("Unrecognized key [%c](%i)\n", ret, ret);
            continue;
        }
        libusb_control_transfer(handle, REQUESTTYPE, REQUEST, VALUE,
            INDEX, cmds[cmd], LENGTH, 0);
    }
}
