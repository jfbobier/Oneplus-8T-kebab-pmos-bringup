// SPDX-License-Identifier: GPL-2.0
/*
 * mhi_efs_sync - remote EFS (filesystem) service for the SDX55 external modem
 *
 * The SDX55 firmware on Apollo is built RMTEFS:
 *
 *   MPSS.HI.2.5.c1-00029-SDX55_RMTEFS_PACK-1.4393.55.30222.2
 *
 * i.e. it expects the application processor to host its filesystem. Android
 * does this with Qualcomm's kickstart:
 *
 *   /vendor/bin/ks -m -p /dev/mhi_0306_02.01.00_pipe_10 \
 *                  -w /dev/block/bootdevice/by-name/ -t -1 -l -g mdm1
 *
 * where -m is "force Sahara memory debug mode", -l loops forever, -w is where
 * received files are written and -g is a filename prefix. MHI channel 10 is
 * the EFS channel. The modem exports its EFS as named memory regions; the host
 * reads them and writes each to <outdir>/<prefix><region-filename>. With
 * Android's arguments a region named "m9kefs1" therefore lands directly on the
 * partition labelled "mdm1m9kefs1".
 *
 * pmOS never serviced this channel at all (mhi0_EFS was bound to a keepalive
 * stub), so the modem's EFS was effectively read-only, RAM-backed from the
 * static Sahara efs{1,2,3}.bin images. This daemon is the missing piece.
 *
 * Protocol: Sahara, memory-debug flavour.
 *   target -> HELLO
 *   host   -> HELLO_RESP (mode = MEMORY_DEBUG)
 *   target -> MEMORY_DEBUG{,64}  (address + length of the region table)
 *   host   -> MEMORY_READ{,64}   (fetch the table, then each region)
 *   host   -> RESET, target -> RESET_RESP
 *
 * Build on the device (see tools/remoteproc/Makefile).
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Sahara commands */
#define SAHARA_HELLO		0x01
#define SAHARA_HELLO_RESP	0x02
#define SAHARA_READ_DATA	0x03
#define SAHARA_END_IMAGE_TX	0x04
#define SAHARA_DONE		0x05
#define SAHARA_DONE_RESP	0x06
#define SAHARA_RESET		0x07
#define SAHARA_RESET_RESP	0x08
#define SAHARA_MEMORY_DEBUG	0x09
#define SAHARA_MEMORY_READ	0x0a
#define SAHARA_CMD_READY	0x0b
#define SAHARA_SWITCH_MODE	0x0c
#define SAHARA_EXECUTE		0x0d
#define SAHARA_EXECUTE_RESP	0x0e
#define SAHARA_EXECUTE_DATA	0x0f
#define SAHARA_MEMORY_DEBUG64	0x10
#define SAHARA_MEMORY_READ64	0x11

/* Sahara modes */
#define SAHARA_MODE_IMAGE_TX_PENDING	0x00
#define SAHARA_MODE_IMAGE_TX_COMPLETE	0x01
#define SAHARA_MODE_MEMORY_DEBUG	0x02
#define SAHARA_MODE_COMMAND		0x03

#define SAHARA_VERSION		2
#define SAHARA_VERSION_SUPPORTED 1

/* The MHI wwan port MTU is 0x8000; stay comfortably inside it. */
#define MAX_PACKET		0x8000
#define DEFAULT_CHUNK		(8 * 1024)
#define MAX_TABLE_BYTES		(64 * 1024)
#define REGION_NAME_LEN		20

struct sahara_hdr {
	uint32_t command;
	uint32_t length;
} __attribute__((packed));

struct sahara_hello {
	struct sahara_hdr hdr;
	uint32_t version;
	uint32_t version_supported;
	uint32_t cmd_packet_length;
	uint32_t mode;
	uint32_t reserved[6];
} __attribute__((packed));

struct sahara_hello_resp {
	struct sahara_hdr hdr;
	uint32_t version;
	uint32_t version_supported;
	uint32_t status;
	uint32_t mode;
	uint32_t reserved[6];
} __attribute__((packed));

/* Region table entries as exported by the target. */
struct region32 {
	uint32_t save_pref;
	uint32_t mem_base;
	uint32_t mem_length;
	char desc[REGION_NAME_LEN];
	char filename[REGION_NAME_LEN];
} __attribute__((packed));

struct region64 {
	uint64_t save_pref;
	uint64_t mem_base;
	uint64_t mem_length;
	char desc[REGION_NAME_LEN];
	char filename[REGION_NAME_LEN];
} __attribute__((packed));

static bool verbose;
static volatile sig_atomic_t stop_requested;

static void logmsg(const char *fmt, ...)
{
	struct timespec ts;
	va_list ap;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	printf("[%5ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

static void dbg(const char *fmt, ...)
{
	va_list ap;

	if (!verbose)
		return;
	va_start(ap, fmt);
	printf("        ");
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

/* Read exactly len bytes, reassembling across MHI packet boundaries. */
static int read_exact(int fd, void *buf, size_t len, int timeout_ms)
{
	uint8_t *p = buf;
	size_t got = 0;

	while (got < len) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		ssize_t n;
		int rc;

		rc = poll(&pfd, 1, timeout_ms);
		if (rc < 0) {
			if (errno == EINTR) {
				if (stop_requested)
					return -EINTR;
				continue;
			}
			return -errno;
		}
		if (rc == 0)
			return -ETIMEDOUT;
		if (pfd.revents & (POLLHUP | POLLERR))
			return -EIO;

		n = read(fd, p + got, len - got);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (n == 0)
			return -EIO;
		got += (size_t)n;
	}

	return 0;
}

static int write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	size_t done = 0;

	while (done < len) {
		ssize_t n = write(fd, p + done, len - done);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		done += (size_t)n;
	}

	return 0;
}

/* Read one Sahara packet: header first, then the remainder. */
static int read_packet(int fd, void *buf, size_t bufsz, int timeout_ms)
{
	struct sahara_hdr *hdr = buf;
	uint32_t len;
	int ret;

	ret = read_exact(fd, hdr, sizeof(*hdr), timeout_ms);
	if (ret)
		return ret;

	len = hdr->length;
	if (len < sizeof(*hdr) || len > bufsz) {
		logmsg("bad Sahara packet: cmd=%u length=%u", hdr->command, len);
		return -EPROTO;
	}

	if (len > sizeof(*hdr)) {
		ret = read_exact(fd, (uint8_t *)buf + sizeof(*hdr),
				 len - sizeof(*hdr), timeout_ms);
		if (ret)
			return ret;
	}

	dbg("<- cmd=0x%02x len=%u", hdr->command, len);
	return 0;
}

static int send_hello_resp(int fd, uint32_t mode)
{
	struct sahara_hello_resp resp = {
		.hdr = { .command = SAHARA_HELLO_RESP, .length = sizeof(resp) },
		.version = SAHARA_VERSION,
		.version_supported = SAHARA_VERSION_SUPPORTED,
		.status = 0,
		.mode = mode,
	};

	dbg("-> HELLO_RESP mode=%u", mode);
	return write_all(fd, &resp, sizeof(resp));
}

/*
 * Issue MEMORY_READ and collect the raw response. The target answers with
 * unframed data, not a Sahara packet.
 */
static int memory_read(int fd, bool use64, uint64_t addr, uint64_t len,
		       void *out, int timeout_ms)
{
	int ret;

	if (use64) {
		struct {
			struct sahara_hdr hdr;
			uint64_t addr;
			uint64_t length;
		} __attribute__((packed)) req = {
			.hdr = { .command = SAHARA_MEMORY_READ64,
				 .length = sizeof(req) },
			.addr = addr,
			.length = len,
		};

		ret = write_all(fd, &req, sizeof(req));
	} else {
		struct {
			struct sahara_hdr hdr;
			uint32_t addr;
			uint32_t length;
		} __attribute__((packed)) req = {
			.hdr = { .command = SAHARA_MEMORY_READ,
				 .length = sizeof(req) },
			.addr = (uint32_t)addr,
			.length = (uint32_t)len,
		};

		ret = write_all(fd, &req, sizeof(req));
	}
	if (ret)
		return ret;

	dbg("-> MEMORY_READ%s addr=0x%llx len=%llu", use64 ? "64" : "",
	    (unsigned long long)addr, (unsigned long long)len);

	return read_exact(fd, out, len, timeout_ms);
}

/*
 * Open the destination for a region. If <outdir>/<prefix><name> is an existing
 * block device we write it in place (that is the whole point - it is the
 * mdm1m9kefsN partition); otherwise a regular file is created.
 */
static int open_region_dest(const char *outdir, const char *prefix,
			    const char *name, bool allow_block, char *path,
			    size_t pathsz)
{
	struct stat st;
	int fd;

	snprintf(path, pathsz, "%s/%s%s", outdir, prefix, name);

	if (stat(path, &st) == 0 && S_ISBLK(st.st_mode)) {
		if (!allow_block) {
			logmsg("  refusing to write block device %s "
			       "(pass --write-partitions to allow)", path);
			return -EPERM;
		}
		fd = open(path, O_WRONLY | O_SYNC);
	} else {
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	}

	if (fd < 0)
		return -errno;

	return fd;
}

static int handle_region(int fd, bool use64, uint64_t base, uint64_t length,
			 const char *desc, const char *name,
			 const char *outdir, const char *prefix,
			 bool allow_block, size_t chunk, int timeout_ms)
{
	char path[PATH_MAX];
	uint64_t off = 0;
	uint8_t *buf;
	int dst, ret = 0;

	logmsg("  region '%s' (%s) base=0x%llx length=%llu", name, desc,
	       (unsigned long long)base, (unsigned long long)length);

	dst = open_region_dest(outdir, prefix, name, allow_block, path,
			       sizeof(path));
	if (dst < 0) {
		logmsg("  cannot open destination for '%s': %s", name,
		       strerror(-dst));
		return dst;
	}

	buf = malloc(chunk);
	if (!buf) {
		close(dst);
		return -ENOMEM;
	}

	while (off < length) {
		size_t n = (size_t)((length - off) < chunk ? (length - off) : chunk);

		ret = memory_read(fd, use64, base + off, n, buf, timeout_ms);
		if (ret) {
			logmsg("  read failed at offset %llu: %s",
			       (unsigned long long)off, strerror(-ret));
			break;
		}

		ret = write_all(dst, buf, n);
		if (ret) {
			logmsg("  write failed at offset %llu: %s",
			       (unsigned long long)off, strerror(-ret));
			break;
		}

		off += n;
	}

	free(buf);
	fsync(dst);
	close(dst);

	if (!ret)
		logmsg("  wrote %llu bytes to %s", (unsigned long long)off, path);

	return ret;
}

static int do_memory_debug(int fd, bool use64, uint64_t table_addr,
			   uint64_t table_len, const char *outdir,
			   const char *prefix, bool allow_block, size_t chunk,
			   int timeout_ms)
{
	size_t entry_sz = use64 ? sizeof(struct region64) : sizeof(struct region32);
	uint8_t *table;
	unsigned int i, n;
	int ret;

	if (!table_len || table_len > MAX_TABLE_BYTES ||
	    table_len % entry_sz != 0) {
		logmsg("invalid memory table length %llu (entry size %zu)",
		       (unsigned long long)table_len, entry_sz);
		return -EPROTO;
	}

	table = malloc(table_len);
	if (!table)
		return -ENOMEM;

	ret = memory_read(fd, use64, table_addr, table_len, table, timeout_ms);
	if (ret) {
		logmsg("failed to read region table: %s", strerror(-ret));
		free(table);
		return ret;
	}

	n = (unsigned int)(table_len / entry_sz);
	logmsg("modem exported %u EFS region(s)", n);

	for (i = 0; i < n; i++) {
		char desc[REGION_NAME_LEN + 1] = { 0 };
		char name[REGION_NAME_LEN + 1] = { 0 };
		uint64_t base, length;

		if (use64) {
			struct region64 *r = (struct region64 *)(table + i * entry_sz);

			base = r->mem_base;
			length = r->mem_length;
			memcpy(desc, r->desc, REGION_NAME_LEN);
			memcpy(name, r->filename, REGION_NAME_LEN);
		} else {
			struct region32 *r = (struct region32 *)(table + i * entry_sz);

			base = r->mem_base;
			length = r->mem_length;
			memcpy(desc, r->desc, REGION_NAME_LEN);
			memcpy(name, r->filename, REGION_NAME_LEN);
		}

		if (!name[0]) {
			logmsg("  region %u has no filename, skipping", i);
			continue;
		}

		ret = handle_region(fd, use64, base, length, desc, name, outdir,
				    prefix, allow_block, chunk, timeout_ms);
		if (ret)
			break;
	}

	free(table);
	return ret;
}

static int send_reset(int fd, int timeout_ms)
{
	struct sahara_hdr reset = { .command = SAHARA_RESET,
				    .length = sizeof(reset) };
	uint8_t buf[MAX_PACKET];
	int ret;

	dbg("-> RESET");
	ret = write_all(fd, &reset, sizeof(reset));
	if (ret)
		return ret;

	ret = read_packet(fd, buf, sizeof(buf), timeout_ms);
	if (ret)
		return ret;

	if (((struct sahara_hdr *)buf)->command != SAHARA_RESET_RESP)
		logmsg("unexpected reply to RESET: cmd=0x%02x",
		       ((struct sahara_hdr *)buf)->command);

	return 0;
}

/* One full Sahara session: HELLO through to the region dump. */
static int run_session(int fd, const char *outdir, const char *prefix,
		       bool allow_block, bool noreset, size_t chunk,
		       int timeout_ms)
{
	uint8_t buf[MAX_PACKET];
	struct sahara_hdr *hdr = (struct sahara_hdr *)buf;
	struct sahara_hello *hello;
	uint64_t table_addr, table_len;
	bool use64;
	int ret;

	/* Wait indefinitely for the modem to say hello. */
	ret = read_packet(fd, buf, sizeof(buf), -1);
	if (ret)
		return ret;

	if (hdr->command != SAHARA_HELLO) {
		logmsg("expected HELLO, got cmd=0x%02x", hdr->command);
		return -EPROTO;
	}

	hello = (struct sahara_hello *)buf;
	logmsg("HELLO: version=%u supported=%u mode=%u", hello->version,
	       hello->version_supported, hello->mode);

	ret = send_hello_resp(fd, SAHARA_MODE_MEMORY_DEBUG);
	if (ret)
		return ret;

	ret = read_packet(fd, buf, sizeof(buf), timeout_ms);
	if (ret)
		return ret;

	switch (hdr->command) {
	case SAHARA_MEMORY_DEBUG: {
		struct {
			struct sahara_hdr hdr;
			uint32_t table_addr;
			uint32_t table_length;
		} __attribute__((packed)) *md = (void *)buf;

		use64 = false;
		table_addr = md->table_addr;
		table_len = md->table_length;
		break;
	}
	case SAHARA_MEMORY_DEBUG64: {
		struct {
			struct sahara_hdr hdr;
			uint64_t table_addr;
			uint64_t table_length;
		} __attribute__((packed)) *md = (void *)buf;

		use64 = true;
		table_addr = md->table_addr;
		table_len = md->table_length;
		break;
	}
	default:
		logmsg("expected MEMORY_DEBUG, got cmd=0x%02x", hdr->command);
		return -EPROTO;
	}

	logmsg("memory table: addr=0x%llx length=%llu (%s-bit entries)",
	       (unsigned long long)table_addr, (unsigned long long)table_len,
	       use64 ? "64" : "32");

	ret = do_memory_debug(fd, use64, table_addr, table_len, outdir, prefix,
			      allow_block, chunk, timeout_ms);
	if (ret)
		return ret;

	if (!noreset)
		ret = send_reset(fd, timeout_ms);

	return ret;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
"Usage: %s [options]\n"
"\n"
"Services the SDX55's remote-EFS (Sahara memory-debug) channel, the job\n"
"Android gives to /vendor/bin/ks on MHI channel 10.\n"
"\n"
"  -d, --device <path>     EFS char device (default /dev/wwan0efs0)\n"
"  -o, --outdir <dir>      where regions are written\n"
"                          (default /var/lib/mhi-efs)\n"
"  -g, --prefix <str>      filename prefix; Android uses \"mdm1\" so that\n"
"                          region \"m9kefs1\" maps to partition\n"
"                          \"mdm1m9kefs1\" (default: none)\n"
"  -W, --write-partitions  allow writing when the destination is a block\n"
"                          device. DESTRUCTIVE - off by default, so a first\n"
"                          run lands in plain files you can inspect.\n"
"  -1, --once              handle a single session then exit\n"
"  -n, --noreset           do not send the trailing Sahara RESET\n"
"  -j, --chunk <kb>        MEMORY_READ chunk size in KB (default 8)\n"
"  -t, --timeout <sec>     per-exchange timeout, 0 = wait forever (default 30)\n"
"  -v, --verbose           log every packet\n"
"  -h, --help              this text\n"
"\n"
"Typical first run (safe, writes files):\n"
"  %s -v -o /var/lib/mhi-efs\n"
"Android-equivalent (writes the partitions directly):\n"
"  %s -o /dev/disk/by-partlabel -g mdm1 -W\n",
		argv0, argv0, argv0);
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/wwan0efs0";
	const char *outdir = "/var/lib/mhi-efs";
	const char *prefix = "";
	bool allow_block = false, once = false, noreset = false;
	size_t chunk = DEFAULT_CHUNK;
	int timeout_ms = 30000;
	int fd, ret = 0;

	static const struct option opts[] = {
		{ "device",           required_argument, NULL, 'd' },
		{ "outdir",           required_argument, NULL, 'o' },
		{ "prefix",           required_argument, NULL, 'g' },
		{ "write-partitions", no_argument,       NULL, 'W' },
		{ "once",             no_argument,       NULL, '1' },
		{ "noreset",          no_argument,       NULL, 'n' },
		{ "chunk",            required_argument, NULL, 'j' },
		{ "timeout",          required_argument, NULL, 't' },
		{ "verbose",          no_argument,       NULL, 'v' },
		{ "help",             no_argument,       NULL, 'h' },
		{}
	};
	int c;

	while ((c = getopt_long(argc, argv, "d:o:g:W1nj:t:vh", opts, NULL)) != -1) {
		switch (c) {
		case 'd': dev = optarg; break;
		case 'o': outdir = optarg; break;
		case 'g': prefix = optarg; break;
		case 'W': allow_block = true; break;
		case '1': once = true; break;
		case 'n': noreset = true; break;
		case 'j': chunk = (size_t)atoi(optarg) * 1024; break;
		case 't': timeout_ms = atoi(optarg) * 1000; break;
		case 'v': verbose = true; break;
		case 'h': usage(argv[0]); return 0;
		default:  usage(argv[0]); return 1;
		}
	}

	if (chunk == 0 || chunk > MAX_PACKET)
		chunk = DEFAULT_CHUNK;
	if (timeout_ms <= 0)
		timeout_ms = -1;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);

	if (!allow_block)
		mkdir(outdir, 0700);

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "cannot open %s: %s\n", dev, strerror(errno));
		fprintf(stderr,
			"(needs the kernel patch binding mhi0_EFS to "
			"mhi_wwan_ctrl)\n");
		return 1;
	}

	logmsg("serving remote EFS on %s -> %s (prefix '%s', %s)", dev, outdir,
	       prefix, allow_block ? "PARTITION WRITES ENABLED" : "files only");

	while (!stop_requested) {
		ret = run_session(fd, outdir, prefix, allow_block, noreset,
				  chunk, timeout_ms);
		if (ret == -EINTR)
			break;
		if (ret == -ETIMEDOUT) {
			logmsg("session timed out; waiting for next HELLO");
			continue;
		}
		if (ret) {
			logmsg("session failed: %s", strerror(-ret));
			if (ret == -EIO) {
				logmsg("channel hung up (modem gone?); exiting");
				break;
			}
		} else {
			logmsg("session complete");
		}

		if (once)
			break;
	}

	close(fd);
	return ret ? 1 : 0;
}
