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
#include "AudioSink.h"

using namespace akmedia;

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
	int max_buf_size = (sample_rate * channels * sample_bits);
	fprintf(stderr, "prepare buf %d\n", max_buf_size);
	do {
		buf = 
		  (T_U8 *)malloc(max_buf_size);
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
		int count = 0;
		while ((len = recv(conn, buf, max_buf_size, 0)) > 0) {
			fprintf(stderr, "read %d\n", len);
			count += len;
			ret = play.render(buf, len);
			if (ret != 0) {
				fprintf(stderr, "render data failed.\n");
				break;
			}
		}
		fprintf(stderr, "total: %d\n", count);
	} while (false);
	if (play.isOpened()) {
		play.stop();
		play.close();
	}
	if (buf) {
		free(buf);
	}
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
