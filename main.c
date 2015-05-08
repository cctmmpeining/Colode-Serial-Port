#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>



#define PUT_2B(array,offset,value)  \
        (array[offset] = (char) ((value) & 0xFF), \
         array[offset+1] = (char) (((value) >> 8) & 0xFF))
#define PUT_4B(array,offset,value)  \
        (array[offset] = (char) ((value) & 0xFF), \
         array[offset+1] = (char) (((value) >> 8) & 0xFF), \
         array[offset+2] = (char) (((value) >> 16) & 0xFF), \
         array[offset+3] = (char) (((value) >> 24) & 0xFF))

void write_bmp_header(j_decompress_ptr cinfo, FILE *output_file)
{
        char bmpfileheader[14];
        char bmpinfoheader[40];
        long headersize, bfSize;
        int bits_per_pixel, cmap_entries;


        int step;

        /* Compute colormap size and total file size */
        if (cinfo->out_color_space == JCS_RGB) {
                if (cinfo->quantize_colors) {
                        /* Colormapped RGB */
                        bits_per_pixel = 8;
                        cmap_entries = 256;
                } else {
                        /* Unquantized, full color RGB */
                        bits_per_pixel = 24;
                        cmap_entries = 0;
                }
        } else {
                /* Grayscale output.  We need to fake a 256-entry colormap. */
                bits_per_pixel = 8;
                cmap_entries = 256;
        }

        step = cinfo->output_width * cinfo->output_components;

        while ((step & 3) != 0) step++;

        /* File size */
        headersize = 14 + 40 + cmap_entries * 4; /* Header and colormap */

        bfSize = headersize + (long) step * (long) cinfo->output_height;

        /* Set unused fields of header to 0 */
        memset(bmpfileheader, 0, sizeof(bmpfileheader));
        memset(bmpinfoheader, 0 ,sizeof(bmpinfoheader));

        /* Fill the file header */
        bmpfileheader[0] = 0x42;/* first 2 bytes are ASCII 'B', 'M' */
        bmpfileheader[1] = 0x4D;
        PUT_4B(bmpfileheader, 2, bfSize); /* bfSize */
        /* we leave bfReserved1 & bfReserved2 = 0 */
        PUT_4B(bmpfileheader, 10, headersize); /* bfOffBits */

        /* Fill the info header (Microsoft calls this a BITMAPINFOHEADER) */
        PUT_2B(bmpinfoheader, 0, 40);   /* biSize */
        PUT_4B(bmpinfoheader, 4, cinfo->output_width); /* biWidth */
        PUT_4B(bmpinfoheader, 8, cinfo->output_height); /* biHeight */
        PUT_2B(bmpinfoheader, 12, 1);   /* biPlanes - must be 1 */
        PUT_2B(bmpinfoheader, 14, bits_per_pixel); /* biBitCount */
        /* we leave biCompression = 0, for none */
        /* we leave biSizeImage = 0; this is correct for uncompressed data */
        if (cinfo->density_unit == 2) { /* if have density in dots/cm, then */
                PUT_4B(bmpinfoheader, 24, (INT32) (cinfo->X_density*100)); /* XPels/M */
                PUT_4B(bmpinfoheader, 28, (INT32) (cinfo->Y_density*100)); /* XPels/M */
        }
        PUT_2B(bmpinfoheader, 32, cmap_entries); /* biClrUsed */
        /* we leave biClrImportant = 0 */

        if (fwrite(bmpfileheader, 1, 14, output_file) != (size_t) 14) {
                printf("write bmpfileheader error\n");
        }
        if (fwrite(bmpinfoheader, 1, 40, output_file) != (size_t) 40) {
                printf("write bmpinfoheader error\n");
        }

        if (cmap_entries > 0) {
        }
}

void write_pixel_data(j_decompress_ptr cinfo, unsigned char *output_buffer, FILE *output_file)
{
        int rows, cols;
        int row_width;
        int step;
        unsigned char *tmp = NULL;

        unsigned char *pdata;

        row_width = cinfo->output_width * cinfo->output_components;
        step = row_width;
        while ((step & 3) != 0) step++;

        pdata = (unsigned char *)malloc(step);
        memset(pdata, 0, step);

        tmp = output_buffer + row_width * (cinfo->output_height - 1);
        for (rows = 0; rows < cinfo->output_height; rows++) {
                for (cols = 0; cols < row_width; cols += 3) {
                        pdata[cols + 2] = tmp[cols + 0];
                        pdata[cols + 1] = tmp[cols + 1];
                        pdata[cols + 0] = tmp[cols + 2];
                }
                tmp -= row_width;
                fwrite(pdata, 1, step, output_file);
        }

        free(pdata);
}

int read_jpeg_file(const char *input_filename, const char *output_filename)
{
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        FILE *input_file;
        FILE *output_file;
        JSAMPARRAY buffer;
        int row_width;

        unsigned char *output_buffer;
        unsigned char *tmp = NULL;

        cinfo.err = jpeg_std_error(&jerr);

        if ((input_file = fopen(input_filename, "rb")) == NULL) {
                fprintf(stderr, "can't open %s\n", input_filename);
                return -1;
        }

        if ((output_file = fopen(output_filename, "wb")) == NULL) {

                fprintf(stderr, "can't open %s\n", output_filename);
                return -1;
        }

        jpeg_create_decompress(&cinfo);

        /* Specify data source for decompression */
        jpeg_stdio_src(&cinfo, input_file);

        /* Read file header, set default decompression parameters */
        (void) jpeg_read_header(&cinfo, TRUE);

        /* Start decompressor */
        (void) jpeg_start_decompress(&cinfo);

        row_width = cinfo.output_width * cinfo.output_components;

        buffer = (*cinfo.mem->alloc_sarray)
                ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_width, 1);

        write_bmp_header(&cinfo, output_file);

        output_buffer = (unsigned char *)malloc(row_width * cinfo.output_height);
        memset(output_buffer, 0, row_width * cinfo.output_height);
        tmp = output_buffer;

        /* Process data */
        while (cinfo.output_scanline < cinfo.output_height) {
                (void) jpeg_read_scanlines(&cinfo, buffer, 1);

                memcpy(tmp, *buffer, row_width);
                tmp += row_width;
        }

        write_pixel_data(&cinfo, output_buffer, output_file);

        free(output_buffer);

        (void) jpeg_finish_decompress(&cinfo);

        jpeg_destroy_decompress(&cinfo);

        /* Close files, if we opened them */
        fclose(input_file);
        fclose(output_file);



        return 0;
}


int set_opt(int fd,int nSpeed, int nBits, char nEvent, int nStop)
{
    struct termios newtio,oldtio;
    if  ( tcgetattr( fd,&oldtio)  !=  0) 
    { 
        perror("SetupSerial 1");
        return -1;
    }
    bzero( &newtio, sizeof( newtio ) );
    newtio.c_cflag  |=  CLOCAL | CREAD; 
    newtio.c_cflag &= ~CSIZE; 

    switch( nBits )
    {
    case 7:
        newtio.c_cflag |= CS7;
        break;
    case 8:
        newtio.c_cflag |= CS8;
        break;
    }

    switch( nEvent )
    {
    case 'O':                     //奇校验
        newtio.c_cflag |= PARENB;
        newtio.c_cflag |= PARODD;
        newtio.c_iflag |= (INPCK | ISTRIP);
        break;
    case 'E':                     //偶校验
        newtio.c_iflag |= (INPCK | ISTRIP);
        newtio.c_cflag |= PARENB;
        newtio.c_cflag &= ~PARODD;
        break;
    case 'N':                    //无校验
        newtio.c_cflag &= ~PARENB;
        break;
    }

switch( nSpeed )
    {
    case 2400:
        cfsetispeed(&newtio, B2400);
        cfsetospeed(&newtio, B2400);
        break;
    case 4800:
        cfsetispeed(&newtio, B4800);
        cfsetospeed(&newtio, B4800);
        break;
    case 9600:
        cfsetispeed(&newtio, B9600);
        cfsetospeed(&newtio, B9600);
        break;
    case 38400:
        cfsetispeed(&newtio, B38400);
        cfsetospeed(&newtio, B38400);
        break;
    case 115200:
        cfsetispeed(&newtio, B115200);
        cfsetospeed(&newtio, B115200);
        break;
    default:
        cfsetispeed(&newtio, B9600);
        cfsetospeed(&newtio, B9600);
        break;
    }
    if( nStop == 1 )
    {
        newtio.c_cflag &=  ~CSTOPB;
    }
    else if ( nStop == 2 )
    {
        newtio.c_cflag |=  CSTOPB;
    }
    newtio.c_cc[VTIME]  = 0;
    newtio.c_cc[VMIN] = 0;
    tcflush(fd,TCIFLUSH);
    if((tcsetattr(fd,TCSANOW,&newtio))!=0)
    {
        perror("com set error");
        return -1;
    }
    printf("set done!\n");
    return 0;
}

int open_port(int fd,int comport)
{
    char *dev[]={"/dev/ttyS0","/dev/ttyS1","/dev/ttyS2"};
    long  vdisable;
/*    if (comport==1)
    {    fd = open( "/dev/ttyS0", O_RDWR|O_NOCTTY|O_NDELAY);
        if (-1 == fd)
        {
            perror("Can't Open Serial Port");
            return(-1);
        }
        else 
        {
            printf("open ttyS0 .....\n");
        }
    }
    else if(comport==2)
    {    fd = open( "/dev/ttyS1", O_RDWR|O_NOCTTY|O_NDELAY);
        if (-1 == fd)
        {
            perror("Can't Open Serial Port");
            return(-1);
        }
        else 
        {
            printf("open ttyS1 .....\n");
        }    
    }
    else if (comport==3)
    {
        fd = open( "/dev/ttyS2", O_RDWR|O_NOCTTY|O_NDELAY);
        if (-1 == fd)
        {
            perror("Can't Open Serial Port");
            return(-1);
        }
        else 
        {
            printf("open ttyS2 .....\n");
        }
    }
*/
	fd = open( "/dev/ttyUSB0", O_RDWR|O_NOCTTY);
    if (-1 == fd)
    {
        perror("Can't Open Serial Port");
        return(-1);
    }
/*    if(fcntl(fd, F_SETFL, 0)<0)
    {
        printf("fcntl failed!\n");
    }
    else
    {
        printf("fcntl=%d\n",fcntl(fd, F_SETFL,0));
    }*/
    if(isatty(STDIN_FILENO)==0)
    {
        printf("standard input is not a terminal device\n");
    }
    else
    {
        printf("isatty success!\n");
    }
    printf("fd-open=%d\n",fd);
    return fd;
}

int main(void)
{
    int fd;
	int fd1;
    int nread,n,i,length;
    unsigned char cmdreset[4]="\x56\x00\x26\x00";
	unsigned char cmdshoot[5]="\x56\x00\x36\x01\x00";
	unsigned char cmdgetlength[5]="\x56\x00\x34\x01\x00";
	unsigned char cmdgetpic[16]="\x56\x00\x32\x0c\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff";
	unsigned char recv[65536];

	signal(SIGIO, SIG_IGN);

    if((fd=open_port(fd,1))<0)
    {
        perror("open_port error");
        return 0;
    }
    if((i=set_opt(fd,38400,8,'N',1))<0)
    {
        perror("set_opt error");
		close(fd);
        return 0;
    }
    printf("fd=%d\n",fd);

getchar();

	//reset
	nread = write(fd,cmdreset,4);
	n = 0;
	while (n < 55) {
		nread = read(fd,&recv[n],65536);
		n += nread;
	}
	if (nread <= 0) {
	perror("read");}
	if (recv[0]==0x76
	&&  recv[1]==0x00
	&&  recv[2]==0x26
	&&  recv[3]==0x00
	) {
		printf("reset.\n");
	} else {
		printf("reset failed.\n");
		close(fd);
		return 0;
	}

getchar();
	//shoot
	nread = write(fd,cmdshoot,5);

	n = 0;
	while (n < 5) {
		nread = read(fd,&recv[n],65536);
		n += nread;
	}
	if (recv[0]==0x76
	&&  recv[1]==0x00
	&&  recv[2]==0x36
	&&  recv[3]==0x00
	&&  recv[4]==0x00
	) {
		printf("shoot.\n");
	} else {
		printf("shoot failed.\n");
		close(fd);
		return 0;
	}

getchar();


	//get length
	nread = write(fd,cmdgetlength,5);
	n = 0;
	while (n < 9) {
		nread = read(fd,&recv[n],65536);
		n += nread;
	}
	if (recv[0]==0x76
	&&  recv[1]==0x00
	&&  recv[2]==0x34
	&&  recv[3]==0x00
	&&  recv[4]==0x04
	&&  recv[5]==0x00
	&&  recv[6]==0x00
	) {
		printf("get length.\n");
		length = (recv[7] << 8) + recv[8];
		cmdgetpic[12] = recv[7];
		cmdgetpic[13] = recv[8];
	} else {
		printf("get length failed.\n");
		close(fd);
		return 0;
	}

getchar();

	n = 0;
	while (n < length+10) {
		nread = read(fd,&recv[n],65536);
		n += nread;
	}
	//get photo
	nread = write(fd,cmdgetpic,16);
	nread = read(fd,recv,65536);
	if (recv[0]==0x76
	&&  recv[1]==0x00
	&&  recv[2]==0x32
	&&  recv[3]==0x00
	&&  recv[4]==0x00
	) {
		printf("get photo.\n");
		for (i = 5; i < nread-5; i ++) {
			printf("%02X ", recv[i]);
		}
		printf("\n");
	} else {
		printf("get photo failed.\n");
		close(fd);
		return 0;
	}

getchar();
	//save jpeg
	fd1 = open("tmp.jpg",O_WRONLY);
	if (fd1 < 0) {
		perror("can\'t open tmp.jpg");
		close(fd);
		return 0;
	}

	write(fd1, &recv[5], length);
	close(fd1);

getchar();
	//jpeg to bmp
	read_jpeg_file("tmp.jpg", "tmp.bmp");

	printf("done.\n");
    close(fd);
    return;
}





