# TFTP Server và Client trên Linux (C, UDP, đa luồng)

## 1. Giới thiệu

Dự án này là một bản cài đặt giao thức TFTP (Trivial File Transfer Protocol) theo RFC 1350, viết bằng ngôn ngữ C, chạy trên Linux. Dự án gồm hai thành phần:

- Server: lắng nghe yêu cầu trên cổng UDP 6969, xử lý đồng thời nhiều client bằng cách tạo một luồng (pthread) cho mỗi yêu cầu.
- Client: chương trình dòng lệnh tương tác, cho phép tải file từ server về (RRQ) và đẩy file lên server (WRQ).

Các đặc điểm chính:

- Truyền file ở chế độ nhị phân (octet), kích thước khối dữ liệu cố định 512 byte.
- Cơ chế gửi lại (retransmission) khi quá thời gian chờ 3 giây, giới hạn số lần thử lại bằng MAX_RETRIES.
- Mỗi giao dịch dùng một socket riêng (Transfer ID riêng), server từ chối gói tin đến từ địa chỉ/cổng lạ bằng lỗi "Unknown transfer ID".
- Khóa file bằng flock: nhiều client có thể đọc cùng lúc (khóa chia sẻ), nhưng chỉ một client được ghi (khóa độc quyền).
- Chặn đường dẫn nguy hiểm trong RRQ (chứa "..", bắt đầu bằng "/") và bảo vệ file log.txt.
- Có thể cài đặt thành dịch vụ systemd, tự khởi động cùng hệ thống và tự khởi động lại khi gặp sự cố.

## 2. Cấu trúc thư mục

```
.
├── Makefile                    Biên dịch, cài đặt, gỡ cài đặt
├── README.md                   Tài liệu này
├── tftp-server.service.in      Mẫu file dịch vụ systemd
├── common
│   ├── common.c                Hàm dùng chung: tạo/phân tích gói RRQ, WRQ, DATA, ACK, ERROR
│   ├── common.h                Khai báo hàm, opcode, mã lỗi
│   └── config.h                Cấu hình: PORT, MAX_RETRIES, BIN_MODE...
├── server
│   ├── server.c                Mã nguồn server
│   └── serverstorage           Thư mục lưu file của server
└── client
    ├── client.c                Mã nguồn client
    └── clientstorage           Thư mục lưu file của client
```

## 3. Kiến thức cơ bản về TFTP

### 3.1. Tổng quan

TFTP là giao thức truyền file đơn giản, chạy trên UDP (cổng chuẩn là 69; dự án này dùng cổng 6969 để không cần quyền root khi chạy). Giao thức không có xác thực, không có liệt kê thư mục, chỉ hỗ trợ hai thao tác: đọc file và ghi file. Vì UDP không đảm bảo độ tin cậy, TFTP tự xử lý độ tin cậy ở tầng ứng dụng theo cơ chế "dừng và chờ" (stop-and-wait): mỗi gói DATA phải được ACK trước khi gửi gói tiếp theo.

### 3.2. Các loại gói tin

| Opcode | Tên   | Ý nghĩa                                   |
|--------|-------|-------------------------------------------|
| 1      | RRQ   | Yêu cầu đọc file (tải về)                 |
| 2      | WRQ   | Yêu cầu ghi file (tải lên)                |
| 3      | DATA  | Gói dữ liệu, kèm số thứ tự khối           |
| 4      | ACK   | Xác nhận đã nhận khối                     |
| 5      | ERROR | Báo lỗi, kết thúc giao dịch               |

Định dạng gói tin (tất cả số nguyên 16 bit theo thứ tự byte mạng - big endian):

```
RRQ/WRQ : | opcode (2) | filename | 0 | mode | 0 |
DATA    : | opcode (2) | block# (2) | data (0..512 byte) |
ACK     : | opcode (2) | block# (2) |
ERROR   : | opcode (2) | errorcode (2) | errmsg | 0 |
```

### 3.3. Mã lỗi

| Mã | Ý nghĩa                      |
|----|------------------------------|
| 0  | Lỗi không xác định           |
| 1  | Không tìm thấy file          |
| 2  | Vi phạm quyền truy cập       |
| 3  | Đĩa đầy                      |
| 4  | Thao tác không hợp lệ        |
| 5  | Transfer ID không xác định   |
| 6  | File đã tồn tại              |
| 7  | Không có người dùng như vậy  |

### 3.4. Transfer ID (TID)

Client gửi yêu cầu ban đầu tới cổng chính của server (6969). Server không trả lời từ cổng này mà tạo một socket mới với cổng ngẫu nhiên, dùng cổng đó để trao đổi dữ liệu với client trong suốt giao dịch. Cặp (địa chỉ IP, cổng) ở hai phía chính là TID. Nếu có gói tin tới từ một TID khác, bên nhận phải trả lời lỗi số 5 và bỏ qua gói đó, không làm hỏng giao dịch đang diễn ra.

### 3.5. Điều kết thúc truyền

Mỗi gói DATA mang tối đa 512 byte. Gói DATA có kích thước nhỏ hơn 512 byte (kể cả 0 byte) là gói cuối cùng. Nếu file có kích thước chia hết cho 512, bên gửi phải gửi thêm một gói DATA rỗng để báo kết thúc.

## 4. Kịch bản hoạt động

### 4.1. Kịch bản tải file về (RRQ)

```
Client                                  Server
  |  RRQ(filename, octet)  --------->  cổng 6969
  |                                     tạo thread + socket mới (TID mới)
  |  <---------  DATA(block 1)          mở file, khóa chia sẻ (LOCK_SH)
  |  ACK(1)  ------------------------>
  |  <---------  DATA(block 2)
  |  ACK(2)  ------------------------>
  |             ...
  |  <---------  DATA(block N, < 512 byte)
  |  ACK(N)  ------------------------>  kết thúc, mở khóa, đóng file
```

Các tình huống bất thường:

- File không tồn tại: server trả ERROR mã 1.
- Không có quyền đọc: server trả ERROR mã 2.
- Tên file chứa ".." hoặc bắt đầu bằng "/": server từ chối với ERROR "Access violation".
- Gói DATA hoặc ACK bị mất: sau 3 giây không nhận được phản hồi, bên gửi gửi lại gói gần nhất, tối đa MAX_RETRIES lần rồi bỏ cuộc.
- Gói trùng lặp (client nhận lại block cũ): client gửi lại ACK của block đó, không ghi dữ liệu lần hai.

### 4.2. Kịch bản đẩy file lên (WRQ)

```
Client                                  Server
  |  WRQ(filename, octet)  --------->  cổng 6969
  |                                     tạo thread + socket mới (TID mới)
  |                                     mở file, khóa độc quyền (LOCK_EX)
  |  <---------  ACK(0)
  |  DATA(1)  ----------------------->
  |  <---------  ACK(1)
  |  DATA(2)  ----------------------->
  |  <---------  ACK(2)
  |             ...
  |  DATA(N, < 512 byte)  ----------->  ghi nốt, kết thúc
  |  <---------  ACK(N)
```

Các tình huống bất thường:

- File đang bị tiến trình khác khóa: server trả ERROR (mã 6).
- Ghi đĩa thất bại: server trả ERROR mã 3 (đĩa đầy).
- Không nhận được DATA đúng hạn: server gửi lại ACK gần nhất, tối đa MAX_RETRIES lần.

### 4.3. Xử lý đồng thời trên server

Luồng chính của server chỉ làm một việc: nhận gói yêu cầu đầu tiên trên cổng 6969, sao chép nội dung gói và địa chỉ client vào một cấu trúc ThreadArgs, rồi tạo một luồng detached để xử lý toàn bộ giao dịch. Nhờ vậy nhiều client tải và đẩy file cùng lúc mà không chặn nhau. Mutex được dùng để việc in log từ nhiều luồng không bị lẫn lộn; flock được dùng để bảo vệ tính nhất quán của file.

## 5. Yêu cầu hệ thống

- Hệ điều hành Linux có systemd (Ubuntu, Debian, CentOS, Fedora...).
- Trình biên dịch gcc, công cụ make.
- Thư viện pthread (có sẵn trong glibc).
- Quyền sudo để cài dịch vụ.

Cài công cụ trên Ubuntu/Debian:

```
sudo apt update
sudo apt install -y build-essential
```

## 6. Hướng dẫn biên dịch

Lưu ý: dự án này dùng Makefile (GNU Make), không dùng CMake. Nếu bạn muốn chuyển sang CMake, xem mục 10 để có file CMakeLists.txt mẫu.

### 6.1. Biên dịch toàn bộ

Chạy tại thư mục gốc của dự án:

```
make
```

Lệnh trên sẽ:

- Biên dịch server thành server/server (liên kết với -lpthread).
- Biên dịch client thành client/client.
- Tạo hai thư mục server/serverstorage và client/clientstorage nếu chưa có.

Cờ biên dịch đang dùng: -Wall -Wextra -I. Nên đọc kỹ các cảnh báo (warning) nếu có.

### 6.2. Dọn dẹp

```
make clean
```

Lệnh này xóa hai file thực thi server/server và client/client.

### 6.3. Chạy thử trên máy cục bộ

Bắt buộc chạy từ thư mục gốc của dự án, vì đường dẫn lưu file trong mã nguồn là đường dẫn tương đối (server/serverstorage/..., client/clientstorage/...).

Cửa sổ terminal thứ nhất (server):

```
./server/server
```

Cửa sổ terminal thứ hai (client):

```
./client/client 127.0.0.1
```

Giao diện client:

```
RRQ or WRQ? (1 or 2) (0 to quit):
```

- Nhập 1: tải file từ server. Sau đó nhập tên file, file được lưu vào client/clientstorage/.
- Nhập 2: đẩy file lên server. Sau đó nhập tên file, file phải có sẵn trong client/clientstorage/.
- Nhập 0: thoát.

Ví dụ: file emmoilanguoiyeuanh.txt có sẵn trong cả hai thư mục dùng để thử nghiệm.

## 7. Hướng dẫn cài đặt lên server (systemd)

### 7.1. Các bước cài đặt

1. Đưa mã nguồn lên máy chủ (git clone hoặc scp) và di chuyển vào thư mục dự án.
2. Biên dịch:

```
make
```

3. Cài đặt dịch vụ:

```
make install
```

Mục tiêu install thực hiện lần lượt:

- Tạo người dùng hệ thống tftpuser (không có shell đăng nhập) để chạy dịch vụ với quyền hạn tối thiểu.
- Chuyển quyền sở hữu thư mục server/serverstorage cho tftpuser.
- Sinh file tftp-server.service từ mẫu tftp-server.service.in, thay các biến @EXEC_PATH@, @WORKING_DIR@, @USER@ bằng giá trị thực.
- Sao chép file dịch vụ vào /etc/systemd/system/.
- Nạp lại systemd, bật tự khởi động cùng hệ thống và khởi động dịch vụ.

4. Kiểm tra trạng thái:

```
sudo systemctl status tftp-server.service
```

### 7.2. Quản lý dịch vụ

```
sudo systemctl start tftp-server.service
sudo systemctl stop tftp-server.service
sudo systemctl restart tftp-server.service
sudo systemctl status tftp-server.service
sudo journalctl -u tftp-server.service -f
```

Lệnh cuối dùng để xem log theo thời gian thực.

### 7.3. Gỡ cài đặt

```
make uninstall
```

Lệnh này dừng dịch vụ, tắt tự khởi động, xóa file dịch vụ khỏi systemd. Người dùng tftpuser và dữ liệu trong serverstorage không bị xóa; nếu muốn xóa người dùng:

```
sudo userdel tftpuser
```

### 7.4. Mở cổng tường lửa

Server dùng cổng UDP 6969. Nếu máy chủ bật tường lửa, cần mở cổng này.

Với ufw:

```
sudo ufw allow 6969/udp
```

Với firewalld:

```
sudo firewall-cmd --permanent --add-port=6969/udp
sudo firewall-cmd --reload
```

Lưu ý quan trọng: do mỗi giao dịch dùng một cổng ngẫu nhiên phía server để gửi dữ liệu, tường lửa phải cho phép lưu lượng UDP phản hồi (trạng thái ESTABLISHED/RELATED) từ máy chủ ra ngoài. Cấu hình mặc định của ufw và firewalld thường đã đáp ứng.

### 7.5. Kiểm tra từ máy khác

Cài client TFTP có sẵn của hệ điều hành (ví dụ tftp-hpa) để kiểm tra tương thích:

```
sudo apt install -y tftp-hpa
tftp <IP_may_chu> 6969
tftp> binary
tftp> get emmoilanguoiyeuanh.txt
tftp> put ten_file_khac.txt
tftp> quit
```

Hoặc dùng client của dự án:

```
./client/client <IP_may_chu>
```

## 8. Cấu hình

Các tham số nằm trong common/config.h:

- PORT: cổng UDP của server (mặc định 6969).
- MAX_RETRIES: số lần gửi lại tối đa trước khi hủy giao dịch.
- BIN_MODE: chuỗi chế độ truyền ("octet").

Thời gian chờ mỗi lần nhận là 3 giây, đặt bằng SO_RCVTIMEO trong server.c và client.c. Sau khi thay đổi cấu hình, biên dịch lại và khởi động lại dịch vụ:

```
make
sudo systemctl restart tftp-server.service
```

## 9. Bảo mật và hạn chế đã biết

TFTP vốn không có xác thực và không mã hóa, nên chỉ nên dùng trong mạng nội bộ tin cậy (mạng LAN, mạng cấu hình thiết bị, PXE boot...). Không nên mở ra Internet.

Những điểm đã có trong mã nguồn:

- Kiểm tra đường dẫn ".." và "/" ở RRQ.
- Dịch vụ chạy bằng người dùng tftpuser, không phải root.
- Khóa file bằng flock.

Những điểm nên cải thiện (đã quan sát trong mã nguồn):

1. Yêu cầu WRQ chưa kiểm tra tên file (chặn ".." và "/") như RRQ. Kẻ tấn công có thể ghi ra ngoài thư mục serverstorage. Cần dùng cùng một hàm kiểm tra tên file cho cả hai loại yêu cầu và cũng nên chặn log.txt ở WRQ.
2. Trong handle_client, nhánh kiểm tra đường dẫn của RRQ dùng "return;" trong hàm trả về void*, đồng thời gọi send_error (khác với send_err ở các chỗ còn lại). Nên thống nhất tên hàm và thay bằng "goto cleanup" hoặc "return NULL" sau khi đóng client_fd.
3. Khi ghi đĩa thất bại, server gọi unlink(filename) thay vì unlink(filepath), nên file dở dang không bị xóa đúng chỗ.
4. Trong client ở nhánh WRQ, điều kiện nhận lỗi đang là "ec == OP_ERROR" (ec là mã trả về của get_opcode) thay vì "opcode == OP_ERROR", nên gói lỗi từ server không bao giờ được nhận diện.
5. Trong server ở nhánh RRQ, khi nhận gói ERROR hoặc gói lạ từ client, vòng lặp chưa thoát đúng cách (chưa break), dễ gây vòng lặp chờ cho đến hết thời gian.
6. Server mở file ghi bằng chế độ "wb+" trước khi giành được khóa độc quyền, nghĩa là file có thể bị cắt rỗng ngay cả khi sau đó từ chối vì đang bị khóa. Nên mở bằng open() với O_CREAT mà không O_TRUNC, khóa xong mới ftruncate.
7. Chưa có cơ chế giới hạn số luồng đồng thời, một lượng lớn yêu cầu có thể làm cạn tài nguyên. Nên đặt giới hạn hoặc dùng thread pool.
8. Client chưa kiểm tra TID của gói trả về trong giao dịch tải xuống và đẩy lên (chỉ nhận mọi gói). Nên so sánh địa chỉ nguồn sau gói đầu tiên.
9. Mutex chỉ dùng cho printf; nên dùng thêm ghi log vào file hoặc syslog thay vì in ra stdout.

## Giấy phép

Dự án được phát hành theo giấy phép MIT. Xem file LICENSE để biết chi tiết.