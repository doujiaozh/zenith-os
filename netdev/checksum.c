/*
 * ZenithOS v6.0 - netdev/checksum.c - RFC 1071 ones' complement checksum.
 *
 * Assembly note (RFC 1071 1.C): summing 16-bit words of the packet in the
 * host's own byte order yields the same result as summing them in network
 * order - with all words byte-swapped. This implementation sidesteps the
 * subtlety entirely by building each 16-bit word out of its two wire bytes,
 * so the running sum is already in wire order and no swap is needed at the
 * end. It also costs nothing for unaligned buffers, which matters because
 * protocol headers land wherever the packet lands.
 */
#include "zenith.h"
#include "checksum.h"

uint32_t checksum_partial(const void *data, size_t len, uint32_t sum)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i = 0;

    if (p == NULL)
        return sum;
    for (; i + 1 < len; i += 2)
        sum += ((uint32_t)p[i] << 8) | (uint32_t)p[i + 1];
    if (i < len)
        sum += (uint32_t)p[i] << 8;    /* odd trailing byte, zero-padded */
    return sum;
}

uint16_t checksum_fold(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

uint16_t checksum_mem(const void *data, size_t len)
{
    return checksum_fold(checksum_partial(data, len, 0));
}

uint16_t checksum_ip(const void *iph, size_t hlen)
{
    if (iph == NULL || hlen < 20 || (hlen & 3) != 0)
        return 0;
    return checksum_fold(checksum_partial(iph, hlen, 0));
}

uint16_t checksum_l4(uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                     const void *seg, size_t seg_len)
{
    uint32_t sum = 0;

    /* 12-byte pseudo header in wire order: src(4) dst(4) 0x00 proto len(2) */
    sum += ((src_ip >> 24) & 0xFFu) << 8 | ((src_ip >> 16) & 0xFFu);
    sum += ((src_ip >> 8) & 0xFFu) << 8 | (src_ip & 0xFFu);
    sum += ((dst_ip >> 24) & 0xFFu) << 8 | ((dst_ip >> 16) & 0xFFu);
    sum += ((dst_ip >> 8) & 0xFFu) << 8 | (dst_ip & 0xFFu);
    sum += (uint32_t)proto;
    sum += ((seg_len >> 8) & 0xFFu) << 8 | (seg_len & 0xFFu);
    return checksum_fold(checksum_partial(seg, seg_len, sum));
}

uint16_t checksum_tcp(uint32_t src_ip, uint32_t dst_ip, const void *seg,
                      size_t seg_len)
{
    return checksum_l4(src_ip, dst_ip, IP_PROTO_TCP, seg, seg_len);
}

uint16_t checksum_udp(uint32_t src_ip, uint32_t dst_ip, const void *seg,
                      size_t total_len)
{
    return checksum_l4(src_ip, dst_ip, IP_PROTO_UDP, seg, total_len);
}

/*
 * Self-test vectors, both hand-computed (arithmetic shown in the comments so
 * a reviewer can re-verify without running anything):
 *
 * IPv4 header, checksum field zeroed:
 *   45 00 00 28 00 00 00 00 40 06 00 00 c0 a8 01 01 c0 a8 01 02
 *   words: 4500+0028+4006+c0a8+0101+c0a8+0102 = 0x20881
 *   fold: 0x0881+0x0002 = 0x0883; complement = 0xF77C
 * Same header with F7 7C in the checksum field sums to 0x2FFFD, which folds
 * to 0xFFFF and complements to 0 - a valid header tests as 0.
 *
 * UDP over 192.168.1.1 -> 192.168.1.2, proto 17, UDP length 11:
 *   pseudo: c0a8+0101+c0a8+0102+0011+000b = 0x1836F
 *   seg:    1234 0035 000b 0000 61 62 63 (odd length, zero-pad the 'c')
 *           1234+0035+000b+0000+6162+6300 = 0xD6D6
 *   total 0x1836F + 0xD6D6 = 0x25A45; fold 0x5A45+0x0002 = 0x5A47;
 *   complement = 0xA5B8
 */
int checksum_selftest(void)
{
    static const uint8_t ip_zeroed[20] = {
        0x45, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x06, 0x00, 0x00, 0xC0, 0xA8, 0x01, 0x01,
        0xC0, 0xA8, 0x01, 0x02
    };
    static const uint8_t ip_filled[20] = {
        0x45, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x06, 0xF7, 0x7C, 0xC0, 0xA8, 0x01, 0x01,
        0xC0, 0xA8, 0x01, 0x02
    };
    static const uint8_t udp_seg[11] = {
        0x12, 0x34, 0x00, 0x35, 0x00, 0x0B, 0x00, 0x00,
        0x61, 0x62, 0x63
    };

    if (checksum_ip(ip_zeroed, 20) != 0xF77Cu)
        return 1;                    /* header checksum computation wrong */
    if (checksum_ip(ip_filled, 20) != 0x0000u)
        return 2;                    /* a valid header must fold to 0 */
    if (checksum_ip(ip_zeroed, 19) != 0x0000u)
        return 3;                    /* malformed hlen must be rejected */
    if (checksum_udp(0xC0A80101u, 0xC0A80102u, udp_seg, 11) != 0xA5B8u)
        return 4;                    /* UDP pseudo-header computation wrong */
    return 0;
}
