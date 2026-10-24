/*
 * ============================================================================
 *  ZenithOS v6.0 - netdev/checksum.h - Internet checksum (RFC 1071)
 *
 *  Status: [已编译·未接线] skeleton. Pure C, no hardware access, so unlike the
 *  NIC driver this module is verifiable on the build host: checksum_selftest()
 *  runs two hand-computed known-answer vectors (see checksum.c).
 *
 *  Scope: this is the ones' complement Internet checksum for IPv4, ICMP, UDP
 *  and TCP - i.e. everything above the Ethernet header. The Ethernet FCS is a
 *  different animal (CRC-32, computed by the NIC hardware, see rtl8139.h and
 *  docs/NETWORK.md), hence the module name says "checksum", not "crc".
 *
 *  Convention: every function returns the checksum in the byte order it has
 *  on the wire (big-endian 16-bit value). Callers store or compare it
 *  directly; there is no htons()/ntohs() in the kernel yet.
 * ============================================================================
 */
#ifndef ZENITH_CHECKSUM_H
#define ZENITH_CHECKSUM_H

#include "zenith.h"

/* IP protocol numbers (IANA). */
#define IP_PROTO_ICMP   1
#define IP_PROTO_IGMP   2
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

/*
 * Accumulate `len` bytes of `data` into a running ones' complement sum
 * (summing 16-bit big-endian words built up from bytes, so the result is
 * independent of host byte order and of buffer alignment; a trailing odd
 * byte is padded with a zero byte per RFC 1071 4.1). May be called several
 * times with the running sum to checksum non-contiguous data.
 */
uint32_t checksum_partial(const void *data, size_t len, uint32_t sum);

/* Fold a 32-bit running sum to the final 16-bit ones' complement checksum. */
uint16_t checksum_fold(uint32_t sum);

/* One-shot: checksum a contiguous region. */
uint16_t checksum_mem(const void *data, size_t len);

/*
 * IPv4 header checksum. `hlen` is the IHL*4 header length; the caller must
 * zero the checksum field first. Returns the value to store in the wire
 * field. A header that already carries a correct in-place checksum folds to
 * 0, so "return == 0" doubles as the validation test. Returns 0 on malformed
 * input (hlen < 20 or not a multiple of 4); callers pre-validate IHL.
 */
uint16_t checksum_ip(const void *iph, size_t hlen);

/*
 * Transport checksum over the RFC 768/793 pseudo-header: src/dst are the
 * IPv4 addresses as big-endian 32-bit values (first octet in the high byte,
 * i.e. exactly the 4 bytes in packet order), `proto` is an IP_PROTO_*
 * constant, `seg`/`seg_len` cover the whole UDP datagram (8-byte header +
 * payload) or TCP segment (20-byte header + payload). The transport checksum
 * field must be zero on input. For UDP, 0 in the field also means "no
 * checksum" (IPv4 only) - see docs/NETWORK.md.
 */
uint16_t checksum_l4(uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                     const void *seg, size_t seg_len);

uint16_t checksum_tcp(uint32_t src_ip, uint32_t dst_ip, const void *seg,
                      size_t seg_len);
uint16_t checksum_udp(uint32_t src_ip, uint32_t dst_ip, const void *seg,
                      size_t total_len);

/*
 * Known-answer self-test against hand-computed vectors (see checksum.c):
 *  0 = pass, nonzero = failing case number. Pure, no allocation, no I/O;
 *  intended to run from the kernel init path once this code is wired in.
 */
int checksum_selftest(void);

#endif /* ZENITH_CHECKSUM_H */
