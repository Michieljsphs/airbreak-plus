#ifndef _ram_pointers_h_
#define _ram_pointers_h_

/* Payload RAM state lives behind one spare RAM word pair (magic_ptr, a
 * per-version NSTUB_VAR). The first user allocates a pointer table with
 * PTR_COUNT entries; every payload that uses it must agree on this layout.
 */
typedef enum {
  PTR_HISTORY,
  PTR_SQUAREWAVE_DATA,
  PTR_TRACKING,
  PTR_ASV_DATA,
  PTR_FEATURES,
  PTR_TRIGGERCYCLE,
  PTR_GRAPH_DATA,
  PTR_CLIMATE_TIMER,

  __PTR_LAST,
} ptr_index;

typedef struct {
  unsigned magic;
  void **pointers;
} magic_ptr_t;

#define RAM_POINTERS_MAGIC 0x07E49001u

// NSTUB defines magic_ptr as an absolute symbol at the RAM address.
extern magic_ptr_t magic_ptr __attribute__((section(".absolute")));

#endif
