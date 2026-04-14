#include <stdint.h>
#include <stddef.h>

#include <lv2/io.h>
#include <lv2/libc.h>
#include <lv2/lv2.h>
#include <lv2/memory.h>
#include <lv2/net.h>
#include <lv2/process.h>
#include <lv2/syscall.h>
#include <lv2/thread.h>
#include <lv2/time.h>

#include "common.h"

typedef int64_t (*kernel_debug_plugin_fn_t)(const char *buffer, size_t size);

#define AF_INET 2
#define SOCK_DGRAM 2

#define MULTICAST_DEBUG_IP   ((uint32_t)0xE6012107) /* 230.1.33.7 */
#define MULTICAST_DEBUG_PORT ((uint16_t)18194)

#define SCRATCH_TOTAL_SIZE     0x1000
#define SCRATCH_MSG_SIZE       0x800
#define SCRATCH_ADDR_OFFSET    0x800

#define WORKER_PRIO            (-0x1D8)
#define WORKER_STACK           0x4000
#define STARTUP_DELAY_USEC     3000000ULL
#define STARTUP_COUNT          3
#define HEARTBEAT_DELAY_USEC   10000000ULL
#define MAILBOX_SLOTS          16
#define MAILBOX_MSG_SIZE       192

#define ENABLE_STATUS_FILE     1
#define ENABLE_VERBOSE_STATS   1
#define STATS_EVERY_TICKS      6U
#define STATUS_FILE_PATH       "/dev_hdd0/tmp/wifi_debug_status.txt"

struct debug_sockaddr_in
{
    uint8_t sin_len;
    uint8_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} __attribute__((packed));

struct debug_mailbox
{
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t dropped;
    char slots[MAILBOX_SLOTS][MAILBOX_MSG_SIZE];
};

struct wifi_debug_state
{
    int socket;
    process_t init_process;
    void *user_scratch_kbuf;
    void *user_scratch_vbuf;
    void *msg_user_ptr;
    struct debug_sockaddr_in *addr_user_ptr;
    struct debug_sockaddr_in addr;
    int started;
    //uint32_t heartbeat_tick;
    int last_send_ret;
    struct debug_mailbox mailbox;
};

static struct wifi_debug_state mcast_debug =
{
    .socket = -1,
    .started = 0,
    //.heartbeat_tick = 0,
    .last_send_ret = 0
};

static process_t force_current_process(process_t process)
{
    uint8_t *sprg0 = (uint8_t *)mfsprg0();
    uint16_t type = *(volatile uint16_t *)(sprg0 + 0xC0);
    process_t old = 0;

    if (type != 3)
    {
        old = *(process_t *)(sprg0 + 0xB0);
        *(process_t *)(sprg0 + 0xB0) = process;
        return old;
    }

    if (*(void **)(sprg0 + 0x68) && **(void ***)(sprg0 + 0x68))
    {
        uint8_t *ctx = (uint8_t *)**(void ***)(sprg0 + 0x68);
        old = *(process_t *)(ctx + 0x80);
        *(process_t *)(ctx + 0x80) = process;
    }

    return old;
}

static int init_user_scratch(void)
{
    int ret;
    uint8_t *kbuf;
    uint8_t *vbuf;

    if (mcast_debug.user_scratch_kbuf && mcast_debug.user_scratch_vbuf)
    {
        memset(mcast_debug.user_scratch_kbuf, 0, SCRATCH_TOTAL_SIZE);
        mcast_debug.msg_user_ptr = mcast_debug.user_scratch_vbuf;
        mcast_debug.addr_user_ptr = (struct debug_sockaddr_in *)((uint8_t *)mcast_debug.user_scratch_vbuf + SCRATCH_ADDR_OFFSET);
        return 0;
    }

    if (!mcast_debug.init_process)
        return -1;

    ret = page_allocate_auto(mcast_debug.init_process, SCRATCH_TOTAL_SIZE, 0x2F, &mcast_debug.user_scratch_kbuf);
    if (ret != 0)
        return ret;

    ret = page_export_to_proc(mcast_debug.init_process, mcast_debug.user_scratch_kbuf, 0x40000, &mcast_debug.user_scratch_vbuf);
    if (ret != 0)
        return ret;

    kbuf = (uint8_t *)mcast_debug.user_scratch_kbuf;
    vbuf = (uint8_t *)mcast_debug.user_scratch_vbuf;
    memset(kbuf, 0, SCRATCH_TOTAL_SIZE);

    mcast_debug.msg_user_ptr = vbuf;
    mcast_debug.addr_user_ptr = (struct debug_sockaddr_in *)(vbuf + SCRATCH_ADDR_OFFSET);
    return 0;
}

static void prepare_address(void)
{
    memset(&mcast_debug.addr, 0, sizeof(mcast_debug.addr));
    mcast_debug.addr.sin_len = sizeof(mcast_debug.addr);
    mcast_debug.addr.sin_family = AF_INET;
    mcast_debug.addr.sin_port = MULTICAST_DEBUG_PORT;
    mcast_debug.addr.sin_addr = MULTICAST_DEBUG_IP;

    memcpy((uint8_t *)mcast_debug.user_scratch_kbuf + SCRATCH_ADDR_OFFSET,
           &mcast_debug.addr,
           sizeof(mcast_debug.addr));
}

static int ensure_socket(void)
{
    if (mcast_debug.socket >= 0)
        return 0;

    mcast_debug.socket = sys_net_bnet_socket(AF_INET, SOCK_DGRAM, 0);
    return mcast_debug.socket;
}

static int send_text_immediate(const char *text)
{
    int len;

    if (!text)
        return -1;

    len = (int)strlen(text);
    if (len <= 0)
        return -1;
    if (len > SCRATCH_MSG_SIZE)
        len = SCRATCH_MSG_SIZE;

    memcpy(mcast_debug.user_scratch_kbuf, text, (size_t)len);

    mcast_debug.last_send_ret = sys_net_bnet_sendto(mcast_debug.socket,
                                                      mcast_debug.msg_user_ptr,
                                                      len,
                                                      0,
                                                      mcast_debug.addr_user_ptr,
                                                      (uint32_t)sizeof(*mcast_debug.addr_user_ptr));
    return mcast_debug.last_send_ret;
}

static void mailbox_init(void)
{
    memset(&mcast_debug.mailbox, 0, sizeof(mcast_debug.mailbox));
}

static int mailbox_push(const char *text)
{
    uint32_t head;
    uint32_t next;
    uint32_t tail;
    size_t len;

    if (!text)
        return -1;

    head = mcast_debug.mailbox.head;
    next = (head + 1U) % MAILBOX_SLOTS;
    tail = mcast_debug.mailbox.tail;
    if (next == tail)
    {
        mcast_debug.mailbox.dropped++;
        return -1;
    }

    len = strlen(text);
    if (len >= MAILBOX_MSG_SIZE)
        len = MAILBOX_MSG_SIZE - 1;

    memcpy(mcast_debug.mailbox.slots[head], text, len);
    mcast_debug.mailbox.slots[head][len] = '\0';
    mcast_debug.mailbox.head = next;
    return 0;
}

static int mailbox_push_buffer(const char *buffer, size_t size)
{
    uint32_t head;
    uint32_t next;
    uint32_t tail;
    size_t len;

    if (!buffer)
        return -1;

    head = mcast_debug.mailbox.head;
    next = (head + 1U) % MAILBOX_SLOTS;
    tail = mcast_debug.mailbox.tail;
    if (next == tail)
    {
        mcast_debug.mailbox.dropped++;
        return -1;
    }

    len = size;
    if (len >= MAILBOX_MSG_SIZE)
        len = MAILBOX_MSG_SIZE - 1;

    memcpy(mcast_debug.mailbox.slots[head], buffer, len);
    mcast_debug.mailbox.slots[head][len] = '\0';
    mcast_debug.mailbox.head = next;
    return 0;
}

static int mailbox_pop(char *out, size_t out_size)
{
    uint32_t tail;
    uint32_t head;
    size_t len;

    if (!out || out_size == 0)
        return -1;

    tail = mcast_debug.mailbox.tail;
    head = mcast_debug.mailbox.head;
    if (tail == head)
        return 0;

    len = strlen(mcast_debug.mailbox.slots[tail]);
    if (len >= out_size)
        len = out_size - 1;

    memcpy(out, mcast_debug.mailbox.slots[tail], len);
    out[len] = '\0';
    mcast_debug.mailbox.slots[tail][0] = '\0';
    mcast_debug.mailbox.tail = (tail + 1U) % MAILBOX_SLOTS;
    return 1;
}

/*
static void write_status_file(const char *tag)
{

#if ENABLE_STATUS_FILE
    int fd;
    uint64_t written;
    char text[256];

    snprintf(text,
             sizeof(text),
             "tag=%s started=%d pid=0x%08x socket=%d tick=%u dropped=%u head=%u tail=%u last_send=%d\n",
             tag ? tag : "status",
             mcast_debug.started,
             mcast_debug.init_process ? mcast_debug.init_process->pid : 0,
             mcast_debug.socket,
             mcast_debug.heartbeat_tick,
             mcast_debug.mailbox.dropped,
             mcast_debug.mailbox.head,
             mcast_debug.mailbox.tail,
             mcast_debug.last_send_ret);

    if (cellFsOpen(STATUS_FILE_PATH,
                   CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC,
                   &fd,
                   CELL_FS_S_IRUSR | CELL_FS_S_IWUSR | CELL_FS_S_IRGRP | CELL_FS_S_IROTH,
                   NULL,
                   0) == 0)
    {
        cellFsWrite(fd, text, strlen(text), &written);
        cellFsClose(fd);
    }
#else
    (void)tag;
#endif
}
*/

/*
static void queue_startup_messages(void)
{
    char msg[128];
    int i;

    for (i = 0; i < STARTUP_COUNT; i++)
    {
        snprintf(msg,
                 sizeof(msg),
                 "Multicast WiFi Debug Plugin [%d/%d] pid=0x%08x\n",
                 i + 1,
                 STARTUP_COUNT,
                 mcast_debug.init_process ? mcast_debug.init_process->pid : 0);
        mailbox_push(msg);
        timer_usleep(1000000ULL);
    }
}
*/

/*
static void queue_heartbeat_message(void)
{
	
    char msg[160];

    snprintf(msg,
             sizeof(msg),
             "Multicast WiFi Debug Plugin alive tick=%u pid=0x%08x dropped=%u\n",
             mcast_debug.heartbeat_tick,
             mcast_debug.init_process ? mcast_debug.init_process->pid : 0,
             mcast_debug.mailbox.dropped);
    mailbox_push(msg);

#if ENABLE_VERBOSE_STATS
    if ((mcast_debug.heartbeat_tick % STATS_EVERY_TICKS) == 0)
    {
        snprintf(msg,
                 sizeof(msg),
                 "Multicast WiFi Debug Plugin stats tick=%u socket=%d head=%u tail=%u dropped=%u last_send=%d\n",
                 mcast_debug.heartbeat_tick,
                 mcast_debug.socket,
                 mcast_debug.mailbox.head,
                 mcast_debug.mailbox.tail,
                 mcast_debug.mailbox.dropped,
                 mcast_debug.last_send_ret);
        mailbox_push(msg);
    }
#endif
}
*/

static void flush_mailbox(void)
{
    char out[MAILBOX_MSG_SIZE];

    while (mailbox_pop(out, sizeof(out)) > 0)
        send_text_immediate(out);
}

static int64_t debug_plugin_callback(const char *buffer, size_t size)
{
    if (!buffer || size == 0)
        return 0;

    mailbox_push_buffer(buffer, size);

    return 0;
}

static void worker_main(uint64_t arg)
{
    (void)arg;

    timer_usleep(STARTUP_DELAY_USEC);

    if (mcast_debug.init_process)
        force_current_process(mcast_debug.init_process);

    if (init_user_scratch() != 0)
        return;

    prepare_address();
    if (ensure_socket() < 0)
        return;

    //write_status_file("worker_ready");
    //queue_startup_messages();

    for (;;)
    {
        flush_mailbox();

        //mcast_debug.heartbeat_tick++;
        //queue_heartbeat_message();
        //write_status_file("heartbeat");

        //timer_usleep(HEARTBEAT_DELAY_USEC);
    }
}

static int wifi_debug_start(void)
{
    thread_t thread;

    if (mcast_debug.started)
        return 0;

    mcast_debug.init_process = get_current_process();
    mcast_debug.started = 1;
    mailbox_init();
    //write_status_file("start");

    mailbox_push("\nPS3HEN WiFi Debug Plugin Ready!\n");

    return ppu_thread_create(&thread, worker_main, 0, WORKER_PRIO, WORKER_STACK, 0, "PS3HEN_WifiDebug");
}

kernel_debug_plugin_fn_t main(void)
{
    int ret;

    ret = wifi_debug_start();
    (void)ret;

    return debug_plugin_callback;
}
