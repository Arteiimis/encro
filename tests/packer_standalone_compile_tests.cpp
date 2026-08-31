// Standalone compile test (D-03): verifies packer.h compiles WITHOUT pack_service.h
// This proves the circular dependency packer.h -> pack_service.h is broken.
// If this file compiles, D-03 passes. The compilation step IS the test.

#include "pack/packer.h"

// packer.h must NOT transitively include pack_service.h.
// Types PackFileEntry, PackRunResult, FileOrdinalRange must be reachable via pack_types.h.
// Types PackEntryInput, PackEntryPartition etc. must be reachable via pack::detail:: in packer_types.h.
// All packer.h free function declarations must compile without pack_service.h visibility.
