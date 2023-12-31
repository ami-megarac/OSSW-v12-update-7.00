/****************************************************************
 ****************************************************************
 **                                                            **
 **    (C)Copyright 2009-2015, American Megatrends Inc.        **
 **                                                            **
 **            All Rights Reserved.                            **
 **                                                            **
 **        5555 Oakbrook Pkwy Suite 200, Norcross              **
 **                                                            **
 **        Georgia - 30093, USA. Phone-(770)-246-8600.         **
 **                                                            **
 ****************************************************************
 ****************************************************************/

#ifndef _WATCHDOG_CORE_H_
#define _WATCHDOG_CORE_H_

/* operations provided by hardware module */
struct watchdog_hal_ops_t {
	void (*set_value)(int);
	void (*count)(void);
	int (*get_status)(void);
	void (*enable)(void);
	void (*disable)(void);
};
struct watchdog_hal_ops_new_t {
	void (*set_value)(int, int);
	void (*count)(int);
	int (*get_status)(int);
	void (*enable)(int);
	void (*disable)(int);
};

/* operations provided by common module */
struct watchdog_core_funcs_t {
};

struct watchdog_hal_t {
	struct watchdog_hal_ops_t *hal_ops;
};
struct watchdog_hal_new_t {
	struct watchdog_hal_ops_new_t *hal_ops;
};

#endif /* !_WATCHDOG_CORE_H_ */
