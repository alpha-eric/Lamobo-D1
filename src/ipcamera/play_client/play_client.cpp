#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "notify.h"

#define SERVER_ADDR "192.168.0.1"
#define SERVER_PORT 3702

int setupV4(const char *ip, int port)
{
     int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
     struct sockaddr_in serv_addr;
     memset((unsigned char *)&serv_addr, 0x00, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_port = htons(port);
     inet_pton(AF_INET, ip, &serv_addr.sin_addr);
     if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
          fprintf(stderr, "ipv4 connect failed:%s\n", strerror(errno));
          close(sock);
          sock = -1;
     }
     return sock;
}

int main(int argc, const char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "play_client ip test.pcm\n");
		return EXIT_FAILURE;
	}
	int sample_rate = 16000;
	int channels = 2;
	int sample_bits = 16;

	int max_buf_size = (sample_rate * channels * sample_bits);
	fprintf(stderr, "prepare buf %d\n", max_buf_size);
	FILE *fp = NULL;
	int sock4 = 0;
	char *buf = NULL;
	int ret = 0;
	int len = 0;
	struct notify_t notify;
	do {
		sock4 = setupV4(argv[1], SERVER_PORT);
		if (sock4 < 0) {
			fprintf(stderr, "setup socket failed.\n");
			break;
		}
		fprintf(stderr, "connect server done\n");
		buf = (char *)malloc(max_buf_size);
		if (!buf) {
			fprintf(stderr, "calloc failed.\n");
			break;
		}
		fp = fopen(argv[2], "r");
		if (!fp) {
			fprintf(stderr, "open failed.\n");
			break;
		}
		fprintf(stderr, "open file done\n");
		while ((len = fread(buf, 1, max_buf_size, fp)) > 0) {
			int offset = 0;
			fprintf(stderr, "send %d\n", len);
			do {
				ret = send(sock4, buf + offset, len, 0);	
				if (ret < 0) {
					fprintf(stderr, "send data failed.\n");
					break;
				}
				len -= ret;
				offset += ret;
			} while(len > 0);
			while (recv(sock4, &notify, sizeof(struct notify_t), MSG_DONTWAIT) > 0) {
				notify.type = ntohl(notify.type);
				notify.happen_sec = ntohl(notify.happen_sec);
				fprintf(stderr, "key event: %d\n", notify.type);
			}
		}
	} while (false);
	if (sock4 > 0) {
		close(sock4);
	}
	if (fp) {
		fclose(fp);
	}
	if (buf) {
		free(buf);
	}

	return EXIT_SUCCESS;
}
