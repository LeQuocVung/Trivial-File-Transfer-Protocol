#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "../common/config.h"
#include "../common/common.h"
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<unistd.h>
#include<errno.h>
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
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
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
                        printf("SEND FAIL\n");
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
                    int retry = 0;
                    uint16_t opcode;
                    uint16_t errcode;
                    FILE *f = fopen(filename, "wb+");
                    if(!f){
                        printf("CAN'T OPEN FILE: %s\n", filename);
                        break;
                    }
                    else{
                        while(1){
                            int got = recvfrom(client, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&tmp_addr, &len);
                            if(got < 0){
                                if(errno == EAGAIN || errno == EWOULDBLOCK ){
                                    retry++;
                                    if(retry >= MAX_RETRIES){
                                        printf("SERVER NOT RESPONDING. CONNECTION FAIL\n");
                                        break;
                                    }
                                    if(next_blocknums == 1){
                                        send_rq(client, &saddr, sizeof(saddr), OP_RRQ, filename, BIN_MODE); 
                                    }
                                    else{
                                        send_ack(client, &tmp_addr, sizeof(tmp_addr), next_blocknums -1);
                                    }
                                    continue;
                                }else{
                                    printf("RECV FAIL\n");
                                    break;
                                }   
                            }
                            retry = 0;
                            int ec = get_opcode(&opcode, buf, got);
                            if(ec < 0){
                                printf("GET OPCODE FAIL\n");
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
                    int sent = send_rq(client, &saddr, sizeof(saddr), OP_WRQ, filename, BIN_MODE);
                    if(sent < 0){
                        printf("SEND REQUEST FAILED\n");
                        break;
                    }


                    char buf[1024];
                    memset(buf, 0, sizeof(buf));
                    struct sockaddr_in tmp_addr;
                    int len = sizeof(tmp_addr);
                    int retry = 0;
                    uint16_t opcode;
                    uint16_t block_num;
                    uint16_t error_code;
                    const char *error_msg;
                    uint16_t next_block_num = 0;
                    char buffer[512];
                    int bytes_read = 0;
                    int is_the_last_pkt = 0;
                    memset(buffer, 0, sizeof(buffer));
                    FILE *f = fopen(filename, "rb");
                    if(!f){
                        printf("Can't open File: %s\n", filename);
                    }
                    else{
                        while(1){
                            int got = recvfrom(client, buf, sizeof(buf)-1, 0, (struct sockaddr*)&tmp_addr, &len);
                            if(got < 0){
                                if(errno == EAGAIN || errno == EWOULDBLOCK){
                                    retry++;
                                    if(retry >= MAX_RETRIES){
                                        printf("SERVER NOT RESPONDING. CONNECTION LOST!\n");
                                        break;
                                    }
                                    if(next_block_num == 0){
                                        send_rq(client, &saddr, sizeof(saddr), OP_WRQ, filename, BIN_MODE);
                                    }
                                    else{
                                        send_data(client, &tmp_addr, sizeof(tmp_addr), next_block_num, buffer, bytes_read);
                                    }
                                    continue;
                                }
                                else{
                                    printf("RECV FAILD\n");
                                    break;
                                }
                            }
                            retry = 0;

                            int ec = get_opcode(&opcode, buf, got);
                            if(ec < 0){
                                printf("GET OPCODE FAIL\n");
                                break;
                            }
                            else{
                                if(opcode == OP_ACK){
                                    int ecc = parse_ack(buf, got, &block_num);
                                    if(ecc < 0){
                                        printf("PARSE ACK ERROR\n");
                                        break;
                                    }
                                    if(block_num == next_block_num){
                                        if(is_the_last_pkt){
                                            printf("Upload file completed\n");
                                            break;
                                        }
                                        bytes_read = fread(buffer, 1, 512, f);
                                        next_block_num = block_num+1;
                                        if(bytes_read < 512){
                                            is_the_last_pkt = 1;
                                        }
                                        send_data(client, &tmp_addr, sizeof(tmp_addr), next_block_num, buffer, bytes_read);
                                    }
                                }
                                else if(ec == OP_ERROR){
                                    int ecc = parse_err(buf, got, &error_code, &error_msg);
                                    if(ecc < 0){
                                        printf("PARSE ERROR FAIL\n");
                                    }
                                    else{
                                        printf("ERROR: [%d]: %s\n",error_code, error_msg);
                                    }
                                    break;
                                }
                                else{
                                    printf("UNDEFINED ERROR\n");
                                    break;
                                }
                            }
                        }
                        fclose(f);
                    }
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