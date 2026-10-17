#include "transport.h"
void transport_seek(transport_t *t, uint32_t count, uint32_t rate, int seconds)
{
    int64_t target=(int64_t)t->position+(int64_t)seconds*rate;
    if(target<0) target=0;
    if(target>count) target=count;
    t->position=(uint32_t)target;
    t->phase=0;
}
