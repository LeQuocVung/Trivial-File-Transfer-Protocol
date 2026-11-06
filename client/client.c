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

struct timeval tv;

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
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char)&tv, sizeof(tv));
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
                if(n == 0){
                    break;
                }
                else if(n == 1){
                    while((chr = getchar()) != '\n' && chr != EOF);
                    printf("Enter filename:");
                    fflush(stdout);
                    fgets(filename, sizeof(filename) - 1, stdin);
                    filename[strcspn(filename, "\r\n")] = '\0';
                    int sent = send_rq(client, &saddr, sizeof(saddr), OP_RRQ, filename, BIN_MODE);
                    if(sent < 0){
                        printf("Send failed\n");
                        break;
                    }
                    char buf[1024];
                    memset(buf, 0, sizeof(buf));
                    struct sockaddr_in tmp_addr;
                    int len = sizeof(struct sockaddr);
                    uint16_t blocknums;
                    uint16_t next_blocknums = 1;
                    const char *data_ptr;
                    const char *msg_ptr;
                    int data_len;
                    uint16_t errcode;
                    FILE *f = fopen(filename, "wb+");
                    if(!f){
                        printf("Can't open file: %s\n", filename);
                    }
                    else{
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
                            if(opcode == OP_DATA){
                                int ecc = parse_data(buf, got, &blocknums, &data_ptr, &data_len);
                                if(ecc < 0){
                                    printf("PARSE ERROR\n");
                                    break;
                                }
                                printf("current block nums: %d\n", blocknums);
                                printf("size of current data package: %d\n", data_len);
                                if(blocknums ==  next_blocknums){
                                    if(data_len > 0){
                                        fwrite(data_ptr, sizeof(char), data_len, f);       
                                    }
                                    send_ack(client, &tmp_addr, sizeof(tmp_addr), blocknums);
                                    next_blocknums++;
                                    if(data_len < 512) break;
                                }
                                else if(blocknums == next_blocknums - 1){
                                    send_ack(client, &tmp_addr, sizeof(tmp_addr), blocknums);
                                }
                            }
                            else if(opcode == OP_ERROR){
                                int ecc = parse_err(buf, got, &errcode, &msg_ptr);
                                if(ecc < 0){
                                    printf("PARSE ERROR FAIL\n");
                                }
                                else{
                                    printf("ERROR: [%d] : [%s]\n", errcode, msg_ptr);
                                }
                                break;
                            }
                            else{
                                printf("UNDEFINED ERROR\n");
                                break;
                            }
                        }
                        fclose(f);
                    }
                }
                else if(n == 2){
                    while((chr = getchar()) != '\n' && chr != EOF);
                    printf("Enter filename:");
                    fflush(stdout);
                    fgets(filename, sizeof(filename) - 1, stdin);
                    filename[strcspn(filename, "\r\n")] = '\0';
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