#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "../common/config.h"
#include "../common/common.h"
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<unistd.h>
#include<stddef.h>
#include<stdint.h>
#include<netdb.h>

static int get_opcode(uint16_t *opcode, char *buf, int buf_size){
    if(buf_size < 2) return -1;
    if(!buf) return -1;
    uint16_t raw_opcode;
    memcpy(&raw_opcode, buf, 2);
    *opcode = ntohs(raw_opcode);
    return 0;
}

int main(int argc, char *argv[]){
    if(argc != 2){
        printf("few args!\n");
        printf("./client <IP address>\n");
    }
    else{
        int client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(client != -1){
            struct sockaddr_in saddr;
            saddr.sin_family = AF_INET;
            saddr.sin_port = htons(PORT);
            saddr.sin_addr.s_addr = inet_addr(argv[1]);
            int n;
            char filename[512];
            while(1){
                memset(filename, 0, sizeof(filename));
                printf("RRQ or WRQ? (1 or 2) (0 to quit):");
                fflush(stdout);
                scanf("%d",&n);
                int chr;
                while((chr = getchar()) != '\n' && chr != EOF);
                printf("Enter filename:");
                fflush(stdout);
                fgets(filename, sizeof(filename) - 1, stdin);
                filename[strcspn(filename, "\r\n")] = '\0';
                if(n == 0){
                    break;
                }
                else if(n == 1){
                    int sent = send_rq(client, &saddr, sizeof(saddr), OP_RRQ, filename, BIN_MODE);
                    if(sent < 0){
                        printf("Send failed\n");
                        break;
                    }
                    char buf[1024];
                    char parse_buf[1024];
                    memset(buf, 0, sizeof(buf));
                    struct sockaddr_in tmp_addr;
                    int len = sizeof(struct sockaddr);
                    while(1){
                        int got = recvfrom(client, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&tmp_addr, &len);
                        if(got < 0){
                            printf("recv fail");
                            break;
                        }
                        uint16_t opcode;
                        int ec = get_opcode(&opcode, buf, got);
                        if(ec < 0){
                            printf("get OPCODE failed\n");
                            break;
                        }
                        uint16_t blocknums;
                        if(opcode == OP_ACK){
                            int ecc = parse_ack(buf, got, &blocknums);
                            if(ecc < 0){
                                printf("PARSE ERROR\n");
                                break;
                            }
                            printf("%d\n", blocknums);
                            break;
                        }
                    }
                }
                else if(n == 2){
                    printf("choose 2\n");
                }
                else{
                    printf("ERROR\n");
                }
            }
            close(client);
        }
        else{
            printf("SOCKET ERROR\n");
        }
    }
    return 0;
}