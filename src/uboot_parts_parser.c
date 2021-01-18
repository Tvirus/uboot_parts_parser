#include "uboot_parts_parser.h"

#ifdef CONFIG_MMC
#include "mmc.h"
#endif




#define MAX_PART_NAME      16
#define MAX_PARTS          16
#define BOOTARGS           "bootargs"


#define SKIPCHAR(str, ch)     do {for(; *(str) == (ch); (str)++);} while(0)
#define PRINT(fmt, arg...)    printf("[%s] " fmt "\n", __FUNCTION__, ##arg)


#if 1
#define LOG(fmt, arg...)  printf("[%s] " fmt "\n", __FUNCTION__, ##arg)
#else
#define LOG(fmt, arg...)
#endif


#define SIZE_REMAINING          (~0llu)
#define OFFSET_NOT_SPECIFIED    (~0llu)



/*
mtdparts 格式

mtdparts=<mtddef>[;<mtddef]
<mtddef>  := <mtd-id>:<partdef>[,<partdef>]
<partdef> := <size>[@offset][<name>][ro]
<mtd-id>  := unique id used in mapping driver/device
<size>    := standard linux memsize OR "-" to denote all remaining space
<name>    := (NAME)
*/



typedef struct
{
    char name[MAX_PART_NAME];
    u64  size;
    u64  offset;
}part_info_t;

static int parts_num = -1; /* 解析得到的分区数量, -1:还未解析 */
static part_info_t parts_table[MAX_PARTS] = {0};


static char *part_keys[] =
{
    "blkdevparts",
    "mtdparts",
    NULL
};




static int parse_size(const char *str, char **end, u64 *size)
{
    if (('0' > *str) || ('9' < *str))
    {
        *end = (char *)str;
        return -1;
    }

    *size = simple_strtoull(str, end, 0);
    switch (**end)
    {
        case 'G':
        case 'g':
            if (0xffc0000000000000 & (*size))
                return -1;
            *size <<= 10;
        case 'M':
        case 'm':
            if (0xffc0000000000000 & (*size))
                return -1;
            *size <<= 10;
        case 'K':
        case 'k':
            if (0xffc0000000000000 & (*size))
                return -1;
            *size <<= 10;
            (*end)++;
        default:
            break;
    }

    return 0;
}



#ifdef CONFIG_MMC
static int get_mmc_user_cap(int dev_num, u64 *cap)
{
    struct mmc *mmc;

    mmc = find_mmc_device(dev_num);
    if (NULL == mmc)
    {
        PRINT("find_mmc_device(%d) return error !", dev_num);
        return -1;;
    }
    if (0 != mmc_init(mmc))
    {
        PRINT("mmc_init() return error !", dev_num);
        return -1;
    }
    *cap = mmc->capacity_user;

    return 0;
}
#endif


int get_total_cap(u64 *cap)
{
    int rst = 0;

#ifdef CONFIG_ENV_IS_IN_NAND
    rst = -1;
#endif

#ifdef CONFIG_MMC
    rst = get_mmc_user_cap(0, cap);
#endif

    return rst;
}


int get_value_by_key(const char *str, const char *key, char *value, int value_len)
{
    char *p;
    int   i;


    if ((NULL == str) || (NULL == key) || ('\0' == str[0]) || ('\0' == key[0]) || (1 > value_len))
        return -1;

    /* 找到key */
    p = strstr(str, key);
    if (NULL == p)
        return -1;
    p += strlen(key);

    /* 略过空格 */
    SKIPCHAR(p, ' ');

    /* 找到'=' */
    if ('=' != *p)
        return -1;
    p++;

    /* 略过空格 */
    SKIPCHAR(p, ' ');

    for (i = 0; i < value_len; i++)
    {
        if ((' ' == p[i]) || ('\0' == p[i]) || ('\n' == p[i]) || ('\r' == p[i]) || ('\t' == p[i]))
            break;
        value[i] = p[i];
    }

    if (i < value_len)
        value[i] = '\0';

    return i;
}


static int parse_one_part(char *str)
{
    u64   size;
    u64   offset;
    char *name = NULL;
    char *p = str;
    int   i;


    /* size */
    if (('0' <= *p) && ('9' >= *p))
    {
        if (0 != parse_size(p, &p, &size))
        {
            PRINT("Partition size too big !  \"%s\"", str);
            return -1;
        }
    }
    else if ('-' == *p)
    {
        size = SIZE_REMAINING;
        p++;
    }
    else
    {
        PRINT("No partition size specified !  \"%s\"", str);
        return -1;
    }

    /* offset */
    if ('@' == *p)
    {
        p++;
        if (('0' > *p) || ('9' < *p))
        {
            PRINT("Invalid partition format of @offset !  \"%s\"", str);
            return -1;
        }
        if (0 != parse_size(p, &p, &offset))
        {
            PRINT("Partition offset too big !  \"%s\"", str);
            return -1;
        }
    }
    else
    {
        offset = OFFSET_NOT_SPECIFIED;
    }

    /* name */
    if ('(' == *p)
    {
        name = ++p;
        p = strchr(p, ')');
        if (NULL == p)
        {
            PRINT("Invalid partition format of (name) !  \"%s\"", str);
            return -1;
        }
        if (MAX_PART_NAME <= (u32)(p - name)) /* 最后留一个结束标记 '\0' */
        {
            PRINT("Partition name exceeds the max buf len(%u) !  \"%s\"", MAX_PART_NAME, str);
            return -1;
        }
        p++;
    }
    else
    {
        name = NULL;
    }

    /* ro */
    if ((('r' == p[0]) || ('R' == p[0]))  &&  (('o' == p[1]) || ('O' == p[1])))
    {
        p += 2;
        // ...
    }

    if ('\0' != *p)
    {
        PRINT("Invalid partition format or order !  \"%s\"", str);
        return -1;
    }


    if (MAX_PARTS <= parts_num)
    {
        PRINT("Found too many partitions, max(%u) !", MAX_PARTS);
        return -1;
    }
    parts_num = (0 > parts_num)? 0: parts_num;

    /* 存储解析得到的信息 */
    parts_table[parts_num].size   = size;
    parts_table[parts_num].offset = offset;
    if (NULL != name)
    {
        for (i = 0; (i + 1) < MAX_PART_NAME; i++)
        {
            if ((')' == name[i]) || ('\0' == name[i]))
                break;
            parts_table[parts_num].name[i] = name[i];
        }
        parts_table[parts_num].name[i] = '\0';
    }
    else
    {
        parts_table[parts_num].name[0] = '\0';
    }

    parts_num++;
    return 0;
}


static int parse_one_dev(char *str)
{
    u64   offset = 0;
    u64   cap = 0;
    char *start = NULL;
    char *end = NULL;
    char  finish = 0;
    char  have_filled_up = 0; /* 已经读到了'-'分区 */
    char  offset_specified = 0; /* 当一个分区有@offset时的标志 */
    part_info_t *part = NULL;


    LOG("Start parse: \"%s\"", str);

    start = strchr(str, ':');
    if (NULL == start)
    {
        PRINT("Failed to find ':' followed by <mtd-id> !  \"%s\"", str);
        goto ERROR;
    }
    end = ++start;

    for (;;)
    {
        if ((',' != *end) && ('\0' != *end))
        {
            end++;
            continue;
        }

        finish = ('\0' == *end)? 1: 0;
        *end = '\0';
        if (0 > parse_one_part(start))
        {
            PRINT("Failed to parse \"%s\"", start);
            goto ERROR;
        }
        part = &parts_table[parts_num - 1];

        if (OFFSET_NOT_SPECIFIED == part->offset)
        {
            /* '-'分区后不能再有非@offset分区 */
            if (have_filled_up)
            {
                PRINT("No capacity left after '-'partition !");
                goto ERROR;
            }
            part->offset = offset;
            offset_specified = 0;
        }
        else
        {
            offset_specified = 1;
        }

        if (SIZE_REMAINING == part->size)
        {
            /* 只能有一个不带@offset的'-'分区 */
            if (have_filled_up && !offset_specified)
            {
                PRINT("Cannot parse anthor '-' !  \"%s\"", str);
                goto ERROR;
            }
            /* 只获取一次总容量 */
            if ((0 == cap) && (0 != get_total_cap(&cap)))
            {
                PRINT("Cannot get total capacity to parse '-' !  \"%s\"", str);
                goto ERROR;
            }
            if (offset >= cap)
            {
                PRINT("No capacity left for partition \"%s\"", str);
                goto ERROR;
            }
            part->size = cap - offset;
            have_filled_up = 1;
        }
        /* 设置了 @offset 的分区大小不改变累计偏移量 */
        if (!offset_specified)
        {
            /* 大小溢出64位范围 */
            if (offset + part->size < offset)
            {
                PRINT("Partition size(%llu) too big !  \"%s\"", part->size, str);
                goto ERROR;
            }
            offset += part->size;
        }

        if (finish)
            break;

        start = ++end;
    }

    return 0;

ERROR:
    parts_num = -1;
    return -1;
}


static int parse_parts(void)
{
    char *bootargs = NULL;
    char  parts_str[200] = {0};
    char *p;
    int   rst;
    int   i;


    bootargs = getenv(BOOTARGS);
    if ((NULL == bootargs) || ('\0' == bootargs[0]))
    {
        PRINT("Failed to get env \"%s\" !", BOOTARGS);
        return -1;
    }

    /* 找到mtd或者blk设备分区字段 */
    i = 0;
    for (;;)
    {
        if (NULL == part_keys[i])
        {
            PRINT("Failed to find partitions from env \"%s\" !", bootargs);
            return -1;
        }
        rst = get_value_by_key(bootargs, part_keys[i], parts_str, sizeof(parts_str));
        if (0 > rst)
        {
            i++;
            continue;
        }
        if (sizeof(parts_str) <= rst)
        {
            PRINT("Partitions str exceeds the max buf len(%zu) !  \"%s\"", sizeof(parts_str), part_keys[i]);
            return -1;
        }
        break;
    }

    /* 把';'替换成'\0', 暂只解析一个设备分区 */
    for (p = parts_str; ('\0' != *p) && (';' != *p); p++);
    *p = '\0';

    return parse_one_dev(parts_str);
}


int get_partition_size(const char *name, u64 *offset, u64 *size, char force_init)
{
    int i;

    if ((NULL == name) || (NULL == offset) || (NULL == size) || ('\0' == *name))
    {
        PRINT("Invalid arguments !");
        return -1;
    }

    if ((0 >= parts_num) || force_init)
    {
        if (0 > parse_parts())
            return -1;
    }

    for (i = 0; i < parts_num; i++)
    {
        if (0 != strcmp(name, parts_table[i].name))
            continue;
        *offset = parts_table[i].offset;
        *size   = parts_table[i].size;
        return 0;
    }

    return -1;
}

int get_partition_size_by_id(int id, u64 *offset, u64 *size, char force_init)
{
    if ((NULL == offset) || (NULL == size))
    {
        PRINT("Invalid arguments !");
        return -1;
    }

    if ((0 >= parts_num) || force_init)
    {
        if (0 > parse_parts())
            return -1;
    }

    if (parts_num <= id)
    {
        PRINT("Invalid id(%d) !", id);
        return -1;
    }

    *offset = parts_table[id].offset;
    *size   = parts_table[id].size;

    return 0;
}
