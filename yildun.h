// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef YILDUN_H
#define YILDUN_H

#include "flir_kernel_os.h"
#include "yildun_internal.h"

// Function prototypes to set up hardware specific items
int SetupMX6S(PFVD_DEV_INFO pDev);

// Function prototypes for common FVD functions
int LoadFPGA(PFVD_DEV_INFO pDev);
PUCHAR get_fpga_data(PFVD_DEV_INFO pDev, ULONG *size, char *out_revision);
void free_fpga_data(PFVD_DEV_INFO pDev);

#endif
