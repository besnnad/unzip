#ifndef GETZIPFILE_H
#define GETZIPFILE_H 
#include "inftrees.h"
/* tm_unz contain date/time info */
typedef struct tm_unz_s
{
    int tm_sec;             /* seconds after the minute - [0,59] */
    int tm_min;             /* minutes after the hour - [0,59] */
    int tm_hour;            /* hours since midnight - [0,23] */
    int tm_mday;            /* day of the month - [1,31] */
    int tm_mon;             /* months since January - [0,11] */
    int tm_year;            /* years - [1980..2044] */
} tm_unz;

/* unz_file_info contain information about a file in the zipfile */
typedef struct unz_file_info64_s
{   
    unsigned long version;              /* version made by                 2 bytes */
    unsigned long version_needed;       /* version needed to extract       2 bytes */
    unsigned long flag;                 /* general purpose bit flag        2 bytes */
    unsigned long compression_method;   /* compression method              2 bytes */
    unsigned long dosDate;              /* last mod file date in Dos fmt   4 bytes */
    unsigned long crc;                  /* crc-32                          4 bytes */
    unsigned long long compressed_size;   /* compressed size                 8 bytes */
    unsigned long long uncompressed_size; /* uncompressed size               8 bytes */
    unsigned long size_filename;        /* filename length                 2 bytes */
    unsigned long size_file_extra;      /* extra field length              2 bytes */
    unsigned long size_file_comment;    /* file comment length             2 bytes */

    unsigned long disk_num_start;       /* disk number start               2 bytes */
    unsigned long internal_fa;          /* internal file attributes        2 bytes */
    unsigned long external_fa;          /* external file attributes        4 bytes */
    char* filename;

    tm_unz tmu_date;
} unz_file_info;

typedef struct zip_file_info {
    unsigned long hold; // 存储从 data 中拿到的数据
    unsigned bits;  // hold 中含有多少个比特

    int last;   // 当前是否是最后一块
    int zip_mode;   // 使用码表的模式，动态哈夫曼 和 静态哈夫曼

    unsigned ncode;             // code码表长度
    unsigned nlen;              // 长度码表长度
    unsigned ndist;             // 距离码表长度

    const code* len_code;   // 长度码表
    unsigned len_bits;  // 每次检验的长度的比特
    const code* dist_code;  // 距离码表
    unsigned dist_bits; // 每次检验的距离的比特

    unsigned short lens[320];   // 保存数据的临时场所
    unsigned short work[288];   // 用于构建码树的数组
    unsigned have;          // 用于计数
    int back;
    unsigned length;
    unsigned extra;             /* extra bits needed */
    unsigned was;               /* initial length of match */
    unsigned offset;            /* distance back to copy string from */
    code *next;             /* next available space in codes[] */
    code codes[ENOUGH];         /* space for code tables */
    unsigned char window[50000];    // 用于存储之前的字节
    int wsize;          // window 中存储的字节数
} zip_file;

typedef enum {
    FIND_HEAD_INFO = 16180,  // 未解析头部信息
    HEAD_INFO,
    HEAD_LENGTH,        // 文件名和扩展长度不够
    FILE_START, // 头部信息解析完成，进入文件解析
    PARSING_DYNAMIC_LENGTH,    // 正在解析动态码表长度
    PARSING_DYNAMIC_NCODE,      // 正在解析动态码表
    PARSING_DYNAMIC_LENDIST,    // 解析动态的长度距离表

    PARSING_CODE_FILE,  // 正在解码文件
    PARSING_BARE_FILE_LENGTH,    // 正在解析未编码文件的长度    
    PARSING_BARE_FILE,      // 正在存储未编码文件
    FILE_NOT_END    // 文件还未到最后一块
} file_state;

typedef struct zip_memory_store
{
    unz_file_info file_info;
    zip_file file;
    file_state state;
    
    unsigned char* data;
    int size;
    int index;
} zip_memory;

#endif