#ifndef SERVAL_HELPER_H
#define SERVAL_HELPER_H

/*
 * Taken from one of the serval files 
 * because the macros require it but
 * don't export it :-/
 */
const char hexdigit[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

char *tohex(char *dstHex, const unsigned char *srcBinary, size_t bytes)
{
  char *p;
  for (p = dstHex; bytes--; ++srcBinary) {
    *p++ = hexdigit[*srcBinary >> 4];
    *p++ = hexdigit[*srcBinary & 0xf];
  }
  *p = '\0';
  return dstHex;
}

#endif
