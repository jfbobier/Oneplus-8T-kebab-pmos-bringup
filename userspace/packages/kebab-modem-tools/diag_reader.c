// SPDX-License-Identifier: GPL-2.0
/*
 * diag_reader - Read Qualcomm DIAG messages from SDX55 before crash.
 *
 * Opens the DIAG wwan port (/dev/wwan0qcdm0), enables F3 message
 * streaming, and dumps all received messages.  Run this immediately
 * after boot to capture the modem's internal logs before ERRFATAL.
 *
 * Protocol notes:
 * - MHI DIAG channel: each MHI transfer = one DIAG packet
 * - Different firmwares appear to accept different TX formats on QCDM.
 *   For this SDX55 endpoint, EVENT_REPORT over HDLC+trailing flag is the
 *   only command path that has proven useful so far.
 * - RX packets are often HDLC-like, but some BAD_CMD replies look closer
 *   to raw packets with only a trailing 0x7e delimiter.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

static volatile int g_stop;

static FILE *g_raw_fp;

/* Defined below; dump_event() feeds embedded sub-packets back into it. */
static void process_packet(const uint8_t *p, size_t len);

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static double mono_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* HDLC framing */
#define HDLC_FLAG	0x7E
#define HDLC_ESC	0x7D
#define HDLC_ESC_MASK	0x20

/* CRC-16/CCITT for DIAG HDLC */
static uint16_t crc16(const uint8_t *buf, size_t len)
{
	static const uint16_t crc_table[256] = {
		0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
		0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
		0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
		0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
		0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
		0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
		0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
		0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
		0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
		0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
		0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
		0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
		0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
		0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
		0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
		0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
		0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
		0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
		0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
		0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
		0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
		0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
		0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
		0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
		0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
		0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
		0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
		0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
		0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
		0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
		0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
		0x7bc7, 0x6a4e, 0x58d5, 0x4954, 0x3deb, 0x2c62, 0x1ef9, 0x0f70,
	};
	uint16_t crc = 0xFFFF;

	while (len--)
		crc = crc_table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

/*
 * Encode a DIAG packet for MHI: escaped payload + CRC + trailing 0x7E.
 * NO leading 0x7E — the modem treats it as a data byte (BAD_CMD).
 */
static int hdlc_encode(const uint8_t *payload, size_t plen,
		       uint8_t *out, size_t out_sz)
{
	uint16_t crc;
	size_t pos = 0;
	size_t i;
	uint8_t crc_bytes[2];

	crc = crc16(payload, plen);
	crc_bytes[0] = crc & 0xFF;
	crc_bytes[1] = (crc >> 8) & 0xFF;

	/* No leading 0x7E — modem on MHI doesn't expect it */
	for (i = 0; i < plen && pos + 3 < out_sz; i++) {
		if (payload[i] == HDLC_FLAG || payload[i] == HDLC_ESC) {
			out[pos++] = HDLC_ESC;
			out[pos++] = payload[i] ^ HDLC_ESC_MASK;
		} else {
			out[pos++] = payload[i];
		}
	}
	for (i = 0; i < 2 && pos + 2 < out_sz; i++) {
		if (crc_bytes[i] == HDLC_FLAG || crc_bytes[i] == HDLC_ESC) {
			out[pos++] = HDLC_ESC;
			out[pos++] = crc_bytes[i] ^ HDLC_ESC_MASK;
		} else {
			out[pos++] = crc_bytes[i];
		}
	}
	/* Trailing 0x7E marks end-of-frame */
	if (pos < out_sz) out[pos++] = HDLC_FLAG;
	return (int)pos;
}

/* HDLC decode for receive path */
static int hdlc_decode(const uint8_t *frame, size_t flen,
		       uint8_t *out, size_t out_sz)
{
	size_t pos = 0;
	size_t i;
	int esc = 0;

	for (i = 0; i < flen && pos < out_sz; i++) {
		if (frame[i] == HDLC_FLAG)
			continue;
		if (frame[i] == HDLC_ESC) {
			esc = 1;
			continue;
		}
		if (esc) {
			out[pos++] = frame[i] ^ HDLC_ESC_MASK;
			esc = 0;
		} else {
			out[pos++] = frame[i];
		}
	}

	/* Strip 2-byte CRC */
	if (pos >= 2)
		pos -= 2;
	else
		return -1;

	return (int)pos;
}

/* DIAG command codes */
#define DIAG_VERNO_F		0x00
#define DIAG_LOG_CONFIG_F	0x73
#define DIAG_EXT_MSG_CONFIG_F	0x7D
#define DIAG_EXT_MSG_F		0x79
#define DIAG_LOG_F		0x10
#define DIAG_EVENT_REPORT_F	0x60
#define DIAG_MSG_F		0x1F
#define DIAG_BAD_CMD_F		0x13
#define DIAG_BAD_PARM_F		0x14
#define DIAG_QSR4_EXT_MSG_F	0x61

/* DIAG_EXT_MSG_CONFIG sub-commands */
#define MSG_EXT_CFG_GET_SSID_RANGES	1
#define MSG_EXT_CFG_GET_BUILD_MASK	2
#define MSG_EXT_CFG_GET_RT_MASK		3
#define MSG_EXT_CFG_SET_RT_MASK		4
#define MSG_EXT_CFG_SET_ALL_RT_MASKS	5

/* DIAG_LOG_CONFIG operations */
#define LOG_CONFIG_SET_MASK_OP	3

enum tx_mode {
	TX_RAW_ONLY,
	TX_RAW_FLAG,
	TX_HDLC_FLAG,
};

static const char *tx_mode_name(enum tx_mode mode)
{
	switch (mode) {
	case TX_RAW_ONLY: return "raw";
	case TX_RAW_FLAG: return "raw+flag";
	case TX_HDLC_FLAG: return "hdlc+flag";
	default: return "?";
	}
}

static int diag_send_mode(int fd, const uint8_t *payload, size_t plen,
			  enum tx_mode mode)
{
	uint8_t frame[65536];
	ssize_t ret;
	size_t total;
	int enc;

	switch (mode) {
	case TX_RAW_ONLY:
		if (plen > sizeof(frame))
			return -1;
		memcpy(frame, payload, plen);
		total = plen;
		break;
	case TX_RAW_FLAG:
		if (plen + 1 > sizeof(frame))
			return -1;
		memcpy(frame, payload, plen);
		frame[plen] = HDLC_FLAG;
		total = plen + 1;
		break;
	case TX_HDLC_FLAG:
		enc = hdlc_encode(payload, plen, frame, sizeof(frame));
		if (enc < 0)
			return -1;
		total = enc;
		break;
	default:
		return -1;
	}

	ret = write(fd, frame, total);
	if (ret < 0) {
		fprintf(stderr, "[%9.3f] write(%s,%zu) failed: %s\n",
			mono_sec(), tx_mode_name(mode), total, strerror(errno));
		return -1;
	}
	if ((size_t)ret != total) {
		fprintf(stderr, "[%9.3f] write(%s,%zu) short: %zd\n",
			mono_sec(), tx_mode_name(mode), total, ret);
		return -1;
	}
	return 0;
}

/* Send DIAG version request */
static int send_version_req(int fd, enum tx_mode mode)
{
	uint8_t cmd[] = { DIAG_VERNO_F };

	fprintf(stderr, "[%9.3f] TX[%s]: VERSION request (%zu bytes)\n",
		mono_sec(), tx_mode_name(mode), sizeof(cmd));
	return diag_send_mode(fd, cmd, sizeof(cmd), mode);
}

/* Enable extended F3 messages for a range of SSIDs */
static int send_msg_config_range(int fd, uint16_t ssid_start, uint16_t ssid_end,
				 enum tx_mode mode)
{
	uint8_t cmd[4096];
	int count = ssid_end - ssid_start + 1;
	size_t total;
	int i;

	if (count <= 0 || (size_t)(8 + count * 4) > sizeof(cmd))
		return -1;

	cmd[0] = DIAG_EXT_MSG_CONFIG_F;
	cmd[1] = MSG_EXT_CFG_SET_RT_MASK;
	cmd[2] = 0; /* padding */
	cmd[3] = 0;
	/* SSID start (LE16) */
	cmd[4] = ssid_start & 0xFF;
	cmd[5] = (ssid_start >> 8) & 0xFF;
	/* SSID end (LE16) */
	cmd[6] = ssid_end & 0xFF;
	cmd[7] = (ssid_end >> 8) & 0xFF;

	/* RT mask for each SSID: 0xFFFFFFFF = all levels */
	for (i = 0; i < count; i++) {
		cmd[8 + i * 4 + 0] = 0xFF;
		cmd[8 + i * 4 + 1] = 0xFF;
		cmd[8 + i * 4 + 2] = 0xFF;
		cmd[8 + i * 4 + 3] = 0xFF;
	}

	total = 8 + count * 4;
	fprintf(stderr, "[%9.3f] TX[%s]: MSG_CONFIG SSID %u-%u (%zu bytes)\n",
		mono_sec(), tx_mode_name(mode), ssid_start, ssid_end, total);
	return diag_send_mode(fd, cmd, total, mode);
}

/* Enable event reporting */
static int send_event_report_enable(int fd, enum tx_mode mode)
{
	uint8_t cmd[2] = { DIAG_EVENT_REPORT_F, 0x01 };

	fprintf(stderr, "[%9.3f] TX[%s]: EVENT_REPORT enable (%zu bytes)\n",
		mono_sec(), tx_mode_name(mode), sizeof(cmd));
	return diag_send_mode(fd, cmd, sizeof(cmd), mode);
}

/* Enable log packets for a given equip_id with all codes enabled */
static int send_log_config(int fd, uint16_t equip_id, uint16_t num_codes,
			   enum tx_mode mode)
{
	uint8_t cmd[4096];
	size_t total;
	int num_bytes;
	int i;

	/* LOG_CONFIG_SET_MASK format:
	 * [0]    cmd = 0x73
	 * [1-3]  padding
	 * [4-7]  operation = 3 (SET_MASK)
	 * [8-11] equip_id
	 * [12-15] num_items
	 * [16+]  bitmask (1 bit per log code)
	 */
	num_bytes = (num_codes + 7) / 8;
	total = 16 + num_bytes;
	if (total > sizeof(cmd))
		return -1;

	memset(cmd, 0, total);
	cmd[0] = DIAG_LOG_CONFIG_F;
	/* operation = SET_MASK (3), LE32 at offset 4 */
	cmd[4] = LOG_CONFIG_SET_MASK_OP;
	/* equip_id LE32 at offset 8 */
	cmd[8] = equip_id & 0xFF;
	cmd[9] = (equip_id >> 8) & 0xFF;
	/* num_items LE32 at offset 12 */
	cmd[12] = num_codes & 0xFF;
	cmd[13] = (num_codes >> 8) & 0xFF;
	/* Enable all log codes */
	for (i = 0; i < num_bytes; i++)
		cmd[16 + i] = 0xFF;

	fprintf(stderr, "[%9.3f] TX[%s]: LOG_CONFIG equip=%u codes=%u (%zu bytes)\n",
		mono_sec(), tx_mode_name(mode), equip_id, num_codes, total);
	return diag_send_mode(fd, cmd, total, mode);
}

/* DIAG command names for display */
static const char *diag_cmd_name(uint8_t cmd)
{
	switch (cmd) {
	case 0x00: return "VERSION";
	case 0x0C: return "STATUS";
	case 0x10: return "LOG";
	case 0x13: return "BAD_CMD";
	case 0x14: return "BAD_PARM";
	case 0x1F: return "MSG";
	case 0x24: return "DIAG_SUBSYS";
	case 0x4B: return "SUBSYS_CMD_VER2";
	case 0x60: return "EVENT_REPORT";
	case 0x61: return "QSR4_EXT_MSG";
	case 0x73: return "LOG_CONFIG";
	case 0x79: return "EXT_MSG";
	case 0x7D: return "EXT_MSG_CONFIG";
	case 0x98: return "QSR_EXT_MSG";
	default:   return "UNKNOWN";
	}
}

static void dump_ext_msg(const uint8_t *p, int len)
{
	uint8_t num_args;
	uint16_t line, ssid;
	uint32_t ts_lo, ts_hi;
	const char *fmt;
	int hdr_len;

	if (len < 12)
		return;

	num_args = p[1];
	ts_lo = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
	ts_hi = p[8] | (p[9] << 8) | (p[10] << 16) | (p[11] << 24);

	if (len < 20)
		return;

	line = p[12] | (p[13] << 8);
	ssid = p[14] | (p[15] << 8);

	hdr_len = 20 + num_args * 4;
	if (hdr_len >= len)
		fmt = "(truncated)";
	else
		fmt = (const char *)&p[hdr_len];

	fprintf(stderr, "  F3 SSID=%u line=%u args=%u ts=0x%08x%08x: %s\n",
		ssid, line, num_args, ts_hi, ts_lo, fmt);
}

static void dump_log_pkt(const uint8_t *p, int len)
{
	uint16_t log_len, log_code;

	if (len < 5)
		return;

	log_len = p[1] | (p[2] << 8);
	log_code = p[3] | (p[4] << 8);

	fprintf(stderr, "  LOG code=0x%04x len=%u\n", log_code, log_len);
}

static void dump_version(const uint8_t *p, int len)
{
	if (len < 24)
		return;

	fprintf(stderr, "  comp_date=%.11s comp_time=%.8s\n",
		(const char *)&p[1], (const char *)&p[12]);
	if (len >= 55)
		fprintf(stderr, "  mob_model=%u mob_sw_rev=%u\n",
			p[50] | (p[51] << 8), p[52] | (p[53] << 8));
}

static void dump_event(const uint8_t *p, int len)
{
	uint16_t elen;
	int off;
	int i;
	int nested = 0;

	/* Event report: [0]=cmd, [1-2]=len, [3+]=payload */
	if (len < 3)
		return;

	elen = p[1] | (p[2] << 8);
	fprintf(stderr, "  EVENT report_len=%u payload=%d\n", elen, len - 3);

	/*
	 * Some SDX55 reads bundle multiple EVENT_REPORT records into one outer
	 * report payload. Walk the payload and split any nested 0x60,len
	 * sequences so we can see each logical event separately.
	 */
	off = 3;
	while (off + 3 <= len) {
		uint16_t sub_len;

		if (p[off] != DIAG_EVENT_REPORT_F)
			break;

		sub_len = p[off + 1] | (p[off + 2] << 8);
		if (off + 3 + sub_len > len)
			break;

		fprintf(stderr, "  nested_event@%02x len=%u\n", off, sub_len);
		process_packet(&p[off], 3 + sub_len);
		nested++;
		off += 3 + sub_len;
	}

	if (nested)
		return;

	/*
	 * The SDX55 packets seen on this endpoint do not match the simple event
	 * list parser well. Dump the payload as structured hex instead; the
	 * useful data so far are embedded strings such as PD names.
	 */
	off = 3;
	for (i = 0; off < len && i < 4; i++) {
		int chunk = len - off;
		int j;

		if (chunk > 16)
			chunk = 16;

		fprintf(stderr, "  payload[%02x]:", off);
		for (j = 0; j < chunk; j++)
			fprintf(stderr, " %02x", p[off + j]);
		fprintf(stderr, "\n");
		off += chunk;
	}
}

static void process_packet(const uint8_t *p, size_t len)
{
	int i;

	if (len == 0)
		return;

	fprintf(stderr, "[%9.3f] DIAG 0x%02x (%s) %zu bytes:",
		mono_sec(), p[0], diag_cmd_name(p[0]), len);

	/* First 64 bytes hex */
	for (i = 0; i < (int)len && i < 64; i++)
		fprintf(stderr, " %02x", p[i]);
	if (len > 64)
		fprintf(stderr, " ...");
	fprintf(stderr, "\n");

	/* Print any ASCII strings >= 4 chars in the payload */
	{
		int in_str = 0, str_start = 0;

		for (i = 0; i < (int)len; i++) {
			if (p[i] >= 0x20 && p[i] < 0x7f) {
				if (!in_str) { in_str = 1; str_start = i; }
			} else {
				if (in_str && (i - str_start) >= 4) {
					fprintf(stderr, "  str@%d: \"%.*s\"\n",
						str_start, i - str_start,
						(const char *)&p[str_start]);
				}
				in_str = 0;
			}
		}
		if (in_str && ((int)len - str_start) >= 4)
			fprintf(stderr, "  str@%d: \"%.*s\"\n",
				str_start, (int)len - str_start,
				(const char *)&p[str_start]);
	}

	switch (p[0]) {
	case DIAG_VERNO_F:
		dump_version(p, len);
		break;
	case DIAG_EXT_MSG_F:
		dump_ext_msg(p, len);
		break;
	case DIAG_LOG_F:
		dump_log_pkt(p, len);
		break;
	case DIAG_EVENT_REPORT_F:
		dump_event(p, len);
		break;
	}

	fflush(stderr);
}

static size_t diag_packet_len(const uint8_t *p, size_t len)
{
	if (!len)
		return 0;

	switch (p[0]) {
	case DIAG_EVENT_REPORT_F:
		if (len < 3)
			return 0;
	{
		size_t want = (size_t)3 + (size_t)(p[1] | (p[2] << 8));

		return len < want ? len : want;
	}
	default:
		return len;
	}
}

static int looks_like_diag(const uint8_t *p, size_t len)
{
	if (!len)
		return 0;

	switch (p[0]) {
	case DIAG_VERNO_F:
	case DIAG_LOG_F:
	case DIAG_BAD_CMD_F:
	case DIAG_BAD_PARM_F:
	case DIAG_MSG_F:
	case DIAG_EVENT_REPORT_F:
	case DIAG_QSR4_EXT_MSG_F:
	case DIAG_LOG_CONFIG_F:
	case DIAG_EXT_MSG_F:
	case DIAG_EXT_MSG_CONFIG_F:
		return 1;
	default:
		return 0;
	}
}

static void process_raw_packet(const uint8_t *buf, size_t len)
{
	size_t used = len;
	size_t off = 0;

	if (!used)
		return;
	if (buf[used - 1] == HDLC_FLAG)
		used--;
	if (!used)
		return;

	if (looks_like_diag(buf, used)) {
		fprintf(stderr, "[%9.3f] RAW packet %zu bytes\n", mono_sec(), used);
		while (off < used && looks_like_diag(buf + off, used - off)) {
			size_t plen = diag_packet_len(buf + off, used - off);

			if (!plen)
				break;
			process_packet(buf + off, plen);
			off += plen;
		}
		if (off == used)
			return;
		fprintf(stderr, "[%9.3f] RAW trailing undecoded %zu bytes:",
			mono_sec(), used - off);
		for (size_t j = off; j < used && j < off + 48; j++)
			fprintf(stderr, " %02x", buf[j]);
		fprintf(stderr, "\n");
		return;
	}

	fprintf(stderr, "[%9.3f] RAW undecoded %zu bytes:", mono_sec(), used);
	for (size_t j = 0; j < used && j < 48; j++)
		fprintf(stderr, " %02x", buf[j]);
	fprintf(stderr, "\n");
}

static int process_hdlc_stream(const uint8_t *buf, size_t len,
			       uint8_t *decoded, size_t decsz)
{
	size_t start = 0;
	size_t i;
	int processed = 0;

	for (i = 0; i < len; i++) {
		int dlen;

		if (buf[i] != HDLC_FLAG)
			continue;
		if (i == start) {
			start = i + 1;
			continue;
		}

		dlen = hdlc_decode(buf + start, (i - start) + 1, decoded, decsz);
		start = i + 1;
		if (dlen <= 0 || !looks_like_diag(decoded, (size_t)dlen))
			continue;

		process_raw_packet(decoded, (size_t)dlen);
		processed++;
	}

	return processed;
}

static int recv_one(int fd, uint8_t *buf, size_t bufsz,
		    uint8_t *decoded, size_t decsz, int timeout_ms)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	ssize_t n;
	int rc;

	rc = poll(&pfd, 1, timeout_ms);
	if (rc <= 0)
		return rc; /* 0 = timeout, -1 = error */

	n = read(fd, buf, bufsz);
	if (n <= 0) {
		if (errno == EAGAIN || errno == EINTR)
			return 0;
		fprintf(stderr, "[%9.3f] read error: %s\n", mono_sec(), strerror(errno));
		return -1;
	}

	/*
	 * Archive the untouched stream. QSR4 messages (0x61) carry only a
	 * numeric hash on the wire, so they are meaningless here - but they
	 * decode to real strings when replayed through the modem's QSR4
	 * database (image/sdx55/qdsp6m.qdb):
	 *   scat -t qc -d raw.qmdl --qsr4-hash qdsp6m.qdb --msgs
	 * That database is what was missing when this tool was first written.
	 */
	if (g_raw_fp) {
		fwrite(buf, 1, (size_t)n, g_raw_fp);
		fflush(g_raw_fp);
	}

	/*
	 * Try HDLC decode first. If that does not produce a plausible packet,
	 * fall back to treating the read buffer as a raw DIAG payload with an
	 * optional trailing 0x7e delimiter.
	 */
	if (process_hdlc_stream(buf, (size_t)n, decoded, decsz) > 0)
		return (int)n;

	process_raw_packet(buf, (size_t)n);
	return (int)n;
}

static int enable_event_reports(int fd, uint8_t *buf, size_t bufsz,
				uint8_t *decoded, size_t decsz)
{
	enum tx_mode mode = TX_HDLC_FLAG;
	int ret;

	fprintf(stderr, "[%9.3f] enabling EVENT_REPORT with %s\n",
		mono_sec(), tx_mode_name(mode));
	send_event_report_enable(fd, mode);
	usleep(100000);
	ret = recv_one(fd, buf, bufsz, decoded, decsz, 400);
	if (ret > 0)
		return 0;

	/*
	 * Keep one fallback for comparison in case this endpoint changes under
	 * different firmware. Do not keep spamming unsupported config commands.
	 */
	mode = TX_RAW_FLAG;
	fprintf(stderr, "[%9.3f] EVENT_REPORT via %s fallback\n",
		mono_sec(), tx_mode_name(mode));
	send_event_report_enable(fd, mode);
	usleep(100000);
	ret = recv_one(fd, buf, bufsz, decoded, decsz, 400);
	if (ret > 0)
		return 0;

	return -1;
}

/* Send one DIAG command and return the HDLC-decoded reply. */
static int diag_txrx(int fd, const uint8_t *req, size_t reqlen,
		     uint8_t *out, size_t outsz, int timeout_ms)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	uint8_t raw[65536];
	ssize_t n;
	int dlen;

	if (diag_send_mode(fd, req, reqlen, TX_HDLC_FLAG) < 0)
		return -1;
	if (poll(&pfd, 1, timeout_ms) <= 0)
		return -1;

	n = read(fd, raw, sizeof(raw));
	if (n <= 0)
		return -1;

	if (g_raw_fp) {
		fwrite(raw, 1, (size_t)n, g_raw_fp);
		fflush(g_raw_fp);
	}

	dlen = hdlc_decode(raw, (size_t)n, out, outsz);
	if (dlen > 0 && out[0] == req[0])
		return dlen;

	/* Some replies come back unescaped with only a trailing flag. */
	{
		size_t len = (size_t)n;

		while (len && raw[len - 1] == HDLC_FLAG)
			len--;
		if (len > outsz)
			len = outsz;
		memcpy(out, raw, len);
		return (int)len;
	}
}

/*
 * Enable F3 (extended message) logging.
 *
 * The previous approach guessed a fixed SSID range (0..125) and always got
 * BAD_CMD: the ranges are firmware-specific, so a range this build does not
 * implement is simply rejected. Ask the modem which ranges it has
 * (EXT_MSG_CONFIG sub-command 0x01) and set a full runtime mask only over
 * those (sub-command 0x04), which is what libdiag itself does.
 *
 * Layout, little-endian:
 *   query    : 7D 01
 *   response : 7D 01 <u16 unk> <u16 num_ranges> <u16 unk> then num_ranges
 *              x { u16 first_ssid, u16 last_ssid }
 *   set mask : 7D 04 <u16 first> <u16 last> <u16 0> then (last-first+1)
 *              x u32 level mask (0xFFFFFFFF = every level)
 */
/* Try one SSID range with every level encoding. 0 = accepted. */
static int f3_set_range(int fd, unsigned int first, unsigned int last)
{
	static const uint32_t try_levels[] = { 0xFFFFFFFFu, 0x0000001Fu };
	unsigned int count = last - first + 1;
	uint8_t setreq[8 + 4 * 4096];
	uint8_t ack[8 + 4 * 4096];
	unsigned int attempt, j;
	int dlen;

	if (last < first || count > 4096)
		return -1;

	for (attempt = 0; attempt < 2; attempt++) {
		setreq[0] = DIAG_EXT_MSG_CONFIG_F;
		setreq[1] = MSG_EXT_CFG_SET_RT_MASK;	/* 0x04 */
		setreq[2] = first & 0xFF;
		setreq[3] = (first >> 8) & 0xFF;
		setreq[4] = last & 0xFF;
		setreq[5] = (last >> 8) & 0xFF;
		setreq[6] = 0;
		setreq[7] = 0;
		for (j = 0; j < count; j++) {
			uint32_t lv = try_levels[attempt];

			setreq[8 + 4 * j + 0] = lv & 0xFF;
			setreq[8 + 4 * j + 1] = (lv >> 8) & 0xFF;
			setreq[8 + 4 * j + 2] = (lv >> 16) & 0xFF;
			setreq[8 + 4 * j + 3] = (lv >> 24) & 0xFF;
		}

		dlen = diag_txrx(fd, setreq, 8 + 4 * count, ack, sizeof(ack), 800);
		if (dlen > 0 && ack[0] == DIAG_EXT_MSG_CONFIG_F &&
		    ack[1] == MSG_EXT_CFG_SET_RT_MASK)
			return 0;
	}

	/* Last resort: ask what levels this range supports, echo them back. */
	{
		uint8_t q[8];

		q[0] = DIAG_EXT_MSG_CONFIG_F;
		q[1] = 0x02;
		q[2] = first & 0xFF;
		q[3] = (first >> 8) & 0xFF;
		q[4] = last & 0xFF;
		q[5] = (last >> 8) & 0xFF;
		q[6] = 0;
		q[7] = 0;

		dlen = diag_txrx(fd, q, sizeof(q), ack, sizeof(ack), 800);
		if (dlen >= (int)(8 + 4 * count) &&
		    ack[0] == DIAG_EXT_MSG_CONFIG_F && ack[1] == 0x02) {
			setreq[0] = DIAG_EXT_MSG_CONFIG_F;
			setreq[1] = MSG_EXT_CFG_SET_RT_MASK;
			memcpy(&setreq[2], &ack[2], 6);
			memcpy(&setreq[8], &ack[8], 4 * count);
			dlen = diag_txrx(fd, setreq, 8 + 4 * count, ack,
					 sizeof(ack), 800);
			if (dlen > 0 && ack[0] == DIAG_EXT_MSG_CONFIG_F)
				return 0;
		}
	}

	return -1;
}

/*
 * A block is refused when *any* SSID inside it is unregistered in this build,
 * so bisect on failure: the subsystems that do exist still get enabled. This
 * matters because 0-130 (default/general, where CM and the mode controller
 * live) is refused wholesale even though sub-command 0x02 reports real levels
 * for it.
 */
/*
 * The big hammer: one command that sets the runtime mask of *every* SSID at
 * once, which is what QXDM sends when you ask it for all F3 messages. Worth
 * trying before walking the ranges, because a modem that takes this needs
 * nothing else.
 */
static int f3_set_all_masks(int fd, uint32_t level)
{
	uint8_t cmd[8], ack[64];
	int dlen;

	cmd[0] = DIAG_EXT_MSG_CONFIG_F;
	cmd[1] = MSG_EXT_CFG_SET_ALL_RT_MASKS;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = level & 0xFF;
	cmd[5] = (level >> 8) & 0xFF;
	cmd[6] = (level >> 16) & 0xFF;
	cmd[7] = (level >> 24) & 0xFF;

	dlen = diag_txrx(fd, cmd, sizeof(cmd), ack, sizeof(ack), 1000);
	if (dlen > 0 && ack[0] == DIAG_EXT_MSG_CONFIG_F &&
	    ack[1] == MSG_EXT_CFG_SET_ALL_RT_MASKS) {
		fprintf(stderr, "[%9.3f] F3: SET_ALL_RT_MASKS(0x%08x) accepted\n",
			mono_sec(), level);
		return 0;
	}

	fprintf(stderr, "[%9.3f] F3: SET_ALL_RT_MASKS(0x%08x) refused\n",
		mono_sec(), level);
	return -1;
}

/*
 * Read back what the modem says is actually enabled. This is the measurement
 * that separates "our commands never took effect" from "the masks are set and
 * the modem still will not emit" - and nothing so far has made that
 * distinction, which is why a silent capture has been so hard to interpret.
 */
static void f3_dump_runtime_mask(int fd, unsigned int first, unsigned int last)
{
	unsigned int count = last - first + 1;
	uint8_t q[8], rsp[8 + 4 * 4096];
	unsigned int j, set = 0;
	int dlen;

	if (last < first || count > 4096)
		return;

	q[0] = DIAG_EXT_MSG_CONFIG_F;
	q[1] = MSG_EXT_CFG_GET_RT_MASK;
	q[2] = first & 0xFF;
	q[3] = (first >> 8) & 0xFF;
	q[4] = last & 0xFF;
	q[5] = (last >> 8) & 0xFF;
	q[6] = 0;
	q[7] = 0;

	dlen = diag_txrx(fd, q, sizeof(q), rsp, sizeof(rsp), 1000);
	if (dlen < (int)(8 + 4 * count) || rsp[0] != DIAG_EXT_MSG_CONFIG_F ||
	    rsp[1] != MSG_EXT_CFG_GET_RT_MASK) {
		fprintf(stderr, "[%9.3f] F3: runtime mask %u-%u unreadable\n",
			mono_sec(), first, last);
		return;
	}

	for (j = 0; j < count; j++) {
		const uint8_t *p = &rsp[8 + 4 * j];
		uint32_t lv = p[0] | (p[1] << 8) | (p[2] << 16) |
			      ((uint32_t)p[3] << 24);

		if (lv)
			set++;
	}

	fprintf(stderr, "[%9.3f] F3: runtime mask %u-%u: %u/%u SSID(s) non-zero\n",
		mono_sec(), first, last, set, count);
}

static unsigned int f3_apply_range(int fd, unsigned int first, unsigned int last,
				   int depth)
{
	unsigned int mid, n;

	if (last < first)
		return 0;

	if (f3_set_range(fd, first, last) == 0) {
		n = last - first + 1;
		fprintf(stderr, "[%9.3f] F3: enabled SSID %u-%u (%u)\n",
			mono_sec(), first, last, n);
		return n;
	}

	if (first == last || depth >= 10) {
		fprintf(stderr, "[%9.3f] F3: SSID %u-%u rejected\n",
			mono_sec(), first, last);
		return 0;
	}

	mid = first + (last - first) / 2;
	n  = f3_apply_range(fd, first, mid, depth + 1);
	n += f3_apply_range(fd, mid + 1, last, depth + 1);
	return n;
}

/*
 * Enable log packets as well as F3 messages. Android's diag_mdlog sets both;
 * message masks alone produced a completely silent modem.
 */
static void enable_log_masks(int fd)
{
	static const struct {
		uint16_t equip;
		uint16_t items;
		const char *name;
	} tbl[] = {
		{ 0x01, 0x0fff, "1X"     },
		{ 0x04, 0x0ff7, "WCDMA"  },
		{ 0x05, 0x0ff7, "GSM"    },
		{ 0x07, 0x0b5e, "UMTS"   },
		{ 0x0b, 0x09ff, "LTE/NR" },
	};
	unsigned int i;

	for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
		unsigned int items = tbl[i].items;
		unsigned int nbytes = (items + 7) / 8;
		uint8_t cmd[16 + 8192];
		uint8_t ack[4096];
		int dlen;

		if (16 + nbytes > sizeof(cmd))
			continue;

		memset(cmd, 0, 16 + nbytes);
		cmd[0] = DIAG_LOG_CONFIG_F;			/* 0x73 */
		cmd[4] = LOG_CONFIG_SET_MASK_OP;		/* 3, LE32 */
		cmd[8] = tbl[i].equip & 0xFF;
		cmd[9] = (tbl[i].equip >> 8) & 0xFF;
		cmd[12] = items & 0xFF;
		cmd[13] = (items >> 8) & 0xFF;
		memset(&cmd[16], 0xFF, nbytes);		/* every log code */

		dlen = diag_txrx(fd, cmd, 16 + nbytes, ack, sizeof(ack), 800);
		if (dlen > 0 && ack[0] == DIAG_LOG_CONFIG_F)
			fprintf(stderr, "[%9.3f] LOG: enabled equip %s (0x%02x)\n",
				mono_sec(), tbl[i].name, tbl[i].equip);
		else
			fprintf(stderr, "[%9.3f] LOG: equip %s (0x%02x) rejected%s\n",
				mono_sec(), tbl[i].name, tbl[i].equip,
				(dlen > 0 && ack[0] == DIAG_BAD_CMD_F) ? " (BAD_CMD)" : "");
	}
}

/*
 * Enable F3 (extended message) logging.
 *
 * Ranges are firmware-specific, so ask the modem which it has
 * (EXT_MSG_CONFIG sub-command 0x01) and set masks over those (sub-command
 * 0x04), bisecting anything it refuses.
 */
static int enable_f3_messages(int fd)
{
	uint8_t rsp[8192];
	uint8_t req[2];
	unsigned int num_ranges, i, total = 0;
	int dlen;

	req[0] = DIAG_EXT_MSG_CONFIG_F;
	req[1] = 0x01;

	dlen = diag_txrx(fd, req, sizeof(req), rsp, sizeof(rsp), 1000);
	if (dlen < 8 || rsp[0] != DIAG_EXT_MSG_CONFIG_F || rsp[1] != 0x01) {
		fprintf(stderr, "[%9.3f] F3: SSID range query failed (dlen=%d)\n",
			mono_sec(), dlen);
		return -1;
	}

	num_ranges = (unsigned int)(rsp[4] | (rsp[5] << 8));
	fprintf(stderr, "[%9.3f] F3: modem reports %u SSID range(s)\n",
		mono_sec(), num_ranges);

	/*
	 * Try the all-at-once mask first. The per-range walk below still runs
	 * either way: a modem that took this will simply accept every range.
	 */
	f3_set_all_masks(fd, 0xFFFFFFFFu);

	if (8 + 4 * num_ranges > (unsigned int)dlen) {
		fprintf(stderr, "[%9.3f] F3: truncated range list\n", mono_sec());
		return -1;
	}

	for (i = 0; i < num_ranges; i++) {
		unsigned int off = 8 + 4 * i;
		unsigned int first = rsp[off] | (rsp[off + 1] << 8);
		unsigned int last  = rsp[off + 2] | (rsp[off + 3] << 8);

		total += f3_apply_range(fd, first, last, 0);
	}

	fprintf(stderr, "[%9.3f] F3: %u SSID(s) enabled across %u range(s)\n",
		mono_sec(), total, num_ranges);

	/*
	 * Now ask the modem what it thinks is enabled. Setting a mask and being
	 * told "ok" says nothing about whether it stuck; this does. If these all
	 * read back zero the commands are not taking effect, and if they read
	 * back non-zero while the capture stays empty the masks are not the
	 * problem at all.
	 */
	for (i = 0; i < num_ranges; i++) {
		unsigned int off = 8 + 4 * i;
		unsigned int first = rsp[off] | (rsp[off + 1] << 8);
		unsigned int last  = rsp[off + 2] | (rsp[off + 3] << 8);

		f3_dump_runtime_mask(fd, first, last);
	}

	enable_log_masks(fd);
	return total ? 0 : -1;
}

static void one_shot_probe(int fd, uint8_t *buf, size_t bufsz,
			   uint8_t *decoded, size_t decsz)
{
	fprintf(stderr, "[%9.3f] probe: VERSION via raw/no-flag\n", mono_sec());
	send_version_req(fd, TX_RAW_ONLY);
	usleep(100000);
	recv_one(fd, buf, bufsz, decoded, decsz, 250);

	fprintf(stderr, "[%9.3f] probe: VERSION via raw+flag\n", mono_sec());
	send_version_req(fd, TX_RAW_FLAG);
	usleep(100000);
	recv_one(fd, buf, bufsz, decoded, decsz, 250);

	fprintf(stderr, "[%9.3f] probe: VERSION via hdlc+flag\n", mono_sec());
	send_version_req(fd, TX_HDLC_FLAG);
	usleep(100000);
	recv_one(fd, buf, bufsz, decoded, decsz, 250);

	if (enable_event_reports(fd, buf, bufsz, decoded, decsz) == 0)
		fprintf(stderr, "[%9.3f] EVENT_REPORT enabled\n", mono_sec());
	else
		fprintf(stderr, "[%9.3f] EVENT_REPORT enable not confirmed\n", mono_sec());

	/* F3 messages: the whole point of the capture. */
	enable_f3_messages(fd);

	/*
	 * Superseded by enable_f3_messages() - kept compiled for reference.
	 * These guessed a fixed SSID range and only ever produced BAD_CMD.
	 */
	if (0) {
		send_msg_config_range(fd, 0, 125, TX_HDLC_FLAG);
		send_log_config(fd, 1, 2048, TX_HDLC_FLAG);
	}
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/wwan0qcdm0";
	uint8_t buf[65536];
	uint8_t decoded[65536];
	int fd;
	int sent_cmds = 0;
	double start;

	if (argc > 1)
		dev = argv[1];
	if (argc > 2) {
		g_raw_fp = fopen(argv[2], "wb");
		if (!g_raw_fp) {
			perror(argv[2]);
			return 1;
		}
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return 1;
	}

	start = mono_sec();
	fprintf(stderr, "[%9.3f] opened %s — event-report capture mode\n",
		mono_sec(), dev);

	while (!g_stop) {
		int ret = recv_one(fd, buf, sizeof(buf), decoded, sizeof(decoded), 500);

		if (ret < 0)
			break; /* I/O error = modem crashed */

		if (ret > 0 && !sent_cmds) {
			fprintf(stderr, "[%9.3f] channel alive, running one-shot probe\n",
				mono_sec());
			one_shot_probe(fd, buf, sizeof(buf), decoded, sizeof(decoded));
			fprintf(stderr, "[%9.3f] probe done, continuing passive listen...\n",
				mono_sec());
			sent_cmds = 1;
		} else if (!sent_cmds && mono_sec() - start > 1.5) {
			fprintf(stderr, "[%9.3f] no unsolicited DIAG traffic, forcing one-shot probe\n",
				mono_sec());
			one_shot_probe(fd, buf, sizeof(buf), decoded, sizeof(decoded));
			fprintf(stderr, "[%9.3f] forced probe done, continuing passive listen...\n",
				mono_sec());
			sent_cmds = 1;
		}
	}

	fprintf(stderr, "[%9.3f] exiting\n", mono_sec());
	close(fd);
	return 0;
}
