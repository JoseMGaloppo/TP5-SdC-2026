#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>

#define DEFAULT_DEV "/dev/SdeC_signals"

static volatile sig_atomic_t running = 1;

static void on_sigint(int sig)
{
	(void)sig;
	running = 0;
}

static void scale(int sig, int raw, double *value, const char **type,
		  const char **unit)
{
	if (sig == 2) {
		*type  = "cuadrada";
		*unit  = "V";
		*value = raw * 3.3 / 1000.0;
	} else {
		*type  = "senoidal";
		*unit  = "V";
		*value = raw * 5.0 / 1000.0;
	}
}


static int select_signal(const char *dev, int sel)
{
	char c = (sel == 2) ? '2' : '1';
	int fd = open(dev, O_WRONLY);
	ssize_t w;

	if (fd < 0)
		return -1;
	w = write(fd, &c, 1);
	close(fd);
	return (w < 0) ? -1 : 0;
}


static int read_sample(const char *dev, int *sig, long *t, int *raw)
{
	char line[64];
	int fd = open(dev, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, line, sizeof(line) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	line[n] = '\0';
	return (sscanf(line, "%d %ld %d", sig, t, raw) == 3) ? 0 : -1;
}

int main(int argc, char **argv)
{
	int sel = 1;
	const char *dev = DEFAULT_DEV;

	if (argc >= 2)
		sel = (atoi(argv[1]) == 2) ? 2 : 1;
	if (argc >= 3)
		dev = argv[2];

	signal(SIGINT, on_sigint);
	signal(SIGTERM, on_sigint);

	if (access(dev, R_OK | W_OK) != 0) {
		fprintf(stderr, "reader: no puedo acceder a %s: %s\n",
			dev, strerror(errno));
		fprintf(stderr, "        (cargaste el modulo? proba: "
				"chmod 666 %s)\n", dev);
		return 1;
	}

	select_signal(dev, sel);

	while (running) {
		fd_set rfds;
		struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
		int sig, raw;
		long t;
		double value;
		const char *type, *unit;

		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0 &&
		    FD_ISSET(STDIN_FILENO, &rfds)) {
			char cmd[16];
			if (fgets(cmd, sizeof(cmd), stdin)) {
				if (cmd[0] == '1' || cmd[0] == '2') {
					sel = (cmd[0] == '2') ? 2 : 1;
					select_signal(dev, sel);
				}
			} else {
				clearerr(stdin);
			}
		}

		if (read_sample(dev, &sig, &t, &raw) != 0)
			continue;
		scale(sig, raw, &value, &type, &unit);

		printf("{\"signal\":%d,\"type\":\"%s\",\"unit\":\"%s\","
		       "\"t\":%ld,\"raw\":%d,\"value\":%.3f}\n",
		       sig, type, unit, t, raw, value);
		fflush(stdout);
	}

	return 0;
}
