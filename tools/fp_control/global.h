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
#define VFDBRIGHTNESS		0xc0425a03
#define VFDDISPLAYCLR		0xc0425b00
/*spark*/
#define VFDGETSTARTUPSTATE	0xc0425af8


struct vfd_ioctl_data {
	unsigned char start;
	unsigned char data[64];
	unsigned char length;
};

typedef enum {NONE, TIMER} eWakeupReason;

typedef enum {Unknown, Spark} eBoxType;

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
	int     (* SetTimer)       (Context_t* context, time_t* theGMTTime);
	int     (* GetTimer)       (Context_t* context, time_t* theGMTTime);
	int     (* SetDisplayTime) (Context_t* context, int on);
	int     (* Shutdown)       (Context_t* context, time_t* shutdownTimeGMT);
	int     (* Reboot)         (Context_t* context, time_t* rebootTimeGMT);
	int     (* Sleep)          (Context_t* context, time_t* wakeUpGMT);
	int     (* SetText)        (Context_t* context, char* theText);
	int     (* SetLed)         (Context_t* context, int which, int on);
	int     (* SetIcon)        (Context_t* context, int which, int on);
	int     (* SetBrightness)  (Context_t* context, int brightness);
	int     (* SetPwrLed)  	   (Context_t* context, int pwrled); /* added by zeroone; set PowerLed Brightness on HDBOX*/
	int     (* SetLight)       (Context_t* context, int on);
	int     (* Exit)           (Context_t* context);
	int     (* GetWakeupReason)(Context_t* context, eWakeupReason* reason);
    void* private;
} Model_t;

extern Model_t Spark_model;

static Model_t * AvailableModels[] = {
	&Spark_model,
	NULL
};

double modJulianDate(struct tm *theTime);
time_t read_timers_utc(time_t curTime);
time_t read_fake_timer_utc(time_t curTime);
int searchModel(Context_t  *context, eBoxType type);
int checkConfig(int* display, int* display_custom, char** timeFormat, int* wakeup);

int getWakeupReasonPseudo(eWakeupReason *reason);
int syncWasTimerWakeup(eWakeupReason reason);

#endif
