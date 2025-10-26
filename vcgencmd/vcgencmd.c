/*
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


/* ---- Include Files ---------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>		/* ioctl */

#define DEVICE_FILE_NAME "/dev/vcio"
#define MAJOR_NUM 100
#define IOCTL_MBOX_PROPERTY _IOWR(MAJOR_NUM, 0, char *)

#define MAX_STRING 1024

/*
 * use ioctl to send mbox property message
 */

static int mbox_property(int file_desc, void *buf)
{
   int ret_val = ioctl(file_desc, IOCTL_MBOX_PROPERTY, buf);

   if (ret_val < 0) {
      printf("ioctl_set_msg failed:%d\n", ret_val);
   }
   return ret_val;
}


static int mbox_open()
{
   int file_desc;

   // open a char device file used for communicating with kernel mbox driver
   file_desc = open(DEVICE_FILE_NAME, 0);
   if (file_desc < 0) {
      printf("Can't open device file: %s\n", DEVICE_FILE_NAME);
      printf("Try creating a device file with: sudo mknod %s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM);
      exit(-1);
   }
   return file_desc;
}

static void mbox_close(int file_desc) {
  close(file_desc);
}


#define GET_GENCMD_RESULT 0x00030080

static unsigned gencmd(int file_desc, const char *command, char *result, int result_len)
{
   int i=0;
   unsigned p[(MAX_STRING>>2) + 7];
   int len = strlen(command);
   // maximum length for command or response
   if (len + 1 >= MAX_STRING)
   {
     fprintf(stderr, "gencmd length too long : %d\n", len);
     return -1;
   }
   p[i++] = 0; // size
   p[i++] = 0x00000000; // process request

   p[i++] = GET_GENCMD_RESULT; // (the tag id)
   p[i++] = MAX_STRING;// buffer_len
   p[i++] = 0; // request_len (set to response length)
   p[i++] = 0; // error repsonse

   memcpy(p+i, command, len + 1);
   i += MAX_STRING >> 2;

   p[i++] = 0x00000000; // end tag
   p[0] = i*sizeof *p; // actual size

   mbox_property(file_desc, p);
   result[0] = 0;
   strncat(result, (const char *)(p+6), result_len);

   return p[5];
}

static void show_usage()
{
   puts( "Usage: vcgencmd command [ params ]" );
   puts( "Send a command to the VideoCore and print the result.\n" );
   puts( "Without any argument this information is shown.\n" );
   puts( "Use the command 'vcgencmd commands' to get a list of available commands\n" );
   puts( "Exit status:" );
   puts( "   0    command completed successfully" );
   puts( " else   VideoCore returned an error\n" );
   puts( "For further documentation please see" );
   puts( "https://www.raspberrypi.com/documentation/computers/os.html#vcgencmd\n" );
}

int main(int argc, char *argv[])
{
   int mb = mbox_open();
   int i;
   char command[MAX_STRING] = {};
   char result[MAX_STRING] = {};

   if ( argc == 1 )
   {
      // no arguments passed, so show basic usage
      show_usage();
      return 0;
   }

   for (i = 1; i < argc; i++)
   {
      char *c = command + strlen(command);
      if (c > command)
      {
         strncat(c, " ", command + sizeof command - c);
         c = command + strlen(command);
      }
      strncat(c, argv[i], command + sizeof command - c);
   }

   int ret = gencmd(mb, command, result, sizeof result);
   if (ret)
      printf( "vc_gencmd_read_response returned %d\n", ret );

   mbox_close(mb);

   printf("%s\n", result);
   return ret;
}
