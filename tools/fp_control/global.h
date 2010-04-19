#ifndef GLOBAL_H_
#define GLOBAL_H_

#ifndef bool
#define bool unsigned char
#define true 1
#define false 0
#endif

#define VFDGETTIME		0xc0425afa
#define VFDSETTIME		0xc0425afb
#define VFDSTANDBY		0xc0425afc
#define VFDREBOOT		0xc0425afd
#define VFDSETLED		0xc0425afe
#define VFDICONDISPLAYONOFF	0xc0425a0a
#define	VFDDISPLAYCHARS 	0xc0425a00
#define VFDBRIGHTNESS		0xc0425a03
#define VFDDISPLAYWRITEONOFF	0xc0425a05
#define VFDDISPLAYCLR		0xc0425b00
/* ufs912, 922, hdbox ->unset compat mode */
#define VFDSETMODE		0xc0425aff

/* ufs912 */
#define VFDLEDBRIGHTNESS	0xc0425af8

struct vfd_ioctl_data {
	unsigned char start;
	unsigned char data[64];
	unsigned char length;
};

typedef enum {Unknown, Ufs910_1W, Ufs910_14W, Ufs922, Tf7700, Hl101, Vip2, HdBox, Hs5101, Ufs912} eBoxType;

typedef struct Context_s {
	void* /* Model_t */  *m; /* instance data */
	int                  fd; /* filedescriptor of fd */

} Context_t;

typedef struct Model_s {
	char *   Name;
	eBoxType Type;
	int     (* Init)           (Context_t* context);
	int     (* Clear)          (Context_t* context);
	int     (* Usage)          (Context_t* context, char* prg_name);
	int     (* SetTime)        (Context_t* context, time_t* theGMTTime);
	int     (* GetTime)        (Context_t* context, time_t* theGMTTime);
	int     (* SetTimer)       (Context_t* context);
	int     (* SetTimerManual) (Context_t* context, time_t* theGMTTime);
	int     (* GetTimer)       (Context_t* context, time_t* theGMTTime);
	int     (* Shutdown)       (Context_t* context, time_t* shutdownTimeGMT);
	int     (* Reboot)         (Context_t* context, time_t* rebootTimeGMT);
	int     (* Sleep)          (Context_t* context, time_t* wakeUpGMT);
	int     (* SetText)        (Context_t* context, char* theText);
	int     (* SetLed)         (Context_t* context, int which, int on);
	int     (* SetIcon)        (Context_t* context, int which, int on);
	int     (* SetBrightness)  (Context_t* context, int brightness);
	int     (* GetWakeupReason)(Context_t* context, int* reason);
	int     (* SetLight)       (Context_t* context, int on);
	int     (* Exit)           (Context_t* context);
	int     (* SetLedBrightness) (Context_t* context, int brightness);

        void* private;
} Model_t;

extern Model_t Ufs910_1W_model;
extern Model_t Ufs910_14W_model;
extern Model_t UFS912_model;
extern Model_t UFS922_model;
extern Model_t HDBOX_model;
extern Model_t HL101_model;
extern Model_t VIP2_model;
extern Model_t Hs5101_model;

static Model_t * AvailableModels[] = {
	&Ufs910_1W_model,
	&Ufs910_14W_model,
	&UFS922_model,
	&HDBOX_model,
	&HL101_model,
	&VIP2_model,
	&Hs5101_model,
	&UFS912_model,
	NULL
};

double modJulianDate(struct tm *theTime);
unsigned long int read_e2_timers(time_t curTime);
int searchModel(Context_t  *context, eBoxType type);
int checkConfig(int* display, int* display_custom, char** timeFormat, int* wakeup);

#endif
