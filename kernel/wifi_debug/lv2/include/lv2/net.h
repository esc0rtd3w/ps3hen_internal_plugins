#ifndef __LV2_NET_H__
#define __LV2_NET_H__

#include <stdint.h>
#include <lv2/lv2.h>

LV2_EXPORT int sys_net_bnet_bind(int s, const void *name, uint32_t namelen);
LV2_EXPORT int sys_net_bnet_close(int s);
LV2_EXPORT int sys_net_bnet_connect(int s, const void *name, uint32_t namelen);
LV2_EXPORT int sys_net_bnet_poll(void *fds, uint32_t nfds, int timeout);
LV2_EXPORT int sys_net_bnet_sendto(int s, const void *msg, int len, int flags, const void *to, uint32_t tolen);
LV2_EXPORT int sys_net_bnet_setsockopt(int s, int level, int optname, const void *optval, uint32_t optlen);
LV2_EXPORT int sys_net_bnet_shutdown(int s, int how);
LV2_EXPORT int sys_net_bnet_socket(int family, int type, int protocol);

#endif /* __LV2_NET_H__ */
