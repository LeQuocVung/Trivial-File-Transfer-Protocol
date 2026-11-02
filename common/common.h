#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "config.h"

typedef enum {
    OP_RRQ = 1,
    OP_WRQ = 2,
    OP_DATA = 3,
    OP_ACK = 4,
    OP_ERROR = 5
} opcode_t; 

typedef enum {
    ERR_NOT_DEFINE = 0,
    ERR_FILE_NOT_FOUND = 1,
    ERR_ACCESS_VIOLATION = 2,
    ERR_DISK_FULL = 3,
    ERR_ILLEGAL_OPERATION = 4,
    ERR_UNKNOWN_TRANSFER_ID = 5,
    ERR_FILE_EXISTS = 6,
    ERR_NO_SUCH_USER = 7
} err_code_t;

const char* strerro(err_code_t err_code);
int build_rq(char *buf, opcode_t op, const char *fn, const char *mode);
int build_data(char *buf, uint16_t block_num, const char *data, int data_len);
int build_ack(char *buf, uint16_t block_num);
int build_err(char *buf, err_code_t err_code, const char *err_msg);

int parse_rq(const char *buf, int len, char *fn, int fn_len, char *mode, int mode_len);

void send_err(int sockfd, struct sockaddr_in *dst, socklen_t addr_len, err_code_t err_code, const char *custom_msg);

#endif