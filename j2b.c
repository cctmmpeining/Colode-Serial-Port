#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <jpeglib.h>

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

int main(int argc, char *argv[])
{
        if (argc < 3) {
                read_jpeg_file("tt.jpg", "tt.bmp");
        } else {
                read_jpeg_file(argv[1], argv[2]);
        }
        return 0;
}
