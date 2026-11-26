#ifndef REGIONS_H
#define REGIONS_H

#include "ast.h"

/* Function-scoped regions. Pointer results use the caller's allocation
   region; possible pointer writes to heap conservatively use the root.
   Root effects propagate to all callers before code generation. */
enum RegionPolicy { REGION_LOCAL = 0, REGION_CALLER = 1, REGION_ROOT = 2 };
void regions_analyze(ASTNode *program);

#endif
