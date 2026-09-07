/* Symbol-only native-link stub, not ABI declarations or executable runtime code.
 * The module writer turns these names into system libSceVideoOut imports.
 * Actual call signatures live in ps5_agc_native_runtime.c. */
#define IMPORT(name) int name(void) { return -1; }
IMPORT(sceVideoOutOpen)
IMPORT(sceVideoOutClose)
IMPORT(sceVideoOutSetFlipRate)
IMPORT(sceVideoOutSetBufferAttribute2)
IMPORT(sceVideoOutRegisterBuffers2)
IMPORT(sceVideoOutUnregisterBuffers)
IMPORT(sceVideoOutSubmitFlip)
IMPORT(sceVideoOutIsFlipPending)
IMPORT(sceVideoOutWaitVblank)
IMPORT(sceVideoOutGetFlipStatus)
IMPORT(sceVideoOutGetResolutionStatus)
IMPORT(sceVideoOutGetOutputStatus)
IMPORT(sceVideoOutIsOutputSupported)
IMPORT(sceVideoOutConfigureOutput)
IMPORT(sceVideoOutVrrUnpegFromFixedRate)
#undef IMPORT
