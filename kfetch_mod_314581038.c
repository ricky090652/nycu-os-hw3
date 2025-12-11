#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/atomic.h>
#include <linux/utsname.h>
#include <linux/sysinfo.h>
#include <linux/ktime.h>
#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/timekeeping.h>
#include <linux/mutex.h>

#define DEVICE_NAME "kfetch"
#define BUF_LEN 1024

#define KFETCH_NUM_INFO 6

#define KFETCH_RELEASE   (1 << 0)
#define KFETCH_NUM_CPUS  (1 << 1)
#define KFETCH_CPU_MODEL (1 << 2)
#define KFETCH_MEM       (1 << 3)
#define KFETCH_UPTIME    (1 << 4)
#define KFETCH_NUM_PROCS (1 << 5)
#define KFETCH_FULL_INFO ((1 << KFETCH_NUM_INFO) - 1)

enum {
    CDEV_NOT_USED,
    CDEV_EXCLUSIVE_OPEN,
};

static int major;
static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED);
static char kfetch_buf[BUF_LEN + 1];
static struct class *cls;
static int mask_info = KFETCH_FULL_INFO;
static DEFINE_MUTEX(kfetch_lock);



const char *logo_lines[] ={
"        .-.        ",
"       (.. |       ",
"       \033[33m<>\033[0m  |       ",
"      / --- \\      ",
"     ( |   | )     ",
"   \033[33m|\\\033[0m\\_)___/\\)\033[33m/\\\033[0m   ",
"  \033[33m<__)\033[0m------\033[33m(__/\033[0m   "
};


static int kfetch_open(struct inode *inode, struct file *file)
{
    if (atomic_cmpxchg(&already_open, CDEV_NOT_USED, CDEV_EXCLUSIVE_OPEN))
        return -EBUSY;
    return 0;
}

static int kfetch_release(struct inode *inode, struct file *file)
{
    atomic_set(&already_open, CDEV_NOT_USED);
    return 0;
}

static ssize_t kfetch_read(struct file *filp,
                           char __user *buffer,
                           size_t length,
                           loff_t *offset)
{
    const struct new_utsname *uts;
    struct task_struct *task;
    const char *host;
    size_t len = 0;
    size_t hlen;
    int i;

    enum { LOGO_LINES = 7, INFO_LINES = 6 };
    const int LOGO_WIDTH = 20;

    char info_lines[INFO_LINES][64];
    int info_count = 0;   /* 目前已填入的資訊行數 */

    if (mutex_lock_interruptible(&kfetch_lock))
        return -ERESTARTSYS;

    if (*offset > 0) {
        mutex_unlock(&kfetch_lock);
        return 0;
    }

    memset(info_lines, 0, sizeof(info_lines));

    uts  = &init_uts_ns.name;
    host = uts->nodename;
    hlen = strlen(host);

    /* Kernel */
    if (mask_info & KFETCH_RELEASE) {
        snprintf(info_lines[info_count], sizeof(info_lines[info_count]),
                 "\033[33mKernel:\033[0m %s", uts->release);
        info_count++;
    }

    /* CPU model */
    if (mask_info & KFETCH_CPU_MODEL) {
        snprintf(info_lines[info_count], sizeof(info_lines[info_count]),
                 "\033[33mCPU:\033[0m    %s", "RISC-V Processor");
        info_count++;
    }

    /* CPUs */
    if (mask_info & KFETCH_NUM_CPUS) {
        int online = num_online_cpus();
        int total  = num_possible_cpus();
        snprintf(info_lines[info_count], sizeof(info_lines[info_count]),
                 "\033[33mCPUs:\033[0m   %d / %d", online, total);
        info_count++;
    }

    /* Mem */
    if (mask_info & KFETCH_MEM) {
        struct sysinfo si;
        unsigned long total_mb, free_mb;

        si_meminfo(&si);
        total_mb = (si.totalram * si.mem_unit) / (1024 * 1024);
        free_mb  = (si.freeram  * si.mem_unit) / (1024 * 1024);

        snprintf(info_lines[info_count], sizeof(info_lines[info_count]),
                 "\033[33mMem:\033[0m    %lu / %lu MB", free_mb, total_mb);
        info_count++;
    }

    /* Procs */
    if (mask_info & KFETCH_NUM_PROCS) {
        int procs = 0;
        for_each_process(task)
            if (task->mm)
                procs++;
        snprintf(info_lines[info_count], sizeof(info_lines[info_count]),
                 "\033[33mProcs:\033[0m  %d", procs);
        info_count++;
    }

    /* Uptime */
    if (mask_info & KFETCH_UPTIME) {
        unsigned long up_sec = ktime_get_boottime_seconds();
        unsigned long up_min = up_sec / 60;
        snprintf(info_lines[info_count], sizeof(info_lines[info_count]),
                 "\033[33mUptime:\033[0m %lu mins", up_min);
        info_count++;
    }

    /* hostname 行 */
    for (i = 0; i < LOGO_WIDTH && len < BUF_LEN - 1; i++)
        kfetch_buf[len++] = ' ';//一開始先補20個空白再接hostname
    len += scnprintf(kfetch_buf + len, BUF_LEN - len, "%s\n", host);

    /* logo line 0 + 分隔線 */
    {
        int logo_len = strlen(logo_lines[0]);

        len += scnprintf(kfetch_buf + len, BUF_LEN - len, "%s", logo_lines[0]);
        for (; logo_len < LOGO_WIDTH && len < BUF_LEN - 1; logo_len++)
            kfetch_buf[len++] = ' ';

        for (i = 0; i < hlen && len < BUF_LEN - 1; i++)
            kfetch_buf[len++] = '-';
        kfetch_buf[len++] = '\n';
    }

    /* logo line 1~6 + info_lines 動態內容 */
    for (i = 1; i < LOGO_LINES && len < BUF_LEN - 1; i++) {
        int logo_len = strlen(logo_lines[i]);
        int info_idx = i - 1;   /* 第 1 行對應 info_lines[0] */

        len += scnprintf(kfetch_buf + len, BUF_LEN - len, "%s", logo_lines[i]);

        for (; logo_len < LOGO_WIDTH && len < BUF_LEN - 1; logo_len++)
            kfetch_buf[len++] = ' ';

        if(strlen(logo_lines[i]) > 20){
            kfetch_buf[len++] = ' ';
        }

        if (info_idx >= 0 && info_idx < info_count) {
            len += scnprintf(kfetch_buf + len, BUF_LEN - len,
                             "%s", info_lines[info_idx]);
        }

        kfetch_buf[len++] = '\n';
    }

    if (len > BUF_LEN)
        len = BUF_LEN;

    if (copy_to_user(buffer, kfetch_buf, len)) {
        mutex_unlock(&kfetch_lock);
        return -EFAULT;
    }

    mutex_unlock(&kfetch_lock);

    *offset = len;
    return len;
}

static ssize_t kfetch_write(struct file *filp,
                            const char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
    int new_mask;

    if (mutex_lock_interruptible(&kfetch_lock))
        return -ERESTARTSYS;

    if (length < sizeof(int)) {
        mutex_unlock(&kfetch_lock);
        return -EINVAL;
    }

    if (copy_from_user(&new_mask, buffer, sizeof(int))) {
        mutex_unlock(&kfetch_lock);
        return -EFAULT;
    }

    mask_info = new_mask;

    mutex_unlock(&kfetch_lock);
    return sizeof(int);
}


static const struct file_operations kfetch_ops = {
    .owner   = THIS_MODULE,
    .read    = kfetch_read,
    .write   = kfetch_write,
    .open    = kfetch_open,
    .release = kfetch_release,
};


static int __init kfetch_init(void)
{
    major = register_chrdev(0, DEVICE_NAME, &kfetch_ops);
    if (major < 0)
        return major;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    cls = class_create(DEVICE_NAME);
#else
    cls = class_create(THIS_MODULE, DEVICE_NAME);
#endif
    device_create(cls, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    return 0;
}

static void __exit kfetch_exit(void)
{
    device_destroy(cls, MKDEV(major, 0));
    class_destroy(cls);
    unregister_chrdev(major, DEVICE_NAME);
}

module_init(kfetch_init);
module_exit(kfetch_exit);

MODULE_LICENSE("GPL");
