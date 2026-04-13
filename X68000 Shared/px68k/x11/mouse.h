#ifndef _winx68k_mouse
#define _winx68k_mouse

#include "common.h"

extern	int	MousePosX;
extern	int	MousePosY;
extern	BYTE	MouseStat;
extern	BYTE	MouseSW;

typedef struct {
	float dx;
	float dy;
	BYTE stat;
	BYTE sw;
	signed char sccX;
	signed char sccY;
	BYTE sccStatus;
	int queueCount;
	int sentCount;
	int doubleClickInProgress;
	int compatMode;
} MouseMonitorState;

void Mouse_Init(void);
void Mouse_Event(int wparam, float dx, float dy);
void Mouse_SetData(void);
void Mouse_StartCapture(int flag);
void Mouse_ChangePos(void);
void Mouse_ResetState(void);
void Mouse_SetDoubleClickInProgress(int flag);

// Toggle strict original px68k behavior for mouse handling
void Mouse_SetCompatMode(int enable);
void Mouse_GetMonitorState(MouseMonitorState* state);

#endif //_winx68k_mouse
