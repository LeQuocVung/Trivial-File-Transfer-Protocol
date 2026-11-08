CC = gcc
CFLAGS = -Wall -Wextra -I.
LDFLAGS = -lpthread

SERVER_BIN = server/server
CLIENT_BIN = client/client

COMMON_SRC = common/common.c
SERVER_SRC = server/server.c $(COMMON_SRC)
CLIENT_SRC = client/client.c $(COMMON_SRC)

SERVICE_NAME = tftp-server.service
SYSTEMD_DIR = /etc/systemd/system
CUR_DIR = $(shell pwd)
SERVICE_USER = tftpuser

all: $(SERVER_BIN) $(CLIENT_BIN) dirs
$(SERVER_BIN): $(SERVER_SRC)
		$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)
$(CLIENT_BIN): $(CLIENT_SRC)
		$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC)

dirs:
		mkdir -p server/serverstorage
		mkdir -p client/clientstorage
install:
		-sudo useradd -r -s /bin/false $(SERVICE_USER) 2>/dev/null || true
		sudo chown -R $(SERVICE_USER):$(SERVICE_USER) $(CUR_DIR)/server/serverstorage
		sed -e 's|@EXEC_PATH@|$(CUR_DIR)/$(SERVER_BIN)|g' \
			-e 's|@WORKING_DIR@|$(CUR_DIR)|g' \
			-e 's|@USER@|$(SERVICE_USER)|g' \
			tftp-server.service.in > $(SERVICE_NAME)
		sudo cp $(SERVICE_NAME) $(SYSTEMD_DIR)/
		sudo systemctl daemon-reload
		sudo systemctl enable $(SERVICE_NAME)
		sudo systemctl restart $(SERVICE_NAME)

uninstall:
		-sudo systemctl stop $(SERVICE_NAME)
		-sudo systemctl disable $(SERVICE_NAME)
		sudo rm -f $(SYSTEMD_DIR)/$(SERVICE_NAME)
		rm -f $(SERVICE_NAME)

clean:
		rm -f $(SERVER_BIN) $(CLIENT_BIN)
		rm -f client/test_cli

.PHONY: all dirs clean install uninstall
