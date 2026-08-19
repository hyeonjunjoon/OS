// ipt.h - Inverted Page Table structure definition

struct ipt_entry {
  uint pfn;               // Physical frame number
  uint pid;               // Owner process PID
  uint va;                // Mapped virtual address (page-aligned)
  ushort flags;           // PTE flags (P/W/U etc.) snapshot
  ushort refcnt;          // Reference count (optional)
  struct ipt_entry *next; // Hash chain
};
