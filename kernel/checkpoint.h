// kernel/checkpoint.h
// Data structures for xv6 process checkpoint / restore.
//
// A snapshot file has this layout on disk:
//
//   [ xv6_chk_header ]
//   [ struct trapframe ]             -- user register state
//   [ xv6_chk_page  * num_pages ]   -- virtual-address → page-data pairs
//
// Magic signature spells "XV6C" in little-endian ASCII.

#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "types.h"

#define XV6_CHK_MAGIC   0x43365658   // "XV6C"
#define XV6_CHK_VERSION 1

// File descriptor info saved per open file.
// We only handle regular files and the console (fd 0/1/2) for now.
struct xv6_chk_fd {
  int   valid;        // 1 if this slot was open
  int   type;        // FD_DEVICE or FD_INODE (from file.h)
  int   readable;
  int   writable;
  uint  off;         // current file offset
  char  path[128];   // resolved path of the inode (empty for device fds)
};

// Fixed-size header written at byte 0 of every snapshot file.
struct xv6_chk_header {
  uint32 magic;        // XV6_CHK_MAGIC
  uint32 version;      // format version
  int    pid;          // original PID (informational)
  char   name[16];     // process name
  uint64 sz;           // process address-space size in bytes
  uint64 num_pages;    // number of xv6_chk_page records that follow trapframe
  struct xv6_chk_fd fds[16]; // open file descriptors (NOFILE == 16)
};

// One entry per mapped user page saved in the snapshot.
struct xv6_chk_page {
  uint64 va;           // virtual address (page-aligned)
  char   data[4096];   // raw page content (PGSIZE == 4096)
};

#endif // CHECKPOINT_H
