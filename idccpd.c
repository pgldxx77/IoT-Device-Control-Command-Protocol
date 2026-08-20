#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdint.h>
#include<errno.h>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<ctype.h>
#include<unistd.h>
#include<getopt.h>
#include<signal.h>
#include<stdbool.h>
#include "idccp.h"

#define DEDUP_TABLE_SIZE 64

typedef struct 
{
    uint16_t req_id;
    uint32_t cli_ip;
    uint16_t cli_port;
} __attribute__((packed)) dedup_table_element;

static dedup_table_element dedup_table[DEDUP_TABLE_SIZE];
static int dedup_table_max_index = 0;
static conf_table_element conf_table[CONF_TABLE_MAX_SIZE];
static int conf_table_max_index = 0;

int socket_create(cfg_t * cfg_ptr)
{
    int sockfd = socket(AF_INET,SOCK_DGRAM,0);
    if (sockfd < 0) 
    {
        perror("socket create failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(cfg_ptr->port);
    addr.sin_addr.s_addr=cfg_ptr->addr.sin_addr.s_addr;
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    } 
    return sockfd;
}
bool dedup_table_find(struct sockaddr_in * cli_addr_ptr,uint16_t req_id)
{
    for(int i = 0;i < dedup_table_max_index;i++)
    {
        if(cli_addr_ptr->sin_port == dedup_table[i].cli_port && cli_addr_ptr->sin_addr.s_addr == dedup_table[i].cli_ip && req_id == dedup_table[i].req_id )
        {
            return true;
        }
    }
    if(dedup_table_max_index == DEDUP_TABLE_SIZE)
    {
        dedup_table_max_index = 0;
    }
    dedup_table[dedup_table_max_index].cli_ip = cli_addr_ptr->sin_addr.s_addr;
    dedup_table[dedup_table_max_index].cli_port = cli_addr_ptr->sin_port;
    dedup_table[dedup_table_max_index].req_id = req_id;
    dedup_table_max_index++;
    return false;
}
void handle_frame(IDCCPFrame * frame)
{
    uint8_t flag = frame->ver_flag & FLAG_MASK;
    uint8_t command = frame->command;
    uint8_t length = frame->length;
    uint8_t * payload = (uint8_t *)(frame + 1);
    char cmdbuf[512];
    if(flag == 0)
    {
        for(int i = 0;i < conf_table_max_index;i++)
        {
            if(command == conf_table[i].cmdnum)
            {
                if(flag != conf_table[i].payload_flag)
                {
                    printf("[+] Flag diff: %x: %s\n",(unsigned int)command,conf_table[i].cli_str);
                    exit(EXIT_FLAG_DIFF);
                }
                strncpy(cmdbuf,conf_table[i].cli_str,MAX_CMD_STR);
                trim(cmdbuf);
                pid_t new_pid = fork();
                if(new_pid < 0)
                {
                    perror("Fork error");
                    exit(EXIT_FORK_ERROR);
                }
                else if(new_pid == 0)
                {
                    execl("/bin/sh","sh","-c",cmdbuf,(char *)NULL);
                    perror("execl");
                    exit(EXIT_EXECL);
                }
                else
                {
                    return;
                }
            }
        }
    }
    else
    {
        for(int i = 0;i < conf_table_max_index;i++)
        {
            if(command == conf_table[i].cmdnum)
            {
                if(flag != conf_table[i].payload_flag)
                {
                    printf("[+] Flag diff: %x: %s\n",command,conf_table[i].cli_str);
                    exit(EXIT_FLAG_DIFF);
                }
                if(length == 0)
                {
                    printf("[+] Payload Length Error.");
                    exit(EXIT_LENGTH_ERR);
                }
                uint8_t copy_length;
                char payload_str[256] = {0};
                char new_cmdbuf[512];
                copy_length = length;
                memcpy(payload_str,payload,copy_length);
                payload_str[copy_length] = '\0';
                strncpy(cmdbuf,conf_table[i].cli_str,MAX_CMD_STR);
                trim(cmdbuf);
                char * holder = strstr(cmdbuf,"{payload}");
                if(!holder)
                {
                    printf("[+] Payload holder Missing.");
                    exit(EXIT_HOLDER_MISSING);
                }
                size_t pre_len = holder - cmdbuf;
                memcpy(new_cmdbuf,cmdbuf,pre_len);
                new_cmdbuf[pre_len] = '\0';
                strcat(new_cmdbuf,payload_str);
                strcat(new_cmdbuf,holder+strlen("{payload}"));
                strncpy(cmdbuf, new_cmdbuf, sizeof(cmdbuf) - 1);
                cmdbuf[sizeof(cmdbuf) - 1] = '\0';
                trim(cmdbuf);
                pid_t new_pid = fork();
                if(new_pid < 0)
                {
                    perror("Fork error");
                    exit(EXIT_FORK_ERROR);
                }
                else if(new_pid == 0)
                {
                    execl("/bin/sh","sh","-c",cmdbuf,(char *)NULL);
                    perror("execl");
                    exit(EXIT_EXECL);
                }
                else
                {
                    return;
                }
            }
        }
    }
}
void loop(int sockfd)
{
    uint8_t buf[512];
    struct sockaddr_in cli_addr;
    socklen_t addr_len = sizeof(cli_addr);
    while(1)
    {
        addr_len = sizeof(cli_addr);
        int n = recvfrom(sockfd,buf,sizeof(buf),0,(struct sockaddr*)&cli_addr,&addr_len);
        if(n < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            else
            {
                printf("[+] Recvfrom Error: %s\n",strerror(errno));
                continue;
            }
        }
        else if((long unsigned int)n < sizeof(IDCCPFrame))
        {
            continue;
        }
        else
        {
            IDCCPFrame * frame = (IDCCPFrame *)buf;
            if(frame->magic_0 != _IDCCP_MAGIC_0_ || frame->magic_1 !=_IDCCP_MAGIC_1_ || frame->magic_2 != _IDCCP_MAGIC_2_)
            {
                printf("[+] Pakcet diff\n");
                continue;
            }
            else if(((frame->ver_flag & _VERSION_MASK_) >> 4) != IDCCP_VERSION)
            {
                printf("[+] Version diff\n");
                continue;
            }
            else if(!dedup_table_find(&cli_addr,frame->req_id))
            {
                handle_frame(frame);
            }
            else 
            {
                continue;
            }    
        }
    }
}
void trim(char * str_ptr)
{
    if(!str_ptr || *str_ptr == '\0')
    {
        return;
    }
    char * start = str_ptr;
    char * end;
    while(isspace((unsigned char) *start))
    {
        start++;
    }
    if(*start == '\0')
    {
        *str_ptr = '\0';
        return;
    }
    end = start + strlen(start) -1;
    while(end > start && isspace((unsigned char) *end))
    {
        end--;
    }
    if(start != str_ptr)
    {
        memmove(str_ptr,start,end-start+1);
    }
    str_ptr[end - start + 1] = '\0';
}
void load_configure(FILE * fp)
{
    char line[256];
    char * pos;
    while(fgets(line,sizeof(line),fp))
    {
        trim(line);
        if(line[0] == '#' || line[0] == '\0')
        {
            continue;
        }
        if((pos = strchr(line,'[')) != NULL)
        {
            *pos = '\0';
            pos++;
            trim(pos);
            char * section = pos;
            if((pos = strchr(pos,']')) != NULL)
            {
                *pos = '\0';
                trim(section);
            }
            if(!strcmp(section,"Command"))
            {
                load_single_conf(fp);
            }
        }
    }
}
void load_single_conf(FILE * fp)
{
    char line[256];
    char * key;
    char * value;
    char * pos;
    bool cmdnum_flag = false;
    bool cli_str_flag = false;
    bool payload_flag_flag = false;
    if(conf_table_max_index == CONF_TABLE_MAX_SIZE)
    {
        printf("[+] Warning: The max size of configure.\n");
        exit(EXIT_FULL_OF_CONF_TABLE);
    }
    while(fgets(line,sizeof(line),fp))
    {
        trim(line);
        if(line[0] == '#' || line[0] == '\0')
        {
            continue;
        }
        if(line[0] == '[')
        {
            return;
        }
        if((pos = strchr(line,'=')) != NULL)
        {
            *pos = '\0';
            key = line;
            value = pos + 1;
            trim(key);
            trim(value);
            if(!strcmp(key,"cmdnum"))
            {
                char * tmp_str;
                long val = strtol(value,&tmp_str,0);
                if(tmp_str == value || *tmp_str != '\0' || val < 0 || val >255)
                {
                    printf("[+] Unsupported cmdnum: %s\n",value);
                    exit(EXIT_UNSUPPORTED_CMDNUM);
                }
                conf_table[conf_table_max_index].cmdnum = (uint8_t)val;
                cmdnum_flag = true;
            }
            else if(!strcmp(key,"cli_str"))
            {
                strncpy(conf_table[conf_table_max_index].cli_str,value,MAX_CMD_STR - 1);
                conf_table[conf_table_max_index].cli_str[MAX_CMD_STR - 1] = '\0';
                cli_str_flag = true;
            }
            else if(!strcmp(key,"payload_flag"))
            {
                
                if(!strcmp(value,"true") || !strcmp(value,"1"))
                {
                    conf_table[conf_table_max_index].payload_flag = 1;
                }
                else if(!strcmp(value,"false") || !strcmp(value,"0"))
                {
                    conf_table[conf_table_max_index].payload_flag = 0;
                }
                else
                {
                    printf("[+] Unsupported payload_flag: %s\n",value);
                    exit(EXIT_UNSUPPORTED_PAYLOAD_FLAG);
                }
                payload_flag_flag = true;
            }
            else
            {
                printf("[+] Unknown key: %s\n",key);
                exit(EXIT_UNKNOWN_KEY);
            }
            if(payload_flag_flag && cmdnum_flag && cli_str_flag)
            {
                conf_table_max_index++;
                return;
            }
        }
    }
    if(payload_flag_flag && cmdnum_flag && cli_str_flag)
    {
        conf_table_max_index++;
        return;
    }
    else
    {
        printf("[+] Warning: The rules needs the complete table.\n");
        return;
    }
}
void option(int argc,char * argv[],cfg_t * cfg_ptr)
{
    int opt;
    cfg_ptr->port = 30052;
    cfg_ptr->configure = "/etc/idccp.conf";
    cfg_ptr->addr.sin_family = AF_INET;
    cfg_ptr->addr.sin_addr.s_addr = INADDR_ANY;
    while((opt = getopt(argc,argv,"a:p:c:hd")) != -1)
    {
        switch(opt)
        {
            case 'a':
                if(inet_pton(AF_INET,optarg,&cfg_ptr->addr.sin_addr) <= 0)
                {
                    printf("Mistake IP: %s\n",optarg);
                    exit(EXIT_ERROR_IP);
                }
                break;
            case 'p':
                cfg_ptr->port = atoi(optarg);
                break;
            case 'h':
                print_help();
                break;
            case 'c':
                cfg_ptr->configure = optarg;
                break;
            case 'd':
                turn_to_daemon();
                break;
            default:
                exit(1);
        }
    }
    return;
}
void print_help()
{
    printf("Usage: idccpd [options]\n");
    printf("Options:\n");
    printf("  -a <IP>       Bind to IP address (default: 0.0.0.0)\n");
    printf("  -p <port>     Listen on specified port (default: 30052)\n");
    printf("  -c <file>     Use configuration file (default: /etc/idccp.conf)\n");
    printf("  -d            Run as daemon (background)\n");
    printf("  -h            Show this help message and exit\n");
}
void turn_to_daemon()
{
    if (daemon(0, 0) == -1) 
    {
        perror("daemon");
        exit(EXIT_FAILURE);
    }
    return;
}
int main(int argc,char * argv[])
{
    cfg_t cfg;
    option(argc,argv,&cfg);
    signal(SIGCHLD, SIG_IGN);
    FILE *fp = fopen(cfg.configure,"r");
    if(fp == NULL)
    {
        perror("idccpd wants a configure");
        return EXIT_NOCONF;
    }
    load_configure(fp);
    int sockfd = socket_create(&cfg);
    loop(sockfd);
    return 0;
}