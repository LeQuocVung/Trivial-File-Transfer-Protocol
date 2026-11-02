#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "common.h"

static const char *err_messages[] = {
    "Undefined",
    "File not found",
    "Access violation",
    "Disk full or allocation exceeded",
    "Illegal operation",
    "Unknown transfer Id",
    "File already exists",
    "No such user"
};

const char* strerro(err_code_t err_code){
    if(err_code >= ERR_NOT_DEFINE && err_code <= ERR_NO_SUCH_USER){
        return err_messages[err_code];
    }
    return err_messages[ERR_NOT_DEFINE];
}

int build_rq(char *buf, opcode_t op, const char *fn, const char *mode){
    uint16_t opcode = htons((uint16_t)op);
    int fn_len = strlen(fn);
    int mode_len = strlen(mode);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, fn, fn_len + 1);
    memcpy(buf + 2 + fn_len + 1, mode, mode_len + 1);
    return (2 + fn_len + 1 + mode_len + 1);
}

int build_data(char *buf, uint16_t block_num, const char *data, int data_len){
    uint16_t opcode = htons((uint16_t)OP_DATA);
    uint16_t block = htons(block_num);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, &block, 2);
    if(data && data_len > 0){
        memcpy(buf + 4, data, data_len);
    }
    return 4 + data_len;
}

int build_ack(char *buf, uint16_t block_num){
    uint16_t opcode = (htons((uint16_t)OP_ACK));
    uint16_t block = htons(block_num);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, &block, 2);
    return 4;
}

int build_err(char *buf, err_code_t err_code, const char *err_msg){
    uint16_t opcode = htons((uint16_t)OP_ERROR);
    uint16_t errorcode = htons((uint16_t)err_code);
    int msg_len = strlen(err_msg);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, &err_code, 2);
    memcpy(buf + 4, err_msg, msg_len + 1);
    return 2 + 2 + msg_len + 1;
}

int parse_rq(const char *buf, int len, char *fn, int fn_len, char *mode, int mode_len){
    if(len < 4) return -1;
    int f_len = strnlen(buf + 2, len -2);
    if(f_len >= len - 2 || f_len >= fn_len) return -1;
    strncpy(fn, buf + 2, fn_len);
    fn[fn_len] = '\0';
    int offset = 2 + f_len + 1;
    int m_len = strnlen(buf + offset, len - offset);
    if ((offset + m_len >= len) || (m_len >= mode_len)) return -1;
    strncpy(mode, buf + offset, mode_len);
    mode[mode_len] = '\0';
    return 0;
}

void send_err(int sockfd, struct sockaddr_in *dst, socklen_t addr_len, err_code_t err_code, const char *custom_msg){
    char pkt[MAX_PKT_SIZE];
    const char *msg = (custom_msg && strlen(custom_msg) > 0) ? custom_msg : strerro(err_code);
    int pkt_len = build_err(pkt, err_code, msg);
    sendto(sockfd, pkt, pkt_len, 0, (struct sockaddr*)dst, addr_len);
    printf("[ERR]: %d %s\n", err_code, msg);
}