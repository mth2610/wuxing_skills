#ifndef CORE_LIQUID_CAPTURE_BUDGET_H
#define CORE_LIQUID_CAPTURE_BUDGET_H
#include <math.h>

/* Thinning a coherent sheet must not leave its optical supports as isolated
 * dots. Compensate the sampled area, bounded so silhouettes cannot balloon.
 * This changes rendering support only, never particle collision or dynamics. */
static inline float LiquidCaptureBudget_OpticalScale(int demand,int admitted)
{
    if(admitted<=0 || demand<=admitted) return 1.0f;
    return fminf(2.0f,sqrtf((float)demand/(float)admitted));
}

/* Max-min sharing: small streams retain their complete shape; the remaining
 * capacity is shared by larger streams. No caller can consume another stream's
 * share merely by submitting first. Tie order differs by at most one sample. */
static inline int LiquidCaptureBudget_Allocate(const int *demand, int count,
                                               int capacity, int *admitted)
{
    int total=0;
    for(int i=0;i<count;i++) admitted[i]=0;
    while(total<capacity) {
        int progress=0;
        for(int i=0;i<count && total<capacity;i++) {
            if(admitted[i]<demand[i]) { admitted[i]++; total++; progress=1; }
        }
        if(!progress) break;
    }
    return total;
}
#endif
