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
/**
 * @brief Đóng gói yêu cầu đọc hoặc ghi vào buf
 * @return Trả về số bytes dữ liệu đã được đóng gói. Nếu có lỗi trả về giá trị -1.
 */
int build_rq(char *buf, int buf_size, opcode_t op, const char *fn, const char *mode);

/**
 * @brief Đóng gói dữ liệu vào buf
 * @return Trả về số bytes dữ liệu đã được đóng gói. Nếu có lỗi trả về giá trị -1.
 */
int build_data(char *buf, int buf_size, uint16_t block_num, const char *data, int data_len);

/**
 * @brief Đóng gói acknowlegdment vào buf
 * @return Trả về số bytes dữ liệu đã được đóng gói. Nếu có lỗi trả về giá trị -1.
 */
int build_ack(char *buf, int buf_size, uint16_t block_num);


/**
 * @brief Đóng gói error vào buf
 * @return Trả về số bytes dữ liệu đã được đóng gói. Nếu có lỗi trả về giá trị -1.
 */
int build_err(char *buf, int buf_size, err_code_t err_code, const char *err_msg);

/**
 * @brief Tách gói request từ buf vào filename và mode
 * @return Trả về 0 nếu thành công, -1 nếu có lỗi xảy ra.
 */
int parse_rq(const char *buf, int len, char *fn, int fn_len, char *mode, int mode_len);

/**
 * @brief Tách acknowlegdment từ buf vào block number.
 * @return Trả về 0 nếu thành công, -1 nếu có lỗi xảy ra.
 */
int parse_ack(const char *buf, int len, uint16_t *block_num);

/**
 * @brief Tách data từ buf vào block number, data pointer, data len
 * @return Trả về 0 nếu thành công, -1 nếu có lỗi xảy ra.
 */
int parse_data(const char *buf, int len, uint16_t *block_num, const char **data_ptr, int *data_len);

/**
 * @brief Tách gói lỗi từ buf vào error code, error msg
 * @return Trả về 0 nếu thành công, -1 nếu có lỗi xảy ra.
 */
int parse_err(const char *buf, int len, uint16_t *err_code, const char **err_msg);

int send_rq(int sockfd, struct sockaddr_in *dst, socklen_t len, opcode_t op, const char *fn, const char *mode);

int send_ack(int sockfd, struct sockaddr_in *dst, socklen_t len, uint16_t block_num);

int send_data(int sockfd, struct sockaddr_in *dst, socklen_t len, uint16_t block_num, const char *data, int data_len);

int send_err(int sockfd, struct sockaddr_in *dst, socklen_t addr_len, err_code_t err_code, const char *custom_msg);

const char *strerro(err_code_t err_code);

#endif