#include <ANIM.H>

#include <stdlib.h>

#include <SYSTEM/TIMER.H>
#include <SYSTEM/DEBUG_TRACES.H>
// Portable endian macros: LBA2_LE16/LE32 are __builtin_bswap on Wii U and a
// no-op (x) elsewhere. The header MUST be included unconditionally because the
// macro is used in host code below; the SYSTEM/wiiu_endian.h #else branch
// supplies the no-op definition on little-endian desktop targets (CI).
#include <SYSTEM/wiiu_endian.h>
#ifdef LBA2_TARGET_WIIU
#include <SYSTEM/LOGPRINT.H>
#endif

void ObjectInitAnim(T_OBJ_3D *obj, void *anim) {
    LIB386_TRACE_CPP("ObjectInitAnim", "1", "obj=%p anim=%p", (void *)obj, anim);
    obj->Body = obj->NextBody;
    obj->Texture = obj->NextTexture;

    if (obj->Anim.Ptr == anim) {
        return;
    }

    obj->Anim.Ptr = anim;

    if (TransFctAnim != NULL) {
        anim = TransFctAnim(obj->Anim.Num);
    }

    U16 *animPtr = (U16 *)anim;

    // Anim record header is little-endian on disk. Without swap, animNbGroups
    // explodes and the currentFrame copy loop overruns into ListObjet[1+],
    // clobbering PtrFile3D (manifests as 0x2c010000 garbage and SearchBody hang).
    U16 animNbFrames = LBA2_LE16(animPtr[0]);
    U16 animNbGroups = LBA2_LE16(animPtr[1]);
    U16 animLoopFrame = LBA2_LE16(animPtr[2]);

    obj->Interpolator = 0;
    obj->Master = LBA2_LE16(animPtr[8]);
    obj->NbFrames = animNbFrames;
    obj->NextNbGroups = animNbGroups;
    obj->LoopFrame = animLoopFrame;

    obj->LastAnimStepX = 0;
    obj->LastAnimStepY = 0;
    obj->LastAnimStepZ = 0;

    obj->LoopOfsFrame = animLoopFrame * (animNbGroups + 1) * 8 + 8;

    obj->LastAnimStepAlpha = 0;
    obj->LastAnimStepBeta = 0;
    obj->LastAnimStepGamma = 0;

    obj->LastTimer = TimerRefHR;
    obj->Time = TimerRefHR;

    if (obj->LastOfsFrame == 0) {
        obj->Status = FLAG_FRAME;
        obj->LastOfsFrame = (PTR_U32)8;
        obj->LastOfsIsPtr = 0;
        obj->LastFrame = 0;

        obj->LastNbGroups = animNbGroups;
        obj->NbGroups = animNbGroups;

        U32 *currentFrame = (U32 *)obj->CurrentFrame;
        U32 *srcAnim = (U32 *)animPtr + 4 + 2; // group 1?
        U32 count = animNbGroups * 2 - 2;

#ifdef LBA2_TARGET_WIIU
        // Anim frame data is LE on disk; the same per-S16 swap rationale
        // as ObjectSetFrame applies here (T_GROUP_INFO {Type,A,B,G}).
        {
            U16 *srcS16 = (U16 *)srcAnim;
            S16 *cfS16 = (S16 *)currentFrame;
            U32 nS16 = count * 2u;
            for (U32 i = 0; i < nS16; ++i) {
                cfS16[i] = (S16)LBA2_LE16(srcS16[i]);
            }
            static int s_log = 0;
            if (s_log < 2) {
                LogPrintf("[ObjectInitAnim] swap-copied %u S16 to CurrentFrame; cf[0..3]=%d,%d,%d,%d\n",
                          (unsigned)nS16, (int)cfS16[0], (int)cfS16[1], (int)cfS16[2], (int)cfS16[3]);
                s_log++;
            }
        }
#else
        for (int i = 0; i < count; i++) {
            currentFrame[i] = srcAnim[i];
        }
#endif
        currentFrame += count;
        srcAnim += count;

        if (obj->NbFrames != 1) {
            obj->NextOfsFrame = (PTR_U32)((U64)srcAnim - (U64)animPtr);
            obj->NextFrame = 1;
            obj->NextTimer = TimerRefHR + LBA2_LE16(((U16 *)srcAnim)[0]);
        } else {
            obj->NextOfsFrame = (PTR_U32)8;
            obj->NextFrame = 0;
            obj->NextTimer = TimerRefHR + LBA2_LE16(((U16 *)animPtr)[4]);
        }

        return;
    }

    obj->Status = 0;
    obj->NextOfsFrame = (PTR_U32)8;
    obj->NextFrame = 0;
    obj->LastNbGroups = animNbGroups;

    U32 targetNbGroups = obj->NbGroups;
    U32 increasedToNbGroups = 0;

    if (animNbGroups != targetNbGroups) {
        obj->NbGroups = animNbGroups;

        if (animNbGroups > targetNbGroups) {
            increasedToNbGroups = targetNbGroups;
            animNbGroups = targetNbGroups;
        } else {
            targetNbGroups = animNbGroups;
        }
    }

    U16 animFrameDeltaTime = LBA2_LE16(animPtr[4]);
    obj->NextTimer = TimerRefHR + animFrameDeltaTime;
    obj->LastOfsFrame = (U32 *)PtrLib3DBufferAnim;
    obj->LastOfsIsPtr = 1;

    U32 *bufferAnim = (U32 *)PtrLib3DBufferAnim;
    bufferAnim[0] = 0;
    bufferAnim[1] = 0;
    bufferAnim[2] = 0;
    bufferAnim[3] = 0;

    obj->LastFrame = -1;

    bufferAnim += 4;
    U32 *currentFrame = (U32 *)obj->CurrentFrame;
    U32 count = animNbGroups * 2 - 2;
    for (int i = 0; i < count; i++) {
        bufferAnim[i] = currentFrame[i];
    }
    bufferAnim += count;

    if (increasedToNbGroups != 0) {
        if (TransFctAnim != NULL) {
            anim = TransFctAnim(obj->Anim.Num);
        }

        U32 diff = obj->NbGroups - increasedToNbGroups;
        U32 *nextOfsFrame = (U32 *)((U8 *)anim + (U64)obj->NextOfsFrame) + (increasedToNbGroups + 1) * 8;
        U32 count = diff << 1;
#ifdef LBA2_TARGET_WIIU
        // This tail reads raw LE anim data (groups beyond the previous anim's
        // count) while bufferAnim is consumed host-order by INTFRAME via
        // LastOfsIsPtr==1 — same per-S16 swap as the CurrentFrame copy above.
        {
            U16 *srcS16 = (U16 *)nextOfsFrame;
            S16 *dstS16 = (S16 *)bufferAnim;
            U32 nS16 = count * 2u;
            for (U32 i = 0; i < nS16; ++i) {
                dstS16[i] = (S16)LBA2_LE16(srcS16[i]);
            }
        }
#else
        for (int i = 0; i < count; i++) {
            bufferAnim[i] = nextOfsFrame[i];
        }
#endif

        bufferAnim += count;
    }

    if ((U8 *)bufferAnim > EndLib3DBufferAnim) {
        PtrLib3DBufferAnim = Lib3DBufferAnim;
        return;
    }

    PtrLib3DBufferAnim = (U8 *)bufferAnim;
}
