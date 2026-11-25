/* STUB so para checar sintaxe de main.c no PC. NAO e a libctru. Assinaturas conforme a documentacao da libctru. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef s32 Result; typedef u32 Handle;
#define R_FAILED(r) ((r) < 0)
#define U64_MAX UINT64_MAX
#define BIT(n) (1U<<(n))
enum { KEY_A=BIT(0),KEY_B=BIT(1),KEY_SELECT=BIT(2),KEY_START=BIT(3),KEY_DRIGHT=BIT(4),KEY_DLEFT=BIT(5),KEY_DUP=BIT(6),KEY_DDOWN=BIT(7),KEY_R=BIT(8),KEY_L=BIT(9),KEY_X=BIT(10),KEY_Y=BIT(11),KEY_ZL=BIT(14),KEY_ZR=BIT(15),KEY_TOUCH=BIT(20)};
typedef enum { GFX_TOP=0, GFX_BOTTOM=1 } gfxScreen_t;
typedef enum { GFX_LEFT=0, GFX_RIGHT=1 } gfx3dSide_t;
typedef enum { GSP_RGBA8_OES=0, GSP_BGR8_OES=1, GSP_RGB565_OES=2 } GSPGPU_FramebufferFormats;
void gfxInit(GSPGPU_FramebufferFormats t, GSPGPU_FramebufferFormats b, bool vram);
void gfxSetDoubleBuffering(gfxScreen_t s, bool d);
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t side, u16 *w, u16 *h);
void gfxFlushBuffers(void); void gfxSwapBuffers(void); void gfxExit(void); void gspWaitForVBlank(void);
bool aptMainLoop(void); void hidScanInput(void); u32 hidKeysDown(void); u32 hidKeysHeld(void);
typedef struct { s16 dx, dy; } circlePosition; typedef struct { u16 px, py; } touchPosition;
void hidCircleRead(circlePosition *p); void hidCstickRead(circlePosition *p); void hidTouchRead(touchPosition *p);
typedef enum { NDSP_OUTPUT_MONO, NDSP_OUTPUT_STEREO, NDSP_OUTPUT_SURROUND } ndspOutputMode;
typedef enum { NDSP_INTERP_POLYPHASE, NDSP_INTERP_LINEAR, NDSP_INTERP_NONE } ndspInterpType;
#define NDSP_FORMAT_STEREO_PCM16 5
enum { NDSP_WBUF_FREE, NDSP_WBUF_QUEUED, NDSP_WBUF_PLAYING, NDSP_WBUF_CANCEL, NDSP_WBUF_DONE };
typedef struct ndspWaveBuf { union { s16 *data_pcm16; void *data_vaddr; }; u32 nsamples; void *adpcm_data; u32 offset; bool looping; u16 sequence_id; struct ndspWaveBuf *next; volatile u8 status; } ndspWaveBuf;
Result ndspInit(void); void ndspExit(void); void ndspSetOutputMode(ndspOutputMode m);
void ndspChnReset(int id); void ndspChnSetInterp(int id, ndspInterpType t); void ndspChnSetRate(int id, float r);
void ndspChnSetFormat(int id, u16 f); void ndspChnSetMix(int id, float mix[12]); void ndspChnWaveBufAdd(int id, ndspWaveBuf *b);
typedef void (*ndspCallback)(void *data); void ndspSetCallback(ndspCallback cb, void *data);
Result DSP_FlushDataCache(const void *a, u32 size);
void *linearAlloc(size_t n); void linearFree(void *p); u64 osGetTime(void);
typedef struct { s32 state; } LightEvent; typedef enum { RESET_ONESHOT, RESET_STICKY, RESET_PULSE } ResetType;
void LightEvent_Init(LightEvent *e, ResetType t); void LightEvent_Signal(LightEvent *e); void LightEvent_Wait(LightEvent *e);
typedef struct Thread_tag *Thread; typedef void (*ThreadFunc)(void *);
#define CUR_THREAD_HANDLE 0xFFFF8000
Result svcGetThreadPriority(s32 *out, Handle h);
Thread threadCreate(ThreadFunc e, void *arg, size_t stack, int prio, int core, bool detached);
Result threadJoin(Thread t, u64 timeout); void threadFree(Thread t);

/* LightLock + sleep (usados pelo gravador) */
typedef s32 LightLock; void LightLock_Init(LightLock *l); void LightLock_Lock(LightLock *l); void LightLock_Unlock(LightLock *l);
void svcSleepThread(s64 ns);
/* teclado do sistema (applet swkbd): assinaturas conforme 3ds/applets/swkbd.h */
typedef struct { int dummy; } SwkbdState;
typedef enum { SWKBD_TYPE_NORMAL = 0 } SwkbdType;
typedef enum { SWKBD_BUTTON_LEFT = 0, SWKBD_BUTTON_MIDDLE = 1, SWKBD_BUTTON_RIGHT = 2, SWKBD_BUTTON_CONFIRM = 2, SWKBD_BUTTON_NONE = -1 } SwkbdButton;
typedef enum { SWKBD_ANYTHING = 0, SWKBD_NOTEMPTY = 1, SWKBD_NOTEMPTY_NOTBLANK = 2 } SwkbdValidInput;
void swkbdInit(SwkbdState *s, SwkbdType type, int numButtons, int maxTextLength);
void swkbdSetInitialText(SwkbdState *s, const char *text);
void swkbdSetHintText(SwkbdState *s, const char *text);
void swkbdSetValidation(SwkbdState *s, SwkbdValidInput v, u32 filterFlags, int maxDigits);
void swkbdSetButton(SwkbdState *s, SwkbdButton btn, const char *text, bool submit);
SwkbdButton swkbdInputText(SwkbdState *s, char *buf, size_t bufsize);
