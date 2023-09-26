#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "getZipFile.h"
#include "zipTree.h"
#include "inftrees.h"
#include <sys/types.h>
#include <utime.h>
#include <time.h>
#include <stdlib.h>

static const unsigned short order[19] = /* permutation of code lengths */
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

inline void pullByte(zip_memory* zm, int* index) {
    zm->file.hold += (unsigned long)(zm->data[*index]) << zm->file.bits;
    zm->file.bits += 8;
    *index += 1;
}

// 表示需要多少比特，不足就从 data 中补充比特
inline void needBits(int n, zip_memory* zm, int* index) {
    while (zm->file.bits < n) {
        zm->file.hold += (unsigned long)(zm->data[*index]) << zm->file.bits;
        zm->file.bits += 8;
        *index += 1;
    }
}

// 返回 hold 的低 n 个字节
inline int bits(int n, zip_memory* zm) {
    return ((unsigned)zm->file.hold & ((1 << n)-1));
}

// 丢弃 hold 的低 n 个字节
inline void dropBits(int n, zip_memory* zm) {
    zm->file.hold >>= n;
    zm->file.bits -= n;
}

inline int getByte(unsigned char* data, int* index) {
    *index += 1;
    return data[*index-1];
}

inline int getShort(unsigned char* data, int* index) {
    *index += 2;
    return data[*index-2] + (data[*index-1] << 8);
}

inline unsigned int getLong(unsigned char* data, int* index) {
    *index += 4;
    return data[*index-4] + (data[*index-3] << 8) + (data[*index-2] << 16) + (data[*index-1] << 24);
}

inline int isStartLine(unsigned char* data, int start) {
    return (data[start] == '\x50' && data[start+1] == '\x4b' && data[start+2] == '\x03' && data[start+3] == '\x04');
}

// 用于清理结构体数据
void freeZipMemory(zip_memory* zm) {
    if (zm->state == FIND_HEAD_INFO) {
        if (zm->file_info.filename != NULL) {
            free(zm->file_info.filename);
            zm->file_info.filename = NULL;
        }
        if (zm->size != 0) free(zm->data);
        free(zm);
        zm = NULL;
    }else {
        char* left_data = (char*)malloc(zm->size - zm->index);
        memcpy(left_data, &zm->data[zm->index], zm->size-zm->index);
        zm->size = zm->size - zm->index;
        free(zm->data);
        zm->data = left_data;
        zm->index = 0;
    }

    return;
}

// 从 start 开始找标志 50 4b 03 04
int getFileStart(unsigned char* data, int size, int* start) {
    while (*start < size-3) {
        if (isStartLine(data, *start)) {
            *start += 4;
            return 1;
        }
        *start += 1;
    }
    return 0;
}

// 将旧字节和新字节合并到一起
void getData(unsigned char* data, int size, zip_memory* zm) {
    char* new_data = (char*)malloc(size + zm->size);
    if (zm->size != 0)
        memcpy(new_data, zm->data, zm->size);
    memcpy(new_data+zm->size, data, size);
    if (zm->size != 0) free(zm->data);

    zm->data = new_data;
    zm->size = zm->size + size;
    return;
}

void updateWindow(unsigned char* put, int out, zip_memory* zm) {
    if (zm->file.wsize + out > 40000) {
        int start = zm->file.wsize + out - 40000;
        char new_data[40000];
        memcpy(new_data, &zm->file.window[start], zm->file.wsize-start);
        zm->file.wsize -= start;
        memcpy(zm->file.window, new_data, zm->file.wsize);
    }

    memcpy(&zm->file.window[zm->file.wsize], put, out);
    zm->file.wsize += out;
}

inline void outBuffer(unsigned char* put, int* out, const char* filename, zip_memory* zm) {
    // printf("输出文件：%s, 字节：%d\n", filename, *out);
    FILE* of = fopen(filename, "ab");
    fwrite(put,(unsigned)*out,1,of);
    fclose(of);
    updateWindow(put, *out, zm);
    *out = 0;
}

static void change_file_date(filename,dosdate,tmu_date)
    const char *filename;
    unsigned long dosdate;
    tm_unz tmu_date;
{
  (void)dosdate;
  struct utimbuf ut;
  struct tm newdate;
  newdate.tm_sec = tmu_date.tm_sec;
  newdate.tm_min=tmu_date.tm_min;
  newdate.tm_hour=tmu_date.tm_hour;
  newdate.tm_mday=tmu_date.tm_mday;
  newdate.tm_mon=tmu_date.tm_mon-1;
  if (tmu_date.tm_year > 1900)
      newdate.tm_year=tmu_date.tm_year - 1900;
  else
      newdate.tm_year=tmu_date.tm_year ;
  newdate.tm_isdst=-1;

  ut.actime=ut.modtime=mktime(&newdate);
  utime(filename,&ut);
}

// 解压文件部分
int parseFile(unsigned char* data, int size, int* index, zip_memory* zm) {
    while (1) {

    if (zm->state == FILE_START) {
        if (size-*index < 1) {
            freeZipMemory(zm);
            return 0;
        }
        
        // 获取是否是最后一块的信息
        needBits(3, zm, index);
        zm->file.last = bits(1, zm);
        dropBits(1, zm);

        // 获取码表的模式
        switch (bits(2, zm)) {
        case 0:
            zm->state = PARSING_BARE_FILE_LENGTH;
            break;
        case 1:
            zm->file.len_code = lenfix;
            zm->file.len_bits = 9;
            zm->file.dist_code = distfix;
            zm->file.dist_bits = 5;
            zm->state = PARSING_CODE_FILE;
            break;
        case 2:
            zm->state = PARSING_DYNAMIC_LENGTH;
            break;
        case 3:
            zm->state = FIND_HEAD_INFO;
            return -1;
        }
        dropBits(2, zm);
    }

    if (zm->state == PARSING_BARE_FILE_LENGTH) {
        if (size - *index < 5) {
            freeZipMemory(zm);
            return 0;
        }
        zm->file.hold >>= zm->file.bits & 7;
        zm->file.bits -= zm->file.bits & 7;
        needBits(32, zm, index);
        if ((zm->file.hold & 0xffff) != ((zm->file.hold >> 16) ^ 0xffff)) {
            zm->state = FIND_HEAD_INFO;
            return -1;
        }
        zm->file.length = (unsigned)zm->file.hold & 0xffff;

        zm->file.hold = 0;  zm->file.bits = 0;
        zm->state = PARSING_BARE_FILE;
    }

    if (zm->state == PARSING_BARE_FILE) {
        int copy = zm->file.length < (size - *index) ? zm->file.length : (size - *index);
        int copy_in = copy;
        outBuffer(&data[*index], &copy_in, zm->file_info.filename, zm);
        *index += copy;
        zm->file.length -= copy;

        freeZipMemory(zm);
        size = zm->size;
        index = &zm->index;
        data = zm->data;

        // printf("PARSING BARE FILE after : index : %d", *index);

        if (zm->file.length == 0) {
            if (zm->file.last) {
                zm->state = FIND_HEAD_INFO;
                return 1;
            } else {
                zm->state = FILE_START;
                zm->file.have = 0;
                continue;
            }
        } else {
            return 0;
        }
    }

    // 解析三个码表的长度
    if (zm->state == PARSING_DYNAMIC_LENGTH) {
        if (size - *index < 2) {
            freeZipMemory(zm);
            return 0;
        }
        needBits(14, zm, index);
        zm->file.nlen = bits(5, zm) + 257;    dropBits(5, zm);
        zm->file.ndist = bits(5, zm) + 1;    dropBits(5, zm);
        zm->file.ncode = bits(4, zm) + 4;    dropBits(4, zm);
        // 长度不符合规定
        if (zm->file.nlen > 286 || zm->file.ndist > 30) {
            zm->state = FIND_HEAD_INFO;
            return -1;
        }
        zm->file.have = 0;  // 初始化计数器
        zm->state = PARSING_DYNAMIC_NCODE;
    }

    // 解析ncode码表
    if (zm->state == PARSING_DYNAMIC_NCODE) {
        if (zm->file.bits + 8*(size-*index) < 3*zm->file.ncode) {
            freeZipMemory(zm);
            return 0;
        }
        while (zm->file.have < zm->file.ncode) {
            needBits(3, zm, index);
            zm->file.lens[order[zm->file.have++]] = (unsigned short)bits(3, zm);
            dropBits(3, zm);
        }

        while (zm->file.have < 19)
            zm->file.lens[order[zm->file.have++]] = 0;
        zm->file.next = zm->file.codes;
        zm->file.len_code = (const code*)(zm->file.next);
        zm->file.len_bits = 7;
        // 构建 code 表
        if(inflate_table(CODES, zm->file.lens, 19, &(zm->file.next), &(zm->file.len_bits), zm->file.work)){
            zm->state = FIND_HEAD_INFO;
            return -1;
        }
        zm->file.have = 0;
        zm->state = PARSING_DYNAMIC_LENDIST;
        // printf("%d\n", zm->file.len_code[0].val);
    }

    // 开始解析动态表的长度和距离表
    if (zm->state == PARSING_DYNAMIC_LENDIST) {
        code here;
        int copy = 0, len = 0;
        while (zm->file.have < zm->file.nlen + zm->file.ndist) {
            // 检测是否够长度
            if (size-*index < 5) {
                freeZipMemory(zm);
                return 0;
            }
            
            // 得到下一个码
            while (1) {
                here = zm->file.len_code[bits(zm->file.len_bits, zm)];
                if ((unsigned)(here.bits) <= zm->file.bits) break;
                pullByte(zm, index);
            }

            if (here.val < 16) {
                dropBits(here.bits, zm);
                zm->file.lens[zm->file.have++] = here.val;
            } else {
                if (here.val == 16) {
                    needBits(here.bits + 2, zm, index);
                    dropBits(here.bits, zm);
                    if (zm->file.have == 0) {
                        zm->state = FIND_HEAD_INFO;
                        return -1;
                    }
                    len = zm->file.lens[zm->file.have - 1];
                    copy = 3 + bits(2, zm);
                    dropBits(2, zm);
                } else if (here.val == 17) {
                    needBits(here.bits + 3, zm, index);
                    dropBits(here.bits, zm);
                    len = 0;
                    copy = 3 + bits(3, zm);
                    dropBits(3, zm); 
                } else {
                    needBits(here.bits + 7, zm, index);
                    dropBits(here.bits, zm);
                    len = 0;
                    copy = 11 + bits(7, zm);
                    dropBits(7, zm);
                }

                if (zm->file.have + copy > zm->file.nlen + zm->file.ndist) {
                    zm->state = FIND_HEAD_INFO;
                    return -1;
                }
                while (copy--)
                    zm->file.lens[zm->file.have++] = (unsigned short)len;
            }
        }

        /* check for end-of-block code (better have one) */
        if (zm->file.lens[256] == 0) {
            zm->state = FIND_HEAD_INFO;
            return -1;
        }

        // 开始构建长度表
        zm->file.next = zm->file.codes;
        zm->file.len_code = (const code*)(zm->file.next);
        zm->file.len_bits = 9;
        if (inflate_table(LENS, zm->file.lens, zm->file.nlen, &(zm->file.next),
                            &(zm->file.len_bits), zm->file.work)) {
            zm->state = FIND_HEAD_INFO;
            return -1;
        }

        // 开始构建距离表
        zm->file.dist_code = (const code*)(zm->file.next);
        zm->file.dist_bits = 6;
        if (inflate_table(DISTS, zm->file.lens + zm->file.nlen, zm->file.ndist,
                        &(zm->file.next), &(zm->file.dist_bits), zm->file.work)) {
            zm->state = FIND_HEAD_INFO;
            return -1;
        }

        // 码表建立完成，准备解析文件
        zm->state = PARSING_CODE_FILE;
        // printf("%d and %d\n", zm->file.hold, zm->file.bits);
    }

    // 解析编码的文件内容
    if (zm->state == PARSING_CODE_FILE) {
        unsigned char* put = (unsigned char*)malloc(5000);
        int out = 0;
        code here, last;
        int copy = 0, from = 0;

        while (1) {
            if (out >= 4000 || size - *index < 20) {
                outBuffer(put, &out, zm->file_info.filename, zm);
            }
            if (size - *index < 20) {
                free(put);
                freeZipMemory(zm);
                return 0;
            }

            zm->file.back = 0;
            while (1) {
                here = zm->file.len_code[bits(zm->file.len_bits, zm)];
                if ((unsigned)(here.bits) <= zm->file.bits) break;
                pullByte(zm, index);
            }
            if (here.op && (here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = zm->file.len_code[last.val +
                            (bits(last.bits + last.op, zm) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= zm->file.bits) break;
                    pullByte(zm, index);
                }
                dropBits(last.bits, zm);
                zm->file.back += last.bits;
            }
            dropBits(here.bits, zm);
            zm->file.back += here.bits;
            zm->file.length = (unsigned)here.val;
            if ((int)(here.op) == 0) {  // 是字符
                put[out++] = (unsigned char)zm->file.length;
                continue;
            } 
            if (here.op & 32) { // 是块的结束标志
                zm->file.back = -1;
                outBuffer(put, &out, zm->file_info.filename, zm);
                // printf("%d, %d, %d, %d, %d, %d\n", zm->file_info.tmu_date.tm_year, zm->file_info.tmu_date.tm_mon,
                //     zm->file_info.tmu_date.tm_mday, zm->file_info.tmu_date.tm_hour, zm->file_info.tmu_date.tm_min,
                //     zm->file_info.tmu_date.tm_sec);
                change_file_date(zm->file_info.filename, zm->file_info.dosDate, zm->file_info.tmu_date);
                // printf("输出文件%s\n", zm->file_info.filename);
                free(put);
                freeZipMemory(zm);
                size = zm->size;
                index = &zm->index;
                data = zm->data;

                if (zm->file.last) {
                    zm->state = FIND_HEAD_INFO;
                    return 1;
                } else {
                    zm->state = FILE_START;
                    zm->file.have = 0;
                    break;
                }
            }
            if (here.op & 64) { // 非法字符
                outBuffer(put, &out, zm->file_info.filename, zm);
                free(put);
                zm->state = FIND_HEAD_INFO;

                return -1;
            }
            // 确定是长度
            zm->file.extra = (unsigned)(here.op) & 15;

            if (zm->file.extra) {
                needBits(zm->file.extra, zm, index);
                zm->file.length += bits(zm->file.extra, zm);
                dropBits(zm->file.extra, zm);
                zm->file.back += zm->file.extra;
            }

            zm->file.was = zm->file.length;

            // 开始寻找距离 dist
            for (;;) {
                here = zm->file.dist_code[bits(zm->file.dist_bits, zm)];
                if ((unsigned)(here.bits) <= zm->file.bits) break;
                pullByte(zm, index);
            }
            if ((here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = zm->file.dist_code[last.val +
                            (bits(last.bits + last.op, zm) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= zm->file.bits) break;
                    pullByte(zm, index);
                }
                dropBits(last.bits, zm);
                zm->file.back += last.bits;
            }
            dropBits(here.bits, zm);
            zm->file.back += here.bits;
            if (here.op & 64) { // 错误
                outBuffer(put, &out, zm->file_info.filename, zm);
                free(put);
                zm->state = FIND_HEAD_INFO;

                return -1;
            }
            zm->file.offset = (unsigned)here.val;
            zm->file.extra = (unsigned)(here.op) & 15;

            if (zm->file.extra) {
                needBits(zm->file.extra, zm, index);
                zm->file.offset += bits(zm->file.extra, zm);
                dropBits(zm->file.extra, zm);
                zm->file.back += zm->file.extra;
            }

            from = 0;
            // dist 是 zm->file.offset, len 是 zm->file.length
            // 如果偏移到了window的界面
            if (zm->file.offset > out) {
                if (zm->file.offset > out + zm->file.wsize) {
                    outBuffer(put, &out, zm->file_info.filename, zm);
                    free(put);
                    zm->state = FIND_HEAD_INFO;

                    return -1;
                }
                from = zm->file.wsize - (zm->file.offset - out);

                while (from != zm->file.wsize && zm->file.length > 0) {
                    put[out++] = zm->file.window[from++];
                    --zm->file.length;
                }
            }
            from = out - zm->file.offset; 
            while (zm->file.length > 0) {
                put[out++] = put[from++];
                --zm->file.length;
            }
        }
    }

    }
    // while(1)的循环结束

    return 1;
}

/*
   Translate date/time from Dos format to tm_unz (readable more easilty)
*/
void DosDateToTmuDate (unsigned char* data, int* index, zip_memory* zm)
{   
    zm->file_info.tmu_date.tm_mday = data[*index+2] & 0x1f;
    zm->file_info.tmu_date.tm_mon = (data[*index+3] & 0x1)*8 + ((data[*index+2] & 0xe0) >> 5);
    zm->file_info.tmu_date.tm_year = 1980 + ((data[*index+3] & 0xfe) >> 1);

    zm->file_info.tmu_date.tm_sec = 2 * (data[*index] & 0x1f);
    zm->file_info.tmu_date.tm_min = (data[*index+1] & 0x7)*8 + ((data[*index] & 0xe0) >> 5);
    zm->file_info.tmu_date.tm_hour = ((data[*index+1] & 0xf8) >> 3);

    *index += 4;
    
}

// 解析文件头部信息
int parseFileInfo(unsigned char* data, int size, int* index, zip_memory* zm) {
    // 读取文件头部信息
    if (zm->state == HEAD_INFO) {
        if (size-*index < 30) {
            freeZipMemory(zm);   
            return 0;
        }
        zm->file_info.version_needed = getShort(data, index);
        zm->file_info.flag = getShort(data, index);
        zm->file_info.compression_method = getShort(data, index);
        zm->file_info.dosDate = getLong(data, index);   *index -= 4;
        DosDateToTmuDate(data, index, zm);
        // printf("%d, %d, %d, %d, %d, %d\n", zm->file_info.tmu_date.tm_year, zm->file_info.tmu_date.tm_mon, 
        //     zm->file_info.tmu_date.tm_mday, zm->file_info.tmu_date.tm_hour, zm->file_info.tmu_date.tm_min, 
        //     zm->file_info.tmu_date.tm_sec);
        zm->file_info.crc = getLong(data, index);
        zm->file_info.compressed_size = getLong(data, index);
        zm->file_info.uncompressed_size = getLong(data, index);
        zm->file_info.size_filename = getShort(data, index);
        zm->file_info.size_file_extra = getShort(data, index);

        zm->state = HEAD_LENGTH;
    }

    // 读取文件名
    if (zm->state == HEAD_LENGTH) {
        if (size-*index < zm->file_info.size_filename + zm->file_info.size_file_extra) {
            freeZipMemory(zm);  
            return 0;
        }
        if (zm->file_info.filename != NULL) {
            free(zm->file_info.filename);
            zm->file_info.filename = NULL;
        }

        int last = *index + zm->file_info.size_filename - 1;

        if (data[last] == '/') {
            zm->state = FIND_HEAD_INFO;
            return -1;
        }

        int back = last;
        while (back > *index) {
            if (data[back] == '/') {
                ++back;
                break;
            }
            --back;
        }
        zm->file_info.filename = (char*)malloc(last-back+2);
        memcpy(zm->file_info.filename, &data[back], last-back+1);
        zm->file_info.filename[last-back+1] = '\0';
        *index += zm->file_info.size_filename + zm->file_info.size_file_extra;
        printf("filename: %s\n", zm->file_info.filename);
        
        zm->state = FILE_START;

        memset(&zm->file, 0, sizeof(zip_file));        
    }

    return 1;
}

// 调用的接口，总程序
int getZipInterface(unsigned char* data, int size, void** memory) {
    int file_flag = 0;
    
    if (*memory == NULL) {
        *memory = (zip_memory*)malloc(sizeof(zip_memory));
        memset(*memory, 0, sizeof(zip_memory));
        ((zip_memory*)*memory)->state = FIND_HEAD_INFO;
    }
    zip_memory* zm = *memory;
    getData(data, size, zm);

    // 因为内容不够直接退出
    if (zm->state != FIND_HEAD_INFO) ++file_flag;

    // int i = 0;
    // for (; i < 2; ++i) {
    while (1){
        if (zm->state == FIND_HEAD_INFO) {
            if (!getFileStart(zm->data, zm->size, &zm->index)) {
                freeZipMemory(zm);
                *memory = NULL;
                return file_flag;
            }
            ++file_flag;
            zm->state = HEAD_INFO;
        } else file_flag = 1;   // 证明正在解析
        
        if (zm->state == HEAD_INFO || zm->state == HEAD_LENGTH) {
            if (parseFileInfo(zm->data, zm->size, &zm->index, zm) == 0)
                return --file_flag;
        } 

        if (zm->state != FIND_HEAD_INFO && zm->state != HEAD_INFO && zm->state != HEAD_LENGTH) {
            if (parseFile(zm->data, zm->size, &zm->index, zm) == 0)
                return --file_flag;
        }
    }

    return 0;
}

int main() {
    FILE* fin = fopen("test.zip", "rb");
    unsigned char* in = (unsigned char*)malloc(150000000);
    int size = 0;
    void* zm = NULL;
    srand((unsigned int)time(NULL));

    printf("开始载入");
    while (!feof(fin)) {
        in[size++] = getc(fin);
    }
    
    printf("载入完毕, %d\n", size);

    int i = 0;
    for (; i < size; i += 100){
            int cur_size = (i + 100) > size ? (size - i) : 100;
            if (i == 10677900) {
                i = 10677900;
            }
            getZipInterface(&in[i], cur_size, &zm);
        
    }
    

    free(in);
    return 0;
}