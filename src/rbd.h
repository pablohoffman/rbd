#ifndef _RBD_H 
#define _RBD_H

#include <asm/semaphore.h>      /* struct semaphore */
#include <linux/timer.h>        /* struct timer_list */
#include <linux/workqueue.h>    /* struct work_struct */
#include <linux/blkdev.h>       /* struct request_queue */
#include <linux/genhd.h>        /* struct gendisk */
#include <linux/fs.h>           /* struct inode, file */
#include <linux/list.h>         /* struct list_head */

#include <linux/net.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/file.h>
#include <linux/socket.h>

#include <linux/configfs.h>

#include "proto.h"

#define DEVICE_NAME "rbd"
#define RBD_MINORS 16
#define RBD_SECSIZE 512


struct rbd_dev {
    char *name;                         /* name of this device */
    int active;                         /* is the device active? */

	unsigned long size;                 /* size in sectors */
	spinlock_t req_lock;
	struct request_queue *queue;

    struct list_head devices;           /* linked list of all RBD devices */

    struct work_struct rq_work;   
	struct work_struct setup_work;   
    struct semaphore setupwk_mutex; 
	struct semaphore rqwk_mutex; 

    int first_minor;                    /* first minor of this device */

    struct config_item cfs_item;        /* configfs item */

    struct semaphore sd_mutex; 
    char sd_host[16];                   /* SD address in dotted values format */
	int sd_addr;                        /* SD address in integer format */
	int sd_port;                        /* SD port */
    struct socket *sd_socket;
	unsigned int sd_msguid;             /* UID of last message sent */

	struct gendisk *gd; 
};

//extern struct configfs_subsystem rbd_cfs_subsys;

static int __init rbd_init(void);
static void rbd_exit(void);
static void rbd_transfer(struct rbd_dev *dev, unsigned long sector,
		unsigned long nsect, char *buffer, int write);
static void rbd_request(request_queue_t *q);
int sd_connect(struct rbd_dev *dev);
int sd_disconnect(struct rbd_dev *dev);
int sd_send(struct rbd_dev *dev, void *buf, size_t size);
int sd_recv(struct rbd_dev *dev, void *buf, size_t size);

#endif
