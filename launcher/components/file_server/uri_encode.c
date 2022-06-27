#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "uri_encode.h"

/*
 uri_encode.c - functions for URI percent encoding / decoding
*/

size_t uri_encode (const char *src, const size_t len, char *dst)
{
  size_t i = 0, j = 0;
  while (i < len)
  {
    const char octet = src[i++];
    const int32_t code = ((int32_t*)uri_encode_tbl)[ (unsigned char)octet ];
    if (code) {
        *((int32_t*)&dst[j]) = code;
        j += 3;
    }
    else dst[j++] = octet;
  }
  dst[j] = '\0';
  return j;
}

size_t uri_decode (const char *src, const size_t len, char *dst)
{
  size_t i = 0, j = 0;
  while(i < len)
  {
    int copy_char = 1;
    if(src[i] == '%' && i + 2 < len)
    {
      const unsigned char v1 = hexval[ (unsigned char)src[i+1] ];
      const unsigned char v2 = hexval[ (unsigned char)src[i+2] ];

      /* skip invalid hex sequences */
      if ((v1 | v2) != 0xFF)
      {
        dst[j] = (v1 << 4) | v2;
        j++;
        i += 3;
        copy_char = 0;
      }
    }
    if (copy_char)
    {
      dst[j] = src[i];
      i++;
      j++;
    }
  }
  dst[j] = '\0';
  return j;
}