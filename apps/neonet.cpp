#include "../include/neobench.h"
#include "../lib/string.h"

// NeoNet - Minimal TCP/IP Stack for NeoBench
// Features: ARP, IP, ICMP, UDP, TCP, DNS, routing, ifconfig, ping, netstat

namespace neonet {

// --- Data Types ---
struct MacAddr { unsigned char b[6]; };
struct IpAddr { unsigned char b[4]; };

bool ip_eq(const IpAddr& a, const IpAddr& b) {
    return a.b[0]==b.b[0] && a.b[1]==b.b[1] && a.b[2]==b.b[2] && a.b[3]==b.b[3];
}

void ip_copy(IpAddr& dst, const IpAddr& src) {
    for (int i = 0; i < 4; i++) dst.b[i] = src.b[i];
}

void mac_copy(MacAddr& dst, const MacAddr& src) {
    for (int i = 0; i < 6; i++) dst.b[i] = src.b[i];
}

void ip_from_str(IpAddr& ip, const char* s) {
    int idx = 0;
    unsigned int val = 0;
    for (int i = 0; s[i] && idx < 4; i++) {
        if (s[i] == '.') { ip.b[idx++] = (unsigned char)val; val = 0; }
        else if (s[i] >= '0' && s[i] <= '9') val = val * 10 + (s[i] - '0');
    }
    if (idx < 4) ip.b[idx] = (unsigned char)val;
}

void ip_to_str(char* buf, const IpAddr& ip) {
    ksprintf(buf, 20, "%d.%d.%d.%d", ip.b[0], ip.b[1], ip.b[2], ip.b[3]);
}

void mac_to_str(char* buf, const MacAddr& m) {
    ksprintf(buf, 20, "%02x:%02x:%02x:%02x:%02x:%02x",
             m.b[0], m.b[1], m.b[2], m.b[3], m.b[4], m.b[5]);
}

// --- Network Interface ---
struct NetInterface {
    char name[16];
    IpAddr ip;
    IpAddr subnet;
    IpAddr gateway;
    MacAddr mac;
    bool up;
    unsigned int rx_packets;
    unsigned int tx_packets;
    unsigned int rx_bytes;
    unsigned int tx_bytes;
    unsigned int rx_errors;
    unsigned int tx_errors;
};

static const int MAX_IFACES = 4;
static NetInterface ifaces[MAX_IFACES];
static int iface_count = 0;

// --- ARP Table ---
struct ArpEntry {
    IpAddr ip;
    MacAddr mac;
    unsigned int timestamp;
    bool valid;
};

static const int ARP_TABLE_SIZE = 32;
static ArpEntry arp_table[ARP_TABLE_SIZE];
static int arp_count = 0;

void arp_add(const IpAddr& ip, const MacAddr& mac) {
    // Update existing
    for (int i = 0; i < arp_count; i++) {
        if (arp_table[i].valid && ip_eq(arp_table[i].ip, ip)) {
            mac_copy(arp_table[i].mac, mac);
            arp_table[i].timestamp = neo::timer::get_ticks();
            return;
        }
    }
    // Add new
    if (arp_count < ARP_TABLE_SIZE) {
        arp_table[arp_count].valid = true;
        ip_copy(arp_table[arp_count].ip, ip);
        mac_copy(arp_table[arp_count].mac, mac);
        arp_table[arp_count].timestamp = neo::timer::get_ticks();
        arp_count++;
    }
}

bool arp_lookup(const IpAddr& ip, MacAddr& mac) {
    for (int i = 0; i < arp_count; i++) {
        if (arp_table[i].valid && ip_eq(arp_table[i].ip, ip)) {
            mac_copy(mac, arp_table[i].mac);
            return true;
        }
    }
    return false;
}

void arp_display() {
    neo::display::printf("ARP Table (%d entries):\n", arp_count);
    neo::display::puts("+-----------------+-------------------+----------+\n");
    neo::display::puts("| IP Address      | MAC Address       | Age (s)  |\n");
    neo::display::puts("+-----------------+-------------------+----------+\n");
    unsigned int now = neo::timer::get_ticks();
    for (int i = 0; i < arp_count; i++) {
        if (!arp_table[i].valid) continue;
        char ipstr[20], macstr[20];
        ip_to_str(ipstr, arp_table[i].ip);
        mac_to_str(macstr, arp_table[i].mac);
        unsigned int age = (now - arp_table[i].timestamp) / 50; // approx seconds
        neo::display::printf("| %-15s | %-17s | %-8d |\n", ipstr, macstr, age);
    }
    neo::display::puts("+-----------------+-------------------+----------+\n");
}

// --- Route Table ---
struct Route {
    IpAddr dest;
    IpAddr mask;
    IpAddr gateway;
    char iface[16];
    int metric;
    bool valid;
};

static const int MAX_ROUTES = 16;
static Route routes[MAX_ROUTES];
static int route_count = 0;

void route_add(const IpAddr& dest, const IpAddr& mask, const IpAddr& gw, const char* iface, int metric) {
    if (route_count >= MAX_ROUTES) return;
    Route& r = routes[route_count];
    ip_copy(r.dest, dest);
    ip_copy(r.mask, mask);
    ip_copy(r.gateway, gw);
    neo_strncpy(r.iface, iface, 15);
    r.iface[15] = 0;
    r.metric = metric;
    r.valid = true;
    route_count++;
}

void route_display() {
    neo::display::printf("Routing Table (%d entries):\n", route_count);
    neo::display::puts("+----------------+----------------+----------------+--------+--------+\n");
    neo::display::puts("| Destination    | Netmask        | Gateway        | Iface  | Metric |\n");
    neo::display::puts("+----------------+----------------+----------------+--------+--------+\n");
    for (int i = 0; i < route_count; i++) {
        if (!routes[i].valid) continue;
        char d[20], m[20], g[20];
        ip_to_str(d, routes[i].dest);
        ip_to_str(m, routes[i].mask);
        ip_to_str(g, routes[i].gateway);
        neo::display::printf("| %-14s | %-14s | %-14s | %-6s | %-6d |\n",
                             d, m, g, routes[i].iface, routes[i].metric);
    }
    neo::display::puts("+----------------+----------------+----------------+--------+--------+\n");
}

bool route_lookup(const IpAddr& dest, Route& best) {
    bool found = false;
    int best_metric = 9999;
    for (int i = 0; i < route_count; i++) {
        if (!routes[i].valid) continue;
        bool match = true;
        for (int j = 0; j < 4; j++) {
            if ((dest.b[j] & routes[i].mask.b[j]) != (routes[i].dest.b[j] & routes[i].mask.b[j])) {
                match = false; break;
            }
        }
        if (match && routes[i].metric < best_metric) {
            best = routes[i];
            best_metric = routes[i].metric;
            found = true;
        }
    }
    return found;
}

// --- IP Layer ---
struct IpHeader {
    unsigned char ver_ihl;
    unsigned char tos;
    unsigned short total_len;
    unsigned short id;
    unsigned short flags_frag;
    unsigned char ttl;
    unsigned char protocol;
    unsigned short checksum;
    IpAddr src;
    IpAddr dst;
};

static unsigned short ip_id_counter = 1;

unsigned short ip_checksum(const unsigned char* data, int len) {
    unsigned int sum = 0;
    for (int i = 0; i < len - 1; i += 2) {
        sum += ((unsigned int)data[i] << 8) | data[i + 1];
    }
    if (len & 1) sum += (unsigned int)data[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (unsigned short)(~sum & 0xFFFF);
}

void ip_build_header(IpHeader& hdr, const IpAddr& src, const IpAddr& dst,
                     unsigned char proto, unsigned short payload_len) {
    hdr.ver_ihl = 0x45;
    hdr.tos = 0;
    hdr.total_len = 20 + payload_len;
    hdr.id = ip_id_counter++;
    hdr.flags_frag = 0x4000; // Don't Fragment
    hdr.ttl = 64;
    hdr.protocol = proto;
    hdr.checksum = 0;
    ip_copy(hdr.src, src);
    ip_copy(hdr.dst, dst);
}

// --- ICMP ---
struct IcmpHeader {
    unsigned char type;
    unsigned char code;
    unsigned short checksum;
    unsigned short id;
    unsigned short seq;
};

static unsigned short ping_seq = 0;
static unsigned int ping_sent = 0;
static unsigned int ping_recv = 0;
static unsigned int ping_rtt_total = 0;
static unsigned int ping_rtt_min = 0xFFFFFFFF;
static unsigned int ping_rtt_max = 0;

void cmd_ping(const char* target, int count) {
    IpAddr dest;
    ip_from_str(dest, target);
    char dstr[20];
    ip_to_str(dstr, dest);

    neo::display::printf("PING %s: 56 data bytes\n", dstr);

    ping_sent = 0;
    ping_recv = 0;
    ping_rtt_total = 0;
    ping_rtt_min = 0xFFFFFFFF;
    ping_rtt_max = 0;

    // Simulate ICMP echo
    unsigned int rng = neo::timer::get_ticks();

    for (int i = 0; i < count; i++) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc == 0x01) break; // ESC
        }

        unsigned int start = neo::timer::get_ticks();
        ping_sent++;
        ping_seq++;

        // Simulate RTT based on pseudo-random
        rng = rng * 1103515245 + 12345;
        unsigned int sim_rtt = 10 + (rng % 50); // 10-60ms simulated

        neo::timer::delay_ms(sim_rtt);
        unsigned int elapsed = neo::timer::get_ticks() - start;
        unsigned int rtt_ms = elapsed / 50; // ticks to ms approx

        // Simulate packet loss (~5%)
        if ((rng >> 16) % 20 == 0) {
            neo::display::printf("Request timeout for icmp_seq %d\n", ping_seq);
        } else {
            ping_recv++;
            if (rtt_ms < ping_rtt_min) ping_rtt_min = rtt_ms;
            if (rtt_ms > ping_rtt_max) ping_rtt_max = rtt_ms;
            ping_rtt_total += rtt_ms;
            neo::display::printf("64 bytes from %s: icmp_seq=%d ttl=64 time=%d ms\n",
                                 dstr, ping_seq, rtt_ms);
        }
        neo::timer::delay_ms(1000 - sim_rtt);
    }

    // Statistics
    unsigned int loss = 0;
    if (ping_sent > 0) loss = ((ping_sent - ping_recv) * 100) / ping_sent;
    neo::display::printf("\n--- %s ping statistics ---\n", dstr);
    neo::display::printf("%d packets transmitted, %d received, %d%% packet loss\n",
                         ping_sent, ping_recv, loss);
    if (ping_recv > 0) {
        unsigned int avg = ping_rtt_total / ping_recv;
        neo::display::printf("round-trip min/avg/max = %d/%d/%d ms\n",
                             ping_rtt_min, avg, ping_rtt_max);
    }
}

// --- UDP ---
struct UdpHeader {
    unsigned short src_port;
    unsigned short dst_port;
    unsigned short length;
    unsigned short checksum;
};

// --- TCP ---
enum TcpState {
    TCP_CLOSED, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RECEIVED,
    TCP_ESTABLISHED, TCP_FIN_WAIT_1, TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT, TCP_CLOSING, TCP_LAST_ACK, TCP_TIME_WAIT
};

const char* tcp_state_name(TcpState s) {
    switch (s) {
        case TCP_CLOSED: return "CLOSED";
        case TCP_LISTEN: return "LISTEN";
        case TCP_SYN_SENT: return "SYN_SENT";
        case TCP_SYN_RECEIVED: return "SYN_RECV";
        case TCP_ESTABLISHED: return "ESTABLISHED";
        case TCP_FIN_WAIT_1: return "FIN_WAIT_1";
        case TCP_FIN_WAIT_2: return "FIN_WAIT_2";
        case TCP_CLOSE_WAIT: return "CLOSE_WAIT";
        case TCP_CLOSING: return "CLOSING";
        case TCP_LAST_ACK: return "LAST_ACK";
        case TCP_TIME_WAIT: return "TIME_WAIT";
    }
    return "UNKNOWN";
}

struct TcpConnection {
    IpAddr local_ip, remote_ip;
    unsigned short local_port, remote_port;
    TcpState state;
    unsigned int seq_num, ack_num;
    unsigned int send_window;
    unsigned int recv_window;
    unsigned int bytes_sent;
    unsigned int bytes_recv;
    bool valid;
};

static const int MAX_TCP_CONN = 32;
static TcpConnection tcp_conns[MAX_TCP_CONN];
static int tcp_conn_count = 0;

void tcp_init() {
    for (int i = 0; i < MAX_TCP_CONN; i++) tcp_conns[i].valid = false;
    tcp_conn_count = 0;
}

int tcp_open(const IpAddr& local, unsigned short lport, const IpAddr& remote, unsigned short rport) {
    for (int i = 0; i < MAX_TCP_CONN; i++) {
        if (!tcp_conns[i].valid) {
            TcpConnection& c = tcp_conns[i];
            c.valid = true;
            ip_copy(c.local_ip, local);
            ip_copy(c.remote_ip, remote);
            c.local_port = lport;
            c.remote_port = rport;
            c.state = TCP_SYN_SENT;
            c.seq_num = neo::timer::get_ticks();
            c.ack_num = 0;
            c.send_window = 65535;
            c.recv_window = 65535;
            c.bytes_sent = 0;
            c.bytes_recv = 0;
            tcp_conn_count++;
            return i;
        }
    }
    return -1;
}

void tcp_close(int idx) {
    if (idx >= 0 && idx < MAX_TCP_CONN && tcp_conns[idx].valid) {
        tcp_conns[idx].state = TCP_FIN_WAIT_1;
        // Simulate state transitions
        tcp_conns[idx].state = TCP_CLOSED;
        tcp_conns[idx].valid = false;
        tcp_conn_count--;
    }
}

// --- DNS Resolver ---
struct DnsEntry {
    char hostname[64];
    IpAddr ip;
    unsigned int ttl;
    bool valid;
};

static const int DNS_CACHE_SIZE = 16;
static DnsEntry dns_cache[DNS_CACHE_SIZE];
static int dns_cache_count = 0;
static IpAddr dns_server = {{8, 8, 8, 8}};

bool dns_lookup_cache(const char* name, IpAddr& ip) {
    for (int i = 0; i < dns_cache_count; i++) {
        if (dns_cache[i].valid && neo_strcmp(dns_cache[i].hostname, name) == 0) {
            ip_copy(ip, dns_cache[i].ip);
            return true;
        }
    }
    return false;
}

void dns_cache_add(const char* name, const IpAddr& ip, unsigned int ttl) {
    if (dns_cache_count < DNS_CACHE_SIZE) {
        DnsEntry& e = dns_cache[dns_cache_count];
        neo_strncpy(e.hostname, name, 63);
        e.hostname[63] = 0;
        ip_copy(e.ip, ip);
        e.ttl = ttl;
        e.valid = true;
        dns_cache_count++;
    }
}

void cmd_dns(const char* hostname) {
    neo::display::printf("Resolving %s...\n", hostname);

    IpAddr result;
    if (dns_lookup_cache(hostname, result)) {
        char rstr[20];
        ip_to_str(rstr, result);
        neo::display::printf("  (cached) %s -> %s\n", hostname, rstr);
        return;
    }

    // Simulate DNS resolution - generate deterministic IP from hostname
    unsigned int hash = 0;
    for (int i = 0; hostname[i]; i++) {
        hash = hash * 31 + hostname[i];
    }
    result.b[0] = 93 + (hash % 64);
    result.b[1] = (hash >> 8) % INODE_SIZE;
    result.b[2] = (hash >> 16) % INODE_SIZE;
    result.b[3] = 1 + (hash >> 24) % 254;

    char dnsstr[20], rstr[20];
    ip_to_str(dnsstr, dns_server);
    ip_to_str(rstr, result);

    neo::display::printf("  DNS server: %s\n", dnsstr);
    neo::display::printf("  Query type: A (IPv4)\n");
    neo::timer::delay_ms(200);
    neo::display::printf("  Response: %s -> %s (TTL: 3600)\n", hostname, rstr);

    dns_cache_add(hostname, result, 3600);
}

// --- Network Statistics ---
struct NetStats {
    unsigned int ip_rx, ip_tx;
    unsigned int icmp_rx, icmp_tx;
    unsigned int udp_rx, udp_tx;
    unsigned int tcp_rx, tcp_tx;
    unsigned int arp_rx, arp_tx;
    unsigned int dropped;
    unsigned int checksum_errors;
};

static NetStats stats;

void cmd_netstat() {
    neo::display::puts("+=============================================+\n");
    neo::display::puts("|           Network Statistics                |\n");
    neo::display::puts("+=============================================+\n\n");

    neo::display::puts("Protocol Statistics:\n");
    neo::display::puts("+----------+----------+----------+\n");
    neo::display::puts("| Protocol | RX       | TX       |\n");
    neo::display::puts("+----------+----------+----------+\n");
    neo::display::printf("| IP       | %-8d | %-8d |\n", stats.ip_rx, stats.ip_tx);
    neo::display::printf("| ICMP     | %-8d | %-8d |\n", stats.icmp_rx, stats.icmp_tx);
    neo::display::printf("| UDP      | %-8d | %-8d |\n", stats.udp_rx, stats.udp_tx);
    neo::display::printf("| TCP      | %-8d | %-8d |\n", stats.tcp_rx, stats.tcp_tx);
    neo::display::printf("| ARP      | %-8d | %-8d |\n", stats.arp_rx, stats.arp_tx);
    neo::display::puts("+----------+----------+----------+\n\n");

    neo::display::printf("Dropped packets: %d\n", stats.dropped);
    neo::display::printf("Checksum errors: %d\n\n", stats.checksum_errors);

    // Active TCP connections
    neo::display::puts("Active TCP Connections:\n");
    neo::display::puts("+-----+----------------+-------+----------------+-------+-------------+\n");
    neo::display::puts("| #   | Local Addr     | Port  | Remote Addr    | Port  | State       |\n");
    neo::display::puts("+-----+----------------+-------+----------------+-------+-------------+\n");
    int n = 0;
    for (int i = 0; i < MAX_TCP_CONN; i++) {
        if (!tcp_conns[i].valid) continue;
        char lip[20], rip[20];
        ip_to_str(lip, tcp_conns[i].local_ip);
        ip_to_str(rip, tcp_conns[i].remote_ip);
        neo::display::printf("| %-3d | %-14s | %-5d | %-14s | %-5d | %-11s |\n",
                             n++, lip, tcp_conns[i].local_port,
                             rip, tcp_conns[i].remote_port,
                             tcp_state_name(tcp_conns[i].state));
    }
    if (n == 0) neo::display::puts("|                     No active connections                        |\n");
    neo::display::puts("+-----+----------------+-------+----------------+-------+-------------+\n");
}

// --- Interface configuration ---
void init_default_interface() {
    neo_strcpy(ifaces[0].name, "eth0");
    ip_from_str(ifaces[0].ip, "192.168.1.100");
    ip_from_str(ifaces[0].subnet, "255.255.255.0");
    ip_from_str(ifaces[0].gateway, "192.168.1.1");
    ifaces[0].mac = {{0x00, 0xA0, 0x68, 0x12, 0x34, 0x56}};
    ifaces[0].up = true;
    ifaces[0].rx_packets = 0;
    ifaces[0].tx_packets = 0;
    ifaces[0].rx_bytes = 0;
    ifaces[0].tx_bytes = 0;
    ifaces[0].rx_errors = 0;
    ifaces[0].tx_errors = 0;
    iface_count = 1;

    // Add default route
    IpAddr net, mask, gw;
    ip_from_str(net, "192.168.1.0");
    ip_from_str(mask, "255.255.255.0");
    ip_from_str(gw, "0.0.0.0");
    route_add(net, mask, gw, "eth0", 0);

    // Default gateway route
    IpAddr zero;
    ip_from_str(zero, "0.0.0.0");
    ip_from_str(gw, "192.168.1.1");
    route_add(zero, zero, gw, "eth0", 100);

    // Populate ARP with gateway
    MacAddr gwmac = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    arp_add(ifaces[0].gateway, gwmac);
}

void cmd_ifconfig(const char* args) {
    if (args[0] == 0) {
        // Show all interfaces
        for (int i = 0; i < iface_count; i++) {
            char ipstr[20], snstr[20], gwstr[20], macstr[20];
            ip_to_str(ipstr, ifaces[i].ip);
            ip_to_str(snstr, ifaces[i].subnet);
            ip_to_str(gwstr, ifaces[i].gateway);
            mac_to_str(macstr, ifaces[i].mac);

            neo::display::printf("%s: flags=%s mtu 1500\n",
                                 ifaces[i].name, ifaces[i].up ? "UP,RUNNING" : "DOWN");
            neo::display::printf("        inet %s  netmask %s\n", ipstr, snstr);
            neo::display::printf("        gateway %s\n", gwstr);
            neo::display::printf("        ether %s\n", macstr);
            neo::display::printf("        RX packets %d  bytes %d\n",
                                 ifaces[i].rx_packets, ifaces[i].rx_bytes);
            neo::display::printf("        TX packets %d  bytes %d\n",
                                 ifaces[i].tx_packets, ifaces[i].tx_bytes);
            neo::display::printf("        RX errors %d  TX errors %d\n\n",
                                 ifaces[i].rx_errors, ifaces[i].tx_errors);
        }
    } else {
        // Parse: ifconfig eth0 192.168.1.x
        // Simple: set IP on eth0
        if (iface_count > 0) {
            ip_from_str(ifaces[0].ip, args);
            char ipstr[20];
            ip_to_str(ipstr, ifaces[0].ip);
            neo::display::printf("Set %s address to %s\n", ifaces[0].name, ipstr);
        }
    }
}

// --- Command Parser ---
int str_starts(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

const char* skip_space(const char* s) {
    while (*s == ' ') s++;
    return s;
}

// --- UI ---
void draw_banner() {
    neo::display::clear();
    neo::display::set_color(14, 1); // Yellow on blue
    neo::display::puts("+============================================+\n");
    neo::display::puts("|     NeoNet TCP/IP Stack v1.0               |\n");
    neo::display::puts("|     Bare-Metal Amiga Network Stack         |\n");
    neo::display::puts("+============================================+\n");
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');
}

void show_help() {
    neo::display::puts("Commands:\n");
    neo::display::puts("  ifconfig [ip]      - Show/set network interface\n");
    neo::display::puts("  ping <ip> [count]  - Send ICMP echo requests\n");
    neo::display::puts("  route              - Display routing table\n");
    neo::display::puts("  arp                - Display ARP table\n");
    neo::display::puts("  netstat            - Network statistics\n");
    neo::display::puts("  dns <hostname>     - Resolve hostname\n");
    neo::display::puts("  probe              - Probe Zorro bus for NICs\n");
    neo::display::puts("  dns-server <ip>    - Set DNS server\n");
    neo::display::puts("  gateway <ip>       - Set default gateway\n");
    neo::display::puts("  tcpopen <ip> <port>- Open TCP connection\n");
    neo::display::puts("  tcpclose <id>      - Close TCP connection\n");
    neo::display::puts("  help               - Show this help\n");
    neo::display::puts("  quit               - Exit NeoNet\n");
}

void cmd_probe() {
    neo::display::puts("Probing Zorro bus for network interfaces...\n");
    // neo::network::probe_zorro();
    neo::timer::delay_ms(500);
    neo::display::puts("  Scanning slot 0... ");
    neo::timer::delay_ms(200);
    neo::display::puts("empty\n");
    neo::display::puts("  Scanning slot 1... ");
    neo::timer::delay_ms(200);
    neo::display::puts("empty\n");
    neo::display::puts("  Scanning slot 2... ");
    neo::timer::delay_ms(200);
    neo::display::puts("empty\n");
    neo::display::puts("\nNo supported network cards detected.\n");
    neo::display::puts("Stack operating in simulation mode.\n");
    neo::display::printf("Default interface: %s (%s)\n\n", ifaces[0].name,
                         ifaces[0].up ? "UP" : "DOWN");
}

} // namespace neonet

extern "C" void app_main(int argc, char** argv) {
    using namespace neonet;

    // Initialize subsystems
    neo_memset(&stats, 0, sizeof(stats));
    neo_memset(arp_table, 0, sizeof(arp_table));
    neo_memset(tcp_conns, 0, sizeof(tcp_conns));
    neo_memset(dns_cache, 0, sizeof(dns_cache));
    arp_count = 0;
    route_count = 0;
    dns_cache_count = 0;
    tcp_conn_count = 0;

    tcp_init();
    init_default_interface();

    draw_banner();

    // Auto-probe on startup
    cmd_probe();

    show_help();
    neo::display::putchar('\n');

    char cmdbuf[INODE_SIZE];
    bool running = true;

    while (running) {
        neo::display::set_color(10, 0); // Green
        neo::display::puts("neonet> ");
        neo::display::set_color(7, 0);

        neo::console::getline(cmdbuf, sizeof(cmdbuf), "");

        const char* cmd = skip_space(cmdbuf);
        if (cmd[0] == 0) continue;

        neo::console::history_add(cmdbuf);

        if (str_starts(cmd, "quit") || str_starts(cmd, "exit")) {
            running = false;
        } else if (str_starts(cmd, "help")) {
            show_help();
        } else if (str_starts(cmd, "ifconfig")) {
            const char* arg = skip_space(cmd + 8);
            cmd_ifconfig(arg);
        } else if (str_starts(cmd, "ping ")) {
            const char* arg = skip_space(cmd + 5);
            // Parse optional count
            char target[64];
            int count = 4;
            int ti = 0;
            while (arg[ti] && arg[ti] != ' ' && ti < 63) { target[ti] = arg[ti]; ti++; }
            target[ti] = 0;
            if (arg[ti] == ' ') {
                const char* carg = skip_space(arg + ti);
                count = 0;
                for (int i = 0; carg[i] >= '0' && carg[i] <= '9'; i++)
                    count = count * 10 + (carg[i] - '0');
                if (count < 1) count = 4;
                if (count > 100) count = 100;
            }
            cmd_ping(target, count);
        } else if (str_starts(cmd, "route")) {
            route_display();
        } else if (str_starts(cmd, "arp")) {
            arp_display();
        } else if (str_starts(cmd, "netstat")) {
            cmd_netstat();
        } else if (str_starts(cmd, "dns ")) {
            const char* arg = skip_space(cmd + 4);
            cmd_dns(arg);
        } else if (str_starts(cmd, "dns-server ")) {
            const char* arg = skip_space(cmd + 11);
            ip_from_str(dns_server, arg);
            char s[20]; ip_to_str(s, dns_server);
            neo::display::printf("DNS server set to %s\n", s);
        } else if (str_starts(cmd, "gateway ")) {
            const char* arg = skip_space(cmd + 8);
            if (iface_count > 0) {
                ip_from_str(ifaces[0].gateway, arg);
                char s[20]; ip_to_str(s, ifaces[0].gateway);
                neo::display::printf("Default gateway set to %s\n", s);
            }
        } else if (str_starts(cmd, "probe")) {
            cmd_probe();
        } else if (str_starts(cmd, "tcpopen ")) {
            const char* arg = skip_space(cmd + 8);
            char ip[20]; int ii = 0;
            while (arg[ii] && arg[ii] != ' ' && ii < 19) { ip[ii] = arg[ii]; ii++; }
            ip[ii] = 0;
            const char* parg = skip_space(arg + ii);
            unsigned short port = 0;
            for (int i = 0; parg[i] >= '0' && parg[i] <= '9'; i++)
                port = port * 10 + (parg[i] - '0');

            IpAddr remote; ip_from_str(remote, ip);
            unsigned short lport = 1024 + (neo::timer::get_ticks() % 64000);
            int idx = tcp_open(ifaces[0].ip, lport, remote, port);
            if (idx >= 0) {
                tcp_conns[idx].state = TCP_ESTABLISHED;
                stats.tcp_tx += 3; // SYN, SYN-ACK, ACK
                stats.tcp_rx += 1;
                char rstr[20]; ip_to_str(rstr, remote);
                neo::display::printf("TCP connection %d established to %s:%d\n", idx, rstr, port);
            } else {
                neo::display::puts("Error: No free TCP slots\n");
            }
        } else if (str_starts(cmd, "tcpclose ")) {
            const char* arg = skip_space(cmd + 9);
            int idx = 0;
            for (int i = 0; arg[i] >= '0' && arg[i] <= '9'; i++)
                idx = idx * 10 + (arg[i] - '0');
            if (idx >= 0 && idx < MAX_TCP_CONN && tcp_conns[idx].valid) {
                tcp_close(idx);
                neo::display::printf("TCP connection %d closed\n", idx);
            } else {
                neo::display::puts("Error: Invalid connection ID\n");
            }
        } else {
            neo::display::printf("Unknown command: %s\n", cmd);
            neo::display::puts("Type 'help' for available commands.\n");
        }
        neo::display::putchar('\n');
    }

    neo::display::puts("NeoNet stack shutting down.\n");
}
