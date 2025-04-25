
#if MXML_MAJOR_VERSION < 4
#define MXML_DESCEND_ALL MXML_DESCEND
#define XMLLOAD(x) mxmlLoadString (NULL, x, NULL);
#else
#define XMLLOAD(x) mxmlLoadString (NULL, NULL, x);
#endif

