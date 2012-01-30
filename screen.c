/* Screen Capturing module
 * Some parts are taken from MSDN July 2002 with some modification.
 *
 * Last Update : 30 July 2003
 */

/* Bug Fixes:
 *   - Restore original object after SelectObject() and before DeleteObject()
 */

#if defined(USE_SCREEN_BMP) || defined(USE_SCREEN_JPG)

#ifndef _SCREEN_C_
#define _SCREEN_C_

/* Caller must free returned pointer */
char *CreateBMPMem(HDC hdc, int *size)
{
    HBITMAP hbmp;
    BITMAPINFO bmi;
    BITMAPFILEHEADER hdr, *pbmfh;
    PBITMAPINFOHEADER pbmih;
    PBITMAPINFO pbmi;
    unsigned char *bits, *ptr;
    int i, j, bypp;

    hbmp = GetCurrentObject(hdc, OBJ_BITMAP);
    if (hbmp == 0)
        return NULL;

    /* Fill BITMAPINFO structure. */
    pbmih = (PBITMAPINFOHEADER) &bmi;
    memset(pbmih, 0, sizeof(BITMAPINFO));
    pbmih->biSize = sizeof(BITMAPINFOHEADER);
    if (!GetDIBits(hdc, hbmp, 0, 0, NULL, &bmi, 0))
        return NULL;
    pbmih->biCompression = BI_RGB;

    hdr.bfType = 0x4D42;
    /* Compute the size of the entire file. */
    hdr.bfSize = (DWORD) (sizeof(BITMAPFILEHEADER)
            + pbmih->biClrUsed * sizeof(RGBQUAD)
            + pbmih->biSize + pbmih->biSizeImage);
    hdr.bfReserved1 = 0;
    hdr.bfReserved2 = 0;
    /* Compute the offset to the array of color indices. */
    hdr.bfOffBits = (DWORD) sizeof(BITMAPFILEHEADER) +
        pbmih->biSize + pbmih->biClrUsed * sizeof (RGBQUAD);

    bits = ptr = (PBYTE) malloc(hdr.bfSize);
    if (bits == NULL)
        return NULL;

    pbmfh = (BITMAPFILEHEADER *) ptr;
    memcpy(ptr, &hdr, sizeof(BITMAPFILEHEADER));
    ptr += sizeof(BITMAPFILEHEADER);

    pbmi = (BITMAPINFO *) ptr;
    memcpy(ptr, pbmih, sizeof(BITMAPINFOHEADER));
    ptr += sizeof(BITMAPINFOHEADER);
    ptr += pbmih->biClrUsed * sizeof(RGBQUAD);

    /* Copy the array of color indices into the .BMP file.
     *
     * Retrieve the color table (RGBQUAD array) and the bits
     * (array of palette indices) from the DIB.
     */
    if (!GetDIBits(hdc, hbmp, 0, pbmih->biHeight, ptr, pbmi, DIB_RGB_COLORS))
    {
        free(bits);
        return NULL;
    }

    *size = hdr.bfSize;

    /* Convert 32-bit and 24-bit bitmap to 16-bit */
    if (pbmi->bmiHeader.biBitCount > 16)
    {
    	bypp = pbmi->bmiHeader.biBitCount / 8;
        for (i = 0, j = 0; i < pbmih->biSizeImage; i += bypp, j += 2)
        {
            unsigned short px;
            px = ((ptr[i+0] >> 3) << 0) |
                 ((ptr[i+1] >> 3) << 5) |
                 ((ptr[i+2] >> 3) << 10);
            *((short *) &ptr[j]) = px;
        }

        pbmi->bmiHeader.biBitCount = 16;

        i = pbmih->biSizeImage / bypp;
        pbmfh->bfSize -= i;
        pbmi->bmiHeader.biSizeImage -= i;
        *size -= i;
        realloc(bits, *size);
    }

    return bits;
}

char *create_snapshot(int *size)
{
    HDC hdcScreen, hdcCompatible;
    HBITMAP hbmScreen, old_hbm;
    int width, height;
    char *ret;

    hdcScreen = GetDC(0);
    hdcCompatible = CreateCompatibleDC(hdcScreen);

    width = GetDeviceCaps(hdcScreen, HORZRES);
    height = GetDeviceCaps(hdcScreen, VERTRES);
    hbmScreen = CreateCompatibleBitmap(hdcScreen, width, height);
    if (hbmScreen == 0)
    {
        DeleteDC(hdcCompatible);
        ReleaseDC(0, hdcScreen);
        return NULL;
    }

    old_hbm = SelectObject(hdcCompatible, hbmScreen);
    if (old_hbm == 0)
    {
        DeleteObject(hbmScreen);
        DeleteDC(hdcCompatible);
        ReleaseDC(0, hdcScreen);
        return NULL;
    }

    if (!BitBlt(hdcCompatible, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY))
    {
        SelectObject(hdcCompatible, old_hbm);
        DeleteObject(hbmScreen);
        DeleteDC(hdcCompatible);
        ReleaseDC(0, hdcScreen);
        return NULL;
    }

    ReleaseDC(0, hdcScreen);

    ret = CreateBMPMem(hdcCompatible, size);

    SelectObject(hdcCompatible, old_hbm);
    DeleteObject(hbmScreen);
    DeleteDC(hdcCompatible);

    return ret;
}

#endif
#endif
