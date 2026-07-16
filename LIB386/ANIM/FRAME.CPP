#include <ANIM/FRAME.H>
#include <ANIM.H>
#include <SYSTEM/TIMER.H>

#include <stddef.h>
#include <stdint.h>
#include <SYSTEM/DEBUG_TRACES.H>
// Portable endian macros: LBA2_LE16/LE32 are __builtin_bswap on Wii U and a
// no-op (x) elsewhere. The header MUST be included unconditionally because the
// macro is used in host code below; the SYSTEM/wiiu_endian.h #else branch
// supplies the no-op definition on little-endian desktop targets (CI).
#include <SYSTEM/wiiu_endian.h>
#ifdef LBA2_TARGET_WIIU
#include <SYSTEM/LOGPRINT.H>
#endif

void ObjectSetFrame(T_OBJ_3D *obj, U32 frame) {
    LIB386_TRACE_CPP("ObjectSetFrame", "1", "obj=%p frame=%u", (void *)obj, frame);
    T_PTR_NUM nextBody = obj->NextBody;
    void *nextTexture = obj->NextTexture;
    if (frame >= obj->NbFrames) {
        return;
    }

    obj->Body = nextBody;
    obj->Texture = nextTexture;

    void *anim = obj->Anim.Ptr;
    if (TransFctAnim != NULL) {
        anim = TransFctAnim(obj->Anim.Num);
    }

    obj->Interpolator = 0;
    obj->LastAnimStepX = 0;
    obj->LastAnimStepY = 0;
    obj->LastAnimStepZ = 0;

    U16 *animPtr = (U16 *)anim;
    // anim header U16 fields are little-endian on disk; without swap nbGroups
    // explodes and the currentFrame copy loop blows past T_OBJET into the
    // next ListObjet[] entry, clobbering its PtrFile3D.
    U16 nbGroups = LBA2_LE16(animPtr[1]);

    obj->LastAnimStepAlpha = 0;
    obj->LastAnimStepBeta = 0;
    obj->LastAnimStepGamma = 0;
    obj->LastOfsIsPtr = 0;

    obj->LastNbGroups = nbGroups;
    obj->NextNbGroups = nbGroups;
    obj->NbGroups = nbGroups;

    obj->LastTimer = TimerRefHR;
    obj->Time = TimerRefHR;
    obj->Status = FLAG_FRAME;

    U32 ofsFrame = (nbGroups * 8 + 8) * frame + 8;
    obj->LastOfsFrame = (PTR_U32)(uintptr_t)ofsFrame;
    obj->LastFrame = frame;

    U32 *animFrame = (U32 *)((U8 *)anim + ofsFrame + 16);
    U32 *currentFrame = (U32 *)obj->CurrentFrame;
#ifdef LBA2_TARGET_WIIU
    // Anim frame data on disk is LE; obj->CurrentFrame is read back as
    // T_GROUP_INFO {S16 Type, Alpha, Beta, Gamma}. Without per-S16 swap,
    // Type=1 (TYPE_TRANSLATE) comes out as 0x0100 on PPC.
    {
        U16 *animS16 = (U16 *)animFrame;
        S16 *cfS16 = (S16 *)currentFrame;
        U32 nS16 = (nbGroups * 2u - 2u) * 2u;
        for (U32 i = 0; i < nS16; ++i) {
            cfS16[i] = (S16)LBA2_LE16(animS16[i]);
        }
        static int s_log = 0;
        if (s_log < 2) {
            LogPrintf("[ObjectSetFrame] swap-copied %u S16 to CurrentFrame; cf[0..3]=%d,%d,%d,%d nbGroups=%u frame=%u\n",
                      (unsigned)nS16, (int)cfS16[0], (int)cfS16[1], (int)cfS16[2], (int)cfS16[3],
                      (unsigned)nbGroups, (unsigned)frame);
            s_log++;
        }
        animFrame += (nbGroups * 2u - 2u);
        currentFrame += (nbGroups * 2u - 2u);
    }
#else
    for (U32 i = 0; i < nbGroups * 2 - 2; i++) {
        *(currentFrame++) = *(animFrame++);
    }
#endif
    frame++;

    if (frame == obj->NbFrames) {
        U16 animTime = LBA2_LE16(animPtr[4]);
        obj->NextFrame = 0;
        obj->NextOfsFrame = (PTR_U32)(uintptr_t)8u;
        obj->NextTimer = obj->LastTimer + animTime;
        obj->Master = LBA2_LE16(animPtr[8]);
    } else {
        U16 animTime = LBA2_LE16(((U16 *)animFrame)[0]);
        obj->NextFrame = frame;
        obj->NextOfsFrame = (U32 *)((U64)animFrame - (U64)anim);
        obj->NextTimer = obj->LastTimer + animTime;
        obj->Master = LBA2_LE16(((U16 *)animFrame)[4]);
    }
}
