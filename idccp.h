#ifndef _IDCCP_PROTOCOL_
#define _IDCCP_PROTOCOL_

#define _IDCCP_MAGIC_0_ 0x49
#define _IDCCP_MAGIC_1_ 0x44
#define _IDCCP_MAGIC_2_ 0x43
#define _VERSION_MASK_ 0xF0
#define FLAG_MASK 0x1
#define IDCCP_VERSION 1
#define HEADER_LENGTH 8
#define PAYLOAD_MAX_LEN 255
#define CONF_TABLE_MAX_SIZE 256
#define MAX_CMD_STR 256

#define EXIT_NOCONF 2
#define EXIT_UNKNOWN_KEY 3
#define EXIT_UNSUPPORTED_CMDNUM 4
#define EXIT_FULL_OF_CONF_TABLE 5
#define EXIT_FORK_ERROR 6
#define EXIT_EXECL 7
#define EXIT_FLAG_DIFF 8
#define EXIT_LENGTH_ERR 9
#define EXIT_HOLDER_MISSING 10
#define EXIT_UNSUPPORTED_PAYLOAD_FLAG 11
#define EXIT_ERROR_IP 12
#define EXIT_OUT_BOUND_CONF 13
#define EXIT_OUT_BOUND_PAYLOAD 14
#define EXIT_REQ_OPEN_FAILED 15

#include<stdint.h>
#include<stdbool.h>
#include<netinet/in.h>
#include<stdio.h>

typedef struct
{
    uint8_t magic_0;
    uint8_t magic_1;
    uint8_t magic_2;
    uint8_t ver_flag;
    uint8_t length;
    uint8_t command;
    uint16_t req_id;
} __attribute__((packed)) IDCCPFrame;

typedef struct
{
    uint8_t cmdnum;
    char cli_str[MAX_CMD_STR];
    int payload_flag;
} conf_table_element;

typedef struct
{
    uint16_t port;
    char * configure;
    struct sockaddr_in addr;
    int sockfd;
    uint8_t cmdnum;
    char payload[PAYLOAD_MAX_LEN];
    uint8_t flag;
} cfg_t;

int socket_create(cfg_t * cfg);
bool dedup_table_find(struct sockaddr_in * cli_addr_ptr,uint16_t req_id);
void handle_frame(IDCCPFrame * frame);
void loop(int sockfd);
void trim(char * str_ptr);
void load_configure(FILE * fp);
void load_single_conf(FILE * fp);
void option(int argc,char * argv[],cfg_t * cfg_ptr);
void print_help(void);
void turn_to_daemon(void);

#endif
