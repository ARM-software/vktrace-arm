#pragma once

#include "vktrace_platform.h"
#if defined(ARM_LINUX_64)
#include <sys/ioctl.h>
#endif

// The following code is referred from ddk_repo/product/kernal/include/uapi/base/arm/dma_buf_test_exporter/dma-buf-test-exporter.h

#ifndef _UAPI_DMA_BUF_TEST_EXPORTER_H_
#define _UAPI_DMA_BUF_TEST_EXPORTER_H_

#define DMA_BUF_TE_ENQ 0x642d7465
#define DMA_BUF_TE_ACK 0x68692100

struct dma_buf_te_ioctl_version {
	/** Must be set to DMA_BUF_TE_ENQ by client, driver will set it to DMA_BUF_TE_ACK */
	int op;
	/** Major version */
	int major;
	/** Minor version */
	int minor;
};

struct dma_buf_te_ioctl_alloc
{
    uint64_t size; /* size of buffer to allocate, in pages */
};

struct dma_buf_te_ioctl_status {
	/* in */
	int fd; /* the dma_buf to query, only dma_buf objects exported by this driver is supported */
	/* out */
	int attached_devices; /* number of devices attached (active 'dma_buf_attach's) */
	int device_mappings; /* number of device mappings (active 'dma_buf_map_attachment's) */
	int cpu_mappings; /* number of cpu mappings (active 'mmap's) */
};

struct dma_buf_te_ioctl_set_failing {
	/* in */
	int fd; /* the dma_buf to set failure mode for, only dma_buf objects exported by this driver is supported */

	/* zero = no fail injection, non-zero = inject failure */
	int fail_attach;
	int fail_map;
	int fail_mmap;
};

struct dma_buf_te_ioctl_fill {
	int fd;
	unsigned int value;
};

#define DMA_BUF_TE_IOCTL_BASE 'E'
/* Below all returning 0 if successful or -errcode except DMA_BUF_TE_ALLOC which will return fd or -errcode */
#define DMA_BUF_TE_VERSION _IOR(DMA_BUF_TE_IOCTL_BASE, 0x00, struct dma_buf_te_ioctl_version)
#define DMA_BUF_TE_ALLOC _IOR(DMA_BUF_TE_IOCTL_BASE, 0x01, struct dma_buf_te_ioctl_alloc)
#define DMA_BUF_TE_QUERY _IOR(DMA_BUF_TE_IOCTL_BASE, 0x02, struct dma_buf_te_ioctl_status)
#define DMA_BUF_TE_SET_FAILING                                                                     \
	_IOW(DMA_BUF_TE_IOCTL_BASE, 0x03, struct dma_buf_te_ioctl_set_failing)
#define DMA_BUF_TE_ALLOC_CONT _IOR(DMA_BUF_TE_IOCTL_BASE, 0x04, struct dma_buf_te_ioctl_alloc)
#define DMA_BUF_TE_FILL _IOR(DMA_BUF_TE_IOCTL_BASE, 0x05, struct dma_buf_te_ioctl_fill)

#endif /* _UAPI_DMA_BUF_TEST_EXPORTER_H_ */

int allocateDmaBuffer(unsigned int required_size, unsigned int alignment) {
	const int page_size = sysconf(_SC_PAGESIZE);
	assert(alignment == 0 || page_size % alignment == 0); // For arm_64, the page_size is 4k byte, which should be divided exactly by alignment.

    int dma_buf_te_fd = open("/dev/dma_buf_te", O_RDWR | O_CLOEXEC);
	assert(dma_buf_te_fd >= 0);

	struct dma_buf_te_ioctl_alloc alloc_te;
	alloc_te.size = (required_size + page_size - 1) / page_size;
	int fd = ioctl(dma_buf_te_fd, DMA_BUF_TE_ALLOC, &alloc_te);
	assert (fd >= 0);
	return fd;
}