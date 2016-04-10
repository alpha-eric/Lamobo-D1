#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/input.h>
#include "AudioSink.h"
#include "notify.h"

#define KEY_GPIO_DEV        "/dev/input/event0"
using namespace akmedia;
static bool connection_exit = false;

static void notify_handler(int conn)
{
	int ret = 0;
	do {
		int gpio = open(KEY_GPIO_DEV, O_RDONLY);
		if (gpio < 0) {
			fprintf(stderr, "open gpio dev failed.\n");
			break;
		}
		struct input_event key_event[64];
		fprintf(stderr, "input size:%d key:%d\n", sizeof(struct input_event), sizeof(key_event));
		struct notify_t notify;	
		do {
			fd_set rset;
			FD_ZERO(&rset);
			FD_SET(gpio, &rset);
			ret = select(gpio + 1, &rset, NULL, NULL, NULL);
			if (ret < 1) {
				fprintf(stderr, "select return fail.\n");
				break;
			}
			if (FD_ISSET(gpio, &rset)) {
				int e_size = read(gpio, key_event, sizeof(struct input_event) * sizeof(key_event));	
				if (e_size < sizeof(struct input_event)) {
					fprintf(stderr, "expect %d bytes, but got %d bytes\n", sizeof(struct input_event), e_size);
					break;
				}
				for (int i = 0; i < e_size; i+=sizeof(struct input_event)) {
					if (EV_KEY != key_event[i].type) {
						continue;
					}
					notify.type = htonl(key_event[i].code);
					notify.happen_sec = htonl(time_t(NULL));
					ret = send(conn, &notify, sizeof(struct notify_t), MSG_DONTWAIT | MSG_NOSIGNAL);	
					fprintf(stderr, "key:%d, size:%d\n", key_event[i].code, ret);
				}
			}
		} while (!connection_exit);
		close(gpio);
	} while (!connection_exit);
	pthread_exit(EXIT_SUCCESS);
}

void create_notify_thread(int conn)
{
	pthread_t pid;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	size_t stackSize = 8*1024;
	pthread_attr_setstacksize(&attr, stackSize);
	pthread_create(&pid, &attr, (void *(*)(void *)) notify_handler, (void *)conn);
	pthread_attr_destroy(&attr);	
}

int progress_connection(int conn)
{
	int sample_rate = 16000;
	int channels = 2;
	int sample_bits = 16;
	int volume = 80;

	int ret = 0;
	int	len = 0;
	T_U8 *buf = NULL;
	CAudioSink play;
	int max_buf_size = (sample_rate * channels * sample_bits) / 8;
	fprintf(stderr, "prepare buf %d\n", max_buf_size);
	create_notify_thread(conn);
	do {
		buf = (T_U8 *)malloc(max_buf_size);
		if (!buf) {
			fprintf(stderr, "alloc buffer failed.\n");
			break;
		}
		play.setEQ(0);
		play.setVolume(volume);
		play.setStartTime(0);
		if (play.open(sample_rate, channels, sample_bits) < 0) {
			fprintf(stderr, "open audiosink failed.\n");
			break;
		}
		play.start();
		while (!connection_exit ) {
			len = recv(conn, buf, max_buf_size, MSG_WAITALL);
			if (len == 0) {
				fprintf(stderr, "client close this connection\n");
				break;
			} else if (len < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					fprintf(stderr, "recv was interrupted by signal, do again\n");
					continue;
				} else {
					fprintf(stderr, "error: %s\n", strerror(errno));
					break;
				}
			}
			ret = play.render(buf, len);
			if (ret != 0) {
				fprintf(stderr, "render data failed.\n");
			}
		}
	} while (false);
	if (play.isOpened()) {
		play.stop();
		play.close();
	}
	if (buf) {
		free(buf);
	}
	connection_exit = true;
	fprintf(stderr, "finish one song\n");
	return 0;
}

int setupV4(const int port)
{
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in serv_addr;
	memset((unsigned char *)&serv_addr, 0x00, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	if(bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
		printf("bind4 failed: %s\n", strerror(errno));
		close(sock);
		return -1;
	}
	if(listen(sock, 1) < 0){
		printf("listen failed: %s\n", strerror(errno));
		close(sock);
		return -1;
	}
	return sock;
}

int main(int argc, const char *argv[])
{
	int port = 3702;

	char *buffer = NULL;
	int sock4 = setupV4(port);
	do{
		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(sock4, &rset);
		int maxfd = sock4 + 1;
		if(select(maxfd, &rset, NULL, NULL, 0) > 0){
			int conn = 0;
			if(FD_ISSET(sock4, &rset)){
				struct sockaddr_in client_addr;    
				socklen_t len = sizeof(client_addr);
				conn = accept(sock4, (struct sockaddr *)&client_addr, &len);    
				fprintf(stderr, "Got a ipv4 connection\n");
			}else{
				continue;
			}
			connection_exit = false;
			progress_connection(conn);
			close(conn);
			fprintf(stderr, "connection is close\n");
		}else{
			continue;
		}
	}while(true);
	if (sock4 > 0) {
		close(sock4);
	}
	if (buffer) {
		free(buffer);
	}
	return 0;
}
