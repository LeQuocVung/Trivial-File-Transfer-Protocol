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

int build_rq(char *buf, int buf_size ,opcode_t op, const char *fn, const char *mode){
    if(!buf || !fn || !mode) return -1;
    if(op != OP_RRQ && op!= OP_WRQ) return -1;
    int fn_len = strlen(fn);
    int mode_len = strlen(mode);
    if(fn_len == 0 || mode_len == 0) return -1;
    if((2 + fn_len + 1 +mode_len + 1) > buf_size) return -1;
    uint16_t opcode = htons((uint16_t)op);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, fn, fn_len + 1);
    memcpy(buf + 2 + fn_len + 1, mode, mode_len + 1);
    return (2 + fn_len + 1 + mode_len + 1);
}

int build_data(char *buf, int buf_size, uint16_t block_num, const char *data, int data_len){
    if(!buf || !data) return -1;
    if(data_len < 0 || data_len > 512) return -1;
    if(buf_size < (4 + data_len)) return -1;
    uint16_t opcode = htons((uint16_t)OP_DATA);
    uint16_t block = htons(block_num);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, &block, 2);
    memcpy(buf + 4, data, data_len);
    return 4 + data_len;
}

int build_ack(char *buf, int buf_size, uint16_t block_num){
    if(!buf || buf_size < 4) return -1;
    uint16_t opcode = (htons((uint16_t)OP_ACK));
    uint16_t block = htons(block_num);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, &block, 2);
    return 4;
}

int build_err(char *buf, int buf_size, err_code_t err_code, const char *err_msg){
    if(!buf || !err_msg) return -1;
    if(err_code < ERR_NOT_DEFINE || err_code > ERR_NO_SUCH_USER) return -1;
    uint16_t opcode = htons((uint16_t)OP_ERROR);
    uint16_t errorcode = htons((uint16_t)err_code);
    int msg_len = strlen(err_msg);
    if(buf_size < (2 + 2 + msg_len)) return -1;
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, &errorcode, 2);
    memcpy(buf + 4, err_msg, msg_len + 1);
    return 2 + 2 + msg_len + 1;
}

int parse_rq(const char *buf, int len, char *fn, int fn_len, char *mode, int mode_len){
    if(!buf || !fn || !mode) return -1;
    if(len < 6 || fn_len <= 0 || mode_len <= 0) return -1;
    uint16_t raw_code;
    memcpy(&raw_code, buf, 2);
    uint16_t opcode = ntohs(raw_code);
    if(opcode != OP_RRQ && opcode != OP_WRQ) return -1;
    int f_len = strnlen(buf + 2, len -2);
    if(f_len >= (len - 2) || f_len >= fn_len || f_len == 0) return -1;
    memcpy(fn, buf + 2, f_len + 1);
    int offset = 2 + f_len + 1;
    int m_len = strnlen(buf + offset, len - offset);
    if ((offset + m_len >= len) || (m_len >= mode_len)) return -1;
    memcpy(mode, buf + offset, m_len + 1);
    return 0;
}

int parse_ack(const char *buf, int len, uint16_t *block_num){
    if (len < 4 || !buf) return -1;
    uint16_t raw_opcode, raw_block;
    memcpy(&raw_opcode, buf, 2);
    memcpy(&raw_block, buf + 2, 2);
    if(ntohs(raw_opcode) != OP_ACK) return -1;
    if(block_num){
        *block_num = ntohs(raw_block);
    }
    return 0;
}

int parse_data(const char *buf, int len, uint16_t *block_num, const char **data_ptr, int *data_len){
    if( !buf || len < 4 || len > 516) return -1;
    uint16_t raw_opcode, raw_block;
    memcpy(&raw_opcode, buf, 2);
    memcpy(&raw_block, buf + 2, 2);
    if(ntohs(raw_opcode) != OP_DATA) return -1;
    if(block_num){
        *block_num = ntohs(raw_block);
    }
    if(data_ptr){
        *data_ptr = buf + 4;
    }
    if(data_len){
        *data_len = len -4;
    }
    return 0;
}

int parse_err(const char *buf, int len, uint16_t *err_code, const char **err_msg){
    if(!buf || len < 5) return -1;
    uint16_t raw_opcode, raw_err_code;
    memcpy(&raw_opcode, buf, 2);
    memcpy(&raw_err_code, buf + 2, 2);
    if(ntohs(raw_opcode) != OP_ERROR) return -1;
    if(err_code){
        *err_code = raw_err_code;
    }
    if(err_msg){
        *err_msg = buf + 4;
    }
    return 0;
}

int send_rq(int sockfd, struct sockaddr_in *dst, socklen_t len, opcode_t op, const char *fn, const char *mode){
    if(!dst || len < sizeof(struct sockaddr)) return -1;
    char pkt[1024];
    int pkt_len = build_rq(pkt, sizeof(pkt) ,op, fn, mode);
    if(pkt_len < 0) return -1;
    return sendto(sockfd, pkt, pkt_len, 0, (struct sockaddr*)dst, len);
}

int send_ack(int sockfd, struct sockaddr_in *dst, socklen_t len, uint16_t block_num){
    if(!dst || len < sizeof(struct sockaddr)) return -1;
    char pkt[4];
    int pkt_len = build_ack(pkt, sizeof(pkt), block_num);
    if(pkt_len < 0) return -1;
    return sendto(sockfd, pkt, pkt_len, 0, (struct sockaddr*)dst, len);
}

int send_data(int sockfd, struct sockaddr_in *dst, socklen_t len, uint16_t block_num, const char *data, int data_len){
    if(!dst || len < sizeof(struct sockaddr)) return -1;
    char pkt[MAX_PKT_SIZE];
    int pkt_len = build_data(pkt, sizeof(pkt) ,block_num, data, data_len);
    if(pkt_len < 0) return -1;
    return sendto(sockfd, pkt, pkt_len, 0, (struct sockaddr*)dst, len);
}

int send_err(int sockfd, struct sockaddr_in *dst, socklen_t len, err_code_t err_code, const char *custom_msg){
    if(!dst || len < sizeof(struct sockaddr)) return -1;
    char pkt[MAX_PKT_SIZE];
    const char *msg = (custom_msg && strlen(custom_msg) > 0) ? custom_msg : strerro(err_code);
    int pkt_len = build_err(pkt,sizeof(pkt) ,err_code, msg);
    if(pkt_len < 0) return -1;
    int ret = sendto(sockfd, pkt, pkt_len, 0, (struct sockaddr*)dst, len);
    if(ret < 0){
        perror("sendto()");
        return -1;
    }
    printf("[ERR]: %d %s\n", err_code, msg);
    return ret;
}

