// rung one of the xdp ladder: count packets, pass them all to the stack.
// attaches to the server-side veth end; proves loader, maps and attach path.
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
    __type(key, __u32);
    __type(value, __u64);
} pkts SEC(".maps");

SEC("xdp")
int pass(struct xdp_md* ctx) {
    __u32 k = 0;
    __u64* p = bpf_map_lookup_elem(&pkts, &k);
    if (p) (*p)++;
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
