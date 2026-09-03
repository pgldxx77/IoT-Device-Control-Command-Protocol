#define _CLI_IDCCP_

#include "idccp.h"
#include<getopt.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<fcntl.h>
#include<unistd.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

void print_help()
{
    printf("Usage: idccp_cli [options]\n");
    printf("  -a <IP>       Target IP (default: 127.0.0.1)\n");
    printf("  -p <port>     Target port (default: 30052)\n");
    printf("  -n <cmdnum>   Command number (hex or dec)\n");
    printf("  -l <payload>  Payload string (sets flag=1)\n");
    printf("  -h            Show this help\n");
}
uint16_t req_query()
{
    int req_fd = open("/var/lib/idccp.req",O_RDWR | O_CREAT,0644);
    if (req_fd < 0) 
    {
        printf("[+] Open req file failed.\n");
        exit(EXIT_REQ_OPEN_FAILED);
    }
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    fcntl(req_fd,F_SETLKW,&lock);
    uint16_t req_id = 0;
    read(req_fd,&req_id,sizeof(req_id));
    req_id++;
    lseek(req_fd,0,SEEK_SET);
    write(req_fd,&req_id,sizeof(req_id));
    lock.l_type = F_UNLCK;
    fcntl(req_fd,F_SETLKW,&lock);
    close(req_fd);
    return req_id;
}
void IDCCP_frame(cfg_t * cfg_ptr)
{
    uint8_t flag = cfg_ptr->flag;
    char buf[264];
    IDCCPFrame * frame = (IDCCPFrame*)buf;
    frame->magic_0 = _IDCCP_MAGIC_0_;
    frame->magic_1 = _IDCCP_MAGIC_1_;
    frame->magic_2 = _IDCCP_MAGIC_2_;
    frame->ver_flag = (IDCCP_VERSION << 4) | (flag & 0x0F);
    frame->length = strlen(cfg_ptr->payload);
    frame->command = cfg_ptr->cmdnum;
    frame->req_id = req_query();
    for(int i = 0;i < 3;i++)
    {
        sendto(cfg_ptr->sockfd,buf,sizeof(IDCCPFrame) + frame->length,0,(struct sockaddr*)&cfg_ptr->addr,sizeof(struct sockaddr_in));
    }
}
void IDCCP_send_socket_create(cfg_t * cfg_ptr)
{
    int sockfd = socket(AF_INET,SOCK_DGRAM,0);
    if (sockfd < 0) 
    {
        printf("[+] Socket create failed.\n");
        exit(EXIT_FAILURE);
    }
    cfg_ptr->sockfd = sockfd;
}
void cli_option(cfg_t * cfg_ptr,int argc,char * argv[])
{
    int opt;
    cfg_ptr->port = 30052;
    cfg_ptr->addr.sin_family = AF_INET;
    cfg_ptr->addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(cfg_ptr->payload, 0, PAYLOAD_MAX_LEN);
    char *endptr;
    while((opt = getopt(argc, argv, "l:n:a:p:h")) != -1)
    {
        switch(opt)
        {
            case 'a':
                if(inet_pton(AF_INET,optarg,&cfg_ptr->addr.sin_addr) <= 0)
                {
                    printf("[+] Mistake IP: %s\n",optarg);
                    exit(EXIT_ERROR_IP);
                }
                break;
            case 'n':
                long val = strtol(optarg, &endptr, 0);
                if (*endptr != '\0') 
                {
                    printf("[+] Unknown format: %s\n", optarg);
                    exit(EXIT_UNSUPPORTED_CMDNUM);
                }
                if (val < 0 || val > 255) 
                {
                    printf("[+] Out of the bound: %ld\n", val);
                    exit(EXIT_UNSUPPORTED_CMDNUM);
                }
                cfg_ptr->cmdnum = (uint8_t)val;
                break;
            case 'l':
                if(strlen(optarg) > PAYLOAD_MAX_LEN)
                {
                    printf("[+] Out of the bound of the payload size.\n");
                    exit(EXIT_OUT_BOUND_PAYLOAD);
                }
                strncpy(cfg_ptr->payload,optarg,PAYLOAD_MAX_LEN);
                cfg_ptr->payload[PAYLOAD_MAX_LEN - 1] = '\0';
                cfg_ptr->flag = 1;
                break;
            case 'p':
                cfg_ptr->addr.sin_port = htons(atoi(optarg));
                break;
            case 'h':
                print_help();
                break;
            default:
                exit(1);
        }   
    }
}
int main(int argc,char * argv[])
{
    cfg_t cfg = {0};
    cli_option(&cfg,argc,argv);
    IDCCP_send_socket_create(&cfg);
    IDCCP_frame(&cfg);
    return 0;
}