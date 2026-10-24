/*
 * lm_net.c - what a machine does with a frame once it has one
 *
 * Two protocols and no more: ARP, to turn an address into a hardware address,
 * and ICMP echo, to see whether anything is there. They are enough to prove the
 * adapter end to end in both directions and enough for a caller to ask "is the
 * network real?", which is the question a boot test wants answered.
 *
 * The addresses are the ones a user-mode network hands out (10.0.2.15 for the
 * guest, 10.0.2.2 for the gateway). Nothing here configures anything: a real
 * stack would run DHCP or take an address from a file, and that is a milestone
 * of its own - these constants keep this one about the frames.
 *
 * Everything is synchronous and polled, like the driver under it. `net_ping`
 * sends an echo request and spins on the receive ring until the matching reply
 * arrives or its deadline passes; a task cannot be switched out from inside a
 * syscall, so it is the caller's deadline that bounds the wait.
 */

#include "lm.h"

#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800
#define IP_PROTO_ICMP 1

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

#define ARP_WHO_HAS 0x0001
#define ARP_IS_AT   0x0002

/* The Ethernet header is 14 bytes, and every field below is an offset from the
 * start of the frame. */
#define ETH_DST        0
#define ETH_SRC        6
#define ETH_TYPE       12
#define IPV4_START     14
#define IPV4_VERSION   14
#define IPV4_LENGTH    16
#define IPV4_ID        18
#define IPV4_TTL       22
#define IPV4_PROTO     23
#define IPV4_CHECKSUM  24
#define IPV4_SOURCE    26
#define IPV4_TARGET    30
#define ICMP_START     34
#define ICMP_TYPE      34
#define ICMP_CHECKSUM  36
#define ICMP_ID        38
#define ICMP_SEQ       40
#define ICMP_DATA      42

#define ARP_OP         20
#define ARP_SENDER_MAC 22
#define ARP_SENDER_IP  28
#define ARP_TARGET_MAC 32
#define ARP_TARGET_IP  38

#define ARP_FRAME_BYTES 42
#define NET_FRAME_MAX   2048
#define IPV4_BYTES      20
#define ICMP_BYTES      8
#define PING_DATA_BYTES 32
#define PING_FRAME_BYTES (ICMP_START + ICMP_BYTES + PING_DATA_BYTES)

static const u8 local_ip[4] = {10, 0, 2, 15};
static const u8 gateway_ip[4] = {10, 0, 2, 2};

static u8 gateway_mac[6];
static u8 local_mac[6];
static int have_gateway;
static u16 next_id;

/*
 * Waiting for the clock, from the two contexts this runs in - and they are not
 * the same wait.
 *
 * In a syscall interrupts are off by design, so waiting means opening them
 * again: hlt parks the CPU until the next interrupt, which at 100 Hz is the tick
 * itself. That is how SYS_SLEEP waits, and it is safe for the same reason - a
 * task cannot be switched out of a ring-0 frame, so nothing else runs until this
 * call finishes.
 *
 * At boot there is no task to switch to yet, so a tick is not something to wait
 * for; the ARP self-test simply polls the ring while the clock is stopped. It
 * always answers long before the budget runs out, and the budget is what bounds
 * the wait when nothing answers at all.
 */
static void wait_for_tick(void)
{
    __asm__ volatile("sti; hlt" ::: "memory");
}

static void wait_for_frame(void)
{
    for (int i = 0; i < 2000; i++) {
        __asm__ volatile("pause");
    }
}

static void put_be16(u8 *at, u16 value)
{
    at[0] = (u8)(value >> 8);
    at[1] = (u8)value;
}

static u16 get_be16(const u8 *at)
{
    return (u16)(((u16)at[0] << 8) | at[1]);
}

/* The Internet checksum: one's complement of the one's complement sum of the
 * 16-bit words, with the odd byte padded. Both IP and ICMP use it, which is the
 * only reason this is five lines instead of a library. */
static u16 checksum(const u8 *data, u32 length)
{
    u32 sum = 0;
    for (u32 i = 0; i + 1 < length; i += 2) {
        sum += get_be16(data + i);
    }
    if ((length & 1) != 0) {
        sum += (u32)data[length - 1] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (u16)~sum;
}

static void build_ethernet(u8 *frame, const u8 *target_mac, u16 type)
{
    for (int i = 0; i < 6; i++) {
        frame[ETH_DST + i] = target_mac[i];
        frame[ETH_SRC + i] = local_mac[i];
    }
    put_be16(frame + ETH_TYPE, type);
}

/* htype, ptype, hlen, plen and the operation, then four address fields of two
 * different lengths: the target's hardware address is what the request asks
 * for, so it is zero, and the target's protocol address follows it, not the
 * other way round. */
static void build_arp_request(u8 *frame)
{
    static const u8 broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    memset(frame, 0, ARP_FRAME_BYTES);
    build_ethernet(frame, broadcast, ETH_TYPE_ARP);

    put_be16(frame + 14, 0x0001);           /* Ethernet */
    put_be16(frame + 16, ETH_TYPE_IPV4);
    frame[18] = 6;                          /* hardware address length */
    frame[19] = 4;                          /* protocol address length */
    put_be16(frame + ARP_OP, ARP_WHO_HAS);
    for (int i = 0; i < 6; i++) {
        frame[ARP_SENDER_MAC + i] = local_mac[i];
    }
    for (int i = 0; i < 4; i++) {
        frame[ARP_SENDER_IP + i] = local_ip[i];
        frame[ARP_TARGET_IP + i] = gateway_ip[i];
    }
}

/* A complete echo request: Ethernet, IPv4 and ICMP, with both checksums filled
 * in. The payload is a counter pattern so a reply that is not ours is easy to
 * tell apart from one that is. */
static void build_ping(u8 *frame, u16 id, u16 seq)
{
    memset(frame, 0, PING_FRAME_BYTES);
    build_ethernet(frame, gateway_mac, ETH_TYPE_IPV4);

    frame[IPV4_VERSION] = 0x45;             /* version 4, five 32-bit words */
    put_be16(frame + IPV4_LENGTH, IPV4_BYTES + ICMP_BYTES + PING_DATA_BYTES);
    put_be16(frame + IPV4_ID, id);
    put_be16(frame + IPV4_ID + 2, 0);       /* no fragmentation */
    frame[IPV4_TTL] = 64;
    frame[IPV4_PROTO] = IP_PROTO_ICMP;
    for (int i = 0; i < 4; i++) {
        frame[IPV4_SOURCE + i] = local_ip[i];
        frame[IPV4_TARGET + i] = gateway_ip[i];
    }
    put_be16(frame + IPV4_CHECKSUM, checksum(frame + IPV4_START, IPV4_BYTES));

    frame[ICMP_TYPE] = ICMP_ECHO_REQUEST;
    frame[ICMP_TYPE + 1] = 0;
    put_be16(frame + ICMP_ID, id);
    put_be16(frame + ICMP_SEQ, seq);
    for (u32 i = 0; i < PING_DATA_BYTES; i++) {
        frame[ICMP_DATA + i] = (u8)(i + seq);
    }
    put_be16(frame + ICMP_CHECKSUM,
             checksum(frame + ICMP_START, ICMP_BYTES + PING_DATA_BYTES));
}

static int wait_arp_reply(void)
{
    static u8 frame[NET_FRAME_MAX];

    for (int spin = 0; spin < 200000; spin++) {
        u32 length = 0;
        int got = e1000_recv(frame, &length);
        if (got < 0) {
            return -1;
        }
        if (got > 0 && length >= ARP_FRAME_BYTES &&
            get_be16(frame + ETH_TYPE) == ETH_TYPE_ARP &&
            get_be16(frame + ARP_OP) == ARP_IS_AT &&
            frame[ARP_SENDER_IP] == gateway_ip[0] &&
            frame[ARP_SENDER_IP + 1] == gateway_ip[1] &&
            frame[ARP_SENDER_IP + 2] == gateway_ip[2] &&
            frame[ARP_SENDER_IP + 3] == gateway_ip[3]) {
            for (int i = 0; i < 6; i++) {
                gateway_mac[i] = frame[ARP_SENDER_MAC + i];
            }
            return 0;
        }
        wait_for_frame();
    }
    return -1;
}

int net_init(void)
{
    if (!e1000_present()) {
        return -1;
    }

    const u8 *mac = e1000_mac();
    for (int i = 0; i < 6; i++) {
        local_mac[i] = mac[i];
    }
    next_id = 1;

    static u8 request[ARP_FRAME_BYTES];
    build_arp_request(request);
    if (e1000_send(request, ARP_FRAME_BYTES) != 0) {
        kputs("[net] FAIL the ARP request would not send\r\n");
        return -1;
    }

    if (wait_arp_reply() != 0) {
        kputs("[net] no answer to the ARP request net=none\r\n");
        return -1;
    }
    have_gateway = 1;

    kputs("[net] 10.0.2.2 at ");
    for (int i = 0; i < 6; i++) {
        kput_hex(gateway_mac[i], 2);
        if (i < 5) {
            kputc(':');
        }
    }
    kputs(" net=ok\r\n");
    return 0;
}

const u8 *net_gateway_mac(void)
{
    return have_gateway ? gateway_mac : NULL;
}

/* Ask the network who owns `ip`. The gateway's address was resolved at boot and
 * the answer is cached, so only another host on the same network costs a
 * request - and the cache holds one entry, which is why a second one is looked
 * up rather than remembered. */
static int resolve(const u8 *ip, u8 *out_mac, u32 timeout_ticks)
{
    static u8 frame[ARP_FRAME_BYTES];
    static u8 reply[NET_FRAME_MAX];

    if (ip[0] == gateway_ip[0] && ip[1] == gateway_ip[1] &&
        ip[2] == gateway_ip[2] && ip[3] == gateway_ip[3]) {
        for (int i = 0; i < 6; i++) {
            out_mac[i] = gateway_mac[i];
        }
        return 0;
    }

    build_arp_request(frame);
    for (int i = 0; i < 4; i++) {
        frame[ARP_TARGET_IP + i] = ip[i];
    }
    if (e1000_send(frame, ARP_FRAME_BYTES) != 0) {
        return -1;
    }

    u64 deadline = cpu_this()->ticks + timeout_ticks;
    while (cpu_this()->ticks < deadline) {
        u32 length = 0;
        int got = e1000_recv(reply, &length);
        if (got < 0) {
            return -1;
        }
        if (got == 0) {
            wait_for_tick();
            continue;
        }
        if (length < ARP_FRAME_BYTES ||
            get_be16(reply + ETH_TYPE) != ETH_TYPE_ARP ||
            get_be16(reply + ARP_OP) != ARP_IS_AT ||
            reply[ARP_SENDER_IP] != ip[0] || reply[ARP_SENDER_IP + 1] != ip[1] ||
            reply[ARP_SENDER_IP + 2] != ip[2] ||
            reply[ARP_SENDER_IP + 3] != ip[3]) {
            continue;
        }
        for (int i = 0; i < 6; i++) {
            out_mac[i] = reply[ARP_SENDER_MAC + i];
        }
        return 0;
    }
    return -1;
}

/*
 * One echo request, and the round trip in milliseconds measured with the timer
 * tick - 10 ms of resolution is what a 100 Hz clock can honestly offer, so a
 * reply that beats the first tick reports 10 rather than 0.
 */
int net_ping(const u8 *ip, u32 timeout_ticks)
{
    static u8 frame[PING_FRAME_BYTES];
    static u8 reply[NET_FRAME_MAX];
    u8 target[6];

    if (!have_gateway || ip == NULL || timeout_ticks == 0) {
        return -1;
    }
    if (resolve(ip, target, timeout_ticks) != 0) {
        return -1;
    }

    u16 id = next_id++;
    u16 seq = 1;
    build_ping(frame, id, seq);
    for (int i = 0; i < 6; i++) {
        frame[ETH_DST + i] = target[i];
    }
    for (int i = 0; i < 4; i++) {
        frame[IPV4_TARGET + i] = ip[i];
    }
    put_be16(frame + IPV4_CHECKSUM, 0);
    put_be16(frame + IPV4_CHECKSUM, checksum(frame + IPV4_START, IPV4_BYTES));
    put_be16(frame + ICMP_CHECKSUM, 0);
    put_be16(frame + ICMP_CHECKSUM,
             checksum(frame + ICMP_START, ICMP_BYTES + PING_DATA_BYTES));

    u64 start = cpu_this()->ticks;
    if (e1000_send(frame, PING_FRAME_BYTES) != 0) {
        return -1;
    }

    u64 deadline = start + timeout_ticks;
    while (cpu_this()->ticks < deadline) {
        u32 length = 0;
        int got = e1000_recv(reply, &length);
        if (got < 0) {
            return -1;
        }
        if (got == 0) {
            wait_for_tick();
            continue;
        }
        if (length < PING_FRAME_BYTES ||
            get_be16(reply + ETH_TYPE) != ETH_TYPE_IPV4 ||
            reply[IPV4_PROTO] != IP_PROTO_ICMP ||
            reply[ICMP_TYPE] != ICMP_ECHO_REPLY ||
            get_be16(reply + ICMP_ID) != id ||
            get_be16(reply + ICMP_SEQ) != seq) {
            continue;                   /* somebody else's frame */
        }
        u64 elapsed = cpu_this()->ticks - start;
        if (elapsed == 0) {
            elapsed = 1;                /* faster than this clock can see */
        }
        return (int)(elapsed * (1000 / LM_TIMER_HZ));
    }
    return -1;
}

int net_ready(void)
{
    return have_gateway;
}

const u8 *net_local_ip(void)
{
    return local_ip;
}
