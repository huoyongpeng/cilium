/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright Authors of Cilium */

#ifndef __LIB_HIGH_SCALE_IPCACHE_H_
#define __LIB_HIGH_SCALE_IPCACHE_H_

#include "maps.h"

#ifdef ENABLE_HIGH_SCALE_IPCACHE
/* WORLD_CIDR_STATIC_PREFIX4 gets sizeof non-IP, non-prefix part of
 * world_cidrs_key4.
 */
# define WORLD_CIDR_STATIC_PREFIX4						\
	(8 * (sizeof(struct world_cidrs_key4) - sizeof(struct bpf_lpm_trie_key)	\
	      - sizeof(__u32)))
#define WORLD_CIDR_PREFIX_LEN4(PREFIX) (WORLD_CIDR_STATIC_PREFIX4 + (PREFIX))

static __always_inline __maybe_unused bool
world_cidrs_lookup4(__u32 addr)
{
	__u8 *matches;
	struct world_cidrs_key4 key = {
		.lpm_key = { WORLD_CIDR_PREFIX_LEN4(V4_CACHE_KEY_LEN), {} },
		.ip = addr,
	};

	key.ip &= GET_PREFIX(V4_CACHE_KEY_LEN);
	matches = map_lookup_elem(&WORLD_CIDRS4_MAP, &key);
	return matches != NULL;
}

static __always_inline int
decapsulate_overlay(struct __ctx_buff *ctx, __u32 *src_id)
{
	void *data, *data_end;
	__u16 dport, proto;
	struct iphdr *ip4;
	int shrink;
	__u32 off;

	if (!validate_ethertype(ctx, &proto))
		return DROP_UNSUPPORTED_L2;
	if (proto != bpf_htons(ETH_P_IP))
		return CTX_ACT_OK;

	if (!revalidate_data(ctx, &data, &data_end, &ip4))
		return DROP_INVALID;
	if (ip4->protocol != IPPROTO_UDP)
		return CTX_ACT_OK;

	off = ((void *)ip4 - data) + ipv4_hdrlen(ip4) +
	      offsetof(struct udphdr, dest);
	if (l4_load_port(ctx, off, &dport) < 0)
		return DROP_INVALID;

	if (dport != bpf_htons(TUNNEL_PORT))
		return CTX_ACT_OK;

	off = ((void *)ip4 - data) + ipv4_hdrlen(ip4) +
	      sizeof(struct udphdr) +
	      offsetof(struct vxlanhdr, vx_vni);
	if (ctx_load_bytes(ctx, off, src_id, sizeof(__u32)) < 0)
		return DROP_INVALID;
	*src_id = bpf_ntohl(*src_id) >> 8;
	ctx_store_meta(ctx, CB_SRC_LABEL, *src_id);

	shrink = ipv4_hdrlen(ip4) + sizeof(struct udphdr) +
		 sizeof(struct vxlanhdr) + sizeof(struct ethhdr);
	if (ctx_adjust_hroom(ctx, -shrink, BPF_ADJ_ROOM_MAC,
			     ctx_adjust_hroom_flags()))
		return DROP_INVALID;
	return ctx_redirect(ctx, ENCAP_IFINDEX, BPF_F_INGRESS);
}
#endif /* ENABLE_HIGH_SCALE_IPCACHE */
#endif /* __LIB_HIGH_SCALE_IPCACHE_H_ */
