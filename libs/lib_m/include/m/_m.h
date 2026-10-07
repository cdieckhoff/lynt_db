/**
 * @file _m.h
 * @brief Umbrella header for the _m universal OS layer.
 *
 * Include this to get every _m_* function. If you only need part of the
 * layer (e.g. just file I/O), include the specific header instead:
 *
 *     #include "m/_m_file.h"    // open, close, truncate, size, sync
 *     #include "m/_m_io.h"      // read, write, pread, pwrite, seek
 *     #include "m/_m_map.h"     // map, unmap, msync
 *     #include "m/_m_sparse.h"  // mark_sparse, punch_hole, is_hole, ...
 *     #include "m/_m_mem.h"     // alloc, free, page_size, ...
 *     #include "m/_m_error.h"   // last_error, clear_error
 *
 * WHY THE UNDERSCORE PREFIX
 * -------------------------
 * Every public name in this layer begins with `_m_` (functions) or `_M_`
 * (macros and enum values). This is a deliberate namespace. The layer is
 * intended to be droppable into any project, so its identifiers must not
 * collide with anything in the host project's namespace.
 *
 * Technically, a single leading underscore followed by a lowercase letter
 * is reserved at file scope by the C standard (7.1.3). In practice every
 * real compiler accepts it, and this convention is used by libuv, SDL, and
 * countless other libraries. It's a defensible choice, but if you ever
 * care about strict conformance, rename to `m_` / `M_` throughout.
 *
 * LAYERING
 * --------
 * This header and every _m_*.c source form the "universal layer." Nothing
 * in this layer knows about the database, about lib_lynt, or about any
 * application. It is a self-contained library that could be lifted into
 * any project that needs portable file I/O and memory mapping.
 *
 * The layer above (lib_lynt) wraps _m_* with application-flavored names.
 * The layer above that (lynt_db) implements the storage engine. Neither
 * of those layers contains a single #ifdef.
 */

#ifndef M__M_H
#define M__M_H

#include "m/_m_types.h"
#include "m/_m_error.h"
#include "m/_m_file.h"
#include "m/_m_io.h"
#include "m/_m_map.h"
#include "m/_m_sparse.h"
#include "m/_m_mem.h"

#endif /* M__M_H */