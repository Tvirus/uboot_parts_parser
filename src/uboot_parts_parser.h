
#ifndef __UBOOT_PARTS_PARSER_H__
#define __UBOOT_PARTS_PARSER_H__



#include <sys/types.h>


typedef    unsigned char          u8;
typedef    unsigned short        u16;
typedef    unsigned int          u32;
typedef    unsigned long long    u64;
typedef      signed char          s8;
typedef      signed short        s16;
typedef      signed int          s32;
typedef      signed long long    s64;


extern int get_total_cap(u64 *cap);
extern int get_value_by_key(const char *str, const char *key, char *value, int value_len);
extern int get_partition_size(const char *name, u64 *offset, u64 *size, char force_init);
extern int get_partition_size_by_id(int id, u64 *offset, u64 *size, char force_init);


#endif
