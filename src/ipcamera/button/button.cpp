#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <pthread.h>
#include <linux/netlink.h>
#include <linux/version.h>
#include <linux/input.h>

#define KEY_GPIO_DEV        "/dev/input/event0"

int main(int argc, const char *argv[])
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
					fprintf(stderr, "key:%d\n", key_event[i].code);
				}
			}
		} while (true);
		close(gpio);
	} while (true);
	return 0;
}
