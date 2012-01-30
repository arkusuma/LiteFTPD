/* Bug Fixes:
 *  - create_jpeg(): when working in 8 bit image: using memory dereference
 *    of char (signed) as an index to array
 */

#ifdef USE_SCREEN_JPG

#ifndef _JPEG_C_
#define _JPEG_C_

/* We use libjpeg 6.b from Independent JPEG Group */
#include "libjpeg/jpeglib.h"

#define JPEG_OUTPUT_BUFFER_SIZE  0x10000

char *jpeg_output;
size_t jpeg_output_size;

void init_destination(j_compress_ptr cinfo)
{
    jpeg_output = (char *) malloc(JPEG_OUTPUT_BUFFER_SIZE);
    jpeg_output_size = 0;
    if (jpeg_output == NULL)
    {
        cinfo->dest->next_output_byte = NULL;
        cinfo->dest->free_in_buffer = 0;
    }
    else
    {
        cinfo->dest->next_output_byte = jpeg_output;
        cinfo->dest->free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;
    }
}

boolean empty_output_buffer(j_compress_ptr cinfo)
{
    char *tmp;
    jpeg_output_size += JPEG_OUTPUT_BUFFER_SIZE;
    tmp = realloc(jpeg_output, jpeg_output_size + JPEG_OUTPUT_BUFFER_SIZE);
    if (tmp == NULL)
    {
        free(jpeg_output);
        jpeg_output = NULL;
        cinfo->dest->next_output_byte = NULL;
        cinfo->dest->free_in_buffer = 0;
    }
    else
    {
        jpeg_output = tmp;
        cinfo->dest->next_output_byte =  jpeg_output + jpeg_output_size;
        cinfo->dest->free_in_buffer = JPEG_OUTPUT_BUFFER_SIZE;
    }
    return TRUE;
}

void term_destination(j_compress_ptr cinfo)
{
    jpeg_output_size += JPEG_OUTPUT_BUFFER_SIZE - cinfo->dest->free_in_buffer;
}

char *create_jpeg(int *size)
{
    unsigned char *mem, *bits, *src, *dst, tmp[MAX_PATH];
    PBITMAPFILEHEADER pbmfh;
    PBITMAPINFOHEADER pbmih;
    RGBQUAD *quads;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_destination_mgr dest;
    JSAMPROW row[1];
    int y, x, bpp;
    HDC hdc;

    hdc = GetDC(0);
    bpp = GetDeviceCaps(hdc, BITSPIXEL);
    ReleaseDC(0, hdc);

    if (bpp < 8)
        return NULL;

    mem = create_snapshot(size);
    if (mem == NULL)
        return NULL;

    pbmfh = (PBITMAPFILEHEADER) mem;
    pbmih = (PBITMAPINFOHEADER) (mem+sizeof(BITMAPFILEHEADER));
    quads = (RGBQUAD *) (mem+sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER));
    bits = mem+pbmfh->bfOffBits;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    dest.init_destination = init_destination;
    dest.empty_output_buffer = empty_output_buffer;
    dest.term_destination = term_destination;
    cinfo.dest = &dest;

    cinfo.image_width = pbmih->biWidth;
    cinfo.image_height = pbmih->biHeight;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);

    jpeg_start_compress(&cinfo, TRUE);

    bpp = pbmih->biBitCount / 8;
    row[0] = malloc(3 * cinfo.image_width);
    for (y = 0; y < cinfo.image_height; y++)
    {
        dst = row[0];
        /* the image is bottom-up */
        src = bits + (cinfo.image_height - y - 1) * (cinfo.image_width * bpp);
        for (x = 0; x < cinfo.image_width; x++)
        {
            if (bpp == 1)
            {
                dst[0] = quads[*src].rgbRed;
                dst[1] = quads[*src].rgbGreen;
                dst[2] = quads[*src].rgbBlue;
            }
            else if (bpp == 2)
            {
                short pix;
                pix = *((short *) src);
                dst[0] = ((pix >> 10) & 31) * 255 / 31;
                dst[1] = ((pix >> 5) & 31) * 255 / 31;
                dst[2] = (pix & 31) * 255 / 31;
            }
            if (bpp >= 3)
            {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
            }
            src += bpp;
            dst += 3;
        }
        jpeg_write_scanlines(&cinfo, row, 1);
    }
    free(row[0]);
    free(mem);

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    *size = jpeg_output_size;
    return jpeg_output;
}

#endif
#endif
