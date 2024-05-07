/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 */

enum FAULT_ENTRY_DUMPER {
	/* START 		traps.c */
	FAULT_ENTRY_ARM_DIE = 0,
};

/* The length looks to be enough based on where arm_notify_die is called
 * always align this length with fault.c, 
 * dont generalize since we dont aim to upstream this */ 
#define MAX_LEN_STR_ARM_DIE 128
struct arm_die_entry {
	const char *str;
	char make_additional_space_for_str[MAX_LEN_STR_ARM_DIE - sizeof(const char *)];
	int signo; 
	int si_code;
	void *addr;
	unsigned long err;
	unsigned long trap;
};

struct fault_callback_info
{
	union {
		struct arm_die_entry 		arm_die;
	} entry_type_u;

	struct mutex callback_mutex;
	unsigned int which_entry;
	size_t len_entry;
	void* handle;
};

struct fault_callback
{
	void (*fault_dump_persist)(struct fault_callback *fault_dumper);
	struct fault_callback_info info;
};

int fault_dump_register(struct fault_callback* callback);
int fault_dump_unregister(struct fault_callback* callback);
struct fault_callback* get_current_fault_callback(void);