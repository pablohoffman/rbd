/*
 * Remote Block Device - Linux module
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>         /* __init */
#include <linux/kernel.h>       /* printk */
#include <linux/slab.h>         /* kmalloc */
#include <linux/types.h>        /* size_t */
#include <linux/hdreg.h>        /* struct hd_geometry */
#include <linux/bio.h>          /* struct bio */
#include <linux/blkdev.h>       /* struct request */
#include <linux/genhd.h>        /* add_disk */
#include <linux/slab.h>         /* kmalloc */
#include <linux/workqueue.h>    /* schedule_work, etc */
#include <linux/errno.h>        /* ENONMEM, etc */
#include <linux/fs.h>           /* struct inode, file */
#include <linux/list.h>
#include <asm/uaccess.h>        /* copy_to_user, etc */
#include <asm/semaphore.h>      /* up, down, etc */
#include <linux/configfs.h>

#include "rbd.h"

MODULE_LICENSE("MIT");
MODULE_VERSION("0.1");
MODULE_AUTHOR("Pablo Hoffman");
MODULE_DESCRIPTION("Driver for remote (network) block devices");

static int major = 0;
module_param(major, int, S_IRUGO);
static int debug = 0;
module_param(debug, int, S_IRUGO);

LIST_HEAD(rbd_devices);
int rbd_lastminor = 0;      /* to save las minor used */

unsigned int inet_addr(char *str)
{
    int a, b, c, d;
    char arr[4];
    sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);
    arr[0] = a; arr[1] = b; arr[2] = c; arr[3] = d;
    return *(unsigned int*)arr;
} 

int sd_connect(struct rbd_dev *dev)
{
    struct sockaddr_in saddr;
    int r = -1;

    sd_disconnect(dev);

    printk(KERN_WARNING "RBD: connecting to SD\n");

    r = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &dev->sd_socket);
    if (r < 0) {
        printk(KERN_ERR "RBD: error %d creating socket\n", r);
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(dev->sd_port);
    saddr.sin_addr.s_addr = dev->sd_addr;

    r = dev->sd_socket->ops->connect(dev->sd_socket, (struct sockaddr *)&saddr, sizeof(saddr), O_RDWR);
    if (r && (r != -EINPROGRESS)) {
        printk(KERN_ERR "RBD: connecting to SD %d\n", r);
        return -1;
    }

    return 0;
}

int sd_disconnect(struct rbd_dev *dev)
{
    struct rbdmsg_hdr msg;
    
    if (!dev->sd_socket) 
        return -1;

    if (debug) printk(KERN_INFO "RBD: disconnecting from SD %u\n", (unsigned int)dev->sd_socket);

    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_CLOSE;
    msg.id = ++dev->sd_msguid;
    msg.payload_size = 0;    
    
    sd_send(dev, &msg, sizeof(msg));
    dev->sd_socket = NULL;

    return 0;
}

int sd_send(struct rbd_dev *dev, void *buf, size_t size)
{
    struct kvec iov;
    struct msghdr msg;
    int rv, sent=0;
    unsigned flags = 0;
    
    iov.iov_base = buf;
    iov.iov_len  = size;
    
    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = flags | MSG_NOSIGNAL;
    
    do {
        rv = kernel_sendmsg(dev->sd_socket, &msg, &iov, 1, size);
        if (rv == -EAGAIN) {
            /* TODO: impose a retry limit */
            if (debug) printk(KERN_WARNING "RBD: send | EAGAIN\n");
            continue;
        }
        if (rv == -EINTR) {
            if (debug) printk(KERN_WARNING "RBD: send | EINTR\n");
            flush_signals(current);
            rv = 0;
        }
        if (rv == -EPIPE || rv == -ENOTCONN || rv == -ECONNRESET) {
            /* TODO: impose a retry limit */
            if (debug) printk(KERN_WARNING "RBD: send | ENOTCONN\n");
            sd_connect(dev);
            continue;
        }
        if (rv < 0) {
            if (debug) printk(KERN_WARNING "RBD: send | unknown error: %d\n", rv);
            return rv;
        }
        sent += rv;
        iov.iov_base += rv;
        iov.iov_len  -= rv;
    } while (sent < size);

    return sent;
}


int sd_recv(struct rbd_dev *dev, void *buf, size_t size)
{
    mm_segment_t oldfs;
    struct iovec iov;
    struct msghdr msg;
    int rv;

    iov.iov_len = size;
    iov.iov_base = buf;

    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_iovlen = 1;
    msg.msg_iov = &iov;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_flags = MSG_WAITALL | MSG_NOSIGNAL;

    oldfs = get_fs();
    set_fs(KERNEL_DS);

read_again:
    rv = sock_recvmsg(dev->sd_socket, &msg, size, msg.msg_flags);
    if (rv == -EAGAIN || rv == -ERESTARTSYS) {
        if (debug) printk(KERN_WARNING "RBD: recv | EAGAIN\n");
        goto read_again;
    }
    if (rv == -ENOTCONN) {
        if (debug) printk(KERN_WARNING "RBD: recv | ENOTCONN\n");
        sd_connect(dev);
        goto read_again;
    }
    if (rv < 0) {
        if (debug) printk(KERN_WARNING "RBD: recv | unknown error: %d\n", rv);
    }

/* TODO: add this checks if necessary
    } else {
        iov.iov_base += rv;
        iov.iov_len  -= rv;

        if (iov.iov_len > 0) {
            goto read_again;
        }
    }
    set_fs(oldfs);
*/
    return rv;
}

int rbd_write(struct rbd_dev *dev, unsigned long sector, unsigned long nbytes, char *buf)
{
    struct rbdmsg_hdr msg, rsp;

    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_WRITE;
    msg.id = ++dev->sd_msguid;
    msg.payload_size = nbytes;    
    msg.fsop_offset_sectors = sector;  /* initial sector */
    msg.fsop_size = nbytes;            /* size in bytes */
    
    down(&dev->sd_mutex);
    if (debug) printk(KERN_NOTICE "RBD: write | dev %s | msg.id %d | offset %d | nbytes %d\n", 
                      dev->name, msg.id, msg.fsop_offset_sectors, msg.fsop_size);
    sd_send(dev, &msg, sizeof(msg));
    sd_send(dev, buf, nbytes);
    sd_recv(dev, &rsp, sizeof(rsp));
    up(&dev->sd_mutex);

    return 0;
}


int rbd_read(struct rbd_dev *dev, unsigned long sector, unsigned long nbytes, char *buf)
{  
    struct rbdmsg_hdr msg, rsp;

    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_READ;
    msg.id = ++dev->sd_msguid;
    msg.payload_size = 0;    
    msg.fsop_offset_sectors = sector;  /* initial sector */
    msg.fsop_size = nbytes;            /* size in bytes */
    
    down(&dev->sd_mutex);
    if (debug) printk(KERN_NOTICE "RBD: read | dev %s | msg.id %d | offset %d | nbytes %d\n", 
                      dev->name, msg.id, msg.fsop_offset_sectors, msg.fsop_size);
    sd_send(dev, &msg, sizeof(msg));
    sd_recv(dev, &rsp, sizeof(rsp));
    sd_recv(dev, buf, rsp.payload_size);
    up(&dev->sd_mutex);

    return 0;
}


/*
 * transfer the data
 */
static void rbd_transfer(struct rbd_dev *dev, unsigned long sector, unsigned long nsect, char *buf, int write)
{
    unsigned long offset = sector;
    unsigned long nbytes = nsect*RBD_SECSIZE;

    if ((offset + nsect) > dev->size) {
        if (debug) printk(KERN_WARNING "RBD: transfer - out of range request | dev %s | sector %ld | nbytes %ld | dev->size %ld\n", 
                          dev->name, sector, nbytes, dev->size);
        return;
    }
    if (write)
        rbd_write(dev, sector, nbytes, buf);
    else
        rbd_read(dev, sector, nbytes, buf);
}


/* 
 * this functions runs inside a work queue
 */
static void request_work(void *arg)
{
    request_queue_t *q = arg;
    struct request *req;
    struct rbd_dev *dev = q->queuedata;

    down(&dev->rqwk_mutex);
    while ((req = elv_next_request(q)) != NULL) {
        if (!blk_fs_request(req)) {
            if (debug) printk(KERN_INFO "RBD: not a file system request\n");
            end_request(req, 0);
            continue;
        }
        if (debug) printk(KERN_INFO "RBD: request | dev %s | rw %ld | sec %d | current_nr_sectors %d\n", 
                          dev->name, rq_data_dir(req), (int)req->sector, req->current_nr_sectors);
        rbd_transfer(dev, req->sector, req->current_nr_sectors, req->buffer, rq_data_dir(req));
        end_request(req, 1);
    }
    up(&dev->rqwk_mutex);
}

static void setup_work(void *arg) 
{
    struct rbd_dev *dev = arg;
    struct rbdmsg_hdr msg, rsp;
    unsigned long size;

    msg.version = PROTO_VERSION;
    msg.type = CMD;
    msg.code = CMD_GETSZ;
    msg.id = ++dev->sd_msguid;
    msg.payload_size = 0;    
    
    sd_send(dev, &msg, sizeof(msg));
    sd_recv(dev, &rsp, sizeof(rsp));
    if (sizeof(size) == rsp.payload_size) 
        sd_recv(dev, &size, rsp.payload_size);
    if (debug) printk(KERN_INFO "RBD: getsz | dev %s | value %lu\n", dev->name, size);
    dev->size = size;
    up(&dev->setupwk_mutex);
}

/*
 * attend request y pass it to the workqueue
 */
static void rbd_request(request_queue_t *q)
{
    struct rbd_dev *dev = q->queuedata;

    schedule_work(&dev->rq_work);
}

static int rbd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
    struct rbd_dev *dev = bdev->bd_disk->private_data;

    geo->heads = 2;
    geo->sectors = 4;
    geo->cylinders = dev->size / 8;
    return 0;
}

/* module operations */
static struct block_device_operations rbd_ops = {
    .owner            = THIS_MODULE,
    .getgeo           = rbd_getgeo,
};

struct rbd_dev *init_device(void)
{
    struct rbd_dev *dev;

    dev = kmalloc(sizeof(struct rbd_dev), GFP_KERNEL);
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(struct rbd_dev));

    dev->sd_msguid = 0;
    strcpy(dev->sd_host, "127.0.0.1");
    dev->sd_port = SDPORT;
    dev->first_minor = rbd_lastminor;
    rbd_lastminor += RBD_MINORS;

    return dev;
}

/* installs block device and enables it */
static int enable_device(struct rbd_dev *dev)
{
    int ret;

    INIT_WORK(&dev->setup_work, setup_work, dev);
    init_MUTEX_LOCKED(&dev->setupwk_mutex);
    
    /* connect to SD */
    init_MUTEX(&dev->sd_mutex);
    dev->sd_addr = inet_addr(dev->sd_host);

    down(&dev->sd_mutex);
    ret = sd_connect(dev);
    up(&dev->sd_mutex);
    if (ret)
        return -1;

    /* get storage size from SD and set device size locally */
    schedule_work(&dev->setup_work); 
    down(&dev->setupwk_mutex);

    /* initialize the requests queue */
    spin_lock_init(&dev->req_lock);
    dev->queue = blk_init_queue(rbd_request, &dev->req_lock);

    INIT_WORK(&dev->rq_work, request_work, dev->queue);
    init_MUTEX(&dev->rqwk_mutex);

    blk_queue_hardsect_size(dev->queue, RBD_SECSIZE);
    dev->queue->queuedata = dev;

    dev->gd = alloc_disk(RBD_MINORS);
    if (!dev->gd) {
        printk(KERN_WARNING "RBD: error allocating disks\n");
        return -1;
    }
    dev->gd->major = major;
    dev->gd->first_minor = dev->first_minor; 

    dev->gd->fops = &rbd_ops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    dev->name = dev->cfs_item.ci_name;
    snprintf(dev->gd->disk_name, 32, "rbd%s", dev->name);
    set_capacity(dev->gd, dev->size);

    add_disk(dev->gd);

    dev->active = 1;
    return 0;
}


static int disable_device(struct rbd_dev *dev)
{
    down(&dev->sd_mutex);
    sd_disconnect(dev);
    up(&dev->sd_mutex);

    if (dev->gd) {
        del_gendisk(dev->gd);
        put_disk(dev->gd);
        dev->gd = NULL;
    }
    if (dev->queue) {
        blk_cleanup_queue(dev->queue);
        dev->queue = NULL;
    }
    memset(&dev->rq_work, 0, sizeof(dev->rq_work));
    memset(&dev->setup_work, 0, sizeof(dev->setup_work));
    memset(&dev->setupwk_mutex, 0, sizeof(dev->setupwk_mutex));
    memset(&dev->rqwk_mutex, 0, sizeof(dev->rqwk_mutex));
    memset(&dev->sd_mutex, 0, sizeof(dev->sd_mutex));
    memset(&dev->sd_mutex, 0, sizeof(spinlock_t));

    dev->active = 0;
    return 0;
}

static void free_device(struct rbd_dev *dev) 
{
    printk(KERN_WARNING "free_device\n");

    disable_device(dev);

    if (dev->sd_socket)
        kfree(dev->sd_socket);

    list_del(&dev->devices);
    
    kfree(dev);
}

/* configfs stuff starts here -------------------------------------------------------- */

static inline struct rbd_dev *to_rbddev(struct config_item *item)
{
    return item ? container_of(item, struct rbd_dev, cfs_item) : NULL;
};

static ssize_t rbddev_active_read(struct rbd_dev *dev, char *page)
{
    return sprintf(page, "%d\n", dev->active);
};

static ssize_t rbddev_active_write(struct rbd_dev *dev, const char *page, size_t count)
{
    unsigned long active;
    char *p = (char *) page;

    active = simple_strtoul(p, &p, 10);
    if (!p || (*p && (*p != '\n')))
        return -EINVAL;

    if (active && !dev->active) {         /* activo dispositivo */
        printk(KERN_INFO "RBD: enabling device\n");
        enable_device(dev);
    } else if (!active && dev->active) {  /* desactivo dispositivo */
        printk(KERN_INFO "RBD: disabling device\n");
        disable_device(dev);
    }

    return count;
};

static ssize_t rbddev_host_read(struct rbd_dev *dev, char *page)
{
    return sprintf(page, "%s\n", dev->sd_host);
};

static ssize_t rbddev_host_write(struct rbd_dev *dev, const char *page, size_t count)
{
    memset(&dev->sd_host, 0, sizeof(dev->sd_host));
    return snprintf(dev->sd_host, sizeof(dev->sd_host)-1, page);
};

static ssize_t rbddev_port_read(struct rbd_dev *dev, char *page)
{
    return sprintf(page, "%d\n", dev->sd_port);
};

static ssize_t rbddev_port_write(struct rbd_dev *dev, const char *page, size_t count)
{
    unsigned long tmp;
    char *p = (char *) page;

    tmp = simple_strtoul(p, &p, 10);
    if (!p || (*p && (*p != '\n')))
        return -EINVAL;

    if (tmp > 65535)
        return -ERANGE;

    dev->sd_port = tmp;

    return count;
};

struct rbddev_attribute {
    struct configfs_attribute attr;
    ssize_t (*show)(struct rbd_dev *, char *);
    ssize_t (*store)(struct rbd_dev *, const char *, size_t);
};

static struct rbddev_attribute rbddev_attr_active = {
    .attr  = { .ca_owner = THIS_MODULE, .ca_name = "active", .ca_mode = S_IRUGO | S_IWUSR },
    .show  = rbddev_active_read,
    .store = rbddev_active_write,
};

static struct rbddev_attribute rbddev_attr_host = {
    .attr  = { .ca_owner = THIS_MODULE, .ca_name = "host", .ca_mode = S_IRUGO | S_IWUSR },
    .show  = rbddev_host_read,
    .store = rbddev_host_write,
};

static struct rbddev_attribute rbddev_attr_port = {
    .attr  = { .ca_owner = THIS_MODULE, .ca_name = "port", .ca_mode = S_IRUGO | S_IWUSR },
    .show  = rbddev_port_read,
    .store = rbddev_port_write,
};

static struct configfs_attribute *rbddev_attrs[] = {
    &rbddev_attr_active.attr,
    &rbddev_attr_host.attr,
    &rbddev_attr_port.attr,
    NULL,
};

static void rbddev_release(struct config_item *item)
{
    struct rbd_dev *dev = to_rbddev(item);

    free_device(dev);
};

static ssize_t rbddev_attr_show(struct config_item *item, struct configfs_attribute *attr, char *page)
{
    struct rbd_dev *dev = to_rbddev(item);
    struct rbddev_attribute *rbddev_attr = container_of(attr, struct rbddev_attribute, attr);
    ssize_t ret = 0;
    
    if (rbddev_attr->show)
        ret = rbddev_attr->show(dev, page);
    return ret;
};

static ssize_t rbddev_attr_store(struct config_item *item, struct configfs_attribute *attr, const char *page, size_t count)
{
    struct rbd_dev *dev = to_rbddev(item);
    struct rbddev_attribute *rbddev_attr = container_of(attr, struct rbddev_attribute, attr);
    ssize_t ret = 0;

    if (rbddev_attr->store)
        ret = rbddev_attr->store(dev, page, count);
    return ret;
};

static struct configfs_item_operations rbddev_item_ops = {
    .release         = rbddev_release,
    .show_attribute  = rbddev_attr_show,
    .store_attribute = rbddev_attr_store,
};

static struct config_item_type rbddev_type = {
    .ct_item_ops = &rbddev_item_ops, 
    .ct_attrs    = rbddev_attrs,
    .ct_owner    = THIS_MODULE,
};

struct simple_children {
    struct config_group group;
};

static struct config_item *cfs_make_item(struct config_group *group, const char *name)
{
    struct rbd_dev *dev;

    dev = init_device();
    config_item_init_type_name(&dev->cfs_item, name, &rbddev_type);
    list_add(&dev->devices, &rbd_devices);

    return &dev->cfs_item;
}

static struct configfs_attribute *cfs_attrs[] = {
    NULL,
};

static struct configfs_group_operations cfs_group_ops = {
    .make_item = cfs_make_item,
};

static struct config_item_type cfs_type = {
    .ct_group_ops = &cfs_group_ops,
    .ct_attrs     = cfs_attrs,
    .ct_owner     = THIS_MODULE,
};

struct configfs_subsystem rbd_cfs_subsys = {
    .su_group = {
        .cg_item = {
            .ci_namebuf = DEVICE_NAME,
            .ci_type = &cfs_type,
        },
    },
};

static int configfs_init(void) {
    int ret;
    struct simple_children *simple_children;

    config_group_init(&rbd_cfs_subsys.su_group);
    init_MUTEX(&rbd_cfs_subsys.su_sem);
    ret = configfs_register_subsystem(&rbd_cfs_subsys);
    if (ret) {
        printk(KERN_ERR "RBD: Error %d when registering configfs subsystem\n", ret);
        return ret;
    }
    simple_children = kmalloc(sizeof(struct simple_children), GFP_KERNEL);
    if (!simple_children) {
        printk(KERN_ERR "RBD: Error registerin simple simple_children\n");
        return -1;
    }

    return 0;
}

/* configfs stuff ends here ------------------------------------------------------- */ 


static int __init rbd_init(void)
{
    int ret;

    major = register_blkdev(major, DEVICE_NAME);
    if (major <= 0) {
        printk(KERN_ERR "RBD: error regisering block device\n");
        return -EBUSY;
    }
    
    if (debug) printk(KERN_INFO "rbd_init | major=%d\n", major);

    ret = configfs_init();
    if (ret)
        return ret;

    return 0;
}


static void rbd_exit(void)
{
    configfs_unregister_subsystem(&rbd_cfs_subsys);
    unregister_blkdev(major, DEVICE_NAME);
}
    

module_init(rbd_init);
module_exit(rbd_exit);
