/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc,char **argv)
{
    if(argc!=3){fprintf(stderr,"Usage: %s STAGING DESTINATION\n",argv[0]);return 2;}
#ifdef SYS_renameat2
    if(syscall(SYS_renameat2,AT_FDCWD,argv[1],AT_FDCWD,argv[2],RENAME_NOREPLACE)==0)return 0;
    fprintf(stderr,"Cannot publish icon theme: %s\n",strerror(errno));
#else
    fprintf(stderr,"Cannot publish icon theme: atomic no-replace rename is unavailable\n");
#endif
    return 1;
}
