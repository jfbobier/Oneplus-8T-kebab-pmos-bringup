// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal native replacement for Qualcomm's pm-service.
 *
 * This daemon intentionally implements only the smallest useful subset:
 * - announce a QRTR/QMI service for Peripheral Manager
 * - answer the known restart/shutdown request IDs without rebooting the AP
 * - optionally answer peripheral restart requests with success
 *
 * It is designed to build on-device on Alpine/musl with no external
 * dependencies beyond kernel UAPI headers.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <linux/qrtr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

#define QMI_REQUEST     0
#define QMI_RESPONSE    2
#define QMI_INDICATION  4

#define PM_REQ_SYSTEM_RESTART      0x20
#define PM_REQ_SYSTEM_SHUTDOWN     0x21
#define PM_REQ_PERIPHERAL_RESTART  0x22

#define QMI_RESULT_SUCCESS_V01 0
#define QMI_RESULT_FAILURE_V01 1

#define QMI_ERR_NONE_V01          0
#define QMI_ERR_NOT_SUPPORTED_V01 94

struct qmi_header {
	uint8_t type;
	uint16_t txn_id;
	uint16_t msg_id;
	uint16_t msg_len;
} __attribute__((packed));

struct qrtr_ctrl_pkt_wire {
	uint32_t cmd;
	union {
		struct {
			uint32_t service;
			uint32_t instance;
			uint32_t node;
			uint32_t port;
		} server;
		struct {
			uint32_t node;
			uint32_t port;
		} client;
	};
} __attribute__((packed));

struct config {
	unsigned int service_id;
	unsigned int version;
	unsigned int instance;
	bool send_indications;
	bool verbose;
};

static const struct option long_options[] = {
	{ "service-id", required_argument, NULL, 's' },
	{ "version", required_argument, NULL, 'v' },
	{ "instance", required_argument, NULL, 'i' },
	{ "no-indications", no_argument, NULL, 'n' },
	{ "verbose", no_argument, NULL, 'V' },
	{ "help", no_argument, NULL, 'h' },
	{ NULL, 0, NULL, 0 },
};

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static void usage(FILE *stream, const char *progname)
{
	fprintf(stream,
		"Usage: %s [options]\n"
		"\n"
		"Minimal QRTR/QMI Peripheral Manager shim.\n"
		"\n"
		"Defaults are based on reverse engineering of vendor pm-service:\n"
		"  service-id = 53\n"
		"  version    = 7\n"
		"  instance   = 0\n"
		"\n"
		"Options:\n"
		"  -s, --service-id N      QRTR/QMI service ID (default: 53)\n"
		"  -v, --version N         service version (default: 7)\n"
		"  -i, --instance N        service instance (default: 0)\n"
		"  -n, --no-indications    suppress follow-up indications\n"
		"  -V, --verbose           log payload details\n"
		"  -h, --help              show this help\n",
		progname);
}

static void dump_hex(const uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		fprintf(stderr, "%02x", buf[i]);
		if ((i + 1) != len)
			fputc(' ', stderr);
	}
	fputc('\n', stderr);
}

static void extract_ascii_hint(const uint8_t *buf, size_t len, char *out, size_t out_len)
{
	size_t i;
	size_t j = 0;

	if (out_len == 0)
		return;

	for (i = 0; i < len && j + 1 < out_len; i++) {
		if (buf[i] >= 32 && buf[i] <= 126) {
			out[j++] = (char)buf[i];
		} else if (j != 0) {
			break;
		}
	}

	out[j] = '\0';
}

static uint32_t pack_instance(unsigned int version, unsigned int instance)
{
	return ((instance & 0x00ffffffu) << 8) | (version & 0xffu);
}

static int announce_server(int fd, unsigned int node, unsigned int port,
			   unsigned int service_id, unsigned int version,
			   unsigned int instance)
{
	struct sockaddr_qrtr addr = {
		.sq_family = AF_QIPCRTR,
		.sq_node = QRTR_NODE_BCAST,
		.sq_port = QRTR_PORT_CTRL,
	};
	struct qrtr_ctrl_pkt_wire pkt;
	ssize_t n;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = htole32(QRTR_TYPE_NEW_SERVER);
	pkt.server.service = htole32(service_id);
	pkt.server.instance = htole32(pack_instance(version, instance));
	pkt.server.node = htole32(node);
	pkt.server.port = htole32(port);

	n = sendto(fd, &pkt, sizeof(pkt), 0,
		   (const struct sockaddr *)&addr, sizeof(addr));
	if (n < 0)
		return -errno;

	return 0;
}

static int send_qmi_message(int fd, const struct sockaddr_qrtr *dst,
			    uint8_t type, uint16_t txn_id, uint16_t msg_id,
			    const void *payload, uint16_t payload_len)
{
	uint8_t buf[256];
	struct qmi_header hdr;
	size_t total_len;
	ssize_t n;

	if (sizeof(hdr) + payload_len > sizeof(buf))
		return -EMSGSIZE;

	hdr.type = type;
	hdr.txn_id = htole16(txn_id);
	hdr.msg_id = htole16(msg_id);
	hdr.msg_len = htole16(payload_len);

	memcpy(buf, &hdr, sizeof(hdr));
	if (payload_len)
		memcpy(buf + sizeof(hdr), payload, payload_len);

	total_len = sizeof(hdr) + payload_len;
	n = sendto(fd, buf, total_len, 0,
		   (const struct sockaddr *)dst, sizeof(*dst));
	if (n < 0)
		return -errno;
	if ((size_t)n != total_len)
		return -EIO;

	return 0;
}

static int send_success_response(int fd, const struct sockaddr_qrtr *dst,
				 uint16_t txn_id, uint16_t msg_id)
{
	const uint8_t payload[] = {
		0x02, 0x04, 0x00,
		0x00, 0x00,
		0x00, 0x00,
	};

	return send_qmi_message(fd, dst, QMI_RESPONSE, txn_id, msg_id,
				payload, sizeof(payload));
}

static int send_failure_response(int fd, const struct sockaddr_qrtr *dst,
				 uint16_t txn_id, uint16_t msg_id, uint16_t error)
{
	uint8_t payload[] = {
		0x02, 0x04, 0x00,
		0x01, 0x00,
		0x00, 0x00,
	};

	payload[5] = (uint8_t)(error & 0xff);
	payload[6] = (uint8_t)((error >> 8) & 0xff);

	return send_qmi_message(fd, dst, QMI_RESPONSE, txn_id, msg_id,
				payload, sizeof(payload));
}

static int send_empty_indication(int fd, const struct sockaddr_qrtr *dst,
				 uint16_t msg_id)
{
	return send_qmi_message(fd, dst, QMI_INDICATION, 0, msg_id, NULL, 0);
}

int main(int argc, char **argv)
{
	struct config cfg = {
		.service_id = 53,
		.version = 7,
		.instance = 0,
		.send_indications = true,
		.verbose = false,
	};
	struct sockaddr_qrtr local = {
		.sq_family = AF_QIPCRTR,
		.sq_node = 0,
		.sq_port = 0,
	};
	struct sockaddr_qrtr current;
	socklen_t current_len = sizeof(current);
	socklen_t local_len = sizeof(local);
	int fd;
	int opt;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	while ((opt = getopt_long(argc, argv, "s:v:i:nVh", long_options, NULL)) != -1) {
		switch (opt) {
		case 's':
			cfg.service_id = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'v':
			cfg.version = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'i':
			cfg.instance = (unsigned int)strtoul(optarg, NULL, 0);
			break;
		case 'n':
			cfg.send_indications = false;
			break;
		case 'V':
			cfg.verbose = true;
			break;
		case 'h':
			usage(stdout, argv[0]);
			return 0;
		default:
			usage(stderr, argv[0]);
			return 1;
		}
	}

	fd = socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		perror("socket(AF_QIPCRTR)");
		return 1;
	}

	if (getsockname(fd, (struct sockaddr *)&current, &current_len) < 0) {
		perror("getsockname(AF_QIPCRTR)");
		close(fd);
		return 1;
	}

	local.sq_node = current.sq_node;

	if (bind(fd, (const struct sockaddr *)&local, sizeof(local)) < 0) {
		perror("bind(AF_QIPCRTR)");
		close(fd);
		return 1;
	}

	if (getsockname(fd, (struct sockaddr *)&local, &local_len) < 0) {
		perror("getsockname(AF_QIPCRTR)");
		close(fd);
		return 1;
	}

	if (announce_server(fd, local.sq_node, local.sq_port,
			    cfg.service_id, cfg.version, cfg.instance) != 0) {
		perror("announce_server");
		close(fd);
		return 1;
	}

	fprintf(stderr,
		"pm_service_native: announced service=%u version=%u instance=%u node=%u port=%u\n",
		cfg.service_id, cfg.version, cfg.instance, local.sq_node, local.sq_port);

	while (!g_stop) {
		uint8_t buf[2048];
		struct pollfd pfd = {
			.fd = fd,
			.events = POLLIN,
		};
		struct sockaddr_qrtr peer;
		socklen_t peer_len = sizeof(peer);
		struct qmi_header hdr;
		uint16_t txn_id;
		uint16_t msg_id;
		uint16_t msg_len;
		ssize_t n;
		int rc;
		char ascii_hint[128];

		rc = poll(&pfd, 1, 500);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		}
		if (rc == 0)
			continue;

		n = recvfrom(fd, buf, sizeof(buf), 0,
			     (struct sockaddr *)&peer, &peer_len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("recvfrom");
			break;
		}

		if ((size_t)n < sizeof(hdr))
			continue;

		memcpy(&hdr, buf, sizeof(hdr));
		txn_id = le16toh(hdr.txn_id);
		msg_id = le16toh(hdr.msg_id);
		msg_len = le16toh(hdr.msg_len);

		if (hdr.type != QMI_REQUEST)
			continue;

		if ((size_t)n < sizeof(hdr) + msg_len)
			continue;

		extract_ascii_hint(buf + sizeof(hdr), msg_len,
				   ascii_hint, sizeof(ascii_hint));

		fprintf(stderr,
			"pm_service_native: request msg=0x%04x txn=%u from %u:%u%s%s\n",
			msg_id, txn_id, peer.sq_node, peer.sq_port,
			ascii_hint[0] ? " hint=" : "",
			ascii_hint[0] ? ascii_hint : "");
		if (cfg.verbose && msg_len) {
			fprintf(stderr, "pm_service_native: payload ");
			dump_hex(buf + sizeof(hdr), msg_len);
		}

		switch (msg_id) {
		case PM_REQ_SYSTEM_RESTART:
		case PM_REQ_SYSTEM_SHUTDOWN:
			rc = send_success_response(fd, &peer, txn_id, msg_id);
			if (rc != 0) {
				fprintf(stderr, "pm_service_native: response failed: %s\n",
					strerror(-rc));
				break;
			}
			if (cfg.send_indications) {
				rc = send_empty_indication(fd, &peer, msg_id);
				if (rc != 0)
					fprintf(stderr,
						"pm_service_native: indication failed: %s\n",
						strerror(-rc));
			}
			break;
		case PM_REQ_PERIPHERAL_RESTART:
			rc = send_success_response(fd, &peer, txn_id, msg_id);
			if (rc != 0) {
				fprintf(stderr, "pm_service_native: response failed: %s\n",
					strerror(-rc));
			}
			break;
		default:
			rc = send_failure_response(fd, &peer, txn_id, msg_id,
						   QMI_ERR_NOT_SUPPORTED_V01);
			if (rc != 0)
				fprintf(stderr, "pm_service_native: failure response failed: %s\n",
					strerror(-rc));
			break;
		}
	}

	close(fd);
	return 1;
}
