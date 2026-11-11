#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdint.h>
#include<pthread.h>
#include "../common/common.h"
#include "../common/config.h"
#include<sys/socket.h>
#include<sys/types.h>
#include<sys/file.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<errno.h>

typedef struct {
    struct sockaddr_in caddr;
    char buf[1024];
    int buf_len;
}ThreadArgs;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static int get_opcode(const char *buf, uint16_t *opcode, int buf_size){
    if(buf_size < 2) return -1;
    if(!buf) return -1;
    uint16_t raw_opcode;
    memcpy(&raw_opcode, buf, 2);
    *opcode = ntohs(raw_opcode);
    return 0;
}

void *handle_client(void *arg){
    pthread_detach(pthread_self());
    ThreadArgs *client = (ThreadArgs*)arg;
    struct sockaddr_in caddr = client->caddr;
    char buf[1024];
    memcpy(buf, client->buf, client->buf_len);
    int buf_size = client->buf_len;
    free(client);
    int client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(client_fd < 0){
        printf("CREATE CLIENT SOCKET FAIL\n");
        pthread_exit(NULL);
    }
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    uint16_t opcode;
    int ec = get_opcode(buf, &opcode, buf_size);
    if(ec < 0){
        printf("GET OPCODE FAIL\n");
        send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, "IVALID PACKET HEADER\n");
    }
    else{
        char filename[512];
        char mode[15];
        if(opcode == OP_RRQ){
            int ec = parse_rq(buf, buf_size, filename, sizeof(filename), mode, sizeof(mode));
            if(ec < 0){
                printf("PARSE REQUEST FAIL\n");
                send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, "MALFORMED RRQ PACKET\n");
            }
            else{
                if (strstr(filename, "..") != NULL || filename[0] == '/') {
                    send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, "Access violation: Invalid path");
                    pthread_exit(NULL);
                }
                if (strcmp(filename, "log.txt") == 0) {
                    send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, "Access violation: File log is protected");
                    pthread_exit(NULL);
                }
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "server/serverstorage/%s", filename);
                pthread_mutex_lock(&lock);
                printf("\n[SERVER] RRQ: file '%s' from %s:%d\n",filename, inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
                pthread_mutex_unlock(&lock);
                FILE *f = fopen(filepath, "rb");
                if(!f){
                    if(errno == ENOENT){
                        send_err(client_fd, &caddr, sizeof(caddr), ERR_FILE_NOT_FOUND, NULL);
                    }
                    else if(errno == EACCES){
                        send_err(client_fd, &caddr, sizeof(caddr), ERR_ACCESS_VIOLATION, NULL);
                    }
                    else{
                        send_err(client_fd, &caddr, sizeof(caddr), ERR_NOT_DEFINE, NULL);
                    }
                }
                else{
                    int fd = fileno(f);
                    flock(fd, LOCK_SH);
                    char dbuf[512];
                    uint16_t block_num = 1;
                    int bytes_read;
                    int is_complete = 0;
                    struct sockaddr_in faddr;
                    int len = sizeof(faddr);
                    while(1){
                        bytes_read = fread(dbuf, 1, 512, f);
                        if(bytes_read < 512) is_complete = 1;
                        send_data(client_fd, &caddr, sizeof(caddr), block_num, dbuf, bytes_read);
                        int retry = 0;
                        int ack_success = 0;
                        uint16_t opcode;
                        uint16_t recv_block;
                        while(1){
                            int got = recvfrom(client_fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&faddr, &len);
                            if(got < 0){
                                if(errno == EAGAIN || errno == EWOULDBLOCK){
                                    retry++;
                                    if(retry >= MAX_RETRIES){
                                        pthread_mutex_lock(&lock);
                                        printf("[SERVER] %s:%d TIMEOUT\n", inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
                                        pthread_mutex_unlock(&lock);
                                        flock(fd, LOCK_UN);
                                    }
                                    else{
                                        send_data(client_fd, &caddr, sizeof(caddr), block_num, dbuf, bytes_read);
                                        continue;
                                    }
                                }
                                break;
                            }
                            else{
                                if(faddr.sin_addr.s_addr != caddr.sin_addr.s_addr || faddr.sin_port != caddr.sin_port){
                                    send_err(client_fd, &faddr, sizeof(faddr), ERR_UNKNOWN_TRANSFER_ID, NULL);
                                    continue;
                                }
                                else{
                                    int ecc = get_opcode(buf, &opcode, got);
                                    if(ecc < 0){
                                        printf("GET OPCODE FAIL\n");
                                        break;
                                    }
                                    else{
                                        if(opcode == OP_ACK){
                                            parse_ack(buf, got, &recv_block);
                                            if(recv_block == block_num){
                                                ack_success = 1;
                                                block_num++;
                                                break;
                                            }
                                        }
                                        else if(opcode == OP_ERROR){
                                            flock(fd, LOCK_UN);
                                        }
                                        else{
                                            send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, "UNEXPECTED ACK PKT NUM\n");
                                            flock(fd, LOCK_UN);
                                        }
                                    }      
                                }
                            }
                        }
                        if(ack_success == 0) break;
                        if(is_complete == 1){
                            pthread_mutex_lock(&lock);
                            printf("[SERVER] DONE %s:%d\n",inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
                            pthread_mutex_unlock(&lock);
                            break;
                        }
                    }
                    flock(fd, LOCK_UN);
                    fclose(f);
                }
            }
        }
        else if(opcode == OP_WRQ){
            if(parse_rq(buf, buf_size, filename, sizeof(filename), mode, sizeof(mode)) < 0){
                send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, NULL);
            }
            else{
                pthread_mutex_lock(&lock);
                printf("\n[SERVER] WRQ '%s':'%d'\n",inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
                pthread_mutex_unlock(&lock);
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "server/serverstorage/%s", filename);
                FILE *f = fopen(filepath, "wb+");
                if(!f){
                    if(errno == EACCES){
                        send_err(client_fd, &caddr, sizeof(caddr), ERR_ACCESS_VIOLATION, NULL);
                    }
                    else {
                        send_err(client_fd, &caddr, sizeof(caddr), ERR_NOT_DEFINE, NULL);
                    }
                }
                else{
                    int fd = fileno(f);
                    if(flock(fd, LOCK_EX | LOCK_NB) < 0){
                        pthread_mutex_lock(&lock);
                        printf("[SERVER] File đang bị chiếm bởi tiến trình khác!\n");
                        pthread_mutex_unlock(&lock);
                        send_err(client_fd, &caddr, sizeof(caddr), ERR_FILE_EXISTS, NULL);
                    }
                    else{
                        uint16_t next_block_num = 1;
                        int retry = 0;
                        send_ack(client_fd, &caddr, sizeof(caddr), 0);
                        while(1){
                            struct sockaddr_in faddr;
                            int len = sizeof(faddr);
                            int got = recvfrom(client_fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&faddr, &len);
                            if(got < 0){
                                if(errno == EAGAIN || errno == EWOULDBLOCK){
                                    retry++;
                                    if(retry >= MAX_RETRIES){
                                        pthread_mutex_lock(&lock);
                                        printf("[SERVER] TIMEOUT!\n");
                                        pthread_mutex_unlock(&lock);
                                        break;
                                    }
                                    else{
                                        send_ack(client_fd, &caddr, sizeof(caddr), next_block_num-1);
                                        continue;
                                    }
                                }
                                break;
                            }
                            if(faddr.sin_addr.s_addr != caddr.sin_addr.s_addr || faddr.sin_port != caddr.sin_port){
                                send_err(client_fd, &faddr, sizeof(faddr), ERR_UNKNOWN_TRANSFER_ID, NULL);
                                continue;
                            }
                            retry = 0;
                            uint16_t opcode, block_nums;
                            const char *data_ptr;
                            int data_len;
                            int ec = get_opcode(buf, &opcode, got);
                            if(ec < 0){
                                printf("GET OPCODE FAIL\n");
                                break;
                            }
                            else{
                                if(opcode == OP_DATA){
                                    int ecc = parse_data(buf, got, &block_nums, &data_ptr, &data_len);
                                    if(ecc < 0){
                                        printf("PARSE DATA FAIL\n");
                                        break;
                                    }
                                    else{
                                        if(block_nums == next_block_num){
                                            if(data_len > 0){
                                                int bytes_write = fwrite(data_ptr, 1, data_len, f);
                                                if(bytes_write < data_len){
                                                    send_err(client_fd, &caddr, sizeof(caddr), ERR_DISK_FULL, NULL);
                                                    flock(fd, LOCK_UN);
                                                    unlink(filename);
                                                    break;
                                                }
                                            }
                                            send_ack(client_fd, &caddr, sizeof(caddr), block_nums);
                                            next_block_num++;
                                            if(data_len < 512){
                                                pthread_mutex_lock(&lock);
                                                printf("[SERVER] WRQ DONE %s:%d\n",inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
                                                pthread_mutex_unlock(&lock);
                                                break;
                                            }
                                        }
                                        else if(block_nums == next_block_num -1){
                                            send_ack(client_fd, &caddr, sizeof(caddr), block_nums);
                                        }
                                    }
                                }
                                else if(opcode == OP_ERROR){
                                    pthread_mutex_lock(&lock);
                                    printf("[SERVER] CLIENT ERROR\n");
                                    pthread_mutex_unlock(&lock);
                                    break;
                                }
                                else{
                                    send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, NULL);
                                    break;
                                }
                            }
                        }
                    }
                    flock(fd, LOCK_UN);
                    fclose(f);
                }
            }
        }
        else{
            send_err(client_fd, &caddr, sizeof(caddr), ERR_ILLEGAL_OPERATION, NULL);
        }
    }
    close(client_fd);
    pthread_exit(NULL);
}

int main(){
    int server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(server != -1){
        struct sockaddr_in saddr;
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(PORT);
        saddr.sin_addr.s_addr = INADDR_ANY;
        int ec = bind(server, (struct sockaddr*)&saddr, sizeof(saddr));
        if(ec >= 0){
            while(1){
                ThreadArgs *args = (ThreadArgs*)malloc(sizeof(ThreadArgs));
                if(!args){
                    printf("MALLOC FAIL");
                }
                else{
                    int len = sizeof(struct sockaddr_in);
                    int got = recvfrom(server, args->buf, sizeof(args->buf), 0, (struct sockaddr*)&args->caddr, &len);
                    if(got < 0){
                        printf("RECV FAIL\n");
                        free(args);
                    }
                    else{
                        args->buf_len = got;
                        pthread_t tid;
                        if(pthread_create(&tid, NULL, handle_client, (void*)args) != 0){
                            printf("PTHREAD_CREATE FAIL\n");
                            free(args);
                        }
                    }
                }
            }
        }
        else{
            printf("BIND FAIL\n");
        }
        close(server);
        pthread_mutex_destroy(&lock);
    }
    else{
        printf("SOCKET FAIL\n");
    }

    return 0;
}