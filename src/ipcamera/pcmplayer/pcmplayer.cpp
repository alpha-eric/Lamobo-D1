#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include "AudioSink.h"

using namespace akmedia;

int main(int argc, const char *argv[])
{
	fprintf(stderr, "hello world\n");
	if (argc != 2) {
		fprintf(stderr, "pcmplayer hello.pcm\n");
		return EXIT_FAILURE;
	}
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
	FILE *fp = NULL;
	do {
		buf = 
		  (T_U8 *)malloc(max_buf_size);
		if (!buf) {
			fprintf(stderr, "alloc buffer failed.\n");
			break;
		}
		fp = fopen(argv[1], "r");
		if (!fp) {
			fprintf(stderr, "open failed.\n");
		}
		play.setEQ(0);
		play.setVolume(volume);
		play.setStartTime(0);
		if (play.open(sample_rate, channels, sample_bits) < 0) {
			fprintf(stderr, "open audiosink failed.\n");
			break;
		}
		play.start();
		while ((len = fread(buf, 1, max_buf_size, fp)) > 0) {
			fprintf(stderr, "read %d\n", len);
			ret = play.render(buf, len);
			if (ret != 0) {
				fprintf(stderr, "render data failed.\n");
				break;
			}
		}
	} while (false);
	if (play.isOpened()) {
		play.stop();
		play.close();
	}
	if (fp) {
		fclose(fp);
	}
	if (buf) {
		free(buf);
	}
	return EXIT_SUCCESS;
}
