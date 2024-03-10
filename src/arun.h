#ifndef ACMD_H
#define ACMD_H

#include "acommon.h"

#if !defined(APARSER_H)
typedef struct ArrayConditional ArrayConditional;
#endif

int32 cmdexec(ArrayConditional* conds);

#endif
